.. zephyr:code-sample:: with_mcuboot
   :name: MCUboot with sysbuild

   Build a Zephyr application + MCUboot using sysbuild.

Overview
********
A simple example that demonstrates how building a sample using sysbuild can
automatically include MCUboot as the bootloader.
It showcases how the sample can adjust the configuration of extra image by
creating a image specific Kconfig fragment.

Requirements
************

The board must support MCUboot, i.e. its devicetree must provide the ``boot_partition``,
``slot0_partition`` and ``slot1_partition`` flash partitions.

The sample has been verified on the following boards:

- :zephyr:board:`reel_board`
- :zephyr:board:`nrf52840dk`
- :zephyr:board:`esp32_devkitc`, :zephyr:board:`esp32s2_devkitc`,
  :zephyr:board:`esp32s3_devkitc`, :zephyr:board:`esp32c3_devkitc` and
  :zephyr:board:`esp32c6_devkitc`
- :zephyr:board:`nucleo_c071rb`, :zephyr:board:`nucleo_c5a3zg`, :zephyr:board:`nucleo_f091rc`,
  :zephyr:board:`nucleo_f207zg`, :zephyr:board:`nucleo_f429zi`, :zephyr:board:`nucleo_f746zg`,
  :zephyr:board:`nucleo_g071rb`, :zephyr:board:`nucleo_g474re`, :zephyr:board:`nucleo_h753zi`,
  :zephyr:board:`nucleo_h7s3l8`, :zephyr:board:`nucleo_l152re`, :zephyr:board:`nucleo_u385rg_q`,
  :zephyr:board:`nucleo_wb55rg`, :zephyr:board:`nucleo_wb09ke`, :zephyr:board:`nucleo_wba65ri`,
  :zephyr:board:`nucleo_wba55cg` and :zephyr:board:`nucleo_wl55jc`
- :zephyr:board:`stm32f3_disco`, :zephyr:board:`stm32h7s78_dk`, :zephyr:board:`stm32h573i_dk`,
  :zephyr:board:`stm32h750b_dk`, :zephyr:board:`stm32l562e_dk`, :zephyr:board:`stm32u083c_dk`
  and :zephyr:board:`stm32wba65i_dk1`
- :zephyr:board:`sam_e54_xpro` and :zephyr:board:`pic32cx_sg41_cult`

Sysbuild specific settings
**************************

This sample automatically includes MCUboot as bootloader when built using
sysbuild.

This is achieved with a sysbuild specific Kconfig configuration,
:file:`sysbuild.conf`.

The ``SB_CONFIG_BOOTLOADER_MCUBOOT=y`` setting in the sysbuild Kconfig file
enables the bootloader when building with sysbuild.

The :file:`sysbuild/mcuboot.conf` file will be used as an extra fragment that
is merged together with the default configuration files used by MCUboot.

:file:`sysbuild/mcuboot.conf` adjusts the log level in MCUboot, as well as
configures MCUboot to prevent downgrades and operate in upgrade-only mode.

To build both the sample and MCUboot with ``west`` for the ``reel_board``, run:

.. zephyr-app-commands::
   :tool: west
   :zephyr-app: samples/sysbuild/with_mcuboot
   :board: reel_board
   :goals: build
   :west-args: --sysbuild
   :compact:

Execution output:

.. code-block:: console

   *** Booting Zephyr OS build v3.2.0-rc3-209-gdcf4201d3573  ***
   *** Booting Zephyr OS build v3.2.0-rc3-209-gdcf4201d3573  ***
   Address of sample 0xc000
   Hello sysbuild with mcuboot! nrf52840dk

The first ``Booting Zephyr OS build`` is printed by MCUboot itself and the
following lines are printed by the ``with_mcuboot`` sample.
This sample also prints its flash location.
