.. zephyr:code-sample:: testsuite_integration
   :name: Test Suite Integration Sample
   :relevant-api: ztest_api

   Integrate ztest framework with sample applications.

Overview
********

This sample demonstrates how to integrate the Zephyr Test (ztest) framework
into a sample application for basic testing purposes. While samples should
generally not use the ztest framework (as per Zephyr sample guidelines), this
integration sample shows how the testing infrastructure can be used when needed.

The sample contains basic ztest assertions to verify the test framework
functionality:

- Assert true/false conditions
- Assert null/not-null pointers
- Assert equality checks

Note that this is a demonstration of test integration techniques and should
not be used as a template for typical samples. Most samples should focus on
demonstrating functionality rather than testing it.

Requirements
************

* Any board supported by Zephyr

Building and Running
********************

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/testsuite/integration
   :board: qemu_x86
   :goals: build run
   :compact:

Sample Output
=============

.. code-block:: console

   *** Booting Zephyr OS build ***
   Running TESTSUITE framework_tests
   ===================================================================
   START - test_assert
    PASS - test_assert in 0.001 seconds
   ===================================================================
   TESTSUITE framework_tests succeeded
   ===================================================================
   PROJECT EXECUTION SUCCESSFUL
