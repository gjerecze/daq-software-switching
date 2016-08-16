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
#include <rte_common.h>

#include "daqswitch_msg.h"
#include "daqswitch.h"

void
daqswitch_msg_handle(void)
{
    int ret;
    void *msg;
    struct daqswitch_msg_req *req;

    /* read request message */
    ret = rte_ring_sc_dequeue(daqswitch_get_msg_req_ring(), &msg);
    if (ret != 0) {
        /* no messages */
        return;
    }

    /* handle request */
    req = (struct daqswitch_msg_req *) rte_ctrlmbuf_data((struct rte_mbuf *) msg);

    switch (req->type) {
    case DAQSWITCH_MSG_REQ_DATA_FLOW_ADD:
    {
    }

    };

}

