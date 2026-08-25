# POSIX performance audit artifacts

See posix-audit-report.html for the full report.

- posixbench/: out-of-tree benchmark app (native_sim/native/64, minimal libc,
  callgrind client requests; vendor valgrind/valgrind.h + callgrind.h into
  posixbench/include/valgrind/ from your host valgrind installation)
- extract_cg.py: per-phase inclusive-cost extractor (callgrind_annotate wrapper)
- parse_cg.py: raw per-phase summary parser

Fix branches (one fix each, based on main @ 8dafb9a89):
  posix-lockfree-obj-validation, posix-once-fast-path,
  zvfs-eventfd-drop-fd-scans, zvfs-eventfd-skip-redundant-raise,
  net-sockets-skip-recv-rereg, net-sockets-lockfree-timeo,
  net-context-o1-conn-context, posix-mutex-fast-path,
  posix-rwlock-lazy-uptime, clock-nanosleep-fewer-reads
