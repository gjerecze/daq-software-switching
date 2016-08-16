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
#include <stdint.h>
#include <termios.h>

#include <rte_ethdev.h>
#include <cmdline_rdline.h>
#include <cmdline_parse.h>
#include <cmdline_socket.h>
#include <cmdline_parse_string.h>
#include <cmdline_parse_num.h>
#include <cmdline.h>

#include "../common/common.h"
#include "../stats/stats.h"
#include "../dp/include/dp.h"
#include "../daqswitch/daqswitch_port.h"
#include "cli.h"

struct cmd_all_result {
    cmdline_fixed_string_t all;
};
cmdline_parse_token_string_t cmd_all_string =
    TOKEN_STRING_INITIALIZER(struct cmd_all_result, all, "all");

struct cmd_show_result {
    cmdline_fixed_string_t show;
};
cmdline_parse_token_string_t cmd_show_string =
    TOKEN_STRING_INITIALIZER(struct cmd_show_result, show, "show");

struct cmd_stats_result {
    cmdline_fixed_string_t stats;
    uint32_t interval;
};
cmdline_parse_token_string_t cmd_stats_string =
    TOKEN_STRING_INITIALIZER(struct cmd_stats_result, stats, "stats");

cmdline_parse_token_num_t cmd_stats_interval = 
    TOKEN_NUM_INITIALIZER(struct cmd_stats_result, interval, UINT32);

struct cmd_reset_result {
    cmdline_fixed_string_t reset;
};
cmdline_parse_token_string_t cmd_reset_string =
    TOKEN_STRING_INITIALIZER(struct cmd_reset_result, reset, "reset");

struct cmd_dump_result {
    cmdline_fixed_string_t dump;
};
cmdline_parse_token_string_t cmd_dump_string = 
    TOKEN_STRING_INITIALIZER(struct cmd_dump_result, dump, "dump");

struct cmd_fdir_result {
    cmdline_fixed_string_t fdir;
};
cmdline_parse_token_string_t cmd_fdir_string = 
    TOKEN_STRING_INITIALIZER(struct cmd_fdir_result, fdir, "fdir");


/* reset stats */
static void
cmd_reset_parsed(__attribute__((unused)) void *parsed_result,
                 __attribute__((unused)) struct cmdline *cl,
                 __attribute__((unused)) void *data) {
    stats_reset();
}

cmdline_parse_inst_t cmd_stats_reset = {
    .f = cmd_reset_parsed,
    .data = NULL,
    .help_str = "reset statistics",
    .tokens = {
        (void *)&cmd_stats_string,
        (void *)&cmd_reset_string,
        NULL,
    },
};

/* print stats summary */
static void
cmd_stats_parsed(__attribute__((unused)) void *parsed_result,
                 __attribute__((unused)) struct cmdline *cl,
                 __attribute__((unused)) void *data) {

    struct cmd_stats_result *params = parsed_result;

    if (params->interval) {
        stats_print(params->interval);
    } else {
#define STATS_INTERVAL_MS 200
        stats_print(STATS_INTERVAL_MS);
    }
}

cmdline_parse_inst_t cmd_stats_show_all = {
    .f = cmd_stats_parsed,
    .data = NULL,
    .help_str = "show statistics",
    .tokens = {
        (void *)&cmd_stats_string,
        (void *)&cmd_show_string,
        (void *)&cmd_stats_interval,
        NULL,
    },
};

/* dump cfg */
static void
cmd_dump_parsed(__attribute__((unused)) void *parsed_result,
                 __attribute__((unused)) struct cmdline *cl,
                 __attribute__((unused)) void *data) {

    dp_dump_cfg();

}

cmdline_parse_inst_t cmd_dump = {
    .f = cmd_dump_parsed,
    .data = NULL,
    .help_str = "dump configuration",
    .tokens = {
        (void *)&cmd_dump_string,
        NULL,
    },
};

/* dump fdir */
static void
cmd_dump_fdir_parsed(__attribute__((unused)) void *parsed_result,
                     __attribute__((unused)) struct cmdline *cl,
                     __attribute__((unused)) void *data) {
    uint8_t portid;
    struct rte_eth_fdir fdir;

    DAQSWITCH_PORT_FOREACH(portid) {
        printf("port: %d\n", portid);
        if (rte_eth_dev_fdir_get_infos(portid, &fdir) == 0) {
           printf("\tadd %" PRIu64 " collision %d f_add %" PRIu64 " free %d maxlen %d\n",
                   fdir.add, fdir.collision, fdir.f_add, fdir.free, fdir.maxlen);
        }
    }

}

cmdline_parse_inst_t cmd_dump_fdir = {
    .f = cmd_dump_fdir_parsed,
    .data = NULL,
    .help_str = "dump fdir",
    .tokens = {
        (void *)&cmd_dump_string,
        (void *)&cmd_fdir_string,
        NULL,
    },
};

/* quit */
struct cmd_quit_result {
    cmdline_fixed_string_t quit;
};
cmdline_parse_token_string_t cmd_quit_string = 
    TOKEN_STRING_INITIALIZER(struct cmd_quit_result, quit, "quit");

static void
cmd_quit_parsed(__attribute__((unused)) void *parsed_result,
                 __attribute__((unused)) struct cmdline *cl,
                 __attribute__((unused)) void *data) {

    cmdline_quit(cl);

}

cmdline_parse_inst_t cmd_quit = {
    .f = cmd_quit_parsed,
    .data = NULL,
    .help_str = "quit",
    .tokens = {
        (void *)&cmd_quit_string,
        NULL,
    },
};

/* list of commands */
cmdline_parse_ctx_t cmdline_ctx[] = {
    (cmdline_parse_inst_t *)&cmd_stats_show_all,
    (cmdline_parse_inst_t *)&cmd_stats_reset,
    (cmdline_parse_inst_t *)&cmd_dump,
    (cmdline_parse_inst_t *)&cmd_dump_fdir,
    (cmdline_parse_inst_t *)&cmd_quit,
    NULL
};

/* main cmd-line loop */
void
cmdline_main_loop(void)
{
    struct cmdline *cl;

    DAQSWITCH_LOG_INFO("cmdline ready");

    cl = cmdline_stdin_new(cmdline_ctx, "daqswitch> ");
    if (cl == NULL) {
        return;
    }

    cmdline_interact(cl);
    cmdline_stdin_exit(cl);
}

