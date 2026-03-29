.. zephyr:code-sample:: cpu_freq_pressure
   :name: Pressure based CPU frequency scaling
   :relevant-api: subsys_cpu_freq

   Dynamically scale CPU frequency using the pressure policy.

Overview
********

This sample demonstrates the :ref:`CPU frequency subsystem's <cpu_freq>` pressure policy, which
adjusts CPU frequency based on thread queue pressure. When more runnable threads are competing for
CPU time, the policy selects a higher performance state (P-state); when fewer threads are active,
it scales the frequency down.

The sample creates three threads at different priorities (low, medium, and high) and cycles through
three modes to simulate varying levels of system load:

#. **Mode 1** – Only the low-priority thread is running (low pressure).
#. **Mode 2** – Low and medium-priority threads are running (moderate pressure).
#. **Mode 3** – All three threads are running (high pressure).

Debug logging is enabled so the pressure evaluation and P-state selection are visible on the
console.

Building and Running
********************

Build and run this sample as follows, changing ``native_sim`` for your board:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/cpu_freq/pressure
   :board: native_sim
   :goals: run
   :compact:

Sample Output
=============

.. code-block:: console

   [00:00:00.000,000] <inf> cpu_freq_pressure_sample: Starting CPU Freq Pressure Policy Sample!
   [00:00:00.000,000] <inf> cpu_freq_pressure_sample: Mode 1: Low only
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x80847a0 with prio: 10 status: 0
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: get_normalized_sys_pressure: System pressure is: 10% (raw: 10 / max: 100)
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: CPU0 Pressure: 10%
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: Pressure Policy: Selected P-state 0 with load_threshold=25%
   [00:00:01.500,000] <inf> cpu_freq_pressure_sample: Mode 2: Low + Medium
   [00:00:02.000,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x80847a0 with prio: 10 status: 0
   [00:00:02.000,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x8084ba0 with prio: 5 status: 0
   [00:00:02.000,000] <dbg> cpu_freq_policy_pressure: get_normalized_sys_pressure: System pressure is: 30% (raw: 15 / max: 50)
   [00:00:02.000,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: CPU0 Pressure: 30%
   [00:00:02.000,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: Pressure Policy: Selected P-state 1 with load_threshold=50%
   [00:00:03.000,000] <inf> cpu_freq_pressure_sample: Mode 3: Low + Medium + High
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x80847a0 with prio: 10 status: 0
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x8084ba0 with prio: 5 status: 0
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x8084fa0 with prio: 1 status: 0
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: get_normalized_sys_pressure: System pressure is: 64% (raw: 16 / max: 25)
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: CPU0 Pressure: 64%
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: Pressure Policy: Selected P-state 2 with load_threshold=75%
