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
#include <rte_pipeline.h>
#include <rte_table_lpm.h>
#include <rte_ethdev.h>

#include "../common/common.h"

#include "pipeline.h"

static int
__attribute__((unused))
get_q_out_index(struct pipeline_params *pp, uint32_t internal_port_out_id, uint32_t *q_out_id)
{
    uint32_t i;

    RTE_VERIFY(pp);
    RTE_VERIFY(q_out_id);

    for (i = 0; i < pp->nb_q_out; i++) {
        if (pp->q_out[i].rte_pipeline_port_id == internal_port_out_id) {
            *q_out_id = i;
            return PIPELINE_SUCCESS;
        }
    }

    return PIPELINE_ERR;
}

static int
get_q_in_index(struct pipeline_params *pp, uint32_t internal_port_in_id, uint32_t *q_in_id)
{
    uint32_t i;

    RTE_VERIFY(pp);
    RTE_VERIFY(q_in_id);

    for (i = 0; i < pp->nb_q_in; i++) {
        if (pp->q_in[i].rte_pipeline_port_id == internal_port_in_id) {
            *q_in_id = i;
            return PIPELINE_SUCCESS;
        }
    }

    return PIPELINE_ERR;
}

static int
get_internal_port_out_id(struct pipeline_params *pp, uint8_t out_port_id, uint16_t out_queue_id, uint32_t *internal_id)
{
    uint32_t i;

    RTE_VERIFY(internal_id != NULL);

    for (i = 0; i < pp->nb_q_out; i++) {
        if (pp->q_out[i].port_id == out_port_id && pp->q_out[i].queue_id == out_queue_id) {
            *internal_id = pp->q_out[i].rte_pipeline_port_id;
            return PIPELINE_SUCCESS;
        }
    }

    return PIPELINE_ERR;
}

static int
get_internal_port_in_id(struct pipeline_params *pp, uint8_t in_port_id, uint16_t in_queue_id, uint32_t *internal_id)
{
    uint32_t i;

    RTE_VERIFY(internal_id != NULL);

    for (i = 0; i < pp->nb_q_out; i++) {
        if (pp->q_in[i].port_id == in_port_id && pp->q_in[i].queue_id == in_queue_id) {
            *internal_id = pp->q_in[i].rte_pipeline_port_id;
            return PIPELINE_SUCCESS;
        }
    }

    return PIPELINE_ERR;
}

int
pipeline_get_next_available_in_queue(struct pipeline_params *pp, uint8_t in_port_id, uint16_t *in_queue_id)
{
    uint32_t i;

    RTE_VERIFY(pp);
    RTE_VERIFY(in_queue_id);

    for (i = 0; i < pp->nb_q_in; i++) {
        if (pp->q_in[i].port_id == in_port_id && !pp->q_in[i].enabled) {
            *in_queue_id = pp->q_in[i].queue_id;
            return PIPELINE_SUCCESS;
        }
    }

    PIPELINE_LOG_DEBUG("no free pipeline in ports");

    return PIPELINE_ERR;
}


/* add single ipv4 rule to the rx_default pipeline
 * port_out_id is dpdk port id */
//todo make it static, messaging
int
pipeline_add_ipv4_rule(struct pipeline_params *pp, uint32_t ipv4, uint8_t port_out_id, uint16_t queue_out_id)
{
    int ret;

    PIPELINE_LOG_ENTRY();

    RTE_VERIFY(pp);

    if (pp->type != PIPELINE_TYPE_DEFAULT) {
        PIPELINE_LOG_ERR_AND_RETURN("invalid pipeline type");
    }

    struct rte_pipeline_table_entry entry = {
        .action = RTE_PIPELINE_ACTION_PORT,
    };
    ret = get_internal_port_out_id(pp, port_out_id, queue_out_id, &entry.port_id);
    PIPELINE_LOG_AND_RETURN_ON_ERR("invalid port out id");

    struct rte_table_lpm_key key = {
        .ip = ipv4,
        .depth = 32,
    };

    int key_found;
    struct rte_pipeline_table_entry *entry_ptr;

    ret = rte_pipeline_table_entry_add(pp->pipeline,
                                       pp->rte_pipeline_table_id,
                                       &key,
                                       &entry,
                                       &key_found,
                                       &entry_ptr);
    PIPELINE_LOG_AND_RETURN_ON_ERR("failed to entry to pipeline");

    PIPELINE_LOG_EXIT();

    return PIPELINE_SUCCESS;

error:
    return PIPELINE_ERR;
}

//todo make it static, messaging
int
pipeline_add_data_flow(struct pipeline_params *in_pp,
                       uint8_t in_port_id,
                       uint16_t in_queue_id,
                       struct pipeline_params *out_pp,
                       uint8_t out_port_id,
                       uint16_t out_queue_id,
                       struct rte_fdir_filter *data_flow)
{
    int ret;
    uint32_t fdir_id;

    PIPELINE_LOG_ENTRY();

    RTE_VERIFY(in_pp);
    RTE_VERIFY(out_pp);
    RTE_VERIFY(data_flow);

    if (in_pp->type != PIPELINE_TYPE_RX_DATA) {
        PIPELINE_LOG_ERR_AND_RETURN("invalid in pipeline type");
    }
    if (out_pp->type != PIPELINE_TYPE_TX_DATA) {
        PIPELINE_LOG_ERR_AND_RETURN("invalid out pipeline type");
    }

    /* enable pipeline in port */
    ret = pipeline_enable_port_in(in_pp, in_port_id, in_queue_id);
    PIPELINE_LOG_AND_RETURN_ON_ERR("failed to enable in port");

    /* enable pipeline out port */
    ret = pipeline_enable_port_in(out_pp, out_port_id, out_queue_id);
    PIPELINE_LOG_AND_RETURN_ON_ERR("failed to enable out port");

    /* get the associated out port of the rx pipeline
     * this is the fdir id used for port meta action */
    ret = get_internal_port_out_id(in_pp,
                                   out_port_id,
                                   out_queue_id,
                                   &fdir_id);
    PIPELINE_LOG_AND_RETURN_ON_ERR("in pipeline has no output to port %d queue %d", out_port_id, out_queue_id);

    /* add the fdir filter */ 
    ret = rte_eth_dev_fdir_add_perfect_filter(in_port_id,
                                              data_flow,
                                              fdir_id,
                                              in_queue_id,
                                              0);
    PIPELINE_LOG_AND_RETURN_ON_ERR("failed to add fdir perfect filter: err %d", ret);

    PIPELINE_LOG_EXIT();

    return PIPELINE_SUCCESS;

error:
    return PIPELINE_ERR;
}

int
pipeline_enable_port_in(struct pipeline_params *pp, uint8_t in_port_id, uint16_t in_queue_id)
{
    int ret;

    PIPELINE_LOG_ENTRY();
    
    RTE_VERIFY(pp);

    uint32_t port_id;
    ret = get_internal_port_in_id(pp, in_port_id, in_queue_id, &port_id);
    PIPELINE_LOG_AND_RETURN_ON_ERR("invalid port in id");

    ret = rte_pipeline_port_in_enable(pp->pipeline, port_id);
    PIPELINE_LOG_AND_RETURN_ON_ERR("failed to enable port %d queue %d", in_port_id, in_queue_id);

    uint32_t q_id;
    ret = get_q_in_index(pp, port_id, &q_id);
    RTE_VERIFY(ret == PIPELINE_SUCCESS);
    pp->q_in[q_id].enabled = true;

    PIPELINE_LOG_EXIT();

    return PIPELINE_SUCCESS;

error:
    return PIPELINE_ERR;
}

int pipeline_init(struct pipeline_params *pp, uint32_t lcore_id, enum pipeline_type type)
{
    PIPELINE_LOG_ENTRY();
    
    RTE_VERIFY(pp);
    if (type >= PIPELINE_TYPE_MAX) {
        PIPELINE_LOG_ERR_AND_RETURN("unknown pipeline type %d", type);
    }

    pp->lcore_id = lcore_id;
    pp->type = type;
    pp->nb_q_in = 0;
    pp->nb_q_out = 0;

    char name[32];
    snprintf(name, sizeof(name), "ring_msg_req_%p", pp);
    pp->ring_req = rte_ring_create(name,
                                   PIPELINE_MSG_RING_SIZE,
                                   rte_lcore_to_socket_id(pp->lcore_id),
                                   RING_F_SP_ENQ | RING_F_SC_DEQ);
    RTE_VERIFY(pp->ring_req != NULL);

    snprintf(name, sizeof(name), "ring_msg_resp_%p", pp);
    pp->ring_resp = rte_ring_create(name,
                                   PIPELINE_MSG_RING_SIZE,
                                   rte_lcore_to_socket_id(pp->lcore_id),
                                   RING_F_SP_ENQ | RING_F_SC_DEQ);

    RTE_VERIFY(pp->ring_resp != NULL);

    PIPELINE_LOG_EXIT();

    return PIPELINE_SUCCESS;

error:
    return PIPELINE_ERR;
}

int
pipeline_init_port_out(struct pipeline_params *pp, uint8_t out_port_id, uint16_t out_queue_id, struct rte_ring *out_ring)
{
    PIPELINE_LOG_ENTRY();

    PIPELINE_LOG_DEBUG("init port out: p %d q %d ring %p", out_port_id, out_queue_id, out_ring);

    if (pp->nb_q_out >= PIPELINE_QUEUE_OUT_MAX) {
        PIPELINE_LOG_DEBUG("cannot init new port, max pipeline out ports count reached");
        return PIPELINE_ERR;
    }

    if (out_ring != NULL && pp->type == PIPELINE_TYPE_TX_DATA) {
        PIPELINE_LOG_ERR_AND_RETURN("cannot associate out ring with tx pipeline");
    }
    if (out_ring == NULL && pp->type == PIPELINE_TYPE_RX_DATA) {
        PIPELINE_LOG_ERR_AND_RETURN("out ring required for rx pipeline");
    }

    if (pp->nb_q_out > 0) {
        if (pp->type == PIPELINE_TYPE_RX_DATA && pp->q_out[pp->nb_q_out - 1].port_id != out_port_id) {
            PIPELINE_LOG_ERR_AND_RETURN("port out id %d must be the same across all out ports of the rx data pipeline", pp->q_out[pp->nb_q_out - 1].port_id);
        }
        else if (pp->type == PIPELINE_TYPE_TX_DATA && pp->q_out[pp->nb_q_out - 1].port_id != out_port_id) {
            PIPELINE_LOG_ERR_AND_RETURN("port out id %d must be the same across all out ports of the tx data pipeline", pp->q_out[pp->nb_q_out - 1].port_id);
        }
    }

    pp->q_out[pp->nb_q_out].port_id = out_port_id;
    pp->q_out[pp->nb_q_out].queue_id = out_queue_id;
    pp->q_out[pp->nb_q_out++].ring = out_ring;

    return PIPELINE_SUCCESS;

    PIPELINE_LOG_EXIT();

error:
    return PIPELINE_ERR;
}

int
pipeline_init_port_in(struct pipeline_params *pp, uint8_t in_port_id, uint16_t in_queue_id, struct rte_ring *in_ring)
{
    PIPELINE_LOG_ENTRY();

    PIPELINE_LOG_DEBUG("init port in: p %d q %d ring %p", in_port_id, in_queue_id, in_ring);

    if (pp->nb_q_in >= PIPELINE_QUEUE_IN_MAX) {
        PIPELINE_LOG_DEBUG("cannot init new port, max pipeline in ports count reached");
        return PIPELINE_ERR;
    }

    if (in_ring == NULL && pp->type == PIPELINE_TYPE_TX_DATA) {
        PIPELINE_LOG_ERR_AND_RETURN("in ring required for tx pipeline");
    }
    if (in_ring != NULL && pp->type == PIPELINE_TYPE_RX_DATA) {
        PIPELINE_LOG_ERR_AND_RETURN("cannot associate in ring with rx pipeline");
    }

    if (pp->nb_q_in > 0) {
        if (pp->type == PIPELINE_TYPE_RX_DATA && pp->q_in[pp->nb_q_in - 1].queue_id != in_queue_id) {
            PIPELINE_LOG_ERR_AND_RETURN("rx-q id %d must be the same across all input ports of the rx data pipeline", pp->q_in[pp->nb_q_in - 1].queue_id);
        }
        else if (pp->type == PIPELINE_TYPE_TX_DATA && pp->q_in[pp->nb_q_in - 1].port_id != in_port_id) {
            PIPELINE_LOG_ERR_AND_RETURN("port in id %d  must be the same across all input ports of the tx data pipeline", pp->q_in[pp->nb_q_in - 1].port_id);
        }
    }

    pp->q_in[pp->nb_q_in].port_id = in_port_id;
    pp->q_in[pp->nb_q_in].queue_id = in_queue_id;
    pp->q_in[pp->nb_q_in].ring = in_ring;
    pp->q_in[pp->nb_q_in++].enabled = false;

    PIPELINE_LOG_EXIT();

    return PIPELINE_SUCCESS;

error:
    return PIPELINE_ERR;
}

void
pipeline_dump_cfg(struct pipeline_params *pp)
{
    unsigned i;

    PIPELINE_LOG_ENTRY();

    printf("rte_pipeline: 0x%p\n", pp->pipeline);
    printf("type: %d\n", pp->type);
    printf("lcore id: %d\n", pp->lcore_id);
    for (i = 0; i < pp->nb_q_in; i++) {
        printf("\tq_in %d pid %d qid %d ring 0x%p rte_id %d enabled %d\n",
                i,
                pp->q_in[i].port_id,
                pp->q_in[i].queue_id,
                pp->q_in[i].ring,
                pp->q_in[i].rte_pipeline_port_id,
                pp->q_in[i].enabled);
    }
    printf("\ttable id: %d\n", pp->rte_pipeline_table_id);
    for (i = 0; i < pp->nb_q_out; i++) {
        printf("\tq_out %d pid %d qid %d ring 0x%p rte_id %d\n",
                i,
                pp->q_out[i].port_id,
                pp->q_out[i].queue_id,
                pp->q_out[i].ring,
                pp->q_out[i].rte_pipeline_port_id);
    }
    

    PIPELINE_LOG_EXIT();
}
