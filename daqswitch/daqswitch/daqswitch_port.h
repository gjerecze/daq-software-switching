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
#ifndef DAQSWITCH_PORT_H
#define DAQSWITCH_PORT_H

#include <rte_ethdev.h>
#include <rte_mempool.h>

#include "../common/common.h"

#include "daqswitch.h"

#define DAQSWITCH_PORT_FOREACH(portid)                              \
    for (portid = 0; portid < daqswitch_get_nb_ports(); portid++)
#define DAQSWITCH_PORT_GET_NUMA(portid) (DAQSWITCH_NUMA_ON ? (unsigned) rte_eth_dev_socket_id(portid) : 0)

struct daqswitch_port_conf {
    struct rte_eth_conf rte_port_conf;

    /* Number of rx/tx queues */
    uint16_t nb_rxq;
    uint16_t nb_txq;

    /* Number of rx/tx descriptors */
    uint16_t nb_rxd;
    uint16_t nb_txd;

    /* Mempool for rx packets */
    struct rte_mempool *pkt_mbuf_pool;

    /* Flow director */
    struct rte_fdir_masks fdir_masks;
    bool fdir_enabled;

} __rte_cache_aligned;

struct daqswitch_port_conf *daqswitch_port_get_config(uint8_t portid);
int daqswitch_port_init(uint8_t portid);
int daqswitch_port_configure(uint8_t portid);
int daqswitch_port_start(uint8_t portid);

static inline int
daqswitch_port_set_nb_rxq(uint8_t portid, uint16_t nb_rxq)
{
    if (daqswitch_is_initialized()) {
        DAQSWITCH_LOG_INFO("Cannot configure the number of rx-queues. Daqswitch already initialized");
        return -1;
    }
    
    daqswitch_port_get_config(portid)->nb_rxq = nb_rxq;

    DAQSWITCH_LOG_DEBUG("Port configuration changed, nb_rxq=%d", daqswitch_port_get_config(portid)->nb_rxq);

    return DAQSWITCH_SUCCESS;
};

static inline int
daqswitch_port_set_nb_txq(uint8_t portid, uint16_t nb_txq)
{
    if (daqswitch_is_initialized()) {
        DAQSWITCH_LOG_INFO("Cannot configure the number of tx-queues. Daqswitch already initialized");
        return -1;
    }

    daqswitch_port_get_config(portid)->nb_txq = nb_txq;

    DAQSWITCH_LOG_DEBUG("Port configuration changed, nb_txq=%d", daqswitch_port_get_config(portid)->nb_txq);
    
    return DAQSWITCH_SUCCESS;
};

static inline int
daqswitch_port_set_nb_rxd(uint8_t portid, uint16_t nb_rxd)
{
    if (daqswitch_is_initialized()) {
        DAQSWITCH_LOG_INFO("Cannot configure the number of rx descriptors. Daqswitch already initialized");
        return -1;
    }
    
    daqswitch_port_get_config(portid)->nb_rxd = nb_rxd;

    DAQSWITCH_LOG_DEBUG("Port configuration changed, nb_rxd=%d", daqswitch_port_get_config(portid)->nb_rxd);

    return DAQSWITCH_SUCCESS;
};

static inline int
daqswitch_port_set_nb_txd(uint8_t portid, uint16_t nb_txd)
{
    if (daqswitch_is_initialized()) {
        DAQSWITCH_LOG_INFO("Cannot configure the number of tx descriptors. Daqswitch already initialized");
        return -1;
    }

    daqswitch_port_get_config(portid)->nb_txd = nb_txd;

    DAQSWITCH_LOG_DEBUG("Port configuration changed, nb_txq=%d", daqswitch_port_get_config(portid)->nb_txq);
    
    return DAQSWITCH_SUCCESS;
};

static inline int
daqswitch_port_set_fdir_forwarding(uint8_t portid, struct rte_fdir_masks *masks)
{
    if (daqswitch_is_initialized()) {
        DAQSWITCH_LOG_INFO("Cannot configure fdir. Daqswitch already initialized");
        return -1;
    }
    
    daqswitch_port_get_config(portid)->rte_port_conf.fdir_conf.mode = RTE_FDIR_MODE_PERFECT;
    daqswitch_port_get_config(portid)->rte_port_conf.fdir_conf.status = RTE_FDIR_REPORT_STATUS;
    daqswitch_port_get_config(portid)->rte_port_conf.fdir_conf.pballoc = RTE_FDIR_PBALLOC_64K;

    daqswitch_port_get_config(portid)->fdir_masks = *masks;
    daqswitch_port_get_config(portid)->fdir_enabled = true;

    return DAQSWITCH_SUCCESS;
};

static inline bool
daqswitch_port_is_fdir_enabled(uint8_t portid)
{
    return daqswitch_port_get_config(portid)->fdir_enabled;
}


#endif /* DAQSWITCH_PORT_H */
