#!/usr/bin/env python3
"""Parse callgrind per-phase dumps produced by the posixbench BENCH() macro.

Usage: parse_cg.py <out-prefix> <bench-stdout-log>
Prints: phase, iterations, total instructions, instructions/iteration.
"""
import glob
import re
import sys

prefix, log = sys.argv[1], sys.argv[2]

iters = {}
order = []
for line in open(log):
    m = re.match(r"BM_RUN (\S+) iters=(\d+)", line)
    if m:
        iters[m.group(1)] = int(m.group(2))
        order.append(m.group(1))

results = {}
for f in glob.glob(prefix + ".*"):
    name = None
    total = None
    for line in open(f, errors="replace"):
        m = re.match(r"desc: Trigger: Client Request: (.*)", line)
        if m:
            name = m.group(1).strip()
        m = re.match(r"summary: (\d+)", line)
        if m:
            total = int(m.group(1))
    if name and total is not None:
        results[name] = total

base = None
if "baseline_empty_loop" in results and "baseline_empty_loop" in iters:
    base = results["baseline_empty_loop"] / iters["baseline_empty_loop"]

print(f"{'phase':<34} {'iters':>6} {'instr/iter':>12}")
for name in order:
    if name in results:
        per = results[name] / iters[name]
        adj = per - (base or 0)
        print(f"{name:<34} {iters[name]:>6} {adj:>12.1f}")
    else:
        print(f"{name:<34} {iters[name]:>6} {'MISSING':>12}")
