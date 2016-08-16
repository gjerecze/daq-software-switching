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
#ifndef DAQSWITCH_MSG_H
#define DAQSWITCH_MSG_H

#define DAQSWITCH_MSG_RING_SIZE                                    256

enum daqswitch_msg_req_type {
    DAQSWITCH_MSG_REQ_DATA_FLOW_ADD,
};

struct daqswitch_msg_req {
    enum daqswitch_msg_req_type type;
    union {
        struct {
            uint32_t dip;
            uint16_t dport;
        };
    };
};

void daqswitch_msg_handle(void);

#endif /* DAQSWITCH_MSG_H */
