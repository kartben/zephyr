.. _twister_console_harness:

Console Harness
###############

The ``console`` harness enables Twister to validate test execution by parsing the test's
console output against user-defined regular expressions. This harness provides flexibility
for testing scenarios where standard test frameworks like Ztest may not be suitable, such
as samples, demonstrations, or tests with custom output formats.

Overview
********

The console harness monitors the serial console output from the test application and matches
it against one or more regular expressions defined in the test's YAML configuration. When all
required patterns are found in the output, the test is considered to have passed. This makes
it ideal for:

- Sample applications that demonstrate functionality
- Tests with specific expected output strings
- Applications that produce structured log output
- Integration tests with custom validation criteria

Configuration Options
*********************

The console harness requires configuration through the ``harness_config`` section in the
test's ``testcase.yaml`` file.

Required Options
================

type: <one_line|multi_line> (required)
    Specifies whether the regex patterns should match single lines or can span multiple lines.
    
    - ``one_line``: Each regex pattern must match within a single line of output
    - ``multi_line``: Regex patterns can match across multiple lines

regex: <list of regular expressions> (required)
    A list of regular expression strings to match against the test's output. All expressions
    must be matched for the test to pass.

Optional Options
================

ordered: <True|False> (default False)
    Determines whether the regular expressions must be matched in the order they are listed.
    
    - ``True``: Patterns must appear in the output in the same order as defined
    - ``False``: Patterns can appear in any order

record: <recording options> (optional)
    Enables extraction of structured data from test output for later analysis. See
    :ref:`console_recording` for details.

Basic Example
*************

Here's a simple example that validates sensor output:

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

Ordered Output Example
**********************

For tests where output order matters:

.. code-block:: yaml

   tests:
     sample.boot_sequence:
       harness: console
       harness_config:
         type: multi_line
         ordered: true
         regex:
           - "Booting Zephyr"
           - "Initializing devices"
           - "System ready"

.. _console_recording:

Recording and Data Extraction
******************************

The console harness can extract structured data from test output using named capture groups
in regular expressions. This enables automated collection of performance metrics, test
statistics, and other custom data.

Basic Recording
===============

The ``record`` section enables data extraction:

regex: <list of regular expressions> (required)
    Regular expressions with named subgroups (``(?P<name>pattern)``) to extract data fields.
    Extracted data is written to:
    
    - A CSV file (``recording.csv``) in the build directory
    - The ``recording`` property in ``twister.json``

Example extracting performance metrics:

.. code-block:: yaml

   harness_config:
     type: multi_line
     regex:
       - "Test completed"
     record:
       regex:
         - "(?P<metric>.*):(?P<cycles>.*) cycles, (?P<nanoseconds>.*) ns"

Recording Options
=================

merge: <True|False> (default False)
    When ``true``, keeps only one record with all extracted data fields. Fields with the
    same name are collected into lists in the order they appear.

as_json: <list of regex subgroup names> (optional)
    Specifies which extracted fields should be parsed as JSON strings. This allows
    embedding complex data structures in test output.

Advanced Recording Example
===========================

This example shows JSON data extraction:

.. code-block:: yaml

   harness_config:
     type: multi_line
     regex:
       - "Test passed"
     record:
       regex: "RECORD:(?P<type>.*):DATA:(?P<metrics>.*)"
       as_json: [metrics]

When matched against output like:

.. code-block:: none

   RECORD:jitter_drift:DATA:{"rollovers":0, "mean_us":1000.0}

It produces this entry in ``twister.json``:

.. code-block:: json

   "recording": [
       {
           "type": "jitter_drift",
           "metrics": {
               "rollovers": 0,
               "mean_us": 1000.0
           }
       }
   ]

Use Cases
*********

The console harness is particularly useful for:

**Sample Applications**
    Validate that sample code produces expected output without requiring a full test framework.

**Custom Protocols**
    Verify communication protocols or data formats that don't fit standard test patterns.

**Performance Testing**
    Extract timing data, cycle counts, or other performance metrics for analysis.

**Integration Testing**
    Validate end-to-end behavior across multiple components with specific output patterns.

**Legacy Code**
    Test existing code that doesn't use Ztest or other test frameworks.

Best Practices
**************

When using the console harness:

1. **Make patterns specific**: Avoid overly generic regex patterns that might match
   unintended output. Include enough context to uniquely identify the expected output.

2. **Test your regex**: Verify your regular expressions match the actual output format.
   Remember to escape special characters properly.

3. **Use anchors when needed**: Consider using ``^`` and ``$`` anchors for line-based
   matching to avoid partial matches.

4. **Handle variations**: Account for variable data (numbers, addresses, etc.) in your
   patterns using ``.*`` or more specific patterns like ``[0-9]+``.

5. **Keep it simple**: Don't make regex patterns more complex than necessary. Multiple
   simple patterns are often better than one complex pattern.

6. **Document expectations**: Comment your test configuration to explain what output
   is expected and why.

Limitations
***********

Be aware of these limitations:

- The console harness only validates output presence, not functional behavior
- Timing-dependent output may require careful regex design
- Very large or fast output streams might not be fully captured
- The harness cannot interact with the application (use shell harness for that)

See Also
********

- :ref:`twister_shell_harness` - For interactive command/response testing
- :ref:`twister_harnesses` - Overview of all available harnesses
- :ref:`twister_script` - Main Twister documentation
