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

#include "pipeline.h"

void
pipeline_msg_handle(struct pipeline_params *pp)
{
    int ret;
    void *msg;
    struct pipeline_msg_req *req;

    /* read request message */
    ret = rte_ring_sc_dequeue(pp->ring_req, &msg);
    if (ret != 0) {
        /* no messages */
        return;
    }

    /* handle request */
    req = (struct pipeline_msg_req *) rte_ctrlmbuf_data((struct rte_mbuf *) msg);

    switch (req->type) {
    case PIPELINE_MSG_REQ_ENABLE_PORT_IN:
    {
        /* enable pipeline in port */
        ret = pipeline_enable_port_in(pp, req->port_id, req->queue_id);
    }

    };

}
