#!/usr/bin/env python3
"""Extract per-phase inclusive function costs from callgrind part dumps.

Usage: extract_cg.py <out-prefix> <bench-stdout-log> [--json out.json]

For each part (phase), runs callgrind_annotate --inclusive=yes and reports
instructions/iteration for functions matching the phase's watchlist.
"""
import glob
import json
import re
import subprocess
import sys

WATCH = {
    "clock_gettime_monotonic": ["clock_gettime", "z_impl_sys_clock_gettime"],
    "clock_gettime_realtime": ["clock_gettime", "z_impl_sys_clock_gettime"],
    "k_uptime_ticks": ["z_impl_k_uptime_ticks"],
    "pthread_mutex_lock_unlock": [
        "pthread_mutex_lock", "pthread_mutex_unlock", "to_posix_mutex",
        "z_impl_k_mutex_lock", "z_impl_k_mutex_unlock",
    ],
    "k_mutex_lock_unlock": ["z_impl_k_mutex_lock", "z_impl_k_mutex_unlock"],
    "sem_wait_post": ["sem_wait", "sem_post", "z_impl_k_sem_take", "z_impl_k_sem_give"],
    "k_sem_take_give": ["z_impl_k_sem_take", "z_impl_k_sem_give"],
    "pthread_cond_signal_no_waiter": ["pthread_cond_signal", "to_posix_cond"],
    "pthread_self": ["pthread_self"],
    "pthread_key_get": ["pthread_getspecific"],
    "pthread_once_hot": ["pthread_once"],
    "eventfd_write_read": ["zvfs_eventfd_write", "zvfs_eventfd_read", "z_fdtable_call_ioctl",
                           "zvfs_get_obj_and_vtable", "eventfd_write_locked", "eventfd_read_locked"],
    "fd_write_read_dispatch": ["zvfs_write", "zvfs_read", "zvfs_get_vtable_and_obj_and_mutex_and_lock"],
    "poll_1fd_1ready_t0": ["zvfs_poll", "zvfs_poll_internal", "zvfs_poll_prepare", "zvfs_poll_update", "z_impl_k_poll"],
    "poll_4fd_1ready_t0": ["zvfs_poll", "zvfs_poll_internal", "zvfs_poll_prepare", "zvfs_poll_update", "z_impl_k_poll"],
    "poll_16fd_1ready_t0": ["zvfs_poll", "zvfs_poll_internal", "zvfs_poll_prepare", "zvfs_poll_update", "z_impl_k_poll"],
    "poll_32fd_1ready_t0": ["zvfs_poll", "zvfs_poll_internal", "zvfs_poll_prepare", "zvfs_poll_update", "z_impl_k_poll"],
    "poll_1fd_0ready_t0": ["zvfs_poll", "zvfs_poll_internal", "z_impl_k_poll"],
    "poll_4fd_0ready_t0": ["zvfs_poll", "zvfs_poll_internal", "z_impl_k_poll"],
    "poll_16fd_0ready_t0": ["zvfs_poll", "zvfs_poll_internal", "z_impl_k_poll"],
    "select_4fd_1ready_t0": ["zvfs_select", "zvfs_poll_internal"],
    "udp_sendto_recvfrom_32B": ["zsock_sendto", "zsock_recvfrom", "net_context_sendto",
                                "zsock_send_ctx", "zsock_sendto_ctx", "zsock_recv_ctx", "net_context_recv",
                                "k_mutex_lock", "z_impl_k_mutex_lock"],
    "udp_sendto_recvfrom_512B": ["zsock_sendto", "zsock_recvfrom", "net_context_sendto"],
    "udp_send_connected_32B": ["zsock_send", "zsock_sendto", "net_context_send", "net_context_sendto"],
    "udp_sendmsg_1iov_32B": ["zsock_sendmsg", "net_context_sendmsg", "net_context_sendto"],
    "tcp_send_recv_128B": ["zsock_send", "zsock_recv", "net_context_send", "net_tcp_queue", "tcp_send_data"],
    "tcp_send_recv_1024B": ["zsock_send", "zsock_recv", "net_context_send", "net_tcp_queue"],
    "mq_send_receive_64B": ["mq_send", "mq_receive"],
    "inet_pton_v4": ["zsock_inet_pton", "net_addr_pton"],
    "inet_ntop_v4": ["zsock_inet_ntop", "net_addr_ntop"],
    "nanosleep_zero": ["nanosleep", "sys_clock_nanosleep", "clock_nanosleep"],
}

def main():
    prefix, log = sys.argv[1], sys.argv[2]
    json_out = None
    if "--json" in sys.argv:
        json_out = sys.argv[sys.argv.index("--json") + 1]

    iters = {}
    for line in open(log):
        m = re.match(r"BM_RUN (\S+) iters=(\d+)", line)
        if m:
            iters[m.group(1)] = int(m.group(2))

    results = {}
    for f in sorted(glob.glob(prefix + ".*"),
                    key=lambda p: int(p.rsplit(".", 1)[1]) if p.rsplit(".", 1)[1].isdigit() else 0):
        txt = subprocess.run(["callgrind_annotate", "--inclusive=yes", "--threshold=100", f],
                             capture_output=True, text=True).stdout
        m = re.search(r"Trigger: Client Request: (.*)", txt)
        if not m:
            continue
        phase = m.group(1).strip()
        if phase not in iters:
            continue
        it = iters[phase]
        funcs = {}
        for line in txt.splitlines():
            lm = re.match(r"\s*([\d,]+) \([ \d.]+%\)\s+(\S+):(\S+?)(?:\s|$)", line)
            if not lm:
                continue
            cost = int(lm.group(1).replace(",", ""))
            fn = lm.group(3)
            # keep the max cost seen for a function name (file split dupes)
            if fn not in funcs or cost > funcs[fn]:
                funcs[fn] = cost
        picked = {}
        for want in WATCH.get(phase, []):
            for fn, cost in funcs.items():
                base = fn.split("'")[0]
                if base == want:
                    picked[want] = max(picked.get(want, 0), cost)
        results[phase] = {"iters": it,
                          "funcs": {k: round(v / it, 1) for k, v in sorted(picked.items(), key=lambda x: -x[1])}}

    for phase, data in results.items():
        print(f"== {phase} (iters={data['iters']})")
        for fn, per in data["funcs"].items():
            print(f"   {fn:<44} {per:>10.1f} instr/iter")

    if json_out:
        with open(json_out, "w") as fp:
            json.dump(results, fp, indent=1)

main()
