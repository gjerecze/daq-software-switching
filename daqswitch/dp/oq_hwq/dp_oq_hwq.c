/* © Copyright 2016 CERN
 *
 * This software is distributed under the terms of the GNU General Public 
 * Licence version 3 (GPL Version 3), copied verbatim in the file "LICENSE".
 *
 * In applying this licence, CERN does not waive the privileges and immunities
 * granted to it by virtue of its status as an Intergovernmental Organization 
 * or submit itself to any jurisdiction.
 *
 * Author: Grzegorz Jereczek <grzegorz.jereczek@cern.ch>
 */
/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#include <rte_common.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_per_lcore.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_byteorder.h>
#include <rte_lpm.h>
#include <rte_ip.h>

#include "../../daqswitch/daqswitch.h"
#include "../../daqswitch/daqswitch_port.h"
#include "../../stats/stats.h"
#include "../../common/common.h"
#include "../include/dp.h"

#define MAX_PKT_BURST 32
#ifndef DP_TX_DRAIN_INTERVAL
    #define DP_TX_DRAIN_INTERVAL                                                       10  /* us */
#endif
#define	MAX_TX_BURST	(MAX_PKT_BURST / 2)
#define NB_SOCKETS 8

#define RTE_TEST_RX_DESC_DEFAULT 2048
#define RTE_TEST_TX_DESC_DEFAULT 4096

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT RTE_MAX_ETHPORTS
#define MAX_RX_QUEUE_PER_PORT 128

/* Used to mark destination port as 'invalid'. */
#define	BAD_PORT	((uint16_t)-1)

#define FWDSTEP	4

static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

struct mbuf_table {
	uint16_t len;
	struct rte_mbuf *m_table[MAX_PKT_BURST];
};

typedef struct rte_lpm lookup_struct_t;
static lookup_struct_t *ipv4_lookup_struct[NB_SOCKETS];

struct lcore_rx_queue {
	uint8_t port_id;
	uint8_t queue_id;
} __rte_cache_aligned;

struct lcore_params {
	uint8_t port_id;
	uint8_t queue_id;
	uint8_t lcore_id;
} __rte_cache_aligned;

static struct lcore_params lcore_params[RTE_MAX_LCORE];
static uint16_t nb_lcore_params = 0;

struct lcore_conf {
	uint16_t n_rx_queue;
	struct lcore_rx_queue rx_queue_list[MAX_RX_QUEUE_PER_LCORE];
	uint16_t tx_queue_id[RTE_MAX_ETHPORTS];
	struct mbuf_table tx_mbufs[RTE_MAX_ETHPORTS];
	lookup_struct_t *ipv4_lookup_struct;
} __rte_cache_aligned;
static struct lcore_conf lcore_conf[RTE_MAX_LCORE];

static uint32_t nb_ip_routes = 0;

#define DP_OQ_HWQ_LPM_MAX_RULES 1024

/* Send burst of packets on an output interface */
static inline int
send_burst(struct lcore_conf *qconf, uint16_t n, uint8_t port)
{
	struct rte_mbuf **m_table;
	int ret;
	uint16_t queueid;

    queueid = qconf->tx_queue_id[port];
	m_table = (struct rte_mbuf **)qconf->tx_mbufs[port].m_table;

    while (n > 0) {
        ret = rte_eth_tx_burst(port, queueid, m_table, n);
        
        if (likely(ret > 0)) {
            daqswitch_tx_queue_stats[port][queueid].total_packets += ret;
            daqswitch_tx_queue_stats[port][queueid].total_bursts++;

            /* keep tx until all sent, do not drop packets here */ 
            n -= ret;
            m_table += ret;
        }
    }

	return 0;
}

static inline __attribute__((always_inline)) void
send_packetsx4(struct lcore_conf *qconf, uint8_t port,
	struct rte_mbuf *m[], uint32_t num)
{
	uint32_t len, j, n;

	len = qconf->tx_mbufs[port].len;

	/*
	 * If TX buffer for that queue is empty, and we have enough packets,
	 * then send them straightway.
	 */
	if (num >= MAX_TX_BURST && len == 0) {
        while (num > 0) {
            n = rte_eth_tx_burst(port, qconf->tx_queue_id[port], m, num);
            
            if (likely(n > 0)) {
                daqswitch_tx_queue_stats[port][qconf->tx_queue_id[port]].total_packets += n;
                daqswitch_tx_queue_stats[port][qconf->tx_queue_id[port]].total_bursts++;

                /* keep tx until all sent, do not drop packets here */ 
                num -= n;
                m += n;
            }
        }
		return;
	}

	/*
	 * Put packets into TX buffer for that queue.
	 */

	n = len + num;
	n = (n > MAX_PKT_BURST) ? MAX_PKT_BURST - len : num;

	j = 0;
	switch (n % FWDSTEP) {
	while (j < n) {
	case 0:
		qconf->tx_mbufs[port].m_table[len + j] = m[j];
		j++;
	case 3:
		qconf->tx_mbufs[port].m_table[len + j] = m[j];
		j++;
	case 2:
		qconf->tx_mbufs[port].m_table[len + j] = m[j];
		j++;
	case 1:
		qconf->tx_mbufs[port].m_table[len + j] = m[j];
		j++;
	}
	}

	len += n;

	/* enough pkts to be sent */
	if (unlikely(len == MAX_PKT_BURST)) {

		send_burst(qconf, MAX_PKT_BURST, port);

		/* copy rest of the packets into the TX buffer. */
		len = num - n;
		j = 0;
		switch (len % FWDSTEP) {
		while (j < len) {
		case 0:
			qconf->tx_mbufs[port].m_table[j] = m[n + j];
			j++;
		case 3:
			qconf->tx_mbufs[port].m_table[j] = m[n + j];
			j++;
		case 2:
			qconf->tx_mbufs[port].m_table[j] = m[n + j];
			j++;
		case 1:
			qconf->tx_mbufs[port].m_table[j] = m[n + j];
			j++;
		}
		}
	}

	qconf->tx_mbufs[port].len = len;
}

static inline __attribute__((always_inline)) uint16_t
get_dst_port(const struct lcore_conf *qconf, struct rte_mbuf *pkt,
	uint32_t dst_ipv4, uint8_t portid)
{
	uint8_t next_hop;

	if (pkt->ol_flags & PKT_RX_IPV4_HDR) {
		if (rte_lpm_lookup(qconf->ipv4_lookup_struct, dst_ipv4,
				&next_hop) != 0)
			next_hop = portid;
	} else {
		next_hop = portid;
	}

	return next_hop;
}

static inline void
process_packet(struct lcore_conf *qconf, struct rte_mbuf *pkt,
	uint16_t *dst_port, uint8_t portid)
{
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	uint32_t dst_ipv4;
	uint16_t dp;

	eth_hdr = rte_pktmbuf_mtod(pkt, struct ether_hdr *);
	ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);

	dst_ipv4 = ipv4_hdr->dst_addr;
	dst_ipv4 = rte_be_to_cpu_32(dst_ipv4);
	dp = get_dst_port(qconf, pkt, dst_ipv4, portid);

	dst_port[0] = dp;
}

/*
 * Read ol_flags and destination IPV4 addresses from 4 mbufs.
 */
static inline void
processx4_step1(struct rte_mbuf *pkt[FWDSTEP], __m128i *dip, uint32_t *flag)
{
	struct ipv4_hdr *ipv4_hdr;
	struct ether_hdr *eth_hdr;
	uint32_t x0, x1, x2, x3;

	eth_hdr = rte_pktmbuf_mtod(pkt[0], struct ether_hdr *);
	ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
	x0 = ipv4_hdr->dst_addr;
	flag[0] = pkt[0]->ol_flags & PKT_RX_IPV4_HDR;

	eth_hdr = rte_pktmbuf_mtod(pkt[1], struct ether_hdr *);
	ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
	x1 = ipv4_hdr->dst_addr;
	flag[0] &= pkt[1]->ol_flags;

	eth_hdr = rte_pktmbuf_mtod(pkt[2], struct ether_hdr *);
	ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
	x2 = ipv4_hdr->dst_addr;
	flag[0] &= pkt[2]->ol_flags;

	eth_hdr = rte_pktmbuf_mtod(pkt[3], struct ether_hdr *);
	ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
	x3 = ipv4_hdr->dst_addr;
	flag[0] &= pkt[3]->ol_flags;

	dip[0] = _mm_set_epi32(x3, x2, x1, x0);
}

/*
 * Lookup into LPM for destination port.
 * If lookup fails, use incoming port (portid) as destination port.
 */
static inline void
processx4_step2(const struct lcore_conf *qconf, __m128i dip, uint32_t flag,
	uint8_t portid, struct rte_mbuf *pkt[FWDSTEP], uint16_t dprt[FWDSTEP])
{
	rte_xmm_t dst;
	const  __m128i bswap_mask = _mm_set_epi8(12, 13, 14, 15, 8, 9, 10, 11,
						4, 5, 6, 7, 0, 1, 2, 3);

	/* Byte swap 4 IPV4 addresses. */
	dip = _mm_shuffle_epi8(dip, bswap_mask);

	/* if all 4 packets are IPV4. */
	if (likely(flag != 0)) {
		rte_lpm_lookupx4(qconf->ipv4_lookup_struct, dip, dprt, portid);
	} else {
		dst.m = dip;
		dprt[0] = get_dst_port(qconf, pkt[0], dst.u32[0], portid);
		dprt[1] = get_dst_port(qconf, pkt[1], dst.u32[1], portid);
		dprt[2] = get_dst_port(qconf, pkt[2], dst.u32[2], portid);
		dprt[3] = get_dst_port(qconf, pkt[3], dst.u32[3], portid);
	}
}

/*
 * We group consecutive packets with the same destionation port into one burst.
 * To avoid extra latency this is done together with some other packet
 * processing, but after we made a final decision about packet's destination.
 * To do this we maintain:
 * pnum - array of number of consecutive packets with the same dest port for
 * each packet in the input burst.
 * lp - pointer to the last updated element in the pnum.
 * dlp - dest port value lp corresponds to.
 */

#define	GRPSZ	(1 << FWDSTEP)
#define	GRPMSK	(GRPSZ - 1)

#define GROUP_PORT_STEP(dlp, dcp, lp, pn, idx)	do { \
	if (likely((dlp) == (dcp)[(idx)])) {         \
		(lp)[0]++;                           \
	} else {                                     \
		(dlp) = (dcp)[idx];                  \
		(lp) = (pn) + (idx);                 \
		(lp)[0] = 1;                         \
	}                                            \
} while (0)

/*
 * Group consecutive packets with the same destination port in bursts of 4.
 * Suppose we have array of destionation ports:
 * dst_port[] = {a, b, c, d,, e, ... }
 * dp1 should contain: <a, b, c, d>, dp2: <b, c, d, e>.
 * We doing 4 comparisions at once and the result is 4 bit mask.
 * This mask is used as an index into prebuild array of pnum values.
 */
static inline uint16_t *
port_groupx4(uint16_t pn[FWDSTEP + 1], uint16_t *lp, __m128i dp1, __m128i dp2)
{
	static const struct {
		uint64_t pnum; /* prebuild 4 values for pnum[]. */
		int32_t  idx;  /* index for new last updated elemnet. */
		uint16_t lpv;  /* add value to the last updated element. */
	} gptbl[GRPSZ] = {
	{
		/* 0: a != b, b != c, c != d, d != e */
		.pnum = UINT64_C(0x0001000100010001),
		.idx = 4,
		.lpv = 0,
	},
	{
		/* 1: a == b, b != c, c != d, d != e */
		.pnum = UINT64_C(0x0001000100010002),
		.idx = 4,
		.lpv = 1,
	},
	{
		/* 2: a != b, b == c, c != d, d != e */
		.pnum = UINT64_C(0x0001000100020001),
		.idx = 4,
		.lpv = 0,
	},
	{
		/* 3: a == b, b == c, c != d, d != e */
		.pnum = UINT64_C(0x0001000100020003),
		.idx = 4,
		.lpv = 2,
	},
	{
		/* 4: a != b, b != c, c == d, d != e */
		.pnum = UINT64_C(0x0001000200010001),
		.idx = 4,
		.lpv = 0,
	},
	{
		/* 5: a == b, b != c, c == d, d != e */
		.pnum = UINT64_C(0x0001000200010002),
		.idx = 4,
		.lpv = 1,
	},
	{
		/* 6: a != b, b == c, c == d, d != e */
		.pnum = UINT64_C(0x0001000200030001),
		.idx = 4,
		.lpv = 0,
	},
	{
		/* 7: a == b, b == c, c == d, d != e */
		.pnum = UINT64_C(0x0001000200030004),
		.idx = 4,
		.lpv = 3,
	},
	{
		/* 8: a != b, b != c, c != d, d == e */
		.pnum = UINT64_C(0x0002000100010001),
		.idx = 3,
		.lpv = 0,
	},
	{
		/* 9: a == b, b != c, c != d, d == e */
		.pnum = UINT64_C(0x0002000100010002),
		.idx = 3,
		.lpv = 1,
	},
	{
		/* 0xa: a != b, b == c, c != d, d == e */
		.pnum = UINT64_C(0x0002000100020001),
		.idx = 3,
		.lpv = 0,
	},
	{
		/* 0xb: a == b, b == c, c != d, d == e */
		.pnum = UINT64_C(0x0002000100020003),
		.idx = 3,
		.lpv = 2,
	},
	{
		/* 0xc: a != b, b != c, c == d, d == e */
		.pnum = UINT64_C(0x0002000300010001),
		.idx = 2,
		.lpv = 0,
	},
	{
		/* 0xd: a == b, b != c, c == d, d == e */
		.pnum = UINT64_C(0x0002000300010002),
		.idx = 2,
		.lpv = 1,
	},
	{
		/* 0xe: a != b, b == c, c == d, d == e */
		.pnum = UINT64_C(0x0002000300040001),
		.idx = 1,
		.lpv = 0,
	},
	{
		/* 0xf: a == b, b == c, c == d, d == e */
		.pnum = UINT64_C(0x0002000300040005),
		.idx = 0,
		.lpv = 4,
	},
	};

	union {
		uint16_t u16[FWDSTEP + 1];
		uint64_t u64;
	} *pnum = (void *)pn;

	int32_t v;

	dp1 = _mm_cmpeq_epi16(dp1, dp2);
	dp1 = _mm_unpacklo_epi16(dp1, dp1);
	v = _mm_movemask_ps((__m128)dp1);

	/* update last port counter. */
	lp[0] += gptbl[v].lpv;

	/* if dest port value has changed. */
	if (v != GRPMSK) {
		lp = pnum->u16 + gptbl[v].idx;
		lp[0] = 1;
		pnum->u64 = gptbl[v].pnum;
	}

	return lp;
}

/* main processing loop */
static int
main_loop(__attribute__((unused)) void *dummy)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	unsigned lcore_id;
	uint64_t prev_tsc, diff_tsc, cur_tsc;
	int i, j, nb_rx;
	uint8_t portid, queueid;
	struct lcore_conf *qconf;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * DP_TX_DRAIN_INTERVAL;
	int32_t k;
	uint16_t dlp;
	uint16_t *lp;
	uint16_t dst_port[MAX_PKT_BURST];
	__m128i dip[MAX_PKT_BURST / FWDSTEP];
	uint32_t flag[MAX_PKT_BURST / FWDSTEP];
	uint16_t pnum[MAX_PKT_BURST + 1];

	prev_tsc = 0;

	lcore_id = rte_lcore_id();
	qconf = &lcore_conf[lcore_id];

	if (qconf->n_rx_queue == 0) {
		DP_LOG_INFO("lcore %u has nothing to do", lcore_id);
		return 0;
	}
    
	DP_LOG_INFO("entering main loop on lcore %u", lcore_id);

	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		DP_LOG_DEBUG(" -- lcoreid=%u portid=%hhu rxqueueid=%hhu",
                lcore_id, portid, queueid);
	}

	while (1) {

		cur_tsc = rte_rdtsc();

		/*
		 * TX burst queue drain
		 */
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {

			/*
			 * This could be optimized (use queueid instead of
			 * portid), but it is not called so often
			 */
			for (portid = 0; portid < RTE_MAX_ETHPORTS; portid++) {
				if (qconf->tx_mbufs[portid].len == 0)
					continue;
				send_burst(qconf,
					qconf->tx_mbufs[portid].len,
					portid);
				qconf->tx_mbufs[portid].len = 0;
			}

			prev_tsc = cur_tsc;
		}

		/*
		 * Read packet from RX queues
		 */
		for (i = 0; i < qconf->n_rx_queue; ++i) {
            portid = qconf->rx_queue_list[i].port_id;
            queueid = qconf->rx_queue_list[i].queue_id;
			nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst,
				MAX_PKT_BURST);
			if (nb_rx == 0)
				continue;

            daqswitch_rx_queue_stats[portid][queueid].total_packets += nb_rx;
            daqswitch_rx_queue_stats[portid][queueid].total_bursts++;

			k = RTE_ALIGN_FLOOR(nb_rx, FWDSTEP);
			for (j = 0; j != k; j += FWDSTEP) {
				processx4_step1(&pkts_burst[j],
					&dip[j / FWDSTEP],
					&flag[j / FWDSTEP]);
			}

			k = RTE_ALIGN_FLOOR(nb_rx, FWDSTEP);
			for (j = 0; j != k; j += FWDSTEP) {
				processx4_step2(qconf, dip[j / FWDSTEP],
					flag[j / FWDSTEP], portid,
					&pkts_burst[j], &dst_port[j]);
			}

			/*
			 * Finish packet processing and group consecutive
			 * packets with the same destination port.
			 */
			k = RTE_ALIGN_FLOOR(nb_rx, FWDSTEP);
			if (k != 0) {
				__m128i dp1, dp2;

				lp = pnum;
				lp[0] = 1;

				/* dp1: <d[0], d[1], d[2], d[3], ... > */
				dp1 = _mm_loadu_si128((__m128i *)dst_port);

				for (j = FWDSTEP; j != k; j += FWDSTEP) {

					/*
					 * dp2:
					 * <d[j-3], d[j-2], d[j-1], d[j], ... >
					 */
					dp2 = _mm_loadu_si128((__m128i *)
						&dst_port[j - FWDSTEP + 1]);
					lp  = port_groupx4(&pnum[j - FWDSTEP],
						lp, dp1, dp2);

					/*
					 * dp1:
					 * <d[j], d[j+1], d[j+2], d[j+3], ... >
					 */
					dp1 = _mm_srli_si128(dp2,
						(FWDSTEP - 1) *
						sizeof(dst_port[0]));
				}

				/*
				 * dp2: <d[j-3], d[j-2], d[j-1], d[j-1], ... >
				 */
				dp2 = _mm_shufflelo_epi16(dp1, 0xf9);
				lp  = port_groupx4(&pnum[j - FWDSTEP], lp,
					dp1, dp2);

				/*
				 * remove values added by the last repeated
				 * dst port.
				 */
				lp[0]--;
				dlp = dst_port[j - 1];
			} else {
				/* set dlp and lp to the never used values. */
				dlp = BAD_PORT - 1;
				lp = pnum + MAX_PKT_BURST;
			}

			/* Process up to last 3 packets one by one. */
			switch (nb_rx % FWDSTEP) {
			case 3:
				process_packet(qconf, pkts_burst[j],
					dst_port + j, portid);
				GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
				j++;
			case 2:
				process_packet(qconf, pkts_burst[j],
					dst_port + j, portid);
				GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
				j++;
			case 1:
				process_packet(qconf, pkts_burst[j],
					dst_port + j, portid);
				GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
				j++;
			}

			/*
			 * Send packets out, through destination port.
			 * Consecuteve pacekts with the same destination port
			 * are already grouped together.
			 * If destination port for the packet equals BAD_PORT,
			 * then free the packet without sending it out.
			 */
			for (j = 0; j < nb_rx; j += k) {

				int32_t m;
				uint16_t pn;

				pn = dst_port[j];
				k = pnum[j];

				if (likely(pn != BAD_PORT)) {
					send_packetsx4(qconf, pn,
						pkts_burst + j, k);
				} else {
					for (m = j; m != j + k; m++)
						rte_pktmbuf_free(pkts_burst[m]);
				}
			}
		}
	}

}

static int
configure_lcore_params(void)
{
	uint8_t portid, lcoreid, prev_lcoreid;
    bool numa_ok = true;

    DP_LOG_INFO("Configuring lcore params...");

    lcoreid = rte_get_next_lcore(-1, 1, 1);
    prev_lcoreid = lcoreid;

    DAQSWITCH_PORT_FOREACH(portid) {

        while(1) {
            if (rte_lcore_is_enabled(lcoreid) 
                && (DAQSWITCH_PORT_GET_NUMA(portid) == rte_lcore_to_socket_id(lcoreid)
                    || !numa_ok) ) {
                break;
            }
            lcoreid = rte_get_next_lcore(lcoreid, 1, 1);
            if (lcoreid == prev_lcoreid) {
                numa_ok = false;
            }
        }

        lcore_params[nb_lcore_params].port_id = portid;
        lcore_params[nb_lcore_params].queue_id = 0;
        lcore_params[nb_lcore_params++].lcore_id = lcoreid;

        DP_LOG_INFO(" -- lcoreid=%u portid=%hhu", lcoreid, portid);

        prev_lcoreid = lcoreid;
        lcoreid = rte_get_next_lcore(lcoreid, 1, 1);
    }

    DP_LOG_INFO("Done");

	return 0;
}

static int
configure_lcore_rx_queues(void)
{
	uint16_t i, nb_rx_queue;
	uint8_t lcore;

	for (i = 0; i < nb_lcore_params; ++i) {
		lcore = lcore_params[i].lcore_id;
		nb_rx_queue = lcore_conf[lcore].n_rx_queue;
		if (nb_rx_queue >= MAX_RX_QUEUE_PER_LCORE) {
			DP_LOG_INFO("error: too many queues (%u) for lcore: %u",
				(unsigned)nb_rx_queue + 1, (unsigned)lcore);
			return -1;
		} else {
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].port_id =
				lcore_params[i].port_id;
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].queue_id =
				lcore_params[i].queue_id;
			lcore_conf[lcore].n_rx_queue++;
		}
	}
	return 0;
}

static int
setup_lpm(int socketid)
{
    int ret = 0;
	char s[64];

	/* create the LPM table */
	snprintf(s, sizeof(s), "DP_OQ_HWQ_LPM_%d", socketid);
	ipv4_lookup_struct[socketid] = rte_lpm_create(s, socketid,
				DP_OQ_HWQ_LPM_MAX_RULES, 0);
	if (ipv4_lookup_struct[socketid] == NULL) {
		DP_LOG_INFO("Unable to create the l3fwd LPM table"
				" on socket %d\n", socketid);
        return -1;
    }

    return ret;
}

/* Add IP forwarding rules */
static int
add_ipv4_rule(uint32_t ipv4, uint8_t port_out_id)
{
    int ret = 0;
	int socketid;

	for (socketid = 0; socketid < NB_SOCKETS; socketid++) {
        if (ipv4_lookup_struct[socketid] == NULL) 
            continue;

        ret = rte_lpm_add(ipv4_lookup_struct[socketid], ipv4, 32, port_out_id);

        if (ret < 0) {
            DP_LOG_DEBUG("Unable to add entry to the LPM table on socket %d\n", socketid);
            break;
        }
    }

    nb_ip_routes++;

    return ret;
}

int
dp_init(void)
{
	struct lcore_conf *qconf;
	int ret;
	unsigned lcore_id;
	uint16_t queueid;
	uint8_t portid, socketid;

    DP_LOG_ENTRY();

	ret = configure_lcore_params();
    DP_LOG_AND_RETURN_ON_ERR("lcore_params configuration failed");

	ret = configure_lcore_rx_queues();
    DP_LOG_AND_RETURN_ON_ERR("lcore_rx_queues configuration failed");

    DAQSWITCH_PORT_FOREACH(portid) {
        /* rx queues */
        ret = daqswitch_port_set_nb_rxq(portid, 1);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_rxq on port %d", portid);
        ret = daqswitch_port_set_nb_rxd(portid, nb_rxd);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_rxd on port %d", portid);

        /* tx queues */
        queueid = 0;
        RTE_LCORE_FOREACH(lcore_id) {
			if (rte_lcore_is_enabled(lcore_id) == 0) {
				continue;
            }
            socketid = DAQSWITCH_NUMA_ON ? (uint8_t)rte_lcore_to_socket_id(lcore_id) : 0;
			DP_LOG_DEBUG("txq=%u,%d,%d ", lcore_id, queueid, socketid);
			qconf = &lcore_conf[lcore_id];
			qconf->tx_queue_id[portid] = queueid++;
		}

        ret = daqswitch_port_set_nb_txq(portid, queueid);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_txq on port %d", portid);
        ret = daqswitch_port_set_nb_txd(portid, nb_txd);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_txd on port %d", portid);

	}

    /* set the datapath thread */
    daqswitch_set_dp_thread(main_loop);
    
    DP_LOG_EXIT();

	return DP_SUCCESS;

error:
    return DP_ERR;
}

int
dp_configure(void)
{
	struct lcore_conf *qconf;
	unsigned lcore_id;
	uint8_t portid, socketid;

    DP_LOG_ENTRY();

    /* init lpm */
    DP_LOG_DEBUG("initializing lpm...");
    DAQSWITCH_PORT_FOREACH(portid) {
        RTE_LCORE_FOREACH(lcore_id) {
			if (rte_lcore_is_enabled(lcore_id) == 0)
				continue;

            socketid = DAQSWITCH_NUMA_ON ? (uint8_t)rte_lcore_to_socket_id(lcore_id) : 0;

			qconf = &lcore_conf[lcore_id];
            if (ipv4_lookup_struct[socketid] == NULL) {
                setup_lpm(socketid);
            }
            qconf->ipv4_lookup_struct = ipv4_lookup_struct[socketid];
		}

	}
    DP_LOG_DEBUG("done");

    DP_LOG_EXIT();

	return DP_SUCCESS;
}

int
dp_install_default_tables(void)
{
    DP_LOG_ENTRY();

    add_ipv4_rule(IPv4(20,1,1,1), 9);
    add_ipv4_rule(IPv4(20,1,2,1), 8);
    add_ipv4_rule(IPv4(20,1,3,1), 6);
    add_ipv4_rule(IPv4(20,1,4,1), 7);
    add_ipv4_rule(IPv4(20,1,5,1), 2);
    add_ipv4_rule(IPv4(20,1,6,1), 3);
    add_ipv4_rule(IPv4(20,1,7,1), 0);
    add_ipv4_rule(IPv4(20,1,8,1), 1);
    add_ipv4_rule(IPv4(20,1,9,1), 4);
    add_ipv4_rule(IPv4(20,1,10,1), 5);
    add_ipv4_rule(IPv4(20,1,11,1), 10);
    add_ipv4_rule(IPv4(20,1,12,1), 11);

    DP_LOG_EXIT();

    return DP_SUCCESS;
}

void
dp_dump_cfg(void)
{
}
