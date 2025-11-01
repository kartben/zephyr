.. _twister_harnesses:

Harnesses
#########

What is a Harness?
******************

A harness is a test execution framework used by Twister to run and evaluate tests.
It defines how Twister interacts with the test binary, collects output, and determines
whether a test has passed or failed. Each harness provides a specific mechanism for
test verification:

- Some harnesses parse the test output looking for specific patterns or test framework
  messages (e.g., ``ztest``, ``gtest``, ``console``)
- Others integrate with external test frameworks (e.g., ``pytest``, ``robot``)
- Some provide specialized functionality like power measurement (``power``) or shell
  command execution (``shell``)

The harness is specified in the test's ``testcase.yaml`` or ``sample.yaml`` file
using the ``harness`` keyword. Twister uses this information to select the appropriate
handler for running and evaluating the test.

Harness Selection
*****************

Harnesses ``ztest``, ``gtest`` and ``console`` are based on parsing of the
output and matching certain phrases. ``ztest`` and ``gtest`` harnesses look
for pass/fail/etc. frames defined in those frameworks.

Some widely used harnesses that are not supported yet:

- keyboard
- net
- bluetooth

The following is an example yaml file with a few harness_config options.

.. code-block:: yaml

      sample:
        name: HTS221 Temperature and Humidity Monitor
      common:
        tags: sensor
        harness: console
        harness_config:
          type: multi_line
          ordered: false
          regex:
            - "Temperature:(.*)C"
            - "Relative Humidity:(.*)%"
          fixture: i2c_hts221
      tests:
        test:
          tags: sensors
          depends_on: i2c

Available Harnesses
*******************

.. toctree::
   :maxdepth: 1

   ctest
   gtest
   pytest
   console
   robot
   power
   bsim
   shell
