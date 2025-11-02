.. _twister_test_configuration:

Test Configuration Files
#########################

Twister supports test configuration files that allow you to customize platform
selection, define test levels, and control various aspects of test execution. These
configuration files are particularly useful for adapting Twister to different testing
environments, such as local development, CI systems, or custom validation setups.

Overview
********

Test configuration files provide a flexible way to:

- Define custom sets of default platforms for testing
- Organize tests into hierarchical levels (smoke, unit, integration, etc.)
- Control platform coverage and selection behavior  
- Adapt test execution to specific environments
- Override upstream defaults for local testing needs

Configuration files use YAML format and can be specified via the ``--test-config``
command-line option.

Basic Usage
***********

Create a configuration file (e.g., ``test_config.yaml``) and reference it when
running Twister:

.. code-block:: bash

   scripts/twister --test-config=path/to/test_config.yaml -T tests

The configuration file can contain platform settings, test level definitions, or both.

Platform Configuration
**********************

Platform configuration controls which boards Twister uses for testing by default and
how it expands platform coverage.

Configuration Options
=====================

The platform configuration section supports these options:

override_default_platforms: <True|False> (default False)
    When ``True``, replaces the default platforms defined in board configurations
    with the platforms specified in ``default_platforms``. When ``False``, the
    ``default_platforms`` list extends the existing defaults.
    
    This is useful for:
    
    - Replacing emulation platforms with real hardware
    - Focusing testing on a specific set of boards
    - Adapting to available hardware in a local environment

increased_platform_scope: <True|False> (default True)
    Controls whether Twister automatically expands platform coverage beyond explicitly
    specified platforms. When ``False``, Twister only builds and runs tests on the
    specified platforms without attempting to increase coverage.
    
    Disable this when:
    
    - You want strict control over which platforms are tested
    - Working with limited hardware or resources
    - Testing specific platform configurations only

default_platforms: <list of platforms>
    List of platforms to use as defaults. Depending on ``override_default_platforms``,
    this either replaces or extends the built-in default platforms.

Example: Override Defaults
===========================

Replace built-in defaults with custom platform set:

.. code-block:: yaml

   platforms:
     override_default_platforms: true
     increased_platform_scope: false
     default_platforms:
       - qemu_x86
       - qemu_cortex_m3
       - native_sim

This configuration:

1. Ignores platform defaults from board configurations
2. Disables automatic platform expansion
3. Tests only on the three specified platforms

Example: Extend Defaults
=========================

Add platforms to existing defaults:

.. code-block:: yaml

   platforms:
     override_default_platforms: false
     default_platforms:
       - frdm_k64f
       - nrf52840dk/nrf52840

This keeps all built-in defaults and adds the two specified boards.

Example: Local Hardware Setup
==============================

For testing with local hardware only:

.. code-block:: yaml

   platforms:
     override_default_platforms: true
     increased_platform_scope: false
     default_platforms:
       - my_custom_board
       - my_development_board

Use Cases for Platform Configuration
=====================================

**Local Development**
    Override defaults to match available hardware in your development setup.

**CI Optimization**
    Reduce test execution time by limiting to essential platforms.

**Hardware Validation**
    Focus testing on specific boards during hardware bring-up or validation.

**Resource Constraints**
    Limit platforms when emulators or hardware are limited.

Test Level Configuration
*************************

Test levels provide a way to organize tests hierarchically and selectively execute
subsets of tests based on testing needs. This is valuable for organizing testing
into phases like smoke tests, unit tests, integration tests, etc.

Level Definition
================

Each test level is defined with:

name: <string> (required)
    Unique identifier for the level. Used with the ``--level`` command-line option
    to select this level.

description: <string> (optional)
    Human-readable description of the level's purpose.

adds: <list of test patterns> (optional)
    Test scenarios to include in this level, specified as patterns that can include
    regular expressions.

Example: Single Level
=====================

.. code-block:: yaml

   levels:
     - name: smoke
       description: >
         Quick smoke tests to verify basic functionality.
       adds:
         - sample.basic.helloworld
         - kernel.common
         - drivers.uart.basic

Example: Multiple Levels
=========================

.. code-block:: yaml

   levels:
     - name: smoke
       description: >
         Fast smoke tests for basic functionality verification.
       adds:
         - sample.basic.*
         - kernel.common
         
     - name: unit
       description: >
         Unit tests for individual components.
       adds:
         - kernel.*
         - lib.*
         - drivers.*.unit
         
     - name: integration
       description: >
         Integration tests across multiple components.
       adds:
         - bluetooth.mesh.*
         - net.*
         - subsys.*.integration
         
     - name: acceptance
       description: >
         Acceptance tests for feature validation.
       adds:
         - samples.*
         - tests.acceptance.*

Running Tests by Level
======================

Use the ``--level`` option to run only tests in a specific level:

.. code-block:: bash

   scripts/twister --test-config=test_config.yaml --level=smoke -T tests

This executes only tests included in the "smoke" level definition.

Pattern Matching
================

The ``adds`` field supports flexible pattern matching:

**Exact match:**

.. code-block:: yaml

   adds:
     - kernel.semaphore.usage

**Wildcard patterns:**

.. code-block:: yaml

   adds:
     - kernel.*           # All kernel tests
     - drivers.sensor.*   # All sensor driver tests
     - *.basic           # All tests ending with .basic

**Regular expressions:**

.. code-block:: yaml

   adds:
     - kernel\.(threads|semaphore|mutex)  # Specific kernel tests
     - bluetooth\..*\.central            # Bluetooth central role tests

Use Cases for Test Levels
==========================

**Tiered Testing**
    Run quick smoke tests on every commit, more extensive tests nightly.

**Targeted Validation**
    Run only unit tests during development, integration tests before merging.

**CI Optimization**
    Different CI stages run different test levels (smoke → unit → integration).

**Regression Testing**
    Maintain a regression level with previously failing tests.

**Subsystem Focus**
    Create levels for specific subsystems (networking, Bluetooth, drivers).

Combined Configuration
**********************

Platform and level configurations can be combined in a single file to provide
complete test environment customization.

Complete Example
================

.. code-block:: yaml

   platforms:
     override_default_platforms: true
     increased_platform_scope: false
     default_platforms:
       - qemu_x86
       - qemu_cortex_m3
       - native_sim
       - frdm_k64f
       
   levels:
     - name: smoke
       description: >
         Quick smoke tests to verify basic zephyr features.
       adds:
         - sample.basic.helloworld
         - kernel.common
         - drivers.uart.basic
         
     - name: unit
       description: >
         Unit tests for component validation.
       adds:
         - kernel.threads.*
         - kernel.semaphore.*
         - kernel.mutex.*
         - lib.c_lib.*
         
     - name: integration
       description: >
         Integration tests across components.
       adds:
         - net.socket.*
         - bluetooth.mesh.*
         - subsys.logging.integration
         
     - name: acceptance
       description: >
         Acceptance tests for feature validation.
       adds:
         - samples.*
         
     - name: system
       description: >
         Full system-level tests.
       adds:
         - tests.kernel.*
         - tests.subsys.*
         
     - name: regression
       description: >
         Regression tests from previous issues.
       adds:
         - tests.regression.*

Using Combined Configuration
=============================

Run smoke tests on custom platforms:

.. code-block:: bash

   scripts/twister --test-config=test_config.yaml --level=smoke -T tests

Run all tests on custom platforms (no level specified):

.. code-block:: bash

   scripts/twister --test-config=test_config.yaml -T tests

Run integration tests on default platforms:

.. code-block:: bash

   scripts/twister --test-config=test_config.yaml --level=integration -T tests

Advanced Patterns
*****************

Subsystem-Specific Levels
==========================

Create focused test levels for specific subsystems:

.. code-block:: yaml

   levels:
     - name: bluetooth-basic
       description: Basic Bluetooth stack tests
       adds:
         - bluetooth.init.*
         - bluetooth.hci.*
         
     - name: bluetooth-advanced
       description: Advanced Bluetooth features
       adds:
         - bluetooth.mesh.*
         - bluetooth.audio.*
         - bluetooth.direction_finding.*

Progressive Testing
===================

Define levels that build on each other:

.. code-block:: yaml

   levels:
     - name: quick
       description: Very fast tests (< 1 minute total)
       adds:
         - sample.basic.helloworld
         - kernel.common
         
     - name: fast
       description: Fast tests (< 5 minutes total)
       adds:
         - sample.basic.*
         - kernel.*
         - drivers.uart.*
         
     - name: normal
       description: Normal test suite (< 30 minutes)
       adds:
         - kernel.*
         - drivers.*
         - lib.*
         
     - name: extensive
       description: Full test suite (> 1 hour)
       adds:
         - tests.*
         - samples.*

Platform-Specific Focus
=======================

Different platform sets for different purposes:

.. code-block:: yaml

   # For local development with limited hardware
   platforms:
     override_default_platforms: true
     default_platforms:
       - native_sim
       - qemu_x86
       
   levels:
     - name: dev
       adds:
         - kernel.*
         - lib.*

Best Practices
**************

Configuration File Management
==============================

1. **Version control**: Store configuration files in version control alongside
   your tests.

2. **Multiple configs**: Maintain different configurations for different
   environments (``dev_config.yaml``, ``ci_config.yaml``, ``validation_config.yaml``).

3. **Document purpose**: Include comments explaining the purpose and use case
   for each configuration.

4. **Incremental levels**: Design test levels to build on each other progressively.

5. **Regular review**: Periodically review and update configurations as test
   suites evolve.

Level Organization
==================

1. **Start simple**: Begin with basic levels (smoke, unit, integration) and
   expand as needed.

2. **Clear naming**: Use descriptive, intuitive names for levels.

3. **Balanced coverage**: Ensure each level provides meaningful coverage without
   excessive overlap.

4. **Time-based levels**: Consider organizing by expected execution time (quick,
   fast, normal, slow).

5. **Purpose-based levels**: Organize by testing purpose (smoke, regression,
   acceptance).

Platform Selection
==================

1. **Representative platforms**: Choose platforms that represent different
   architectures and capabilities.

2. **Match hardware**: Align platform selection with available hardware.

3. **Coverage balance**: Balance between broad coverage and practical execution time.

4. **Document requirements**: Note any specific requirements for platforms
   (debuggers, cables, etc.).

Common Patterns
***************

Development Workflow
====================

.. code-block:: yaml

   # dev_config.yaml - Quick iteration during development
   platforms:
     override_default_platforms: true
     increased_platform_scope: false
     default_platforms:
       - native_sim
       
   levels:
     - name: quick
       adds:
         - kernel.common
         - <component being developed>.*

CI Pipeline
===========

.. code-block:: yaml

   # ci_config.yaml - Comprehensive CI testing
   platforms:
     override_default_platforms: false
     increased_platform_scope: true
     default_platforms:
       - native_sim
       - qemu_x86
       - qemu_cortex_m3
       - qemu_cortex_m0
       - qemu_riscv32
       
   levels:
     - name: pr-check
       adds:
         - sample.basic.*
         - kernel.*
     - name: nightly
       adds:
         - tests.*
         - samples.*

Hardware Validation
===================

.. code-block:: yaml

   # hw_validation_config.yaml - Real hardware testing
   platforms:
     override_default_platforms: true
     increased_platform_scope: false
     default_platforms:
       - board_under_test
       
   levels:
     - name: hw-basic
       adds:
         - drivers.*
         - boards.<board_name>.*
     - name: hw-full
       adds:
         - tests.*

Troubleshooting
***************

**Tests not running**
    - Verify platform names match board definitions exactly
    - Check that patterns in ``adds`` match test scenario names
    - Ensure configuration file path is correct

**Wrong platforms selected**
    - Check ``override_default_platforms`` setting
    - Verify platform names in ``default_platforms`` are valid
    - Review ``increased_platform_scope`` setting

**Level not found**
    - Verify level name matches exactly (case-sensitive)
    - Check configuration file is being loaded
    - Ensure level is defined in the configuration file

**Pattern not matching**
    - Test patterns with ``--dry-run`` to see what would execute
    - Check escaping of special regex characters
    - Verify test scenario names with ``-T <path> --list-tests``

See Also
********

- :ref:`twister_script` - Main Twister documentation
- :ref:`twister_tests` - Test scenario configuration
- :ref:`twister_harnesses` - Test harness documentation
- :ref:`board_porting` - Board configuration and default platform settings
