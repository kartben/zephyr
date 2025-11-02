.. _twister_harnesses_index:
.. _twister_harnesses:

Twister Harnesses
#################

A harness in Twister is a feature that defines how test execution results are evaluated and 
reported. Harnesses are specified in the test's ``testcase.yaml`` file using the ``harness`` 
keyword, and they determine how Twister interprets the test output to determine if a test 
has passed or failed.

Overview
********

Different tests require different methods of validation. Some tests produce structured output
that can be parsed automatically, while others may require human interaction or specialized
hardware. Twister provides multiple harness implementations to support various testing scenarios.

Most harnesses work by parsing the test's output and matching specific patterns or frames. 
For example, the ``ztest`` and ``gtest`` harnesses look for pass/fail frames defined in their
respective testing frameworks, while the ``console`` harness matches custom regular expressions 
defined in the test configuration.

Available Harnesses
*******************

Twister supports the following harnesses:

.. toctree::
   :maxdepth: 1

   ztest
   console
   pytest_harness
   gtest
   ctest
   robot
   power
   display_capture
   bsim
   shell

Selecting a Harness
*******************

The harness is specified in the test's configuration file (``testcase.yaml`` or ``sample.yaml``)
using the ``harness`` keyword:

.. code-block:: yaml

   tests:
     my.test.scenario:
       harness: console
       harness_config:
         type: multi_line
         regex:
           - "Test passed"

Harness Configuration
*********************

Each harness may support additional configuration options specified under the ``harness_config``
section. These options control harness-specific behavior such as:

- Regular expressions to match in output
- Timeout values
- Fixture requirements
- External tools and scripts
- Test execution parameters

Refer to the documentation for each specific harness for details on available configuration 
options.

Common Example
**************

Here's a common example showing a test that uses the ``console`` harness to validate sensor output:

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

Unsupported Harnesses
*********************

Some harnesses are recognized but not yet fully implemented in Twister:

- **keyboard** - For tests requiring keyboard interaction
- **net** - For network-based testing scenarios
- **bluetooth** - For Bluetooth protocol testing

Tests using these harnesses are currently placeholders and may be skipped during test execution.
