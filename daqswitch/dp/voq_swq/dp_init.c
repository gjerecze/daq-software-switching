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
#include <rte_ring.h>
#include <rte_errno.h>

#include "../../common/common.h"
#include "../../daqswitch/daqswitch_port.h"

#include "dp_voq_swq.h"

#ifndef DAQ_DATA_FLOWS_DISABLE
/* single rx pipeline is associated with a single rx data queue of
 * every enabled daqswitch port and outputs data to a single tx port 
 * voq with rate limiters at the output queues */
static void
init_data_rx_pipeline(struct pipeline_params *pp, uint32_t lcore_id, uint8_t out_port_id, uint16_t in_queue_id)
{
    int ret;
    uint8_t in_port_id;
    uint16_t out_queue_id;

    DP_LOG_DEBUG("initializing data rx pipeline: lcore id %d out port %d in queue %d", lcore_id, out_port_id, in_queue_id);
    
    ret = pipeline_init(pp, lcore_id, PIPELINE_TYPE_RX_DATA);
    RTE_VERIFY(ret == PIPELINE_SUCCESS);

    /* in ports
     * single rx-q of each enabled port */
    DAQSWITCH_PORT_FOREACH(in_port_id) {
        DP_LOG_DEBUG("\tpipeline port in: port %d, queue %d", in_port_id, in_queue_id);

        ret = pipeline_init_port_in(pp, in_port_id, in_queue_id, NULL);
        RTE_VERIFY(ret == PIPELINE_SUCCESS);

    }

    /* out ports
     * all tx-q of a single port
     * each queue correspond to a single rate limited flow */
    for (out_queue_id = DP_PORT_TXQ_ID_DATA_MIN; out_queue_id < DAQSWITCH_MAX_QUEUES_PER_PORT; out_queue_id++) {

        DP_LOG_DEBUG("\tpipeline port out: port %d, queue %d", out_port_id, out_queue_id);

        RTE_VERIFY(dp.rings[out_port_id][out_queue_id] != NULL);
        ret = pipeline_init_port_out(pp, out_port_id, out_queue_id, dp.rings[out_port_id][out_queue_id]);
        RTE_VERIFY(ret == PIPELINE_SUCCESS);

    }
}

/* single tx pipeline is associated with all tx data queues 
 * of a single daqswitch port */ 
static void
init_data_tx_pipeline(struct pipeline_params *pp, uint32_t lcore_id, uint8_t out_port_id)
{
    int ret;
    uint16_t out_queue_id;

    ret = pipeline_init(pp, lcore_id, PIPELINE_TYPE_TX_DATA);
    RTE_VERIFY(ret == PIPELINE_SUCCESS);

    for (out_queue_id = DP_PORT_TXQ_ID_DATA_MIN; out_queue_id < DAQSWITCH_MAX_QUEUES_PER_PORT; out_queue_id++) {

        RTE_VERIFY(dp.rings[out_port_id][out_queue_id] != NULL);
        ret = pipeline_init_port_in(pp, out_port_id, out_queue_id, dp.rings[out_port_id][out_queue_id]);
        RTE_VERIFY(ret == PIPELINE_SUCCESS);

        ret = pipeline_init_port_out(pp, out_port_id, out_queue_id, NULL);
        RTE_VERIFY(ret == PIPELINE_SUCCESS);

    }
}
#endif

