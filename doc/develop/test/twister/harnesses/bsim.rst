.. _twister_bsim_harness:

Bsim
####

Harness ``bsim`` is implemented in limited way - it helps only to copy the
final executable (``zephyr.exe``) from build directory to BabbleSim's
``bin`` directory (``${BSIM_OUT_PATH}/bin``).

This action is useful to allow BabbleSim's tests to directly run after.
By default, the executable file name is (with dots and slashes
replaced by underscores): ``bs_<platform_name>_<test_path>_<test_scenario_name>``.
This name can be overridden with the ``bsim_exe_name`` option in
``harness_config`` section.

bsim_exe_name: <string>
    If provided, the executable filename when copying to BabbleSim's bin
    directory, will be ``bs_<platform_name>_<bsim_exe_name>`` instead of the
    default based on the test path and scenario name.
