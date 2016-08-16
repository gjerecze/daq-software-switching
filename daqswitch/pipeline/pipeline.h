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
#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdint.h>
#include <stdbool.h>

#include <rte_pipeline.h>
#include <rte_ring.h>
#include <rte_ethdev.h>

/* defines */
#define PIPELINE_TX_BURST_MAX                                      32
#define PIPELINE_RX_BURST_MAX                                      32
#define PIPELINE_QUEUE_IN_MAX                                      64
#define PIPELINE_QUEUE_OUT_MAX                                     64
#define PIPELINE_FORWARDING_RULES_MAX                              16
#define PIPELINE_MSG_RING_SIZE                                    256

#define PIPELINE_QUEUE_FROM_FDIR_ID_MASK                          0xf
#define PIPELINE_DCM_TO_ROS_FDIR_ID_SHIFT                           0
#define PIPELINE_ROS_TO_DCM_FDIR_ID_SHIFT                           4


/* flow key for lpm-based lookups */
struct pipeline_flow_key {
#ifndef PIPELINE_DATA_DISABLE
    union {
        struct {
            uint8_t ttl;
            uint8_t proto;
            uint16_t header_checksum;
            uint32_t sip;
        };
        uint64_t slab0;
    };
#endif

    union {
        struct {
            uint32_t dip;
            uint16_t sport;
            uint16_t dport;
        };
        uint64_t slab1;
    };
#ifndef PIPELINE_DATA_DISABLE
    union {
        struct {
            /* tdaq */
            uint32_t type_id;
            uint32_t transaction_id;
        };
        uint64_t slab2;
    };

    union {
        struct {
            /* tdaq */
            uint32_t size;
            uint32_t event_id;
        };
        uint64_t slab3;
    };
#endif

} __attribute__((__packed__));

#ifndef PIPELINE_DATA_DISABLE
/* atlas tdaq protocol header */
struct tdaq_hdr {
    uint32_t typeId;
    uint32_t transactionId;
    uint32_t size;
    union {
        struct {
            uint32_t event_id;
        };
    };
};
#endif

#define TDAQ_TYPE_ID_FRAGMENT_REQUEST_MESSAGE   0x00dcdf20 /* big endian */

/* packet metadata */
struct pipeline_pkt_metadata {
    union { 
        /* lpm-based lookups */
        struct pipeline_flow_key flow_key;
        /* fdir-based lookups */
        uint32_t rte_pipeline_port_out_id;
    };
} __attribute__((__packed__));

/* pipeline types */
enum pipeline_type {
    PIPELINE_TYPE_NONE = 0,
    PIPELINE_TYPE_DEFAULT,
    PIPELINE_TYPE_RX_DATA,
    PIPELINE_TYPE_TX_DATA,
    PIPELINE_TYPE_MAX,
};

/* pipeline parameters */
struct pipeline_params {
    /* associated rte_pipeline */
    struct rte_pipeline *pipeline;
    int (*run)(struct rte_pipeline *);

    enum pipeline_type type;
    uint32_t lcore_id;

    /* in queues
     * hw-nic-queues or sw-rings */
    struct {
        struct {
            uint8_t  port_id;
            uint16_t queue_id;
        };
        /* ring if sw-queue
         * 1:1 mapping between sw-ring and hw-queue*/
        struct rte_ring *ring;
        /* internal id */
        uint32_t rte_pipeline_port_id;
        /* is enabled */
        bool enabled;
    } q_in[PIPELINE_QUEUE_IN_MAX];
    uint32_t nb_q_in;

    //todo move to pipeline_default.c
    union {
        /* forwarding table
         * default pipeline only */
        uint32_t rte_pipeline_table_id;
    };

    /* out queues
     * hw-nic-queues or sw-rings*/
    struct {
        struct {
            uint8_t  port_id;
            uint16_t queue_id;
        };
        /* ring if sw-queue
         * 1:1 mapping between sw-ring and hw-queue*/
        struct rte_ring *ring;
        /* internal id */
        uint32_t rte_pipeline_port_id;

#ifndef PIPELINE_DATA_DISABLE
        /* for poc */
        struct {
            uint32_t dip;
            uint32_t event_id;
        };
#endif
    } q_out[PIPELINE_QUEUE_OUT_MAX];
    uint32_t nb_q_out;

    /* msg rings */
    struct rte_ring *ring_req;
    struct rte_ring *ring_resp;

} __rte_cache_aligned;

/* message handling */
enum pipeline_msg_req_type {
    PIPELINE_MSG_REQ_ENABLE_PORT_IN,
};

struct pipeline_msg_req {
    enum pipeline_msg_req_type type;
    union {
        struct {
            uint8_t port_id;
            uint16_t queue_id;
        };
    };
};

/* function prototypes */
void pipeline_default_configure(struct pipeline_params *params);
void pipeline_rx_data_configure(struct pipeline_params *params);
void pipeline_tx_data_configure(struct pipeline_params *params);
void pipeline_msg_handle(struct pipeline_params *params);

int pipeline_init(struct pipeline_params *pp, uint32_t lcore_id,
                  enum pipeline_type type);
int pipeline_init_port_out(struct pipeline_params *pp, uint8_t out_port_id,
                           uint16_t out_queue_id, struct rte_ring *out_ring);
int pipeline_init_port_in(struct pipeline_params *pp, uint8_t in_port_id,
                          uint16_t in_queue_id, struct rte_ring *in_ring);
int pipeline_add_ipv4_rule(struct pipeline_params *pp, uint32_t ipv4,
                           uint8_t port_out_id, uint16_t queue_out_id);
int pipeline_add_data_flow(struct pipeline_params *in_pp, uint8_t in_port_id, uint16_t in_queue_id,
                           struct pipeline_params *out_pp, uint8_t out_port_id, uint16_t out_queue_id,
                           struct rte_fdir_filter *data_flow);
int pipeline_get_next_available_in_queue(struct pipeline_params *pp,
                                         uint8_t in_port_id, uint16_t *in_queue_id);
int pipeline_enable_port_in(struct pipeline_params *pp, uint8_t in_port_id, uint16_t in_queue_id);

void pipeline_dump_cfg(struct pipeline_params *pp);

struct pipeline_params *find_data_pipeline(uint8_t port_out_id);

#endif /* PIPELINE_H */
