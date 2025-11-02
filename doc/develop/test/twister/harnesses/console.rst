.. _twister_console_harness:

Console Harness
###############

Overview
********

The ``console`` harness tells Twister to parse a test's text output for specific patterns
defined using regular expressions in the test's YAML configuration file. This harness is
useful for testing applications that produce console output which can be verified against
expected patterns.

The console harness is one of the most versatile harnesses in Twister, supporting both
simple single-line pattern matching and complex multi-line pattern matching with optional
ordering constraints. It can also record custom data from test output for further analysis.

When to Use Console Harness
============================

Use the console harness when:

* Your test produces console output that follows a predictable pattern
* You need to verify specific strings or patterns appear in the output
* You want to extract and record custom metrics from test output
* You need to validate both the presence and order of output messages
* Your test doesn't fit the structured output format of ztest or gtest

Configuration Options
*********************

The console harness is configured in the test's YAML file under ``harness_config``.
The following options are currently supported:

Basic Options
=============

type: <one_line|multi_line> (required)
    Specifies whether the harness should match against single lines or across multiple lines.

    * ``one_line``: Each regular expression is matched against individual output lines
    * ``multi_line``: Regular expressions can match across multiple lines of output

regex: <list of regular expressions> (required)
    A list of regular expression strings to match against the test's output.
    The test passes if all patterns are found in the output (unless ``ordered`` is true,
    in which case they must also appear in the specified order).

    .. code-block:: yaml

        harness_config:
          type: multi_line
          regex:
            - "Temperature:(.*)C"
            - "Relative Humidity:(.*)%"

ordered: <True|False> (default False)
    When set to ``True``, the regular expressions must match in the order they are listed.
    When ``False``, the patterns can appear in any order in the output.

    .. code-block:: yaml

        harness_config:
          type: multi_line
          ordered: true
          regex:
            - "Initialization complete"
            - "Starting test"
            - "Test passed"

Recording Options
=================

The console harness supports extracting structured data from test output using named
regular expression groups. This is useful for collecting performance metrics, measurements,
or other test-specific data.

record: <recording options> (optional)
  Configuration for extracting and recording custom data from test output.

  regex: <list of regular expressions> (required)
    Regular expressions with named subgroups (``(?P<name>...)``) to match and extract
    data fields from the test output. These records are written to:

    * A ``recording.csv`` file in the build directory
    * The ``recording`` property in ``twister.json``

    Multiple regular expressions can be provided, and each will be applied to each
    output line. This allows extracting:

    * Different records from the same output line
    * Different records from different lines
    * Similar records from multiple lines

    .. code-block:: yaml

        harness_config:
          record:
            regex:
              - "(?P<metric>.*):(?P<cycles>.*) cycles, (?P<nanoseconds>.*) ns"

    In this example, the harness extracts three fields (``metric``, ``cycles``, and
    ``nanoseconds``) from each matching line.

  merge: <True|False> (default False)
    When enabled, keeps only one record per test instance with all extracted data fields.
    Fields with the same name are combined into lists ordered by their appearance.

    This is useful when you want to collect all measurements into a single record rather
    than having multiple records.

    .. note::
       Multi-value fields may have different numbers of values depending on the regex
       rules and the test's output.

  as_json: <list of regex subgroup names> (optional)
    Specifies which extracted data fields should be parsed as JSON-encoded strings.
    These fields are written as nested objects in ``twister.json`` while remaining
    as JSON strings in the CSV file.

    This allows tests to convey complex, layered data structures for analysis including
    summary results, traces, statistics, and more.

    Example configuration:

    .. code-block:: yaml

        harness_config:
          record:
            regex: "RECORD:(?P<type>.*):DATA:(?P<metrics>.*)"
            as_json: [metrics]

    When matched against a log line like:

    .. code-block:: none

        RECORD:jitter_drift:DATA:{"rollovers":0, "mean_us":1000.0}

    This will be reported in ``twister.json`` as:

    .. code-block:: json

        "recording":[
            {
                "type":"jitter_drift",
                "metrics":{
                    "rollovers":0,
                    "mean_us":1000.0
                }
            }
        ]

Complete Example
****************

The following example demonstrates a complete console harness configuration for a
sensor monitoring application:

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

This configuration:

* Uses the console harness with multi-line matching
* Doesn't enforce ordering of the patterns
* Verifies that both temperature and humidity readings appear in the output
* Requires the ``i2c_hts221`` fixture
* Depends on I2C support on the target platform

Best Practices
**************

When using the console harness:

1. **Make patterns specific enough**: Avoid overly broad patterns that might match
   unintended output. Use anchors (``^``, ``$``) and specific text where appropriate.

2. **Use ordered matching when sequence matters**: If your test has a specific execution
   flow, set ``ordered: true`` to catch sequence errors.

3. **Extract meaningful data**: Use named groups to extract actual values rather than
   just verifying their presence. This provides better debugging information.

4. **Consider one_line vs multi_line**: Use ``one_line`` for simple pattern matching
   and ``multi_line`` when patterns might span multiple lines or when you need more
   complex matching logic.

5. **Test your regex patterns**: Verify your regular expressions match the expected
   output before committing. Small typos can cause false failures.

6. **Document expected output**: Add comments in your test configuration explaining
   what output patterns are expected and why they matter.
