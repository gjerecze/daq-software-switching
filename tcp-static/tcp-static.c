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
#include <linux/module.h>
#include <net/tcp.h>

#define TCPSTATIC_DEFAULT_CWND 1
#define TCPSTATIC_SSHTRESH 0

static int static_cwnd __read_mostly = TCPSTATIC_DEFAULT_CWND;

module_param(static_cwnd, int, 0644);                                                                                                                                                                                               
MODULE_PARM_DESC(static_cwnd, "Value of the TCP static send congestion window");

static void tcp_static_init(struct sock *sk)
{
    tcp_sk(sk)->snd_cwnd = static_cwnd;
}

static u32 tcp_static_ssthresh(struct sock *sk)
{
	return TCPSTATIC_SSHTRESH;
}

static u32 tcp_static_min_cwnd(const struct sock *sk)
{
	return static_cwnd;
}

static void tcp_static_cong_avoid(struct sock *sk, u32 ack, u32 in_flight)
{
    tcp_sk(sk)->snd_cwnd = static_cwnd;
    tcp_sk(sk)->snd_ssthresh = TCPSTATIC_SSHTRESH;
}

static u32 tcp_static_undo_cwnd(struct sock *sk)
{
    return static_cwnd;
}

static void tcp_static_state(struct sock *sk, u8 new_state) 
{
    switch (new_state) {
        default:
            {
                tcp_sk(sk)->snd_cwnd = static_cwnd;
                tcp_sk(sk)->snd_ssthresh = TCPSTATIC_SSHTRESH;
            }
            break;
    }
}

static struct tcp_congestion_ops tcp_static = {
    .init       = tcp_static_init,
	.ssthresh	= tcp_static_ssthresh,
	.cong_avoid	= tcp_static_cong_avoid,
	.min_cwnd	= tcp_static_min_cwnd,
    .undo_cwnd  = tcp_static_undo_cwnd,
    .set_state  = tcp_static_state,
	.owner		= THIS_MODULE,
	.name		= "static",
};

static int __init tcp_static_register(void)
{
	return tcp_register_congestion_control(&tcp_static);
}

static void __exit tcp_static_unregister(void)
{
	tcp_unregister_congestion_control(&tcp_static);
}

module_init(tcp_static_register);
module_exit(tcp_static_unregister);

MODULE_AUTHOR("Grzegorz Jereczek");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Static TCP");
