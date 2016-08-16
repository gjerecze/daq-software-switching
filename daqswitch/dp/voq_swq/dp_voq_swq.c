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
#include <rte_lcore.h>
#include <rte_byteorder.h>

#include "../../common/common.h"
#include "../../daqswitch/daqswitch.h"
#include "../../daqswitch/daqswitch_port.h"
#include "../../pipeline/pipeline.h"
#include "../include/dp.h"

#include "dp_voq_swq.h"

struct dp_params dp;

static void
init_lcore_default(void)
{
    RTE_VERIFY(dp.nb_lcores >= 1);

    /* verify that the lcore is not used */
    RTE_VERIFY(dp.lcores[DP_LCORE_ID_DEFAULT].type == DP_LCORE_TYPE_UNUSED);

    dp.lcores[DP_LCORE_ID_DEFAULT].type = DP_LCORE_TYPE_DEFAULT;

}

#ifndef DAQ_DATA_FLOWS_DISABLE
/* datapath rings store mbuf pointers of packets buffered in daqswitch
 * single ring corresponds to a single tx-queue
 * the rings should be multi-producer single-consumer
 * since the two lcores may be writting to the same ring */
static void
init_rings(void)
{
    int i, j;
	char s[64];

    DP_LOG_ENTRY();
    DAQSWITCH_PORT_FOREACH(i) {

        DP_LOG_DEBUG("\tport %d", i);

        /* 1 tx-queue is the default queue without assiocated ring */
        for (j = 0; j < DP_PORT_MAX_DATA_FLOWS - 1; j++) {
            DP_LOG_DEBUG("\t\t ring %d", j);
            snprintf(s, sizeof(s), "dp_ring_p%d_q%d", i, j);
            dp.rings[i][j] = rte_ring_create(s,
                                             rte_align32pow2(DP_RING_SIZE),
                                             DAQSWITCH_PORT_GET_NUMA(i),
                                             RING_F_SC_DEQ);
            RTE_VERIFY(dp.rings[i][j]);
        }

    }
    DP_LOG_EXIT();
}

/* get next lcore of a given type with starting with lcore_id,
 * possibly on the specified socket_id */
static struct dp_lcore_params *
get_next_lcore(uint32_t lcore_id,
               enum dp_lcore_type type,
               unsigned socket_id)
{
    uint32_t i, internal_id;
    struct dp_lcore_params *lcore;

    DP_LOG_ENTRY();

    /* find the internal id of the lcore in the list */
    for (i = 0; i < dp.nb_lcores; i++) {
        if (dp.lcores[i].id == lcore_id) {
            internal_id = i;
            break;
        }
    }
    RTE_VERIFY(i < dp.nb_lcores);

    DP_LOG_DEBUG("looking for next lcore: internal id %d lcore_id %d type %d numa %d",
                  internal_id, lcore_id, type, socket_id);

    bool numa_ok = true;
start_over:
    for (i = 0; i < dp.nb_lcores; i++) {
        /* start with the next lcore in the list
         * relative to the supplied lcore id*/
        uint32_t id = (internal_id + i + 1) % dp.nb_lcores;

        DP_LOG_DEBUG("\tcandidate: internal id %d lcore_id %d type %d nb_ports %d numa %d",
                        id,
                        dp.lcores[id].id,
                        dp.lcores[id].type,
                        dp.lcores[id].nb_ports,
                        rte_lcore_to_socket_id(dp.lcores[id].id));

        if ((dp.lcores[id].type == type)
            && (dp.lcores[id].nb_ports < DP_LCORE_PORT_MAX)) {
            DP_LOG_DEBUG("\ttype ok and enough queues available, checking numa..");
            if (!(numa_ok)
                || (socket_id == rte_lcore_to_socket_id(dp.lcores[id].id)) ) {

                DP_LOG_DEBUG("hit");

                lcore = &dp.lcores[id];

                DP_LOG_EXIT();
                return lcore;
            }
        }
    }

    /* if lcore not found yet, try again with non-optimal numa node */
    if (numa_ok) {
        numa_ok = false;
        DP_LOG_DEBUG("not found yet, trying with numa check off");
        goto start_over;
    }

    DP_LOG_EXIT();
    
    return NULL;
}

/* init data rx lcores
 * single lcore polls all rx queues
 * of at least a single port associated with the same numa node */
static void
init_lcores_data_rx(void)
{
    struct dp_lcore_params *lp;
    uint32_t prev_lcore_id;
    uint16_t queue_id;
    uint8_t port_id, nb_ports;

    nb_ports = daqswitch_get_nb_ports();
    prev_lcore_id = dp.lcores[DP_LCORE_ID_DEFAULT].id;

    RTE_VERIFY(nb_ports <= DP_PORT_RXQ_MAX);

    /* distribute ports across all available data rx lcores */
    DAQSWITCH_PORT_FOREACH(port_id) {
        lp = get_next_lcore(prev_lcore_id,
                            DP_LCORE_TYPE_DATA_RX,
                            DAQSWITCH_PORT_GET_NUMA(port_id));
        RTE_VERIFY(lp);

        /* initialize rx-queues 
         * single rx-queue corresponds to single output port */
        lp->rx.port_list[lp->nb_ports].port_id = port_id;
        lp->rx.port_list[lp->nb_ports].nb_queues = nb_ports;
        for (queue_id = 0; queue_id < nb_ports; queue_id++) {
            lp->rx.port_list[lp->nb_ports].queue_list[queue_id].queue_id
                        = DP_PORT_RXQ_ID_DATA_MIN + queue_id; 
            lp->rx.port_list[lp->nb_ports].queue_list[queue_id].out_port_id
                        = queue_id;
        }
        lp->nb_ports++;

        prev_lcore_id = lp->id;
    }
}

/* init data tx lcores
 * single lcore serves all tx queues
 * of at least a single port associated with the same numa node */
static void
init_lcores_data_tx(void)
{
    struct dp_lcore_params *lp;
    uint32_t prev_lcore_id;
    uint8_t port_id;

    prev_lcore_id = dp.lcores[DP_LCORE_ID_DEFAULT].id;

    /* distribute ports across all available data tx lcores */
    DAQSWITCH_PORT_FOREACH(port_id) {
        lp = get_next_lcore(prev_lcore_id,
                            DP_LCORE_TYPE_DATA_TX,
                            DAQSWITCH_PORT_GET_NUMA(port_id));
        RTE_VERIFY(lp);

        /* initialize tx-queues
         * single tx-queue corresponds to single sink
         * of a given data flow */
        lp->tx.port_list[lp->nb_ports].port_id = port_id;
        lp->nb_ports++;

        prev_lcore_id = lp->id;
    }
}
#endif

/* initialize lcore params
 * master lcore is not used for datapath processing, 
 * so it is not initialized here */
static void
init_lcores(void)
{
    uint32_t lcore_id;

    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
        dp.lcores[dp.nb_lcores].id =                        lcore_id;
        dp.lcores[dp.nb_lcores].nb_ports =                         0;
        dp.lcores[dp.nb_lcores++].type =        DP_LCORE_TYPE_UNUSED;
    }

    init_lcore_default();

#ifndef DAQ_DATA_FLOWS_DISABLE
    /* verify at least two lcores available */
    uint32_t lcores_free = 0;
    uint32_t i = 0;

    /* at least one lcore for rx and one lcore for tx */
    while (i < dp.nb_lcores) {
        if (dp.lcores[i++].type == DP_LCORE_TYPE_UNUSED) {
            lcores_free++;
        }
    }
    RTE_VERIFY(lcores_free >= 2);

    /* set available lcores as data rx or tx */
    i = 0;
    while (i < dp.nb_lcores) {
        if (dp.lcores[i].type != DP_LCORE_TYPE_UNUSED) {
            i++;
            continue;
        }

        /* one rx, one tx
         * this should ensure that rx and tx
         * lcores are available on all numa nodes */
        dp.lcores[i++].type = DP_LCORE_TYPE_DATA_RX;
        if (i < dp.nb_lcores) {
            dp.lcores[i++].type = DP_LCORE_TYPE_DATA_TX;
        }

    }

    /* init them */
    init_lcores_data_rx();
    init_lcores_data_tx();
#endif
}

static struct dp_lcore_params *
get_lcore_params(uint32_t lcore_id)
{
    struct dp_lcore_params *lp = NULL;
    uint32_t i;

    DP_LOG_ENTRY();

    for (i = 0; i < DAQSWITCH_MAX_LCORES; i++) {
        lp = &dp.lcores[i];
        if (lp->id == lcore_id) {
            break;
        }
    }

    DP_LOG_EXIT();

    return lp;
}


static int
dp_main_loop(__attribute__((unused)) void *dummy)
{
    uint32_t lcore_id;
    struct dp_lcore_params *lp;

    DP_LOG_ENTRY();

    lcore_id = rte_lcore_id();
    lp = get_lcore_params(lcore_id);

    RTE_VERIFY(lp);

    switch (lp->type) {

    case DP_LCORE_TYPE_DEFAULT:
        DP_LOG_INFO("logical core %u serving default queues:\n"
                    "\tentering main loop", lcore_id);
        dp_main_loop_lcore_default(lp);
        break;

#ifndef DAQ_DATA_FLOWS_DISABLE
    case DP_LCORE_TYPE_DATA_RX:
        DP_LOG_INFO("logical core %u:\n"
                    "\tserving data rx queues for %d output ports\n"
                    "\tentering main loop",
                     lcore_id, lp->nb_ports);
        dp_main_loop_lcore_data_rx(lp);
        break;

    case DP_LCORE_TYPE_DATA_TX:
        DP_LOG_INFO("logical core %u:\n"
                    "\tserving data tx queues for %d output ports\n"
                    "\tentering main loop",
                     lcore_id, lp->nb_ports);
        dp_main_loop_lcore_data_tx(lp);
        break;
#endif

    default:
        DP_LOG_INFO("logical core %u: nothing to do", lcore_id);
    }

    DP_LOG_INFO("logical core %u: exitting main loop", lcore_id);
    
    DP_LOG_EXIT();

    return DP_SUCCESS;
}

int
dp_init(void)
{
    int ret;
    uint8_t portid;

    DP_LOG_ENTRY();
        
    /* create rings */
    DP_LOG_INFO("initializing datapath rings...");
    //todo analyze the influence of number and size of rings on the performance
#ifndef DAQ_DATA_FLOWS_DISABLE
    init_rings();
#endif

    /* initialize lcore params */
    DP_LOG_INFO("initializing lcores...");
    init_lcores();

    /* set the datapath thread */
    daqswitch_set_dp_thread(dp_main_loop);

#ifndef DAQ_DATA_FLOWS_DISABLE
    /* data flows are filtered by the hw flow director
     * in current setup data flows are identified by
     * src and dest ip and tcp ports */
    /* attention: flow director does not filter fragmented
     *            ip packets */
    struct rte_fdir_masks fdir_mask;
    memset(&fdir_mask, 0, sizeof(struct rte_fdir_masks));
    fdir_mask.dst_ipv4_mask = 0xffffffff;
    fdir_mask.src_ipv4_mask = 0xffffffff;
    fdir_mask.only_ip_flow = 0;
    fdir_mask.dst_port_mask = 0xffff;
    fdir_mask.src_port_mask = 0xffff;
#endif

    DAQSWITCH_PORT_FOREACH(portid) {
        ret = daqswitch_port_set_nb_rxd(portid, 4096);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_rxd on port %d", portid);

        ret = daqswitch_port_set_nb_txd(portid, 4096);
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_txd on port %d", portid);

#ifndef DAQ_DATA_FLOWS_DISABLE
        /* single rx-queue per output ports + default queue */
        ret = daqswitch_port_set_nb_rxq(portid, daqswitch_get_nb_ports() + 1);
#else
        /* default queue only */
        ret = daqswitch_port_set_nb_rxq(portid, 1);
#endif
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_rxq on port %d", portid);

#ifndef DAQ_DATA_FLOWS_DISABLE
        /* tx-queue per data flow + default queue */
        ret = daqswitch_port_set_nb_txq(portid, DP_PORT_TXQ_MAX);
#else
        /* default queue only */
        ret = daqswitch_port_set_nb_txq(portid, 1);
#endif
        DP_LOG_AND_RETURN_ON_ERR("failed to set nb_txq on port %d", portid);

#ifndef DAQ_DATA_FLOWS_DISABLE
        ret = daqswitch_port_set_fdir_forwarding(portid, &fdir_mask);
        DP_LOG_AND_RETURN_ON_ERR("failed to set flow director on port %d", portid);
#endif

    }

    DP_LOG_EXIT();

    return DP_SUCCESS;

error:
    return DP_ERR;
}

int
dp_configure(void)
{
    uint32_t i;

    DP_LOG_ENTRY();

    for (i = 0; i < dp.nb_lcores; i++) {
        struct dp_lcore_params *lp = &dp.lcores[i];
        switch (lp->type) {
        case DP_LCORE_TYPE_UNUSED:
        {
            DP_LOG_DEBUG("lcore %d disabled, nothing to configure", lp->id);
            break;
        }

        case DP_LCORE_TYPE_DEFAULT:
        {
            DP_LOG_DEBUG("lcore %d default, configuring...", lp->id);
            dp_configure_lcore_default(lp);
            break;
        }

#ifndef DAQ_DATA_FLOWS_DISABLE
        case DP_LCORE_TYPE_DATA_RX:
        {
            DP_LOG_DEBUG("lcore %d data rx, configuring...", lp->id);
            dp_configure_lcore_data_rx(lp);
            break;
        }

        case DP_LCORE_TYPE_DATA_TX:
        {
            DP_LOG_DEBUG("lcore %d data tx, configuring...", lp->id);
            dp_configure_lcore_data_tx(lp);
            break;
        }
#endif

        default:
            rte_panic("unrecognized lcore type %d", lp->type);
        }
    }

    DP_LOG_EXIT();

    return DP_SUCCESS;

}

void
dp_dump_cfg(void)
{
    uint32_t i;

    DP_LOG_ENTRY();

    for (i = 0; i < dp.nb_lcores; i++) {
        struct dp_lcore_params *lp = &dp.lcores[i];
        printf("\n############### dp lcore id: %d ###############\n", i);
        printf("lcore id: %d\n", lp->id);
        switch (lp->type) {
        case DP_LCORE_TYPE_UNUSED:
            printf("type: unused\n");
            break;

        case DP_LCORE_TYPE_DEFAULT:
            printf("type: default\n");
            break;
            
#ifndef DAQ_DATA_FLOWS_DISABLE
        uint32_t j;
        case DP_LCORE_TYPE_DATA_RX:
            printf("type: data rx\n");
            for (j = 0; j < lp->nb_ports; j++) {
                printf("\tport_id %3d\n",
                        lp->rx.port_list[j].port_id);
            }
            break;

        case DP_LCORE_TYPE_DATA_TX:
            printf("type: data tx\n");
            for (j = 0; j < lp->nb_ports; j++) {
                printf("\tport_id %3d active_flows 0x%0" PRIX64 "\n",
                        lp->tx.port_list[j].port_id,
                        lp->tx.port_list[j].active_flows
                        );
            }
            break;
#endif

        default:
            printf("type: unrecognized\n");
            break;
        }
    }
#ifndef DAQ_DATA_FLOWS_DISABLE
    uint8_t port_id;
    unsigned count;

    printf("+------+-------+----------------+------------+-----------------+-----------------+\n");
    printf("| Port | Queue | Destination IP |  Sink Id   | Is request flow | Ring occupancy  |\n");
    printf("+------+-------+----------------+------------+-----------------+-----------------+\n");

    DAQSWITCH_PORT_FOREACH(port_id) {
        for (i = 0; i < DP_PORT_MAX_DATA_FLOWS; i++) {

            if (dp.flows[port_id][i].active) {
                count = rte_ring_count(dp.rings[port_id][i]); 
                printf("| %4d | %5d |     0x%08x | 0x%08x |               %1d | %15d |\n",
                       port_id, i, dp.flows[port_id][i].dest_ip, dp.flows[port_id][i].sink_id, dp.flows[port_id][i].req_flow, count);
            }
        }
    }

    printf("+------+-------+----------------+------------+-----------------+-----------------+\n");

#endif

    DP_LOG_EXIT();
}
