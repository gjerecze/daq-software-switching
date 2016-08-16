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
#include <stdio.h>
#include <unistd.h>

#include <rte_ethdev.h>

#include "../daqswitch/daqswitch.h"
#include "../daqswitch/daqswitch_port.h"
#include "stats.h"

#define STATS_INTERVAL_S            30 /* In seconds */
#define USECS_IN_SEC           1000000
#define USECS_IN_MSEC             1000
#define MSECS_IN_SEC              1000

/* reset all statistics */
void stats_reset(void)
{
    uint32_t i;
    uint8_t port_id;

    DAQSWITCH_PORT_FOREACH(port_id) {
        rte_eth_stats_reset(port_id);

        for (i = 0; i < DAQSWITCH_MAX_QUEUES_PER_PORT; i++) {
            memset(&daqswitch_tx_queue_stats[port_id][i], 0, sizeof(struct daqswitch_stats));
            memset(&daqswitch_rx_queue_stats[port_id][i], 0, sizeof(struct daqswitch_stats));
        }
    }

}


/* print statistics summary, interval for bw calculation in msec */
void stats_print(unsigned interval)
{
    struct rte_eth_stats stats_before[DAQSWITCH_MAX_PORTS];
    struct rte_eth_stats stats_after[DAQSWITCH_MAX_PORTS];
    struct daqswitch_stats dstats_rx_before[DAQSWITCH_MAX_PORTS][DAQSWITCH_MAX_QUEUES_PER_PORT];
    struct daqswitch_stats dstats_rx_after[DAQSWITCH_MAX_PORTS][DAQSWITCH_MAX_QUEUES_PER_PORT];
    struct daqswitch_stats dstats_tx_before[DAQSWITCH_MAX_PORTS][DAQSWITCH_MAX_QUEUES_PER_PORT];
    struct daqswitch_stats dstats_tx_after[DAQSWITCH_MAX_PORTS][DAQSWITCH_MAX_QUEUES_PER_PORT];
    uint64_t total_rx_dropped;
    uint64_t total_rx_bytes, total_tx_bytes;
    uint64_t total_rx_packets, total_rx_bursts;
    uint64_t total_tx_packets, total_tx_bursts;
    uint32_t i;
    uint8_t port_id;
    double rx_bw, tx_bw, total_rx_bw, total_tx_bw;

    total_rx_dropped = 0;
    total_rx_bytes = 0;
    total_tx_bytes = 0;
    total_tx_bw = 0.0;
    total_rx_bw = 0.0;

    DAQSWITCH_PORT_FOREACH(port_id) {
        rte_eth_stats_get(port_id, &stats_before[port_id]);
    }
    DAQSWITCH_PORT_FOREACH(port_id) {
        for (i = 0; i < DAQSWITCH_MAX_QUEUES_PER_PORT; i++) {
           dstats_rx_before[port_id][i] = daqswitch_rx_queue_stats[port_id][i];
           dstats_tx_before[port_id][i] = daqswitch_tx_queue_stats[port_id][i];
        }
    }

    usleep(interval * USECS_IN_MSEC);

    DAQSWITCH_PORT_FOREACH(port_id) {
       rte_eth_stats_get(port_id, &stats_after[port_id]);
    }
    DAQSWITCH_PORT_FOREACH(port_id) {
        for (i = 0; i < DAQSWITCH_MAX_QUEUES_PER_PORT; i++) {
           dstats_rx_after[port_id][i] = daqswitch_rx_queue_stats[port_id][i];
           dstats_tx_after[port_id][i] = daqswitch_tx_queue_stats[port_id][i];
        }
    }

    printf("\n");
    printf("+------+---------------+-------------+--------------+---------------+---------------+\n");
    printf("| Port | Rx Dropped    | Rx Pause On | Rx Pause Off | Bw Rx [Gbps]  | Bw Tx [Gbps]  |\n");
    printf("+------+---------------+-------------+--------------+---------------+---------------+\n");

    DAQSWITCH_PORT_FOREACH(port_id) {
        total_rx_bytes += (stats_after[port_id].ibytes - stats_before[port_id].ibytes);
        total_tx_bytes += (stats_after[port_id].obytes - stats_before[port_id].obytes);
        total_rx_dropped += stats_after[port_id].ierrors;
        rx_bw = (stats_after[port_id].ibytes - stats_before[port_id].ibytes) * 8 / interval;
        tx_bw = (stats_after[port_id].obytes - stats_before[port_id].obytes) * 8 / interval;

        printf("| %4d | %13" PRIu64 " | %11" PRIu64 " | %12" PRIu64 " | %13.8f | %13.8f |\n",
                port_id, stats_after[port_id].ierrors,
                stats_after[port_id].rx_pause_xon, stats_after[port_id].rx_pause_xoff,
                (double) rx_bw * 1.0e-6, (double) tx_bw * 1.0e-6);
    }

    total_rx_bw = total_rx_bytes * 8 / interval;
    total_tx_bw = total_tx_bytes * 8 / interval;

    printf("+------+---------------+-------------+--------------+---------------+---------------+\n");
    printf("| ALL  | %13" PRIu64 " |             |              | %13.8f | %13.8f |\n",
                        total_rx_dropped, (double) total_rx_bw * 1.0e-6, (double) total_tx_bw * 1.0e-6);
    printf("+--------------------------------------------------------------------------------------------------------------+\n");
    printf("| Port | Queue | Avg rx burst  | Avg tx burst  | Total pkt rx | Total pkt tx | Total burst rx | Total burst tx |\n");
    printf("+------+-------+---------------+---------------+--------------+--------------+----------------+----------------+\n");

    DAQSWITCH_PORT_FOREACH(port_id) {
        for (i = 0; i < DAQSWITCH_MAX_QUEUES_PER_PORT; i++) {
            total_rx_packets = dstats_rx_after[port_id][i].total_packets - dstats_rx_before[port_id][i].total_packets;
            total_tx_packets = dstats_tx_after[port_id][i].total_packets - dstats_tx_before[port_id][i].total_packets;

            if (!total_tx_packets && !total_rx_packets) {
                continue;
            }

            total_rx_bursts = dstats_rx_after[port_id][i].total_bursts - dstats_rx_before[port_id][i].total_bursts;
            total_tx_bursts = dstats_tx_after[port_id][i].total_bursts - dstats_tx_before[port_id][i].total_bursts;
            double av_rx_burst = total_rx_bursts ? ((double) total_rx_packets / (double) total_rx_bursts) : 0.0;
            double av_tx_burst = total_tx_bursts ? ((double) total_tx_packets / (double) total_tx_bursts) : 0.0;

            printf("| %4d | %5d | %13.4f | %13.4f | %12" PRIu64 " | %12" PRIu64 " | %14" PRIu64 " | %14" PRIu64 " |\n",
                   port_id, i, av_rx_burst, av_tx_burst, total_rx_packets, total_tx_packets, total_rx_bursts, total_tx_bursts);
        }
    }

    printf("+------+-------+---------------+---------------+--------------+--------------+----------------+----------------+\n");

}

void 
stats_thread_func(__attribute__((unused)) void *dummy)
{
    while (1) {
        stats_print(STATS_INTERVAL_S * MSECS_IN_SEC);
    }
}
