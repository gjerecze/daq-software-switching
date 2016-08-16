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

#include "../../common/common.h"
#include "../../daqswitch/daqswitch.h"
#include "../include/dp.h"

static int
main_lcore_loop(__attribute__((unused)) void *dummy)
{
    DP_LOG_ENTRY();
    
    DP_LOG_EXIT();

    return DP_SUCCESS;
}

int
dp_init(void)
{
    DP_LOG_ENTRY();
    
    /* set the datapath thread */
    daqswitch_set_dp_thread(main_lcore_loop);

    DP_LOG_EXIT();

    return DP_SUCCESS;
}

int
dp_configure(void)
{
    DP_LOG_ENTRY();
    
    DP_LOG_EXIT();

    return DP_SUCCESS;
}
