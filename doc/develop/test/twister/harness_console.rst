.. _twister_harness_console:

Console Harness
###############

.. _twister_console_harness:

Overview
********

The Console harness is a flexible and powerful tool for validating test output using
custom regular expressions. Unlike framework-specific harnesses like Ztest or Gtest,
the Console harness allows you to define your own success criteria by matching patterns
in the test's console output.

This harness is particularly useful for:

- **Sample applications** that don't use a testing framework
- **Custom validation** of specific output patterns or values
- **Integration tests** that verify system behavior through console messages
- **Legacy tests** that have existing output formats
- **Functional tests** that validate sensor readings, calculations, or other runtime data

The Console harness gives you complete control over what constitutes a successful test
by allowing you to specify exactly what patterns to look for in the output.

How It Works
************

The Console harness monitors the test's console output line-by-line, comparing each
line against one or more regular expressions you define. The test passes when all
required patterns are found (and optionally in the specified order).

Key capabilities:

1. **Pattern Matching**: Define regex patterns that must appear in the output
2. **Ordering Control**: Optionally enforce that patterns appear in a specific order
3. **Single/Multi-line Mode**: Match patterns on individual lines or across multiple lines
4. **Data Recording**: Extract and record specific data values from the output
5. **JSON Support**: Parse and validate JSON-formatted data in console output

Configuration
*************

Required Options
================

The Console harness requires these configuration options:

**type: <one_line|multi_line>** (required)
    Specifies how the harness should process the console output:
    
    - ``one_line``: Each regex is matched against individual lines
    - ``multi_line``: Regex patterns can span multiple lines

**regex: <list of regular expressions>** (required)
    A list of regular expression strings to match against the test output.
    All patterns must be found for the test to pass.

Optional Options
================

**ordered: <True|False>** (default False)
    When ``True``, the regex patterns must appear in the output in the same order
    they are listed in the configuration. When ``False``, patterns can appear in
    any order.

Basic Example
*************

Here's a simple example that validates a sensor reading:

.. code-block:: yaml

    sample:
      name: Temperature Sensor Reading
    tests:
      sample.sensor.temperature:
        harness: console
        harness_config:
          type: one_line
          regex:
            - "Temperature: [0-9]+ C"
            - "Reading successful"

This test passes if both patterns appear in the output, regardless of order.

Advanced Example with Ordering
******************************

This example enforces that output appears in a specific sequence:

.. code-block:: yaml

    tests:
      test.initialization.sequence:
        harness: console
        harness_config:
          type: multi_line
          ordered: true
          regex:
            - "System initializing"
            - "Loading configuration"
            - "Starting services"
            - "System ready"

The test will only pass if all messages appear in exactly this order.

Data Recording
**************

The Console harness can extract and record specific data values from the test output
for later analysis. This is powerful for performance testing, benchmarking, or
validating calculations.

Recording Configuration
=======================

**record: <recording options>** (optional)
    Enables data extraction from console output.
    
    **regex: <list of regular expressions>** (required)
        Regular expressions with named capture groups that identify the data fields
        to extract. Use Python's ``(?P<name>...)`` syntax for named groups.
    
    **merge: <True|False>** (default False)
        When ``True``, all extracted fields are combined into a single record.
        When ``False``, each match creates a separate record.
    
    **as_json: <list of field names>** (optional)
        List of field names whose values should be parsed as JSON.

Recording Example
=================

Here's an example that extracts performance metrics:

.. code-block:: yaml

    tests:
      benchmark.timing:
        harness: console
        harness_config:
          type: one_line
          regex:
            - "Test complete"
          record:
            regex:
              - "(?P<metric>.*): (?P<cycles>[0-9]+) cycles, (?P<nanoseconds>[0-9]+) ns"

When this test runs and produces output like:

.. code-block:: none

    Memory allocation: 1250 cycles, 625000 ns
    Context switch: 480 cycles, 240000 ns
    Test complete

The harness will:

1. Extract the performance data into records
2. Save them to ``recording.csv`` in the build directory
3. Include them in the ``recording`` property of ``twister.json``

The CSV file will contain:

.. code-block:: none

    metric,cycles,nanoseconds
    Memory allocation,1250,625000
    Context switch,480,240000

JSON Data Recording
===================

You can extract structured JSON data from console output:

.. code-block:: yaml

    tests:
      test.data.validation:
        harness: console
        harness_config:
          type: one_line
          regex:
            - "Test complete"
          record:
            regex:
              - "RECORD:(?P<type>.*):DATA:(?P<metrics>.*)"
            as_json: [metrics]

When matched to output:

.. code-block:: none

    RECORD:jitter_drift:DATA:{"rollovers":0, "mean_us":1000.0, "std_dev":15.2}

This will be recorded in ``twister.json`` as:

.. code-block:: json

    "recording": [
        {
            "type": "jitter_drift",
            "metrics": {
                "rollovers": 0,
                "mean_us": 1000.0,
                "std_dev": 15.2
            }
        }
    ]

Complete Configuration Example
******************************

Here's a comprehensive example using multiple Console harness features:

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
          - "Temperature: [0-9.]+ C"
          - "Relative Humidity: [0-9.]+ %"
        record:
          regex:
            - "Temperature: (?P<temp>[0-9.]+) C"
            - "Relative Humidity: (?P<humidity>[0-9.]+) %"
          merge: true
        fixture: i2c_hts221
    tests:
      test:
        tags: sensors
        depends_on: i2c

This configuration:

- Validates that both temperature and humidity readings appear in the output
- Allows them to appear in any order (``ordered: false``)
- Extracts the actual numeric values for later analysis
- Merges all extracted data into a single record
- Requires the ``i2c_hts221`` fixture for hardware testing

Regular Expression Tips
***********************

When writing regex patterns for the Console harness:

1. **Be Specific**: Make patterns specific enough to avoid false matches but flexible
   enough to handle minor variations
   
2. **Escape Special Characters**: Remember to escape regex special characters like
   ``.``, ``*``, ``+``, ``?``, ``[``, ``]``, ``()``, etc.

3. **Use Character Classes**: Use ``[0-9]+`` for numbers, ``[A-Za-z]+`` for letters

4. **Test Your Patterns**: Test regex patterns with actual output before committing

5. **Consider Whitespace**: Be mindful of spaces, tabs, and newlines in your patterns

6. **Named Groups for Recording**: Always use named groups (``(?P<name>...)``) when
   extracting data for recording

Best Practices
**************

1. **Validate Success, Not Failure**: Design patterns that match successful execution,
   not error messages (unless testing error handling).

2. **Use Multiple Patterns**: Break complex validation into multiple simple patterns
   rather than one complex regex.

3. **Include Completion Markers**: Always include a pattern that indicates test
   completion to avoid false positives from partial output.

4. **Document Your Patterns**: Add comments in your YAML explaining what each pattern
   is validating.

5. **Keep Recording Simple**: Extract only the data you actually need for analysis.

Troubleshooting
***************

Common Issues
=============

**Pattern Not Matching**
    - Verify the exact output format using ``twister -v`` to see console output
    - Check for unexpected whitespace or special characters
    - Test your regex pattern using an online regex tester
    - Ensure you're using the correct ``type`` (``one_line`` vs ``multi_line``)

**Test Times Out**
    - Verify all required patterns actually appear in the output
    - Check that the test is actually running (not just building)
    - Consider if ``ordered: true`` is preventing a match

**Recording Not Working**
    - Ensure named capture groups are used: ``(?P<name>...)``
    - Verify the output format exactly matches your regex
    - Check the build directory for ``recording.csv`` to debug

**False Positives**
    - Make patterns more specific
    - Add a completion marker pattern
    - Use ``ordered: true`` if sequence matters

See Also
********

- `Python Regular Expression Syntax <https://docs.python.org/3/library/re.html>`_
- :ref:`Ztest Harness <twister_harness_ztest>` for framework-based testing
- :ref:`Test Configuration <test_config_args>`
- :ref:`Pytest Harness <twister_harness_pytest>` for Python-based testing
