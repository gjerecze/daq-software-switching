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
#ifndef DAQSWITCH_FLOW_H
#define DAQSWITCH_FLOW_H

#include <stdint.h>

struct daqswitch_ipv4_flow {
	uint32_t ip;
	uint8_t  depth;
	uint8_t  if_out;
};

void daqswitch_ipv4_flow_array_dump(void);
int daqswitch_ipv4_flow_add(uint32_t ip, uint8_t depth, uint8_t if_out);


#endif /* DAQSWITCH_FLOW_H */
