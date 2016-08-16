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
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_cycles.h>

#include "../../common/common.h"
#include "../../stats/stats.h"

#include "dp_voq_swq.h"
#include "../../common/common.h"

#ifndef DAQ_DATA_FLOWS_DISABLE
void
dp_configure_lcore_data_tx(__attribute__((unused)) struct dp_lcore_params *lp)
{

    DP_LOG_ENTRY();

    DP_LOG_EXIT();

}

void
dp_main_loop_lcore_data_tx(__attribute__((unused)) struct dp_lcore_params *lp)
{
    struct rte_ring *in_ring;
    struct rte_mbuf *pkts_burst[DP_PORT_MAX_PKT_BURST_TX];
    struct rte_mbuf **pkts_tx;
    struct lcore_data_tx_port_conf *cur_txp;
    uint32_t nb_tx, nb_deq;
    uint16_t queue_id;
    
    RTE_VERIFY(lp);
    RTE_VERIFY(lp->type == DP_LCORE_TYPE_DATA_TX);

    if (lp->nb_ports == 0) {
        DP_LOG_INFO("lcore %u has nothing to do", lp->id);
        return;
    }

#if DP_TX_DRAIN_INTERVAL
    const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * DP_TX_DRAIN_INTERVAL;
    uint64_t last_drain_tsc[DP_LCORE_PORT_MAX][DP_PORT_MAX_DATA_FLOWS];
    uint64_t diff_tsc;
#endif
    uint64_t i;
    uint8_t port_idx = 0;

    while (1) {

        port_idx %= lp->nb_ports;
        cur_txp = &lp->tx.port_list[port_idx]; 

        for (i = 0; i < DP_PORT_MAX_DATA_FLOWS; i++) {

            if ( !(cur_txp->active_flows & (1 << i)) ) {
                continue;
            }

            in_ring = dp.rings[cur_txp->port_id][i];

#if DP_TX_DRAIN_INTERVAL
            diff_tsc = rte_rdtsc() - last_drain_tsc[port_idx][i];

            if (dp.flows[cur_txp->port_id][i].req_flow || diff_tsc > drain_tsc) {
#endif

                nb_deq = rte_ring_dequeue_burst(in_ring,
                                                (void **) pkts_burst,
                                                DP_PORT_MAX_PKT_BURST_TX);

                if (nb_deq > 0) {

                    queue_id = dp.flows[cur_txp->port_id][i].req_flow ? DP_PORT_TXQ_ID_REQ : DP_PORT_TXQ_ID_DATA;
                    daqswitch_tx_queue_stats[cur_txp->port_id][queue_id].total_packets += nb_deq;
                    pkts_tx = pkts_burst;

                    while (1) {

                        nb_tx = rte_eth_tx_burst(cur_txp->port_id, queue_id,
                                                 pkts_tx, nb_deq);

                        daqswitch_tx_queue_stats[cur_txp->port_id][queue_id].total_bursts++;

                        /* keep tx until all sent, do not drop packets here */ 
                        nb_deq -= nb_tx;
                        pkts_tx += nb_tx;

                        if (nb_deq == 0) {
                            break;
                        }

#if DP_TX_DRAIN_INTERVAL
                        rte_delay_us(DP_TX_DRAIN_INTERVAL);
#endif

                    }

#if DP_TX_DRAIN_INTERVAL
                    last_drain_tsc[port_idx][i] = rte_rdtsc();
#endif

                }
#if DP_TX_DRAIN_INTERVAL
            }
#endif
        }

        port_idx++;

    }
}
#endif
