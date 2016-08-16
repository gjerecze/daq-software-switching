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
#ifndef COMMON_H
#define COMMON_H

#include <rte_log.h>

#define RTE_LOGTYPE_DAQSWITCH RTE_LOGTYPE_USER1
#define RTE_LOGTYPE_DP RTE_LOGTYPE_USER2

#define DAQSWITCH_SUCCESS                0
#define DAQSWITCH_ERR                   -1
#define DP_SUCCESS                       0
#define DP_ERR                          -1
#define PIPELINE_SUCCESS                 0
#define PIPELINE_ERR                    -1

#define _MODULE_LOG_DEBUG(MODULE, fmt, args...) \
              RTE_LOG(DEBUG, MODULE, "%s():%d: " fmt "\n", __func__, __LINE__, ##args)
#define _MODULE_LOG_INFO(MODULE, fmt, args...) \
              RTE_LOG(INFO, MODULE, fmt "\n", ##args)
#define _MODULE_LOG_ERR(MODULE, fmt, args...) \
              RTE_LOG(ERR, MODULE, "%s():%d: " fmt "\n", __func__, __LINE__, ##args)
#define _MODULE_LOG_AND_RETURN_ON_ERR(MODULE, fmt, args...)                                 \
              if (ret < 0) {                                                                \
                  RTE_LOG(ERR, MODULE, "%s():%d: " fmt "\n", __func__, __LINE__, ##args);   \
                  goto error;                                                               \
              }
#define _MODULE_LOG_ERR_AND_RETURN(MODULE, fmt, args...)                                \
              RTE_LOG(ERR, MODULE, "%s():%d: " fmt "\n", __func__, __LINE__, ##args);   \
              goto error;                                                               \

#define DAQSWITCH_LOG_DEBUG(fmt, args...) _MODULE_LOG_DEBUG(DAQSWITCH, fmt, ##args)
#define DAQSWITCH_LOG_INFO(fmt, args...) _MODULE_LOG_INFO(DAQSWITCH, fmt, ##args)
#define DAQSWITCH_LOG_ERR(fmt, args...) _MODULE_LOG_ERR(DAQSWITCH, fmt, ##args)
#define DAQSWITCH_LOG_AND_RETURN_ON_ERR(fmt, args...) _MODULE_LOG_AND_RETURN_ON_ERR(DAQSWITCH, fmt, ##args)
#define DAQSWITCH_LOG_ERR_AND_RETURN(fmt, args...) _MODULE_LOG_ERR_AND_RETURN(DAQSWITCH, fmt, ##args)
#define DAQSWITCH_LOG_ENTRY() DAQSWITCH_LOG_DEBUG("entry")
#define DAQSWITCH_LOG_EXIT() DAQSWITCH_LOG_DEBUG("exit")

#define DP_LOG_DEBUG(fmt, args...) _MODULE_LOG_DEBUG(DP, fmt, ##args)
#define DP_LOG_INFO(fmt, args...) _MODULE_LOG_INFO(DP, fmt, ##args)
#define DP_LOG_ERR(fmt, args...) _MODULE_LOG_ERR(DP, fmt, ##args)
#define DP_LOG_AND_RETURN_ON_ERR(fmt, args...) _MODULE_LOG_AND_RETURN_ON_ERR(DP, fmt, ##args)
#define DP_LOG_ERR_AND_RETURN(fmt, args...) _MODULE_LOG_ERR_AND_RETURN(DP, fmt, ##args)
#define DP_LOG_ENTRY() DP_LOG_DEBUG("entry")
#define DP_LOG_EXIT() DP_LOG_DEBUG("exit")

/* using default RTE_PIPELINE log level */
#define PIPELINE_LOG_DEBUG(fmt, args...) _MODULE_LOG_DEBUG(PIPELINE, fmt, ##args)
#define PIPELINE_LOG_INFO(fmt, args...) _MODULE_LOG_INFO(PIPELINE, fmt, ##args)
#define PIPELINE_LOG_ERR(fmt, args...) _MODULE_LOG_ERR(PIPELINE, fmt, ##args)
#define PIPELINE_LOG_AND_RETURN_ON_ERR(fmt, args...) _MODULE_LOG_AND_RETURN_ON_ERR(PIPELINE, fmt, ##args)
#define PIPELINE_LOG_ERR_AND_RETURN(fmt, args...) _MODULE_LOG_ERR_AND_RETURN(PIPELINE, fmt, ##args)
#define PIPELINE_LOG_ENTRY() PIPELINE_LOG_DEBUG("entry")
#define PIPELINE_LOG_EXIT() PIPELINE_LOG_DEBUG("exit")
                                          
#endif /* COMMON_H */
