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
#include <rte_debug.h>
#include <rte_ip.h>

#include "../../daqswitch/daqswitch_port.h"
#include "../../pipeline/pipeline.h"
#include "../../common/common.h"
#include "../include/dp.h"

#include "dp_voq_swq.h"

int
dp_install_default_tables(void)
{
    DP_LOG_ENTRY();

    /* lpm table */
    add_ipv4_rule(IPv4(20,1,1,1), 9);
    add_ipv4_rule(IPv4(20,1,2,1), 8);
    add_ipv4_rule(IPv4(20,1,3,1), 6);
    add_ipv4_rule(IPv4(20,1,4,1), 7);
    add_ipv4_rule(IPv4(20,1,5,1), 2);
    add_ipv4_rule(IPv4(20,1,6,1), 3);
    add_ipv4_rule(IPv4(20,1,7,1), 0);
    add_ipv4_rule(IPv4(20,1,8,1), 1);
    add_ipv4_rule(IPv4(20,1,9,1), 4);
    add_ipv4_rule(IPv4(20,1,10,1), 5);
    add_ipv4_rule(IPv4(20,1,11,1), 10);
    add_ipv4_rule(IPv4(20,1,12,1), 11);

    DP_LOG_EXIT();

    return DP_SUCCESS;
}
