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
#ifndef STATS_H
#define STATS_H

#include "../daqswitch/daqswitch.h"

struct daqswitch_stats {
    uint64_t total_packets;
    uint64_t total_bursts;
} __rte_cache_aligned;

struct daqswitch_stats daqswitch_rx_queue_stats[DAQSWITCH_MAX_PORTS][DAQSWITCH_MAX_QUEUES_PER_PORT];
struct daqswitch_stats daqswitch_tx_queue_stats[DAQSWITCH_MAX_PORTS][DAQSWITCH_MAX_QUEUES_PER_PORT];

void stats_print(unsigned interval);
void stats_reset(void);
void stats_thread_func(void *);

#endif /* STATS_H */
