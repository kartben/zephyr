Pytest
######

The :ref:`pytest harness <integration_with_pytest>` is used to execute pytest
test suites in the Zephyr test. The following options apply to the pytest harness:

.. _pytest_root:

pytest_root: <list of pytest testpaths> (default pytest)
    Specify a list of pytest directories, files or subtests that need to be
    executed when a test scenario begins to run. The default pytest directory is
    ``pytest``. After the pytest run is finished, Twister will check if
    the test scenario passed or failed according to the pytest report.
    As an example, a list of valid pytest roots is presented below:

    .. code-block:: yaml

        harness_config:
          pytest_root:
            - "pytest/test_shell_help.py"
            - "../shell/pytest/test_shell.py"
            - "/tmp/test_shell.py"
            - "~/tmp/test_shell.py"
            - "$ZEPHYR_BASE/samples/subsys/testsuite/pytest/shell/pytest/test_shell.py"
            - "pytest/test_shell_help.py::test_shell2_sample"  # select pytest subtest
            - "pytest/test_shell_help.py::test_shell2_sample[param_a]"  # select pytest parametrized subtest

.. _pytest_args:

pytest_args: <list of arguments> (default empty)
    Specify a list of additional arguments to pass to ``pytest`` e.g.:
    ``pytest_args: ['-k=test_method', '--log-level=DEBUG']``. Note that
    ``--pytest-args`` can be passed multiple times to pass several arguments
    to the pytest.

.. _pytest_dut_scope:

pytest_dut_scope: <function|class|module|package|session> (default function)
    The scope for which ``dut`` and ``shell`` pytest fixtures are shared.
    If the scope is set to ``function``, DUT is launched for every test case
    in python script. For ``session`` scope, DUT is launched only once.


  The following is an example yaml file with pytest harness_config options,
  default pytest_root name "pytest" will be used if pytest_root not specified.
  please refer the examples in samples/subsys/testsuite/pytest/.

  .. code-block:: yaml

      common:
        harness: pytest
      tests:
        pytest.example.directories:
          harness_config:
            pytest_root:
              - pytest_dir1
              - $ENV_VAR/samples/test/pytest_dir2
        pytest.example.files_and_subtests:
          harness_config:
            pytest_root:
              - pytest/test_file_1.py
              - test_file_2.py::test_A
              - test_file_2.py::test_B[param_a]
