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
#include <rte_pipeline.h>
#include <rte_port.h>
#include <rte_port_ethdev.h>
#include <rte_port_ring.h>
#include <rte_table_stub.h>
#include <rte_mbuf.h>
#include <rte_debug.h>

#include "../common/common.h"
#include "../daqswitch/daqswitch.h"

#include "pipeline.h"

static struct pipeline_params *rx_data_pipelines[DAQSWITCH_MAX_PORTS];
static uint8_t nb_rx_data_pipelines = 0;

struct pipeline_params *
find_data_pipeline(uint8_t port_out_id)
{
    uint8_t i;

    for (i = 0; i < nb_rx_data_pipelines; i++) {
        if (rx_data_pipelines[i]->q_out[0].port_id == port_out_id) {
            return rx_data_pipelines[i];
        }
    }

    return NULL;
}

/* fills the packet metadata to be used for fdir-based forwarding */
static inline void
pkt_metadata_fill(struct rte_mbuf *m)
{
    /* access metadata in the mbuf headroom */
    struct pipeline_pkt_metadata *c =
        (struct pipeline_pkt_metadata *) RTE_MBUF_METADATA_UINT8_PTR(m, 0);

    //todo dirty-hack to have dcm->ros flows with unique fdir_id's
    c->rte_pipeline_port_out_id = m->hash.fdir.id & PIPELINE_QUEUE_FROM_FDIR_ID_MASK;
}

/* default action for all rx packets on data queues */
static int
rx_action_handler(struct rte_mbuf               **pkts,
                  uint32_t                           n,
                  uint64_t                  *pkts_mask,
                  __rte_unused void               *arg)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        struct rte_mbuf *m = pkts[i];
        pkt_metadata_fill(m);
    }

    *pkts_mask = (~0LLU) >> (64 - n);

    return PIPELINE_SUCCESS;
}

/* creates new rx pipeline with fdir-based forwarding,
 * does not enable any ports */
void
pipeline_rx_data_configure(struct pipeline_params *params)
{
    int ret;
    uint16_t i;
    struct rte_pipeline *p;

    PIPELINE_LOG_ENTRY();

    RTE_VERIFY(params);
    RTE_VERIFY(params->type == PIPELINE_TYPE_RX_DATA);

    /* pipeline configuration */
    struct rte_pipeline_params rte_params = {
        .name = "pipeline_rx_data",
        .socket_id = rte_lcore_to_socket_id(params->lcore_id),
        /* this defines the offset in packet metadata, where
         * the pipeline output port is stored */
        .offset_port_id = offsetof(struct pipeline_pkt_metadata, rte_pipeline_port_out_id),
    };

    PIPELINE_LOG_DEBUG("creating pipeline %s, lcore_id %d socket_id %d",
                  rte_params.name,
                  params->lcore_id,
                  rte_params.socket_id);
    p = rte_pipeline_create(&rte_params);
    RTE_VERIFY(p);

    /* pipeline input port configuration */
    for (i = 0; i < params->nb_q_in; i++) {
        struct rte_port_ethdev_reader_params port_ethdev_params = {
            .port_id = params->q_in[i].port_id,
            .queue_id = params->q_in[i].queue_id,
        };

        struct rte_pipeline_port_in_params port_params = {
            .ops = &rte_port_ethdev_reader_ops,
            .arg_create = (void *) &port_ethdev_params,
            .f_action = rx_action_handler,
            .arg_ah = NULL,
            .burst_size = PIPELINE_RX_BURST_MAX,
        };

        ret = rte_pipeline_port_in_create(p,
                                          &port_params,
                                          &params->q_in[i].rte_pipeline_port_id);
        RTE_VERIFY(ret == 0);

        PIPELINE_LOG_DEBUG("\tcreated port in: port id %d queue id %d pipeline port id: %d",
                            port_ethdev_params.port_id,
                            port_ethdev_params.queue_id,
                            params->q_in[i].rte_pipeline_port_id);
    }

    /* pipeline table configuration */
    struct rte_pipeline_table_params table_params = {
        .ops = &rte_table_stub_ops,
        .arg_create = NULL,
        .f_action_hit = NULL,
        .f_action_miss = NULL,
        .arg_ah = NULL,
        .action_data_size = 0,
    };

    PIPELINE_LOG_DEBUG("\tcreating stub table");
    ret = rte_pipeline_table_create(p,
                                    &table_params,
                                    &params->rte_pipeline_table_id);
    RTE_VERIFY(ret == 0);

    /* pipeline stub table configuration
     * the offset for the port in packet meta data
     * is set at pipeline creation time .offset_port_id */
    struct rte_pipeline_table_entry default_entry = {
        .action = RTE_PIPELINE_ACTION_PORT_META,
    };
    struct rte_pipeline_table_entry *default_entry_ptr;
    ret = rte_pipeline_table_default_entry_add(p,
                                               params->rte_pipeline_table_id,
                                               &default_entry,
                                               &default_entry_ptr);
    RTE_VERIFY(ret == 0);

    /* pipeline output port configuration */
    for (i = 0; i < params->nb_q_out; i++) {
        /* rings assosiacted with output ports and hardware tx queues */
        struct rte_port_ring_writer_params port_ring_params = {
            .ring = params->q_out[i].ring,
            .tx_burst_sz = PIPELINE_RX_BURST_MAX,
        };
        RTE_VERIFY(port_ring_params.ring != NULL);

        struct rte_pipeline_port_out_params port_params = {
            .ops = &rte_port_ring_writer_ops,
            .arg_create = (void *) &port_ring_params,
            .f_action = NULL,
            .f_action_bulk = NULL,
            .arg_ah = NULL
        };

        ret = rte_pipeline_port_out_create(p,
                                           &port_params,
                                           &params->q_out[i].rte_pipeline_port_id);
        RTE_VERIFY(ret == 0);

        PIPELINE_LOG_DEBUG("\tcreated port out: ring 0x%p pipeline port id: %d",
                            port_ring_params.ring,
                            params->q_out[i].rte_pipeline_port_id);
    }

    /* interconnect ports */
    for (i = 0; i < params->nb_q_in; i++) {
        ret = rte_pipeline_port_in_connect_to_table(p,
                                                    params->q_in[i].rte_pipeline_port_id,
                                                    params->rte_pipeline_table_id);
        RTE_VERIFY(ret == 0);
    }

    /* do not enable now, ports will be enabled as new data flows are added */
#ifndef PIPELINE_DATA_DISABLE
    // todo, for now. enable all ports for poc
    for (i = 0; i < params->nb_q_in; i++) {
        ret = rte_pipeline_port_in_enable(p, params->q_in[i].rte_pipeline_port_id);
        params->q_in[i].enabled = true;
        RTE_VERIFY(ret == 0);
    }
#endif

    /* check pipeline consistency */
    RTE_VERIFY(rte_pipeline_check(p) == 0);

    params->pipeline = p;
    params->run = rte_pipeline_run;

    rx_data_pipelines[nb_rx_data_pipelines++] = params;

    PIPELINE_LOG_EXIT();

}
