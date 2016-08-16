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
#include <rte_ethdev.h>
#include <rte_config.h>
#include <rte_errno.h>

#include "../common/common.h"

#include "daqswitch.h"
#include "daqswitch_port.h"

static struct daqswitch_port_conf port_conf[RTE_MAX_ETHPORTS];

static struct daqswitch_port_conf port_conf_default = {
    .rte_port_conf = {
        .rxmode = {
            .mq_mode = ETH_MQ_RX_NONE,
            .max_rx_pkt_len = ETHER_MAX_LEN,
            .split_hdr_size = 0,
            .header_split   = 0, /**< Header Split disabled */
            .hw_ip_checksum = 1, /**< IP checksum offload enabled */
            .hw_vlan_filter = 0, /**< VLAN filtering disabled */
            .jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
            .hw_strip_crc   = 0, /**< CRC stripped by hardware */
        },
        .txmode = {
            .mq_mode = ETH_MQ_TX_NONE,
        }
	},

    .nb_rxq = 1,
    .nb_txq = 1,

    .nb_rxd = 512,
    .nb_txd = 512,

    .fdir_enabled = false,

};

/* returns per port configuration */
struct daqswitch_port_conf *
daqswitch_port_get_config(uint8_t portid)
{
    RTE_VERIFY(portid < daqswitch_get_nb_ports());
    return &port_conf[portid];
}

/* initialize port */
int daqswitch_port_init(uint8_t portid)
{
    DAQSWITCH_LOG_ENTRY();
    RTE_VERIFY(portid < daqswitch_get_nb_ports());

    /* set default configuration */
    port_conf[portid] = port_conf_default;

    DAQSWITCH_LOG_EXIT();

    return DAQSWITCH_SUCCESS;
}

/* configure port */
int daqswitch_port_configure(uint8_t portid)
{
    int ret;
    uint16_t queueid;
	char s[64];

    DAQSWITCH_LOG_ENTRY();
    RTE_VERIFY(portid < daqswitch_get_nb_ports());

    /* create per port mempool if it does not exists */
    if (port_conf[portid].pkt_mbuf_pool == NULL) {
        snprintf(s, sizeof(s), "mempool_p%d", portid);
        port_conf[portid].pkt_mbuf_pool = rte_mempool_create(s,
                                                             DAQSWITCH_MBUFS_PER_PORT,
                                                             DAQSWITCH_MBUF_SIZE,
                                                             DAQSWITCH_MBUF_CACHE_SIZE,
                                                             sizeof(struct rte_pktmbuf_pool_private),
                                                             rte_pktmbuf_pool_init, NULL,
                                                             rte_pktmbuf_init, NULL,
                                                             DAQSWITCH_PORT_GET_NUMA(portid),
                                                             0); 
        if (port_conf[portid].pkt_mbuf_pool == NULL) {
            DAQSWITCH_LOG_ERR_AND_RETURN("failed to create mempool: err=%d, port=%d", rte_errno, portid);
        }
    }

    /* configure eth devices */
    DAQSWITCH_LOG_INFO("Configuring port %d: nb_rxq=%d, nb_txq=%d, socket_id=%d...",
                        portid, port_conf[portid].nb_rxq, port_conf[portid].nb_txq, DAQSWITCH_PORT_GET_NUMA(portid));

    ret = rte_eth_dev_configure(portid, port_conf[portid].nb_rxq, port_conf[portid].nb_txq,
                                &port_conf[portid].rte_port_conf);
    DAQSWITCH_LOG_AND_RETURN_ON_ERR("Cannot configure device: err=%d, port=%d", ret, portid);

    /* setup queues
     * queues are allocated on the same cpu socket,
     * to which the port is bound
     * tx */
    struct rte_eth_dev_info dev_info;
    rte_eth_dev_info_get(portid, &dev_info);

    for (queueid = 0; queueid < port_conf[portid].nb_txq; queueid++) {
        ret = rte_eth_tx_queue_setup(portid,
                                     queueid,
                                     port_conf[portid].nb_txd,
                                     DAQSWITCH_PORT_GET_NUMA(portid),
                                     &dev_info.default_txconf);
        DAQSWITCH_LOG_AND_RETURN_ON_ERR("failed to setup tx queue port %d", portid);
    }
    /* rx */
    RTE_VERIFY(port_conf[portid].pkt_mbuf_pool);
    for (queueid = 0; queueid < port_conf[portid].nb_rxq; queueid++) {
        ret = rte_eth_rx_queue_setup(portid,
                                     queueid,
                                     port_conf[portid].nb_rxd,
                                     DAQSWITCH_PORT_GET_NUMA(portid),
                                     &dev_info.default_rxconf,
                                     port_conf[portid].pkt_mbuf_pool);
        DAQSWITCH_LOG_AND_RETURN_ON_ERR("failed to setup rx queue port %d", portid);
    }

    DAQSWITCH_LOG_EXIT();

    return DAQSWITCH_SUCCESS;

error:
    return DAQSWITCH_ERR;
}

/* start port */
int daqswitch_port_start(uint8_t portid)
{
    int ret;

    DAQSWITCH_LOG_ENTRY();
    RTE_VERIFY(portid < daqswitch_get_nb_ports());

    /* start device */
    DAQSWITCH_LOG_INFO("starting port %d...", portid);
    ret = rte_eth_dev_start(portid);
    DAQSWITCH_LOG_AND_RETURN_ON_ERR("cannot start device: err=%d, port=%d", ret, portid);

    /* operate always in promiscuous */
    DAQSWITCH_LOG_INFO("\tenabling promiscuous...");
    rte_eth_promiscuous_enable(portid);

    /* configure fdir, if enabled */
    if (port_conf[portid].fdir_enabled) {
        DAQSWITCH_LOG_INFO("\tconfiguring fdir...");
        DAQSWITCH_LOG_DEBUG("\tmasks: only_ip_flow %04x dst_ipv4 %08x src_ipv4 %08x src_port %04x dst_port %04x\n",
                             port_conf[portid].fdir_masks.only_ip_flow, port_conf[portid].fdir_masks.dst_ipv4_mask,
                             port_conf[portid].fdir_masks.src_ipv4_mask, port_conf[portid].fdir_masks.src_port_mask,
                             port_conf[portid].fdir_masks.dst_port_mask);
        ret = rte_eth_dev_fdir_set_masks(portid, &port_conf[portid].fdir_masks);
        DAQSWITCH_LOG_AND_RETURN_ON_ERR("cannot set fdir masks: err=%d, port=%d", ret, portid);
    }

    /* flow control */
    static struct rte_eth_fc_conf fc_conf = {
        .autoneg    = 1,
        .mode       = RTE_FC_RX_PAUSE,
        .pause_time = 0x680,
    };
    ret = rte_eth_dev_flow_ctrl_set(portid, &fc_conf);
    DAQSWITCH_LOG_AND_RETURN_ON_ERR("failed to set flow control port=%d ret=%d", portid, ret);


    DAQSWITCH_LOG_EXIT();

    return DAQSWITCH_SUCCESS;

error:
    return DAQSWITCH_ERR;
}
