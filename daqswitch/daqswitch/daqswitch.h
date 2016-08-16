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
#ifndef DAQSWITCH_H
#define DAQSWITCH_H

#include <stdint.h>
#include <stdbool.h>

#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#include "daqswitch_flow.h"
#include "daqswitch_msg.h"

#ifdef DAQSWITCH_NUMA_DISABLED
    #define DAQSWITCH_NUMA_ON                                                                    0
#else
    #define DAQSWITCH_NUMA_ON                                                                    1
#endif
#define DAQSWITCH_MBUFS_PER_PORT                                                            524287               
#define DAQSWITCH_MBUF_CACHE_SIZE                                                              256
#define DAQSWITCH_MBUF_RX_DATA_SIZE                                                           2048
#define DAQSWITCH_MBUF_OVERHEAD                   (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define DAQSWITCH_MBUF_SIZE                (DAQSWITCH_MBUF_RX_DATA_SIZE + DAQSWITCH_MBUF_OVERHEAD)

#define DAQSWITCH_MAX_PORTS                                                       RTE_MAX_ETHPORTS
#define DAQSWITCH_MAX_LCORES                                                         RTE_MAX_LCORE
#define DAQSWITCH_MAX_SOCKETS                                                                    2
#define DAQSWITCH_MAX_QUEUES_PER_PORT                                                            64

#define get_socket_id(lcore_id) DAQSWITCH_NUMA_ON ? rte_lcore_to_socket_id(lcore_id) : 0

struct daqswitch {
    bool configured;
    bool initialized;
    bool started;
    
    bool cli_enabled;

    unsigned nb_lcores;

    uint8_t nb_ports;

    uint32_t enabled_core_mask;

    int (*dp_thread)(void *);
    int (*dp_ipv4_flow_add)(struct daqswitch_ipv4_flow *);

    /* msg rings */
    struct rte_ring *ring_req;
    struct rte_ring *ring_resp;

} __rte_cache_aligned;


struct daqswitch *daqswitch_get_config(void);
int daqswitch_configure(void);
int daqswitch_init(void);
int daqswitch_start(void);

static inline uint32_t
daqswitch_get_enabled_core_mask(void)
{
    return daqswitch_get_config()->enabled_core_mask;
}

static inline void
daqswitch_set_enabled_core_mask(uint32_t core_mask)
{
    daqswitch_get_config()->enabled_core_mask = core_mask;
}

static inline uint8_t
daqswitch_get_nb_ports(void)
{
    return daqswitch_get_config()->nb_ports;
}

static inline unsigned
daqswitch_get_nb_lcores(void)
{
    return daqswitch_get_config()->nb_lcores;
}

static inline struct rte_ring *
daqswitch_get_msg_req_ring(void)
{
    return daqswitch_get_config()->ring_req;
}

static inline void
daqswitch_set_dp_thread(int (*dp_thread)(void *))
{
   daqswitch_get_config()->dp_thread = dp_thread; 
}

static inline void
daqswitch_set_dp_ipv4_flow_add(int (*f)(struct daqswitch_ipv4_flow *))
{
   daqswitch_get_config()->dp_ipv4_flow_add = f; 
}

static inline bool
daqswitch_is_configured(void)
{
    return daqswitch_get_config()->configured;
}

static inline bool
daqswitch_is_initialized(void)
{
    return daqswitch_get_config()->initialized;
}

static inline bool
daqswitch_is_started(void)
{
    return daqswitch_get_config()->started;
}

#endif /* DAQSWITCH_H */
