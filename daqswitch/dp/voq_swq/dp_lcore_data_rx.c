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
#include <rte_mbuf.h>
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ring.h>

#ifdef DAQ_DATA_FLOWS_DBG
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#endif

#include "../../common/common.h"
#include "../../stats/stats.h"

#include "dp_voq_swq.h"

#ifndef DAQ_DATA_FLOWS_DISABLE
static inline void
enqueue_data_pkt(struct rte_ring *ring, struct rte_mbuf **mbufs, unsigned n)
{   
    unsigned n_done;

#ifdef DP_BACK_PRESSURE_DISABLE
    n_done = rte_ring_enqueue_burst(ring, (void *) mbufs, n);
    if (n_done < n) {
        do {
            rte_pktmbuf_free(mbufs[n_done]);
        } while (++n_done < n);
    }
#else
    while (n > 0) {
        n_done = rte_ring_enqueue_burst(ring, (void *) mbufs, n);
        mbufs += n_done;
        n -= n_done;
    }
#endif

}

void
dp_configure_lcore_data_rx(__attribute__((unused)) struct dp_lcore_params *lp)
{

    DP_LOG_ENTRY();

    DP_LOG_EXIT();

}

void
dp_main_loop_lcore_data_rx(__attribute__((unused)) struct dp_lcore_params *lp)
{
    struct rte_mbuf *pkts_burst[DP_PORT_MAX_PKT_BURST_RX];
    struct rte_mbuf **pkt_enq;
    struct lcore_data_rx_port_conf *cur_rxp;
    struct data_rx_queue *cur_rxq;
    struct rte_ring *out_ring;
    uint32_t nb_rx;
    uint32_t nb_enq;
    uint32_t fdir_id;

    RTE_VERIFY(lp);
    RTE_VERIFY(lp->type == DP_LCORE_TYPE_DATA_RX);

    if (lp->nb_ports == 0) {
        DP_LOG_INFO("lcore %u has nothing to do", lp->id);
        return;
    }

#if DP_RX_POLL_INTERVAL
    const uint64_t poll_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * DP_RX_POLL_INTERVAL;
    uint64_t last_poll_tsc[DP_LCORE_PORT_MAX][DP_PORT_RXQ_MAX];
    uint64_t diff_tsc;
#endif

    uint64_t i;
    uint8_t port_idx = 0;

    while (1) {

        port_idx %= lp->nb_ports;
        cur_rxp = &lp->rx.port_list[port_idx]; 

        for (i = 0; i < cur_rxp->nb_queues; i++) {

            cur_rxq = &cur_rxp->queue_list[i];

#if DP_RX_POLL_INTERVAL
            diff_tsc = rte_rdtsc() - last_poll_tsc[port_idx][i];

            if (diff_tsc < poll_tsc) {
                continue;
            }
#endif

            nb_rx = rte_eth_rx_burst(cur_rxp->port_id, cur_rxq->queue_id,
                                     pkts_burst, DP_PORT_MAX_PKT_BURST_RX);

            if (nb_rx > 0) {

                daqswitch_rx_queue_stats[cur_rxp->port_id][cur_rxq->queue_id].total_packets += nb_rx;
                daqswitch_rx_queue_stats[cur_rxp->port_id][cur_rxq->queue_id].total_bursts++;

#if DP_RX_POLL_INTERVAL
                /* repeat if queue is filling up */
                if (nb_rx < DP_PORT_MAX_PKT_BURST_RX) {
                    last_poll_tsc[port_idx][i] = rte_rdtsc();
                }
#endif

                pkt_enq = pkts_burst;

                while (nb_rx > 0) {
                    fdir_id = (*pkt_enq)->hash.fdir.id & DP_FDIR_OUT_QUEUE_MASK;
                    out_ring = dp.rings[cur_rxq->out_port_id][fdir_id];
                    nb_enq = 1;

                    while (nb_enq < nb_rx) {
                        if (((*(pkt_enq + nb_enq))->hash.fdir.id & DP_FDIR_OUT_QUEUE_MASK) != fdir_id) {
                            break;
                        }
                        ++nb_enq;
                    }

                    enqueue_data_pkt(out_ring, pkt_enq, nb_enq);
                    pkt_enq += nb_enq;
                    nb_rx -= nb_enq;
                }

            }
        }
        
        port_idx++;

    }
    
}
#endif
