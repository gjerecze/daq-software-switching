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
#include <rte_mbuf.h>
#include <rte_port.h>
#include <rte_port_ethdev.h>
#include <rte_table_lpm.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_byteorder.h>
#include <rte_cycles.h>

#include "../../daqswitch/daqswitch_port.h"
#include "../../stats/stats.h"
#include "dp_voq_swq.h"

#define TDAQ_TYPE_ID_FRAGMENT_REQUEST_MESSAGE          0x00dcdf20 /* big endian */

static struct rte_pipeline *p;
static uint32_t port_in_id[DAQSWITCH_MAX_PORTS];
static uint32_t port_out_id[DAQSWITCH_MAX_PORTS];
static uint32_t table_id;
static uint16_t fdir_id[DAQSWITCH_MAX_PORTS] = {0};

/* flow key for lpm-based lookups */
struct pipeline_flow_key {
    union {
        struct {
            uint8_t ttl;
            uint8_t proto;
            uint16_t header_checksum;
            uint32_t sip;
        };
        uint64_t slab0;
    };

    union {
        struct {
            uint32_t dip;
            uint16_t sport;
            uint16_t dport;
        };
        uint64_t slab1;
    };

#ifndef DAQ_DATA_FLOWS_DISABLE
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

#ifndef DAQ_DATA_FLOWS_DISABLE
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

/* packet metadata */
struct pipeline_pkt_metadata {
    union { 
        /* lpm-based lookups */
        struct pipeline_flow_key flow_key;
    };
} __attribute__((__packed__));

#ifdef DAQ_DATA_FLOWS_DUMP_PKT
static void dump_pkt(struct rte_mbuf *pkt)
{
    uint8_t *m_data = rte_pktmbuf_mtod(pkt, uint8_t *);
    struct ipv4_hdr *ip_hdr = 
        (struct ipv4_hdr *) &m_data[sizeof(struct ether_hdr)];

    printf("\n############### pkt ###############\n");

    if (ip_hdr->next_proto_id == IPPROTO_TCP) {

        printf("proto: %d sip: 0x%08x dip: 0x%08x length: %d\n",
                ip_hdr->next_proto_id, rte_be_to_cpu_32(ip_hdr->src_addr),
                rte_be_to_cpu_32(ip_hdr->dst_addr), rte_be_to_cpu_16(ip_hdr->total_length));

        struct tcp_hdr *tcp_hdr = 
            (struct tcp_hdr *) ((uint8_t *) ip_hdr + sizeof(struct ipv4_hdr));

        uint8_t tcp_hdr_size = (tcp_hdr->data_off >> 4) * 4; /* bytes */

        printf("sport: %d dport: %d tcp_hdr_size %d bytes\n",
                rte_be_to_cpu_16(tcp_hdr->src_port), rte_be_to_cpu_16(tcp_hdr->dst_port), tcp_hdr_size);

        if ((rte_be_to_cpu_16(ip_hdr->total_length) - sizeof(struct ipv4_hdr) - tcp_hdr_size)
                >= sizeof(struct tdaq_hdr)) {
            struct tdaq_hdr *tdaq_hdr = 
                (struct tdaq_hdr *) ((uint8_t *) tcp_hdr + tcp_hdr_size);
            if (tdaq_hdr->typeId == TDAQ_TYPE_ID_FRAGMENT_REQUEST_MESSAGE) {

                printf("tdaq fragment request message eventId: 0x%08x\n",
                        tdaq_hdr->event_id);

                fflush(stdout);
            }
        }
    }

    printf("############### end pkt ###############\n\n");

};
#endif

/* fills the packet metadata to be used for lpm-based forwarding
 * and data flow detection (default queue) */
static inline void
pkt_metadata_fill(struct rte_mbuf *m)
{
    /* get the pointer to start of packet data in mbuf */
    uint8_t *m_data = rte_pktmbuf_mtod(m, uint8_t *);

#ifdef DAQ_DATA_FLOWS_DUMP_PKT
    dump_pkt(m);
#endif

    /* access metadata in the mbuf headroom */
    struct pipeline_pkt_metadata *c =
        (struct pipeline_pkt_metadata *) RTE_MBUF_METADATA_UINT8_PTR(m, 0);

    /* get the ip header */
    struct ipv4_hdr *ip_hdr = 
        (struct ipv4_hdr *) &m_data[sizeof(struct ether_hdr)];
    uint64_t *ipv4_hdr_slab = (uint64_t *) ip_hdr;

    /* fill metadata, start with ttl, end with tcp src and dest ports */
    c->flow_key.slab0 = ipv4_hdr_slab[1];
    c->flow_key.slab1 = ipv4_hdr_slab[2];

#ifndef DAQ_DATA_FLOWS_DISABLE
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

#ifdef DAQ_DATA_FLOWS_DBG
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

    for (i = 0; i < (int) n; i++) {

        pkt_metadata_fill(pkts[i]);

        daqswitch_rx_queue_stats[pkts[i]->port][DP_PORT_RXQ_ID_DEFAULT].total_packets++;
        daqswitch_rx_queue_stats[pkts[i]->port][DP_PORT_RXQ_ID_DEFAULT].total_bursts++;

    }

    *pkts_mask = (~0LLU) >> (64 - n);
    
    return DP_SUCCESS;
}

#ifndef DAQ_DATA_FLOWS_DISABLE
/* detect new data flows */
static int
table_action_handler_hit(struct rte_mbuf **pkts, uint64_t *pkts_mask,    
                         struct rte_pipeline_table_entry **entries, __attribute__((unused)) void *arg)
{
    // todo consider using a separate pipeline table (flow classification)
    uint64_t pkts_in_mask = *pkts_mask;
    uint8_t port_idx;
    int ret;
    struct lcore_data_tx_port_conf *tx_port_conf;

    struct rte_fdir_filter filter;
    memset(&filter, 0, sizeof(struct rte_fdir_filter));

    for ( ; pkts_in_mask; ) {
        uint64_t pkt_mask;
        uint32_t pkt_index;

        pkt_index = __builtin_ctzll(pkts_in_mask);
        pkt_mask = 1LLU << pkt_index;
        pkts_in_mask &= ~pkt_mask;

        daqswitch_tx_queue_stats[entries[pkt_index]->port_id][DP_PORT_TXQ_ID_DEFAULT].total_packets++;
        daqswitch_tx_queue_stats[entries[pkt_index]->port_id][DP_PORT_TXQ_ID_DEFAULT].total_bursts++;

        struct rte_mbuf *pkt = pkts[pkt_index];

        /* access metadata in the mbuf headroom */
        struct pipeline_flow_key *flow_key =
            (struct pipeline_flow_key *) RTE_MBUF_METADATA_UINT8_PTR(pkt, 0);

        /* new data flow is detected by a new fragment request
         * going from ros to dcm
         * ensure also that subsequent packets in the same burst
         * do not trigger detection for the same flow */
        //todo for now there might be some race conditions, new fdir filters
        //might not be created fast enough before new tdaq req message from the
        //same dcm arrives
        if (flow_key->type_id == TDAQ_TYPE_ID_FRAGMENT_REQUEST_MESSAGE &&
             flow_key->sport != filter.port_src) {

            uint32_t lcore_id, flow_id;
            uint16_t fdir_id_local;

            /* from ros to dcm */
#ifdef DAQ_DATA_FLOWS_DBG
            printf("#### new data flow detected\n");
            printf("\tconfiguring ros->dcm path\n");
            fflush(stdout);
#endif
            tx_port_conf = NULL;
            for (lcore_id = 0; lcore_id < dp.nb_lcores; lcore_id++) {
                if (dp.lcores[lcore_id].type != DP_LCORE_TYPE_DATA_TX) {
                    continue;
                }
                for (port_idx = 0; port_idx < dp.lcores[lcore_id].nb_ports; port_idx++) {
                    if (dp.lcores[lcore_id].tx.port_list[port_idx].port_id == pkt->port) {
                        tx_port_conf = &dp.lcores[lcore_id].tx.port_list[port_idx];
                    }
                }
            }
            RTE_VERIFY(tx_port_conf != NULL);

            for (flow_id = 0; flow_id < DP_PORT_MAX_DATA_FLOWS; flow_id++) {
                if (dp.flows[pkt->port][flow_id].active == true &&
                    dp.flows[pkt->port][flow_id].dest_ip == flow_key->sip &&
                    dp.flows[pkt->port][flow_id].sink_id == flow_key->event_id) {

                    fdir_id_local = flow_id;
                    break;

                }
            }

            if (flow_id < DP_PORT_MAX_DATA_FLOWS) {
#ifdef DAQ_DATA_FLOWS_DBG
                printf("\tqueue already active %d\n", flow_id);
                fflush(stdout);
#endif

            } else { 

                for (flow_id = 0; flow_id < DP_PORT_MAX_DATA_FLOWS; flow_id++) {
                    if (dp.flows[pkt->port][flow_id].active == false) {
                        //todo not thread-safe
                        dp.flows[pkt->port][flow_id].active = true;

                        dp.flows[pkt->port][flow_id].dest_ip = flow_key->sip;
                        dp.flows[pkt->port][flow_id].sink_id = flow_key->event_id;
                        dp.flows[pkt->port][flow_id].req_flow = false;

                        fdir_id_local = flow_id;
                        /* activate ring polling */
                        tx_port_conf->active_flows |= (1 << flow_id);

                        break;

                    }
                }

                if (flow_id < DP_PORT_MAX_DATA_FLOWS) {
#ifdef DAQ_DATA_FLOWS_DBG
                    printf("\tfree filter found fdir id %d\n", fdir_id_local);
                    fflush(stdout);
#endif
                } else {
                    DP_LOG_INFO("warning: no more filters available for new ros(%d)->dcm(%d) data flow",
                                 entries[pkt_index]->port_id, pkt->port);
                    continue;
                }
            }

            filter.l4type = RTE_FDIR_L4TYPE_TCP;
            filter.ip_dst.ipv4_addr = flow_key->sip;
            filter.port_dst = flow_key->sport;
            filter.ip_src.ipv4_addr = flow_key->dip;
            filter.port_src = flow_key->dport;

            fdir_id[entries[pkt_index]->port_id]++;
            fdir_id_local |= (fdir_id[entries[pkt_index]->port_id] << DP_FDIR_OUT_QUEUE_MASK_SIZE);

            ret = rte_eth_dev_fdir_add_perfect_filter(entries[pkt_index]->port_id,
                                                      &filter,
                                                      fdir_id_local,
                                                      pkt->port + DP_PORT_RXQ_ID_DATA_MIN,
                                                      0);
            RTE_VERIFY(ret == 0);
            
#ifdef DAQ_DATA_FLOWS_DBG
            printf("\tnew filter p:q %d:%d "
                    "ros->dcm flow 0x%08x:%d->0x%08x:%d id 0x%08x fdir:%d\n",
                    entries[pkt_index]->port_id, pkt->port + DP_PORT_RXQ_ID_DATA_MIN,
                    rte_be_to_cpu_32(filter.ip_src.ipv4_addr), rte_be_to_cpu_16(filter.port_src),
                    rte_be_to_cpu_32(filter.ip_dst.ipv4_addr), rte_be_to_cpu_16(filter.port_dst),
                    flow_key->event_id, fdir_id_local);
            fflush(stdout);
#endif

            /* from dcm to ros */
#ifdef DAQ_DATA_FLOWS_DBG
            printf("\tconfiguring dcm->ros path\n");
            fflush(stdout);
#endif
            tx_port_conf = NULL;
            for (lcore_id = 0; lcore_id < dp.nb_lcores; lcore_id++) {
                if (dp.lcores[lcore_id].type != DP_LCORE_TYPE_DATA_TX) {
                    continue;
                }
                for (port_idx = 0; port_idx < dp.lcores[lcore_id].nb_ports; port_idx++) {
                    if (dp.lcores[lcore_id].tx.port_list[port_idx].port_id == entries[pkt_index]->port_id) {
                        tx_port_conf = &dp.lcores[lcore_id].tx.port_list[port_idx];
                    }
                }
            }
            RTE_VERIFY(tx_port_conf != NULL);

            for (flow_id = 0; flow_id < DP_PORT_MAX_DATA_FLOWS; flow_id++) {
                if (dp.flows[entries[pkt_index]->port_id][flow_id].active == true &&
                    dp.flows[entries[pkt_index]->port_id][flow_id].dest_ip == flow_key->dip &&
                    dp.flows[entries[pkt_index]->port_id][flow_id].sink_id == 0xffffffff) {

                    fdir_id_local = flow_id;

                    break;

                }
            }

            if (flow_id < DP_PORT_MAX_DATA_FLOWS) {
#ifdef DAQ_DATA_FLOWS_DBG
                printf("\tqueue already active %d\n", flow_id);
                fflush(stdout);
#endif
            } else {

                for (flow_id = 0; flow_id < DP_PORT_MAX_DATA_FLOWS; flow_id++) {
                    if (dp.flows[entries[pkt_index]->port_id][flow_id].active == false) {
                        //todo not thread-safe
                        dp.flows[entries[pkt_index]->port_id][flow_id].active = true;

                        dp.flows[entries[pkt_index]->port_id][flow_id].dest_ip = flow_key->dip;
                        dp.flows[entries[pkt_index]->port_id][flow_id].sink_id = 0xffffffff;
                        dp.flows[entries[pkt_index]->port_id][flow_id].req_flow = true;

                        fdir_id_local = flow_id;
                        /* activate ring polling */
                        tx_port_conf->active_flows |= (1 << flow_id);

                        break;

                    }
                }
            }

            if (flow_id < DP_PORT_MAX_DATA_FLOWS) {
#ifdef DAQ_DATA_FLOWS_DBG
                printf("\tfree filter found fdir id %d\n", fdir_id_local);
                fflush(stdout);
#endif
            } else {
                DP_LOG_INFO("warning: no more filters available for new dcm(%d)->ros(%d) data flow",
                             pkt->port, entries[pkt_index]->port_id);
                continue;
            }

            filter.l4type = RTE_FDIR_L4TYPE_TCP;
            filter.ip_dst.ipv4_addr = flow_key->dip;
            filter.port_dst = flow_key->dport;
            filter.ip_src.ipv4_addr = flow_key->sip;
            filter.port_src = flow_key->sport;

            fdir_id[pkt->port]++;
            fdir_id_local |= (fdir_id[pkt->port] << DP_FDIR_OUT_QUEUE_MASK_SIZE);

            ret = rte_eth_dev_fdir_add_perfect_filter(pkt->port,
                                                      &filter,
                                                      fdir_id_local,
                                                      entries[pkt_index]->port_id + DP_PORT_RXQ_ID_DATA_MIN,
                                                      0);
            RTE_VERIFY(ret == 0);

#ifdef DAQ_DATA_FLOWS_DBG
            printf("\tnew filter p:q %d:%d "
                    "dcm->ros flow 0x%08x:%d->0x%08x:%d id 0x%08x fdir:%d\n",
                    pkt->port, entries[pkt_index]->port_id + DP_PORT_RXQ_ID_DATA_MIN,
                    rte_be_to_cpu_32(filter.ip_src.ipv4_addr), rte_be_to_cpu_16(filter.port_src),
                    rte_be_to_cpu_32(filter.ip_dst.ipv4_addr), rte_be_to_cpu_16(filter.port_dst),
                    flow_key->event_id, fdir_id_local);
            printf("###\n");
            fflush(stdout);
#endif
        }

    }

    return PIPELINE_SUCCESS;
}
#endif

/* add single ipv4 rule to the rx_default pipeline
 * port_out_id is dpdk port id */
//todo make it static, messaging
int
add_ipv4_rule(uint32_t ipv4, uint8_t port_out_id)
{
    int ret;

    DP_LOG_ENTRY();

    struct rte_pipeline_table_entry entry = {
        .action = RTE_PIPELINE_ACTION_PORT,
        /* it is assumed here that the port_out_id is the same
         * as the internal port_out_id of the pipeline */
        .port_id = port_out_id,
    };

    struct rte_table_lpm_key key = {
        .ip = ipv4,
        .depth = 32,
    };

    int key_found;
    struct rte_pipeline_table_entry *entry_ptr;

    ret = rte_pipeline_table_entry_add(p,
                                       table_id,
                                       &key,
                                       &entry,
                                       &key_found,
                                       &entry_ptr);
    DP_LOG_AND_RETURN_ON_ERR("failed to entry to pipeline");

    DP_LOG_EXIT();

    return DP_SUCCESS;

error:
    return DP_ERR;
}


/* creates new pipeline with default lpm-based forwarding */
void
dp_configure_lcore_default(struct dp_lcore_params *lp)
{
    int ret;
    uint16_t i;
    
    DP_LOG_ENTRY();

    RTE_VERIFY(p == 0);

    /* pipeline configuration */
    struct rte_pipeline_params rte_params = {
        .name = "pipeline_default",
        .socket_id = rte_lcore_to_socket_id(lp->id),
    };

    DP_LOG_DEBUG("creating pipeline %s, lcore_id %d socket_id %d",
                  rte_params.name,
                  lp->id,
                  rte_params.socket_id);
    p = rte_pipeline_create(&rte_params);
    RTE_VERIFY(p);

    /* input port configuration */
    DAQSWITCH_PORT_FOREACH(i) {
        struct rte_port_ethdev_reader_params port_ethdev_params = {
            .port_id = i,
            .queue_id = DP_PORT_RXQ_ID_DEFAULT,
        };

        struct rte_pipeline_port_in_params port_params = {
            .ops = &rte_port_ethdev_reader_ops,
            .arg_create = (void *) &port_ethdev_params,
            .f_action = rx_action_handler,
            .arg_ah = NULL,
            .burst_size = DP_PORT_MAX_PKT_BURST_RX,
        };

        ret = rte_pipeline_port_in_create(p,
                                          &port_params,
                                          &port_in_id[i]);
        RTE_VERIFY(ret == 0);

        DP_LOG_DEBUG("\tcreated port in: port id %d queue id %d pipeline port id: %d",
                            port_ethdev_params.port_id,
                            port_ethdev_params.queue_id,
                            port_in_id[i]);
    }

    /* pipeline forwarding table configuration */
    {
        struct rte_table_lpm_params table_lpm_params = {
            .n_rules = DP_FORWARDING_RULES_MAX,
            .entry_unique_size = sizeof(struct rte_pipeline_table_entry),
            /* forwarding based on destination IP only */
            .offset = __builtin_offsetof(struct pipeline_pkt_metadata, flow_key.dip),
        };

        struct rte_pipeline_table_params table_params = {
            .ops = &rte_table_lpm_ops,
            .arg_create = &table_lpm_params,
            .f_action_miss = NULL,
#ifndef DAQ_DATA_FLOWS_DISABLE
            .f_action_hit = table_action_handler_hit,
#else
            .f_action_hit = NULL,
#endif
            .arg_ah = NULL,
            .action_data_size = 0,
        };

        DP_LOG_DEBUG("\tcreating lpm table");
        ret = rte_pipeline_table_create(p,
                                        &table_params,
                                        &table_id);
        RTE_VERIFY(ret == 0);
    }

    /* pipeline output port configuration */
    DAQSWITCH_PORT_FOREACH(i) {
        struct rte_port_ethdev_writer_params port_ethdev_params = {
            .port_id = i,
            .queue_id = DP_PORT_TXQ_ID_DEFAULT,
            .tx_burst_sz = DP_PORT_MAX_PKT_BURST_RX,
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
                                           &port_out_id[i]);
        RTE_VERIFY(ret == 0);

        DP_LOG_DEBUG("\tcreated port out: port id %d queue id %d pipeline port id: %d",
                            port_ethdev_params.port_id,
                            port_ethdev_params.queue_id,
                            port_out_id[i]);
    }

    /* interconnect ports */
    DAQSWITCH_PORT_FOREACH(i) {
        ret = rte_pipeline_port_in_connect_to_table(p,
                                                    port_in_id[i],
                                                    table_id);
        RTE_VERIFY(ret == 0);
    }

    /* enable ports */
    DAQSWITCH_PORT_FOREACH(i) {
        ret = rte_pipeline_port_in_enable(p, 
                                          port_in_id[i]);
        RTE_VERIFY(ret == 0);
    }

    /* check pipeline consistency */
    RTE_VERIFY(rte_pipeline_check(p) == 0);

    DP_LOG_EXIT();

}

void
dp_main_loop_lcore_default(__attribute__((unused)) struct dp_lcore_params *lp)
{
    RTE_VERIFY(p);

    while (1) {

        rte_pipeline_run(p);
        rte_pipeline_flush(p);
        rte_delay_us(DP_DEFAULT_PIPELINE_RUN_INTERVAL);

    }
}
