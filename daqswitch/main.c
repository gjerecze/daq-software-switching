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
#include <stdio.h>

#include <rte_eal.h>
#include <rte_common.h>
#include <rte_log.h>
#include <rte_ip.h>
#include <rte_cycles.h>

#include "cli/cli.h"
#include "common/common.h"
#include "daqswitch/daqswitch.h"
#include "daqswitch/daqswitch_flow.h"
#include "stats/stats.h"

int
main(int argc, char **argv)
{
	int ret = 0;

	/* init EAL */
    printf("Initializing EAL...\n");
	ret = rte_eal_init(argc, argv);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
    }
	argc -= ret;
	argv += ret;
    printf("Done\n");

	/* parse application arguments (after the EAL ones) */
	ret = parse_args(argc, argv);
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid application parameters\n");
    }

	/* initialize daqswitch */
    printf("Initializing daqswitch...\n");
	ret = daqswitch_init();
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Initialization failed\n");
    }
    printf("Done\n");

	/* configure daqswitch */
    printf("Configuring daqswitch...\n");
	ret = daqswitch_configure();
	if (ret < 0) {
		rte_exit(EXIT_FAILURE, "Invalid daqswitch configuration\n");
    }
    printf("Done\n");

    /* bring daqswitch up */
    printf("Starting daqswitch...\n");
    ret = daqswitch_start();
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "Bring-up failed\n");
    }
    printf("Done\n");

    /* launch stats and message handling */
    rte_delay_ms(3000);

    /* launch cmd-line */
    if (daqswitch_get_config()->cli_enabled) {
        cmdline_main_loop();
    } else {
        stats_thread_func(NULL);
    }
    
    printf("\n\nExitting...\n");

    return ret;
}
