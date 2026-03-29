.. zephyr:code-sample:: cpu_freq_pressure
   :name: Pressure based CPU frequency scaling

   Dynamically scale CPU frequency using the pressure policy.

Overview
********

This sample demonstrates the :ref:`CPU frequency subsystem's <cpu_freq>` pressure policy. The
pressure policy adjusts the CPU P-state based on the number of runnable threads in the system,
increasing frequency when more threads are competing for CPU time and lowering it when the system
is idle.

The sample creates three worker threads at low, medium, and high priorities and cycles through
three scenarios every 1.5 seconds:

- **Mode 1** – only the low-priority thread is active (minimal pressure)
- **Mode 2** – low- and medium-priority threads are active (moderate pressure)
- **Mode 3** – all three threads are active (maximum pressure)

Debug output from both the sample and the CPU frequency subsystem is printed to the console,
making it easy to observe how the policy responds to changes in thread pressure.

Building and Running
********************

The sample can be built and executed on :zephyr:board:`native_sim` as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/cpu_freq/pressure
   :board: native_sim
   :goals: build run
   :compact:

To use a real hardware target such as the :zephyr:board:`frdm_mcxn236`, replace the board name
accordingly.

Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.1.0 ***
   [00:00:00.000,000] <inf> cpu_freq_pressure_sample: Starting CPU Freq Pressure Policy Sample!
   [00:00:00.000,000] <inf> cpu_freq_pressure_sample: Mode 1: Low only
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x20004000 with prio: 10 status: 0
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x20004200 with prio: 5 status: 1
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: thread_eval_cb: Evaluating thread: 0x20004400 with prio: 1 status: 1
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: get_normalized_sys_pressure: System pressure is: 25% (raw: 1 / max: 4)
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: CPU0 Pressure: 25%
   [00:00:00.500,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: Pressure Policy: Selected P-state 0 with load_threshold=25%
   [00:00:01.500,000] <inf> cpu_freq_pressure_sample: Mode 2: Low + Medium
   [00:00:02.000,000] <dbg> cpu_freq_policy_pressure: get_normalized_sys_pressure: System pressure is: 50% (raw: 2 / max: 4)
   [00:00:02.000,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: CPU0 Pressure: 50%
   [00:00:02.000,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: Pressure Policy: Selected P-state 1 with load_threshold=50%
   [00:00:03.000,000] <inf> cpu_freq_pressure_sample: Mode 3: Low + Medium + High
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: get_normalized_sys_pressure: System pressure is: 75% (raw: 3 / max: 4)
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: CPU0 Pressure: 75%
   [00:00:03.500,000] <dbg> cpu_freq_policy_pressure: cpu_freq_policy_select_pstate: Pressure Policy: Selected P-state 2 with load_threshold=75%
