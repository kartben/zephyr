.. _net_loopback_benchmark:

Network loopback throughput benchmark
#####################################

This benchmark measures the CPU cost of the network stack datapath by
pushing UDP and TCP traffic through the loopback interface on
:zephyr:board:`native_sim` and timing it with the host wall clock.

Because computation runs in zero simulated time on ``native_sim``, the
host wall-clock time spent for a fixed amount of packets is a direct
measure of the per-packet CPU cost of the IP stack (socket layer,
net_pkt/net_buf handling, IP/UDP/TCP processing and the loopback
driver), independent of the simulated time and of any configured link
speed.

The following workloads are run:

* UDP ping-pong between two sockets for several payload sizes
  (per-packet stack round trip cost),
* one-way UDP blast with application level batching (RX path
  throughput),
* TCP bulk transfer (segmentation, windowing and ACK processing).

Building and running
********************

.. code-block:: console

   west build -b native_sim/native/64 tests/benchmarks/net/loopback
   ./build/zephyr/zephyr.exe -no-rt

Run with ``-no-rt`` so that simulated time is decoupled from real time,
otherwise TCP protocol timers are paced by the host clock and distort
the results.

Environment variables:

* ``NETBENCH_SCALE=<n>``: divide the iteration counts by ``n`` (useful
  when profiling the benchmark under valgrind/callgrind),
* ``NETBENCH_TCP_ONLY=1``: only run the TCP bulk transfer part.

Results are printed as ``RESULT ...`` lines with packets per second and
goodput in Mbit/s.
