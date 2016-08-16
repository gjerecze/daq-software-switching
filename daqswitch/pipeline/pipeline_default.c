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
#include <netinet/in.h>
#include <rte_pipeline.h>
#include <rte_port.h>
#include <rte_port_ethdev.h>
#include <rte_port_ring.h>
#include <rte_table_lpm.h>
#include <rte_mbuf.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>

#include "../common/common.h"
#include "../daqswitch/daqswitch.h"

#include "pipeline.h"

#ifdef PIPELINE_DBG
static void dump_pkt(struct rte_mbuf *pkt)
{
    /* get the pointer to start of packet data in mbuf */
    uint8_t *m_data = rte_pktmbuf_mtod(pkt, uint8_t *);

    /* get the ip header */
    struct ipv4_hdr *ip_hdr = 
        (struct ipv4_hdr *) &m_data[sizeof(struct ether_hdr)];


    if (ip_hdr->next_proto_id == IPPROTO_TCP) {
        /* get the tcp header */
        struct tcp_hdr *tcp_hdr = 
            (struct tcp_hdr *) ((uint8_t *) ip_hdr + sizeof(struct ipv4_hdr));

        uint8_t tcp_hdr_size = (tcp_hdr->data_off >> 4) * 4; /* bytes */

        if ((rte_be_to_cpu_16(ip_hdr->total_length) - sizeof(struct ipv4_hdr) - tcp_hdr_size)
                >= sizeof(struct tdaq_hdr)) {
            /* get the tdaq header */
            struct tdaq_hdr *tdaq_hdr = 
                (struct tdaq_hdr *) ((uint8_t *) tcp_hdr + tcp_hdr_size);
            if (tdaq_hdr->typeId == TDAQ_TYPE_ID_FRAGMENT_REQUEST_MESSAGE) {
                printf("\n############### pkt ###############\n");

                printf("proto: %d sip: 0x%08x dip: 0x%08x length: %d\n",
                        ip_hdr->next_proto_id, rte_be_to_cpu_32(ip_hdr->src_addr),
                        rte_be_to_cpu_32(ip_hdr->dst_addr), rte_be_to_cpu_16(ip_hdr->total_length));

                printf("sport: %d dport: %d tcp_hdr_size %d bytes\n",
                        rte_be_to_cpu_16(tcp_hdr->src_port), rte_be_to_cpu_16(tcp_hdr->dst_port), tcp_hdr_size);

                printf("tdaq fragment request message eventId: 0x%08x\n",
                        tdaq_hdr->event_id);

                printf("############### end pkt ###############\n\n");
                fflush(stdout);
            }
        }
    }
};
#endif

#ifndef PIPELINE_DATA_DISABLE
static int get_next_data_queue_internal_id(struct pipeline_params *pp,
                                           uint32_t dip, uint32_t event_id,
                                           uint16_t *id)
{
    uint32_t i;

    for (i = 0; i < pp->nb_q_out; i++) {

        if (pp->q_out[i].dip == dip && pp->q_out[i].event_id == event_id) {
            *id = pp->q_out[i].rte_pipeline_port_id;
            return PIPELINE_SUCCESS;
        }
    
    }

    for (i = 0; i < pp->nb_q_out; i++) {
        if (pp->q_out[i].dip == 0) {
            *id = pp->q_out[i].rte_pipeline_port_id;
            pp->q_out[i].dip = dip;
            pp->q_out[i].event_id = event_id;
            return PIPELINE_SUCCESS;
        }
    
    }

    return PIPELINE_ERR;
}
#endif

/* fills the packet metadata to be used for lpm-based forwarding */
static inline void
pkt_metadata_fill(struct rte_mbuf *m)
{
    /* get the pointer to start of packet data in mbuf */
    uint8_t *m_data = rte_pktmbuf_mtod(m, uint8_t *);

    /* access metadata in the mbuf headroom */
    struct pipeline_pkt_metadata *c =
        (struct pipeline_pkt_metadata *) RTE_MBUF_METADATA_UINT8_PTR(m, 0);

    /* get the ip header */
    struct ipv4_hdr *ip_hdr = 
        (struct ipv4_hdr *) &m_data[sizeof(struct ether_hdr)];
    uint64_t *ipv4_hdr_slab = (uint64_t *) ip_hdr;

    /* fill metadata, start with ttl, end with tcp src and dest ports */
#ifndef PIPELINE_DATA_DISABLE
    c->flow_key.slab0 = ipv4_hdr_slab[1];
#endif
    c->flow_key.slab1 = ipv4_hdr_slab[2];
#ifndef PIPELINE_DATA_DISABLE
    c->flow_key.slab2 = 0;
    c->flow_key.slab3 = 0;

    /* fill tdaq event id, if packet is a fragment request message */
    if (ip_hdr->next_proto_id == IPPROTO_TCP) {

        /* get the tcp header */
        struct tcp_hdr *tcp_hdr = 
            (struct tcp_hdr *) ((uint8_t *) ip_hdr + sizeof(struct ipv4_hdr));

        uint8_t tcp_hdr_size = (tcp_hdr->data_off >> 4) * 4; /* bytes */

        if ((rte_be_to_cpu_16(ip_hdr->total_length) - sizeof(struct ipv4_hdr) - tcp_hdr_size)
                >= sizeof(struct tdaq_hdr)) {
            /* get the tdaq header */
            uint64_t *tdaq_hdr_slab = 
                (uint64_t *) ((uint8_t *) ip_hdr + sizeof(struct ipv4_hdr) + tcp_hdr_size);

            c->flow_key.slab2 = tdaq_hdr_slab[0];
            c->flow_key.slab3 = tdaq_hdr_slab[1];

#ifdef PIPELINE_DBG
            if (c->flow_key.type_id == TDAQ_TYPE_ID_FRAGMENT_REQUEST_MESSAGE) {
                printf("tdaq fragment request message src 0x%08x:%d dst 0x%08x:%d id 0x%08x\n",
                        rte_be_to_cpu_32(c->flow_key.sip), rte_be_to_cpu_16(c->flow_key.sport),
                        rte_be_to_cpu_32(c->flow_key.dip), rte_be_to_cpu_16(c->flow_key.dport),
                        c->flow_key.event_id);
                fflush(stdout);
            }
#endif
        }
    }
#endif
}

/* default action for all rx packets */
static int
rx_action_handler(struct rte_mbuf               **pkts,
                  uint32_t                           n,
                  uint64_t                  *pkts_mask,
                  __rte_unused void               *arg)
{
    int i;

#if 0
#define PREFETCH_OFFSET 3
    /* prefetch first packets */
    for (i = 0; i < PREFETCH_OFFSET && i < (int) n; i++) {
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i], void *)); 
        //rte_prefetch0(RTE_MBUF_METADATA_UINT8_PTR(pkts[i], 0));
    }

    /* prefetch and process already prefetched packets */
    for (i = 0; i < ((int) n - PREFETCH_OFFSET); i++) {
        rte_prefetch0(rte_pktmbuf_mtod(pkts[i + PREFETCH_OFFSET], void *)); 
        //rte_prefetch0(RTE_MBUF_METADATA_UINT8_PTR(pkts[i + PREFETCH_OFFSET], 0));
        pkt_metadata_fill(pkts[i]);
    }

    /* process remaining prefetched packets */
    for (; i < (int) n; i++) {
        pkt_metadata_fill(pkts[i]);
    }
#endif

#if 1
    for (i = 0; i < (int) n; i++) {
        pkt_metadata_fill(pkts[i]);
#ifdef PIPELINE_DBG
        dump_pkt(pkts[i]);
#endif
    }
#endif

#if 0
    i = 0;
    switch (n % 4) {
    while (i < (int) n) {
    case 0:
        pkt_metadata_fill(pkts[i]);
        i++;
    case 3:
        pkt_metadata_fill(pkts[i]);
        i++;
    case 2:
        pkt_metadata_fill(pkts[i]);
        i++;
    case 1:
        pkt_metadata_fill(pkts[i]);
        i++;
    }
    }
#endif

    *pkts_mask = (~0LLU) >> (64 - n);
    
    return PIPELINE_SUCCESS;
}

#ifndef PIPELINE_DATA_DISABLE
static int
table_action_handler_hit(struct rte_mbuf **pkts, uint64_t *pkts_mask,    
                         struct rte_pipeline_table_entry **entries, __attribute__((unused)) void *arg)
{
    uint64_t pkts_in_mask = *pkts_mask;
    uint16_t fdir_id;
    int ret;
    struct rte_fdir_filter filter;

    // todo very poc, many hard-codes for current testbed configuration
    //      not thread safe at the moment!
    // todo do it as a separate table (flow classification)
    memset(&filter, 0, sizeof(struct rte_fdir_filter));

    for ( ; pkts_in_mask; ) {
        uint64_t pkt_mask;
        uint32_t pkt_index;

        pkt_index = __builtin_ctzll(pkts_in_mask);
        pkt_mask = 1LLU << pkt_index;
        pkts_in_mask &= ~pkt_mask;

        struct rte_mbuf *pkt = pkts[pkt_index];

        /* access metadata in the mbuf headroom */
        struct pipeline_flow_key *flow_key =
            (struct pipeline_flow_key *) RTE_MBUF_METADATA_UINT8_PTR(pkt, 0);

        if (flow_key->type_id == TDAQ_TYPE_ID_FRAGMENT_REQUEST_MESSAGE &&
             flow_key->sport != filter.port_src) {

            /* from ros to dcm */
            struct pipeline_params *data_pipeline = find_data_pipeline(pkt->port);
            
            ret = get_next_data_queue_internal_id(data_pipeline,
                                                  flow_key->sip,
                                                  flow_key->event_id,
                                                  &fdir_id); 
            if (ret != PIPELINE_SUCCESS) {
#ifdef PIPELINE_DBG
                printf("\tno more filters\n");
                fflush(stdout);
#endif
                continue;
            }
#ifdef PIPELINE_DBG
            printf("#### new data flow detected\n");
            fflush(stdout);
#endif
            filter.l4type = RTE_FDIR_L4TYPE_TCP;
            filter.ip_dst.ipv4_addr = flow_key->sip;
            filter.port_dst = flow_key->sport;
            filter.ip_src.ipv4_addr = flow_key->dip;
            filter.port_src = flow_key->dport;

            fdir_id <<= PIPELINE_DCM_TO_ROS_FDIR_ID_SHIFT;
            
            ret = rte_eth_dev_fdir_add_perfect_filter(entries[pkt_index]->port_id,
                                                      &filter,
                                                      fdir_id,
                                                      data_pipeline->q_in[0].queue_id,
                                                      0);
            RTE_VERIFY(ret == 0);
            rte_eth_set_queue_rate_limit(pkt->port, fdir_id + 1, 1000);
            
#ifdef PIPELINE_DBG
            printf("\tnew filter p:q %d:%d "
                    "ros->dcm flow 0x%08x:%d->0x%08x:%d id 0x%08x fdir:%d\n",
                    entries[pkt_index]->port_id, data_pipeline->q_in[0].queue_id,
                    rte_be_to_cpu_32(filter.ip_src.ipv4_addr), rte_be_to_cpu_16(filter.port_src),
                    rte_be_to_cpu_32(filter.ip_dst.ipv4_addr), rte_be_to_cpu_16(filter.port_dst),
                    flow_key->event_id, fdir_id);
            fflush(stdout);
#endif

            /* from dcm to ros */
            data_pipeline = find_data_pipeline(entries[pkt_index]->port_id);
            filter.l4type = RTE_FDIR_L4TYPE_TCP;
            filter.ip_dst.ipv4_addr = flow_key->dip;
            filter.port_dst = flow_key->dport;
            filter.ip_src.ipv4_addr = flow_key->sip;
            filter.port_src = flow_key->sport;

            /* all fdir_id's must be unique within a single NIC
             * ROS to DCM flows need to have the same output queue id
             * use 2 lower bytes for the output queue id
             * higher 2 bytes to ensure fdir id uniqueness */
            fdir_id = fdir_id | (fdir_id << PIPELINE_ROS_TO_DCM_FDIR_ID_SHIFT);

            ret = rte_eth_dev_fdir_add_perfect_filter(pkt->port,
                                                      &filter,
                                                      fdir_id,
                                                      data_pipeline->q_in[0].queue_id,
                                                      0);
            RTE_VERIFY(ret == 0);

#ifdef PIPELINE_DBG
            printf("\tnew filter p:q %d:%d "
                    "dcm->ros flow 0x%08x:%d->0x%08x:%d id 0x%08x fdir:%d\n",
                    pkt->port, data_pipeline->q_in[0].queue_id,
                    rte_be_to_cpu_32(filter.ip_src.ipv4_addr), rte_be_to_cpu_16(filter.port_src),
                    rte_be_to_cpu_32(filter.ip_dst.ipv4_addr), rte_be_to_cpu_16(filter.port_dst),
                    flow_key->event_id, fdir_id);
            printf("###\n");
            fflush(stdout);
#endif
        }

    }

    return PIPELINE_SUCCESS;
}
#endif

/* creates new pipeline with default lpm-based forwarding,
 * does not enable any ports */
void
pipeline_default_configure(struct pipeline_params *params)
{
    int ret;
    uint16_t i;
    struct rte_pipeline *p;

    PIPELINE_LOG_ENTRY();

    RTE_VERIFY(params);
    RTE_VERIFY(params->type == PIPELINE_TYPE_DEFAULT);

    /* pipeline configuration */
    struct rte_pipeline_params rte_params = {
        .name = "pipeline_rx_default",
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

    /* pipeline forwarding table configuration */
    {
        struct rte_table_lpm_params table_lpm_params = {
            .n_rules = PIPELINE_FORWARDING_RULES_MAX,
            .entry_unique_size = sizeof(struct rte_pipeline_table_entry),
            /* forwarding based on destination IP only */
            .offset = __builtin_offsetof(struct pipeline_pkt_metadata, flow_key.dip),
#if 0
            .offset = sizeof(struct ether_hdr) + __builtin_offsetof(struct ipv4_hdr, dst_addr),
#endif
        };

        struct rte_pipeline_table_params table_params = {
            .ops = &rte_table_lpm_ops,
            .arg_create = &table_lpm_params,
            .f_action_miss = NULL,
#ifdef PIPELINE_DATA_DISABLE
            .f_action_hit = NULL,
#else
            .f_action_hit = table_action_handler_hit,
#endif
            .arg_ah = NULL,
            .action_data_size = 0,
        };

        PIPELINE_LOG_DEBUG("\tcreating lpm table");
        ret = rte_pipeline_table_create(p,
                                        &table_params,
                                        &params->rte_pipeline_table_id);
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
            .ops = &rte_port_ethdev_writer_ops,
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

    /* interconnect ports */
    for (i = 0; i < params->nb_q_in; i++) {
        ret = rte_pipeline_port_in_connect_to_table(p,
                                                    params->q_in[i].rte_pipeline_port_id,
                                                    params->rte_pipeline_table_id);
        RTE_VERIFY(ret == 0);
    }

    /* do not enable ports
     * do it on request only */

    /* check pipeline consistency */
    RTE_VERIFY(rte_pipeline_check(p) == 0);

    params->pipeline = p;
    params->run = rte_pipeline_run;

    PIPELINE_LOG_EXIT();

}
