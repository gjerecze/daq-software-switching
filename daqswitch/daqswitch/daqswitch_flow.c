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
#include <stdint.h>

#include <rte_log.h>

#include "daqswitch_flow.h"
#include "daqswitch.h"
#include "../common/common.h"

#define IPV4_MAX_FLOWS 1024

static struct daqswitch_ipv4_flow ipv4_flow_array[IPV4_MAX_FLOWS] __rte_cache_aligned;
static uint32_t ipv4_nb_flows = 0;

static inline int
dp_ipv4_flow_add(struct daqswitch_ipv4_flow *f)
{
    int ret = 0;
    if (daqswitch_is_started() && daqswitch_get_config()->dp_ipv4_flow_add) {
       ret = daqswitch_get_config()->dp_ipv4_flow_add(f); 
    }

    return ret;
}

static int
ipv4_flow_array_add_row(struct daqswitch_ipv4_flow *f)
{
    int ret = 0;

    DAQSWITCH_LOG_ENTRY();

    ipv4_flow_array[ipv4_nb_flows++] = *f;

    DAQSWITCH_LOG_EXIT();

    return ret;
}

void
daqswitch_ipv4_flow_array_dump(void)
{
    unsigned f_id;

    printf("\n");
    printf("+------+-----------------+--------------+--------------+\n");
    printf("|  Id  |     IP          | Depth        | Out port     |\n");
    printf("+------+-----------------+--------------+--------------+\n");
    for (f_id = 0; f_id < ipv4_nb_flows; f_id++) {
       printf("| %4d | %15x | %12d | %12d |\n",
               f_id, ipv4_flow_array[f_id].ip, ipv4_flow_array[f_id].depth, ipv4_flow_array[f_id].if_out);
    }
    printf("+------+-----------------+--------------+--------------+\n");
}

int
daqswitch_ipv4_flow_add(uint32_t ip, uint8_t depth, uint8_t if_out)
{
    int ret = 0;
    struct daqswitch_ipv4_flow f;

    DAQSWITCH_LOG_ENTRY();

    if (!daqswitch_is_started()) {
        DAQSWITCH_LOG_INFO("Cannot add flow. Daqswitch is not up");
        return -1;
    }

    f.ip = ip;
    f.depth = depth;
    f.if_out = if_out;

    ret = ipv4_flow_array_add_row(&f);
    if (ret < 0) {
        DAQSWITCH_LOG_INFO("Cannot add row to the ipv4 flow array");
        return ret;
    }

    ret = dp_ipv4_flow_add(&f);
    if (ret < 0) {
        DAQSWITCH_LOG_INFO("Cannot add flow to the dataplane");
        return ret;
    }

    DAQSWITCH_LOG_EXIT();

    return ret;
}
