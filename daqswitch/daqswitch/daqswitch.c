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
#include <rte_log.h>
#include <rte_cycles.h>

#include "daqswitch.h"
#include "daqswitch_flow.h"
#include "daqswitch_port.h"
#include "../dp/include/dp.h"
#include "../common/common.h"

static struct daqswitch daqswitch = {
    .configured = false,
    .initialized = false,
    .started = false,

    .cli_enabled = true,

};

/* Check the link status of all ports in up to 9s, and print them finally */
static void
wait_all_links_up(void)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint8_t portid, count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;

    DAQSWITCH_LOG_ENTRY();

	DAQSWITCH_LOG_INFO("Checking link status...");
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		all_ports_up = 1;
		for (portid = 0; portid < daqswitch_get_nb_ports(); portid++) {
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(portid, &link);
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					DAQSWITCH_LOG_INFO("Port %d Link Up - speed %u "
						"Mbps - %s", (uint8_t)portid,
						(unsigned)link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex"));
				else
					DAQSWITCH_LOG_INFO("Port %d Link Down",
						(uint8_t)portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == 0) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			DAQSWITCH_LOG_INFO(".");
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
		}
	}

    DAQSWITCH_LOG_INFO("done");
    DAQSWITCH_LOG_EXIT();

}

struct daqswitch *
daqswitch_get_config(void)
{
    return &daqswitch;
}

/* initialize daqswitch 
 * all parameters set by lower api's can be changed here */
int
daqswitch_init(void)
{
    int ret;
    uint8_t portid;

    DAQSWITCH_LOG_ENTRY();

    if (daqswitch.initialized) {
        DAQSWITCH_LOG_ERR_AND_RETURN("Daqswitch already initialized");
    }

	daqswitch.nb_ports = rte_eth_dev_count();
	if (daqswitch.nb_ports > RTE_MAX_ETHPORTS) {
		daqswitch.nb_ports = RTE_MAX_ETHPORTS;
    }

	daqswitch.nb_lcores = rte_lcore_count();

    /* initialize ports */
    DAQSWITCH_PORT_FOREACH(portid) {
        ret = daqswitch_port_init(portid);
        DAQSWITCH_LOG_AND_RETURN_ON_ERR("Cannot initialize port %d", portid);
    }

    /* initialize datapath */
    ret = dp_init();
    DAQSWITCH_LOG_AND_RETURN_ON_ERR("Cannot init data-plane");

    /* initialize msg rings */
    char name[32];
    snprintf(name, sizeof(name), "ring_msg_req_daqswitch");
    daqswitch.ring_req = rte_ring_create(name,
                                         DAQSWITCH_MSG_RING_SIZE,
                                         rte_socket_id(),
                                         RING_F_SC_DEQ);
    RTE_VERIFY(daqswitch.ring_req != NULL);

    snprintf(name, sizeof(name), "ring_msg_resp_daqswitch");
    daqswitch.ring_resp = rte_ring_create(name,
                                          DAQSWITCH_MSG_RING_SIZE,
                                          rte_socket_id(),
                                          RING_F_SP_ENQ);
    RTE_VERIFY(daqswitch.ring_resp != NULL);

    daqswitch.initialized = true;

    DAQSWITCH_LOG_EXIT();

    return DAQSWITCH_SUCCESS;

error:
    return DAQSWITCH_ERR;
}

/* configure daqswitch
 * configuration cannot be changed anymore
 * memory initialization should be done here */
int
daqswitch_configure(void)
{
    int ret;
    uint8_t portid;

    DAQSWITCH_LOG_ENTRY();

    if (daqswitch.configured) {
        DAQSWITCH_LOG_ERR_AND_RETURN("Daqswitch already configured");
    }

    if (!daqswitch.initialized) {
        DAQSWITCH_LOG_ERR_AND_RETURN("Daqswitch not initialized");
    }
    
    /* configure datapath */
    ret = dp_configure();
    DAQSWITCH_LOG_AND_RETURN_ON_ERR("Cannot configure datapath");

    /* configure ports */
    DAQSWITCH_PORT_FOREACH(portid) {
        ret = daqswitch_port_configure(portid);
        DAQSWITCH_LOG_AND_RETURN_ON_ERR("Cannot configure port %d", portid);
    }

    daqswitch.configured = true;

    DAQSWITCH_LOG_EXIT();

    return DAQSWITCH_SUCCESS;

error:
    return DAQSWITCH_ERR;
}

int
daqswitch_start(void)
{
    int ret;
    uint8_t portid;

    DAQSWITCH_LOG_ENTRY();
    
    if (daqswitch.started) {
        DAQSWITCH_LOG_ERR_AND_RETURN("Daqswitch already running");
    }

    if (!daqswitch.configured) {
        DAQSWITCH_LOG_ERR_AND_RETURN("Daqswitch not configured");
    }
    
    /* start all ports */
    DAQSWITCH_PORT_FOREACH(portid) {
        ret = daqswitch_port_start(portid);
        DAQSWITCH_LOG_AND_RETURN_ON_ERR("Cannot start port %d", portid);
    }

    /* wait all links up */
    wait_all_links_up();

	/* launch per-lcore datapath thread on every lcore */
    RTE_VERIFY(daqswitch.dp_thread);
	rte_eal_mp_remote_launch(daqswitch.dp_thread, NULL, SKIP_MASTER);

    daqswitch.started = true;

    /* install default forwarding tables */
    dp_install_default_tables();

#ifdef DP_DEBUG_CFG
    dp_dump_cfg();
    rte_exit(0, "\n\ncfg dump finished\n");
#endif

    DAQSWITCH_LOG_EXIT();

    return DAQSWITCH_SUCCESS;

error:
    return DAQSWITCH_ERR;
}
