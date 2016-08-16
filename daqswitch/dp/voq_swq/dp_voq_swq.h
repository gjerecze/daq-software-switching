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
#ifndef DP_VOQ_SWQ_H
#define DP_VOQ_SWQ_H

#include <stdint.h>

#include <rte_ring.h>
#include <rte_mbuf.h>

#include "../../daqswitch/daqswitch.h"

/* timing */
#ifndef DP_RX_POLL_INTERVAL
    #define DP_RX_POLL_INTERVAL                                                        100 /* us */
#endif
#ifndef DP_TX_DRAIN_INTERVAL
    #define DP_TX_DRAIN_INTERVAL                                                       10  /* us */
#endif
#define DP_DEFAULT_PIPELINE_RUN_INTERVAL                                               200 /* us */

/* lcore defines */
#define DP_LCORE_ID_DEFAULT                                                               0
#define DP_LCORE_PORT_MAX                                                                16

/* port defines */
#define DP_PORT_TXQ_ID_DEFAULT                                                            0
#define DP_PORT_TXQ_ID_REQ                                                                1
#define DP_PORT_TXQ_ID_DATA                                                               2
#define DP_PORT_RXQ_MAX                                             DAQSWITCH_MAX_PORTS + 1
#define DP_PORT_TXQ_MAX                                                                   3
#define DP_PORT_RXQ_ID_DEFAULT                                                            0
#define DP_PORT_RXQ_ID_DATA_MIN                                                           1
#define DP_PORT_MAX_PKT_BURST_RX                                                         32
#define DP_PORT_MAX_PKT_BURST_TX                                                         32
#define DP_PORT_MAX_DATA_FLOWS                                                           64

/* ring defines */
#ifndef DP_RING_SIZE
    #define DP_RING_SIZE                                           DAQSWITCH_MBUFS_PER_PORT
#endif

/* default queue */
#define DP_FORWARDING_RULES_MAX                                                        1024

/* mask for the fdir id identifying the output queue */
#define DP_FDIR_OUT_QUEUE_MASK                                                          0x3f
#define DP_FDIR_OUT_QUEUE_MASK_SIZE                                                        6 /* bits */ 

extern struct dp_params dp;

enum dp_lcore_type {
    DP_LCORE_TYPE_UNUSED = 0,
    DP_LCORE_TYPE_DEFAULT,
    DP_LCORE_TYPE_DATA_RX,
    DP_LCORE_TYPE_DATA_TX,
};

#ifndef DAQ_DATA_FLOWS_DISABLE
struct data_rx_queue {
    uint16_t queue_id;
    uint8_t out_port_id;
} __rte_cache_aligned;

struct data_flow {

    bool active;
    bool req_flow;

    uint32_t dest_ip;
    uint32_t sink_id;

} __rte_cache_aligned;

struct lcore_data_rx_port_conf {
    uint8_t port_id;
    uint16_t nb_queues;
    struct data_rx_queue queue_list[DP_PORT_RXQ_MAX];
} __rte_cache_aligned;

struct lcore_data_tx_port_conf {
    uint8_t port_id;
    uint64_t active_flows;
} __rte_cache_aligned;
#endif

struct dp_lcore_params {
    uint32_t id;
    enum dp_lcore_type type;

    uint8_t nb_ports;
    union {
#ifndef DAQ_DATA_FLOWS_DISABLE
        /* data_rx lcore */
        struct { 
            struct lcore_data_rx_port_conf port_list[DP_LCORE_PORT_MAX];
        } rx;

        /* data_tx lcore */
        struct { 
            struct lcore_data_tx_port_conf port_list[DP_LCORE_PORT_MAX];
        } tx;
#endif
    };

} __rte_cache_aligned;

struct dp_params {
    /* CPU lcores */
    struct dp_lcore_params lcores[DAQSWITCH_MAX_LCORES];
    uint32_t nb_lcores;

    /* rings */
    struct rte_ring *rings[DAQSWITCH_MAX_PORTS][DP_PORT_MAX_DATA_FLOWS];
    struct data_flow flows[DAQSWITCH_MAX_PORTS][DP_PORT_MAX_DATA_FLOWS];

} __rte_cache_aligned;

/* datapath configuration */
void dp_configure_lcore_default(struct dp_lcore_params *lp);
void dp_configure_lcore_data_tx(struct dp_lcore_params *lp);
void dp_configure_lcore_data_rx(struct dp_lcore_params *lp);
int add_ipv4_rule(uint32_t ipv4, uint8_t port_out_id);

/* main processing loops */
void dp_main_loop_lcore_default(struct dp_lcore_params *lp);
void dp_main_loop_lcore_data_tx(struct dp_lcore_params *lp);
void dp_main_loop_lcore_data_rx(struct dp_lcore_params *lp);

#endif /* DP_VOQ_SWQ_H */
