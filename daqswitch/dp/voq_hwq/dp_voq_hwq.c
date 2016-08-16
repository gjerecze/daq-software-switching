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
#include <rte_ip.h>

#include "../../daqswitch/daqswitch.h"
#include "../../daqswitch/daqswitch_port.h"
#include "../../stats/stats.h"
#include "../../common/common.h"
#include "../include/dp.h"

#define MAX_PKT_BURST 32

#define RTE_TEST_RX_DESC_DEFAULT 4096
#define RTE_TEST_TX_DESC_DEFAULT 4096

#define MAX_TX_QUEUE_PER_LCORE 16
#define MAX_RX_QUEUE_PER_PORT RTE_MAX_ETHPORTS
#define MAX_TX_QUEUE_PER_PORT 1

#ifndef DP_RX_POLL_INTERVAL
    #define DP_RX_POLL_INTERVAL 50
#endif

static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

struct lcore_tx_queue {
	uint8_t port_id;
	uint8_t queue_id;
	uint16_t n_rx_port;
    uint8_t rx_queue_id;
    uint8_t rx_port_ids[MAX_RX_QUEUE_PER_PORT];
} __rte_cache_aligned;

struct lcore_params {
	uint8_t port_id;
	uint8_t lcore_id;
} __rte_cache_aligned;

static struct lcore_params lcore_params[RTE_MAX_LCORE];
static uint16_t nb_lcore_params = 0;

struct lcore_conf {
	uint16_t n_tx_queue;
	struct lcore_tx_queue tx_queue_list[MAX_TX_QUEUE_PER_LCORE];
} __rte_cache_aligned;

static struct lcore_conf lcore_conf[RTE_MAX_LCORE];

static uint32_t nb_ip_routes = 0;

/* main processing loop */
static int
main_loop(__attribute__((unused)) void *dummy)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
    struct rte_mbuf **pkts_tx;
    struct lcore_tx_queue *cur_txq;
	unsigned lcore_id;
    unsigned nb_ports;
	unsigned i, j;
	uint32_t n;
	uint32_t nb_rx;
	struct lcore_conf *qconf;

#if DP_RX_POLL_INTERVAL
    const uint64_t poll_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * DP_RX_POLL_INTERVAL;
    uint64_t prev_tsc[MAX_TX_QUEUE_PER_LCORE] = {0};
    uint64_t diff_tsc, cur_tsc;
#endif

	lcore_id = rte_lcore_id();
	qconf = &lcore_conf[lcore_id];

	if (qconf->n_tx_queue == 0) {
		DP_LOG_INFO("lcore %u has nothing to do", lcore_id);
		return 0;
	}

	DP_LOG_INFO("entering main loop on lcore %u", lcore_id);

	for (i = 0; i < qconf->n_tx_queue; i++) {
        cur_txq = &qconf->tx_queue_list[i];
        nb_ports = cur_txq->n_rx_port;
        for (j = 0; j < nb_ports; j++) {
            DP_LOG_DEBUG(
                " -- lcoreid=%u portid=%hhu txqueueid=%hhu rxportid=%hhu rxqueueid=%hhu",
                lcore_id, cur_txq->port_id, cur_txq->queue_id,
                cur_txq->rx_port_ids[j], cur_txq->rx_queue_id);
        }
	}

    while (1) {
        for (i = 0; i < qconf->n_tx_queue; i++) {
            cur_txq = &qconf->tx_queue_list[i];
            nb_ports = cur_txq->n_rx_port;

#if DP_RX_POLL_INTERVAL
            cur_tsc = rte_rdtsc();
            diff_tsc = cur_tsc - prev_tsc[i];

            if (diff_tsc > poll_tsc) {

                prev_tsc[i] = cur_tsc;
#endif

                for (j = 0; j < nb_ports; j++) {

                    nb_rx = rte_eth_rx_burst(cur_txq->rx_port_ids[j], cur_txq->rx_queue_id,
                                             pkts_burst, MAX_PKT_BURST);

                    if (likely(nb_rx > 0)) {
#if DP_RX_POLL_INTERVAL
                        /* most probably there are more packets in the rx-queue
                         * do not wait the poll interval */
                        if (unlikely(nb_rx == MAX_PKT_BURST)) {
                            prev_tsc[i] = 0;
                        }
#endif

                        daqswitch_rx_queue_stats[cur_txq->rx_port_ids[j]][cur_txq->rx_queue_id].total_packets += nb_rx;
                        daqswitch_rx_queue_stats[cur_txq->rx_port_ids[j]][cur_txq->rx_queue_id].total_bursts++;

                        pkts_tx = pkts_burst;

                        while (nb_rx > 0) {
                            n = rte_eth_tx_burst(cur_txq->port_id, cur_txq->queue_id,
                                                 pkts_tx, nb_rx);
                            
                            if (likely(n > 0)) {
                                daqswitch_tx_queue_stats[cur_txq->port_id][cur_txq->queue_id].total_packets += n;
                                daqswitch_tx_queue_stats[cur_txq->port_id][cur_txq->queue_id].total_bursts++;

                                /* keep tx until all sent, do not drop packets here */ 
                                nb_rx -= n;
                                pkts_tx += n;
                            }
                        }
                    }
                }
#if DP_RX_POLL_INTERVAL
            }
#endif
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
        lcore_params[nb_lcore_params++].lcore_id = lcoreid;

        DP_LOG_INFO(" -- lcoreid=%u portid=%hhu", lcoreid, portid);

        prev_lcoreid = lcoreid;
        lcoreid = rte_get_next_lcore(lcoreid, 1, 1);
    }

    DP_LOG_INFO("Done");

	return 0;
}

static int
configure_lcore_tx_queues(void)
{
	uint16_t i, nb_tx_queue;
	uint8_t lcore;

	for (i = 0; i < nb_lcore_params; ++i) {
		lcore = lcore_params[i].lcore_id;
		nb_tx_queue = lcore_conf[lcore].n_tx_queue;
		if (nb_tx_queue >= MAX_TX_QUEUE_PER_LCORE) {
			DP_LOG_INFO("error: too many queues (%u) for lcore: %u",
				(unsigned)nb_tx_queue + 1, (unsigned)lcore);
			return -1;
		} else {
			lcore_conf[lcore].tx_queue_list[nb_tx_queue].port_id =
				lcore_params[i].port_id;
			lcore_conf[lcore].tx_queue_list[nb_tx_queue].queue_id = 0;
			lcore_conf[lcore].n_tx_queue++;
		}
	}
	return 0;
}

/* Counts the number of RX queues per port */
static uint8_t
get_n_rx_queues_per_port(void)
{
    unsigned n_rx_queues;

    n_rx_queues = daqswitch_get_nb_ports();
    //todo skip the rx queue of the same port

    return n_rx_queues;
}

/* Returns the RX queue id used be the given port on all other ports */
static uint16_t
get_rxqueues_id(uint8_t port_id)
{
	struct lcore_conf *qconf;
    unsigned lcore_id;
    uint8_t queue;

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0) {
			continue;
        }
		qconf = &lcore_conf[lcore_id];
        if (qconf->n_tx_queue == 0) {
            continue;
        }
        for(queue = 0; queue < qconf->n_tx_queue; ++queue) {
            if (qconf->tx_queue_list[queue].port_id == port_id) {
                return qconf->tx_queue_list[queue].rx_queue_id;
            }
        }
    }
    return -1;
}

#ifdef DP_VOQ_HWQ_USE_5TUPLE
/* Add 5tuple filter */
static int
add_ipv4_rule(uint32_t ipv4, uint8_t port_out_id)
{
    int ret = 0;
    uint8_t port_id;
    uint16_t queue_id;
    struct rte_5tuple_filter filter;

    memset(&filter, 0, sizeof(struct rte_5tuple_filter));

    filter.dst_ip_mask = 0;
    filter.src_ip_mask = 1;
    filter.dst_port_mask = 1;
    filter.src_port_mask = 1;
    filter.protocol_mask = 1;
    filter.priority = 1;

    filter.dst_ip = rte_cpu_to_be_32(ipv4);
    queue_id = get_rxqueues_id(port_out_id);

	for (port_id = 0; port_id < daqswitch_get_nb_ports(); port_id++) {
        ret = rte_eth_dev_add_5tuple_filter(port_id, nb_ip_routes, &filter, queue_id);
        if (ret < 0) {
            DP_LOG_INFO("Failed to add IP filter index %d port %d queue %d. Error: (%s)",
                     nb_ip_routes, port_id, queue_id, strerror(-ret));
        }
    }

    nb_ip_routes++;

    return ret;
}
#else
/* Add fdir filter */
static int
add_ipv4_rule(uint32_t ipv4, uint8_t port_out_id)
{
    int ret = 0;
    uint8_t port_id;
    uint16_t queue_id;
    struct rte_fdir_filter filter;

    memset(&filter, 0, sizeof(struct rte_fdir_filter));

    filter.ip_dst.ipv4_addr = rte_cpu_to_be_32(ipv4);
    queue_id = get_rxqueues_id(port_out_id);

    DAQSWITCH_PORT_FOREACH(port_id) {
        ret = rte_eth_dev_fdir_add_perfect_filter(port_id, &filter, nb_ip_routes, queue_id, 0);
        if (ret < 0) {
            DP_LOG_INFO("Failed to add IP filter index %d port %d queue %d. Error: (%s)",
                     nb_ip_routes, port_id, queue_id, strerror(-ret));
        }
    }

    nb_ip_routes++;

    return ret;
}
#endif

int
dp_init(void)
{
    int ret;
    uint8_t portid;

    DP_LOG_ENTRY();

	ret = configure_lcore_params();
    DP_LOG_AND_RETURN_ON_ERR("lcore_params configuration failed");

	ret = configure_lcore_tx_queues();
    DP_LOG_AND_RETURN_ON_ERR("lcore_tx_queues configuration failed");

    /* per port rx/tx queues */
    DAQSWITCH_PORT_FOREACH(portid) {
        ret = daqswitch_port_set_nb_rxq(portid, get_n_rx_queues_per_port());
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_rxq on port %d", portid);
        ret = daqswitch_port_set_nb_rxd(portid, nb_rxd);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_rxd on port %d", portid);

        ret = daqswitch_port_set_nb_txq(portid, 1);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_txq on port %d", portid);
        ret = daqswitch_port_set_nb_txd(portid, nb_txd);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_txd on port %d", portid);
    
#ifndef DP_VOQ_HWQ_USE_5TUPLE
        struct rte_fdir_masks fdir_mask;

        memset(&fdir_mask, 0, sizeof(struct rte_fdir_masks));

        fdir_mask.dst_ipv4_mask = 0xffffffff;
        fdir_mask.only_ip_flow = 1;

        ret = daqswitch_port_set_fdir_forwarding(portid, &fdir_mask);
#endif
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
    struct lcore_tx_queue *cur_txq;
	uint16_t queueid, rxqueueid;
	unsigned lcore_id;
	uint8_t portid, p, queue, socketid;

    DP_LOG_ENTRY();

    rxqueueid = 0;
    RTE_LCORE_FOREACH(lcore_id) {
        if (rte_lcore_is_enabled(lcore_id) == 0) {
            continue;
        }
		qconf = &lcore_conf[lcore_id];

        if (qconf->n_tx_queue == 0) {
            continue;
        }

		DP_LOG_DEBUG("Configuring tx on lcore %u...", lcore_id );

		/* init TX queues */
        for(queue = 0; queue < qconf->n_tx_queue; ++queue) {
            cur_txq = &qconf->tx_queue_list[queue];
            portid = cur_txq->port_id;
            queueid = cur_txq->queue_id;

            cur_txq->rx_queue_id = rxqueueid;
            cur_txq->n_rx_port = 0;

            socketid = get_socket_id(lcore_id);

			DP_LOG_DEBUG("txq=%d,%d,%d", portid, queueid, socketid);

            /* init one RX queue per couple (port,TX queue). */
            DAQSWITCH_PORT_FOREACH(p) {
                cur_txq->rx_port_ids[cur_txq->n_rx_port] = p;
                cur_txq->n_rx_port++;
            }

            rxqueueid++;

		}
	}

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
