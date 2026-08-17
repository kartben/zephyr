.. _hardening:

Hardening Tool
##############

Before launching a product, it's crucial to ensure that your software is as secure as possible. This
process, known as "hardening", involves strengthening the security of a system to protect it from
potential threats and vulnerabilities.

At a high-level, hardening a Zephyr application can be seen as a two-fold process:

#. Disabling features and compilation flags that might lead to security vulnerabilities (ex. making
   sure that no "experimental" features are being used, disabling features typically used for
   debugging purposes such as assertions, shell, etc.).
#. Enabling optional features that can lead to improve security (ex. stack sentinel, hardware stack
   protection, etc.). Some of these features might be hardware-dependent.

To simplify this process, Zephyr offers a **hardening tool** designed to analyze an application's
configuration against the **hardening database**, a set of recommendations curated by the Zephyr
**Security Working Group**. The tool looks at the Kconfig options in the build target and provides
tailored suggestions and recommendations to adjust security-related options, along with the
rationale behind each recommendation.

Usage
*****

.. zephyr-app-commands::
    :tool: all
    :zephyr-app: samples/hello_world
    :board: reel_board
    :goals: hardenconfig

The output should be similar to the table below. For each configuration option set to a value that
could lead to a security vulnerability, the table will propose a recommended value that should be
used instead, together with the reason the option matters.

.. code-block:: console

   Hardening report for profile: strict
   +------------------------------+-----------+---------------+----------------+----------------------------------------------------+
   | Name                         | Current   | Recommended   | Check result   | Rationale                                          |
   +==============================+===========+===============+================+====================================================+
   | CONFIG_BUILD_OUTPUT_STRIPPED | n         | y             | FAIL           | Produces a stripped binary so symbol names and     |
   |                              |           |               |                | debug information are not shipped on the device,   |
   |                              |           |               |                | where they would ease reverse engineering.         |
   +------------------------------+-----------+---------------+----------------+----------------------------------------------------+
   | CONFIG_STACK_SENTINEL        | n         | y             | FAIL           | Places a software sentinel value at the end of     |
   |                              |           |               |                | each thread stack and checks it at context         |
   |                              |           |               |                | switches, catching overflows on hardware without   |
   |                              |           |               |                | MPU/MMU stack protection.                          |
   +------------------------------+-----------+---------------+----------------+----------------------------------------------------+
   | CONFIG_PRINTK                | y         | n             | FAIL           | printk output goes straight to the console;        |
   |                              |           |               |                | leaving it enabled in production leaks whatever    |
   |                              |           |               |                | diagnostic messages remain in the code.            |
   +------------------------------+-----------+---------------+----------------+----------------------------------------------------+

Options that are not applicable to the current target (for example, MPU-based protections on
hardware without an MPU, or symbols that are not user-configurable in the current configuration)
are not reported.

In addition to the database-driven checks, the tool flags any enabled option that is marked in
Kconfig itself as experimental, deprecated or not secure (i.e. options selecting
:kconfig:option:`CONFIG_EXPERIMENTAL`, :kconfig:option:`CONFIG_DEPRECATED` or
:kconfig:option:`CONFIG_NOT_SECURE`).

Profiles
********

Recommendations are grouped into **profiles**. Two profiles are provided in-tree:

``base``
   Baseline hardening every production build should satisfy: enable available memory-protection
   and exploit-mitigation features, and disable inherently insecure options.

``strict`` (default)
   Everything in ``base``, plus removal of debugging, tracing and observability features (logging,
   shell, assertions, etc.) that enlarge the attack surface or disclose internal state. Some
   products legitimately keep a subset of these enabled — if that is your case, check against the
   ``base`` profile instead:

.. code-block:: shell

   west build -t hardenconfig -- -DHARDENCONFIG_PROFILE=base

Configuration options
*********************

The tool is controlled through CMake cache variables (passed after ``--`` on the ``west build``
command line, as above) or environment variables of the same name:

.. list-table::
   :header-rows: 1

   * - Option
     - Effect
   * - ``HARDENCONFIG_PROFILE``
     - Hardening profile to check against. Default: ``strict``.
   * - ``HARDENCONFIG_SHOW_ALL``
     - When set, also list passing and non-applicable options instead of only failures.
   * - ``HARDENCONFIG_STRICT``
     - When set, exit with a non-zero code if any check fails. Useful to gate CI pipelines or
       release processes on a clean hardening report.
   * - ``HARDENCONFIG_JSON``
     - Path to a file where results are additionally written as JSON, for consumption by scripts
       and dashboards.
   * - ``HARDENCONFIG_EXTRA_SOURCES``
     - Semicolon-separated list of additional hardening database files (see below).

The hardening database
**********************

The recommendations live in :zephyr_file:`scripts/kconfig/hardening.yaml`, validated against the
JSON schema in :zephyr_file:`scripts/schemas/hardening-schema.yaml`. Each rule is keyed by the
Kconfig symbol it applies to and recommends either an exact value or an integer constraint:

.. code-block:: yaml

   rules:
     BOOT_BANNER:
       value: n
       rationale: |
         The boot banner prints the exact Zephyr version to the console,
         letting anyone with console access fingerprint the firmware and
         match it against known vulnerabilities for that release.
       references: [CWE-200]

     STACK_POINTER_RANDOM:
       min: 100
       rationale: |
         Randomizing each thread's initial stack pointer makes stack
         addresses unpredictable. 0 disables randomization entirely.
       references: [CWE-121]

Contributing a new rule requires a ``rationale`` — that requirement is enforced by the schema.
Continuous integration additionally verifies that every rule references an existing Kconfig
symbol and that its recommended value is coherent with the symbol's type, so entries cannot
silently go stale when symbols are renamed or removed.

Rules do not carry applicability conditions: whether a recommendation applies to a given target
is already encoded in Kconfig itself (dependencies, hardware support), and the tool reports
options that are not applicable to the current target as not applicable rather than as failures.

Extending the database out of tree
**********************************

Product teams can layer their own recommendations on top of the in-tree database with
``HARDENCONFIG_EXTRA_SOURCES``. Each additional file uses the same schema and may define new
profiles (optionally extending in-tree ones) and new rules, as well as override in-tree rules:

.. code-block:: yaml

   profiles:
     acme-production:
       extends: strict
       description: ACME product security policy.

   rules:
     MY_VENDOR_DEBUG_INTERFACE:
       value: n
       profiles: [acme-production]
       rationale: |
         The vendor debug interface bypasses the product's authentication.

.. code-block:: shell

   west build -t hardenconfig -- \
     -DHARDENCONFIG_EXTRA_SOURCES=/path/to/acme-hardening.yaml \
     -DHARDENCONFIG_PROFILE=acme-production

Future evolution
****************

Directions under consideration for the hardening infrastructure:

* **Maintainer-owned annotations in Kconfig.** Boolean "this feature is unsafe/protective"
  knowledge is best kept next to the symbol definition, where it moves — and is deleted — with
  the symbol. The marker-symbol pattern (``select NOT_SECURE``) already works this way and the
  tool consumes it; widening that vocabulary (e.g. markers for "recommended in production" or
  "debug feature") would let subsystem maintainers own their part of the hardening story and
  shrink the central database to numeric constraints and cross-cutting policy.
* **Applying a profile.** Generating a configuration fragment from a profile, filtered down to
  the options actually applicable to the current target, so recommendations can be applied and
  not just reported.
* **Severity.** A per-rule severity field (possibly CVSS-style scoring) so strict/CI usage can
  gate on high-severity findings only.
