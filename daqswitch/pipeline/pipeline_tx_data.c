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
#include <rte_table_stub.h>
#include <rte_port.h>
#include <rte_port_ethdev.h>
#include <rte_port_ring.h>
#include <rte_debug.h>

#include "../common/common.h"

#include "pipeline.h"

/* creates new tx pipeline,
 * does not enably any ports */
void
pipeline_tx_data_configure(struct pipeline_params *params)
{
    int ret;
    uint16_t i;
    uint32_t stub_table_id[PIPELINE_QUEUE_OUT_MAX];
    struct rte_pipeline *p;

    PIPELINE_LOG_ENTRY();

    RTE_VERIFY(params);
    RTE_VERIFY(params->type == PIPELINE_TYPE_TX_DATA);

    /* pipeline configuration */
    struct rte_pipeline_params rte_params = {
        .name = "dp_pipeline_tx",
        .socket_id = rte_lcore_to_socket_id(params->lcore_id),
    };

    PIPELINE_LOG_DEBUG("creating pipeline %s, lcore_id %d socket_id %d",
                  rte_params.name,
                  params->lcore_id,
                  rte_params.socket_id);
    p = rte_pipeline_create(&rte_params);
    RTE_VERIFY(p);

    /* input port configuration */
    for (i = 0; i < params->nb_q_in; i++) {
        /* rings assosiacted with output ports and hardware tx queues */
        struct rte_port_ring_writer_params port_ring_params = {
            .ring = params->q_in[i].ring,
        };
        RTE_VERIFY(port_ring_params.ring != NULL);

        struct rte_pipeline_port_in_params port_params = {
            .ops = &rte_port_ring_reader_ops,
            .arg_create = (void *) &port_ring_params,
            .f_action = NULL,
            .arg_ah = NULL,
            .burst_size = PIPELINE_TX_BURST_MAX,
        };

        ret = rte_pipeline_port_in_create(p,
                                          &port_params,
                                          &params->q_in[i].rte_pipeline_port_id);
        RTE_VERIFY(ret == 0);
        
        PIPELINE_LOG_DEBUG("\tcreated port in: ring 0x%p pipeline port id: %d",
                            port_ring_params.ring,
                            params->q_in[i].rte_pipeline_port_id);
    }

    /* there is 1-to-1 correspondence between the input software rings
     * and the output eth ports */
    RTE_VERIFY(params->nb_q_out = params->nb_q_in);

    /* pipeline stub table configuration */
    for (i = 0; i < params->nb_q_out; i++) {
        struct rte_pipeline_table_params table_params = {
            .ops = &rte_table_stub_ops,
            .arg_create = NULL,
            .f_action_hit = NULL,
            .f_action_miss = NULL,
            .arg_ah = NULL,
            .action_data_size = 0,
        };

        /* do not save the id anywhere,
         * we do not need it */
        ret = rte_pipeline_table_create(p,
                                        &table_params,
                                        &stub_table_id[i]);
        RTE_VERIFY(ret == 0);
    }

    /* pipeline output port configuration */
    for (i = 0; i < params->nb_q_out; i++) {
        struct rte_port_ethdev_writer_params port_ethdev_params = {
            .port_id = params->q_out[i].port_id,
            .queue_id = params->q_out[i].queue_id,
            .tx_burst_sz = PIPELINE_TX_BURST_MAX,
        };

        struct rte_pipeline_port_out_params port_params = {
#ifdef PIPELINE_NO_DROP
            .ops = &rte_port_ethdev_writer_no_drop_ops,
#else
            .ops = &rte_port_ethdev_writer_ops,
#endif
            .arg_create = (void *) &port_ethdev_params,
            .f_action = NULL,
            .f_action_bulk = NULL,
            .arg_ah = NULL,
        };

        ret = rte_pipeline_port_out_create(p,
                                           &port_params,
                                           &params->q_out[i].rte_pipeline_port_id);
        RTE_VERIFY(ret == 0);

        PIPELINE_LOG_DEBUG("\tcreated port out: port id %d queue id %d pipeline port id: %d",
                            port_ethdev_params.port_id,
                            port_ethdev_params.queue_id,
                            params->q_out[i].rte_pipeline_port_id);
    }

    /* pipeline stub table configuration */
    for (i = 0; i < params->nb_q_out; i++) {
        struct rte_pipeline_table_entry default_entry = {
            .action = RTE_PIPELINE_ACTION_PORT,
            {.port_id = params->q_out[i].rte_pipeline_port_id},
        };
        struct rte_pipeline_table_entry *default_entry_ptr;
        ret = rte_pipeline_table_default_entry_add(p,
                                                   stub_table_id[i],
                                                   &default_entry,
                                                   &default_entry_ptr);
        RTE_VERIFY(ret == 0);

    }

    /* interconnect ports */
    for (i = 0; i < params->nb_q_in; i++) {
        ret = rte_pipeline_port_in_connect_to_table(p,
                                                    params->q_in[i].rte_pipeline_port_id,
                                                    stub_table_id[i]);
        RTE_VERIFY(ret == 0);
    }

    /* do not enable ports
     * do it on request only */
    // todo, for now. enable all ports for poc
#ifndef PIPELINE_DATA_DISABLE
    for (i = 0; i < params->nb_q_in; i++) {
        ret = rte_pipeline_port_in_enable(p, params->q_in[i].rte_pipeline_port_id);
        RTE_VERIFY(ret == 0);
        params->q_in[i].enabled = true;
    }
#endif

    /* check pipeline consistency */
    RTE_VERIFY(rte_pipeline_check(p) == 0);

    params->pipeline = p;
#ifdef PIPELINE_NO_DROP
    params->run = rte_pipeline_run_no_drop;
#else
    params->run = rte_pipeline_run;
#endif

    PIPELINE_LOG_EXIT();

}
