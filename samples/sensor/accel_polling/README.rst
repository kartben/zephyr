.. zephyr:code-sample:: accel_polling
   :name: Generic 3-Axis accelerometer polling
   :relevant-api: sensor_interface

   Get 3-Axis accelerometer data from a sensor (polling mode).

Overview
********

This sample application demonstrates how to use 3-Axis accelerometers using the
:ref:`RTIO framework <rtio>` based :ref:`Read and Decode method <sensor-read-and-decode>`
in polling mode (sensor_read).

Requirements
************

The sample runs on any board or shield that provides an accelerometer with a
devicetree alias ``accel0``.

It has been tested on the following boards, which cover a range of accelerometer drivers:

- :zephyr:board:`b_l4s5i_iot01a` (LSM6DSL)
- :zephyr:board:`bbc_microbit` (MMA8653FC)
- :zephyr:board:`bl5340_dvk` (LIS3DH)
- :zephyr:board:`blueclover_plt_demo_v2` (BMI270)
- :zephyr:board:`cc1352r_sensortag` (ADXL362)
- :zephyr:board:`frdm_k64f` (FXOS8700)
- :zephyr:board:`frdm_kl25z` (MMA8451Q)
- :zephyr:board:`frdm_mcxw23` (FXLS8974)
- :zephyr:board:`lpcxpresso55s28` (MMA8652FC)
- :zephyr:board:`mimxrt1040_evk` (FXLS8974)
- :zephyr:board:`sensortile_box` (LIS2DW12, LSM6DSO and IIS3DHHC)
- :zephyr:board:`sparkfun_thing_plus` (LIS2DH)
- :zephyr:board:`stm32f3_disco` (LSM303DLHC)
- :zephyr:board:`stm32f411e_disco` (LSM303AGR)
- :zephyr:board:`thingy52` (LIS2DH12)
- :zephyr:board:`thingy53` (ADXL362 and BMI270)
- :zephyr:board:`adafruit_qt_py_rp2040` with the :ref:`adafruit_lis3dh` shield

Pull request CI builds the sample on :zephyr:board:`frdm_k64f`, :zephyr:board:`sensortile_box`
and :zephyr:board:`adafruit_qt_py_rp2040` with the :ref:`adafruit_lis3dh` shield.

Building and Running
********************

This sample supports up to 10 3-Axis accelerometers. Each accelerometer needs
to be aliased as ``accelN`` where ``N`` goes from ``0`` to ``9``.
For example, in case of x_nucleo_iks4a1 shield:

.. code-block:: devicetree

  / {
	aliases {
			accel0 = &lsm6dso16is_6a_x_nucleo_iks4a1;
			accel1 = &lsm6dsv16x_6b_x_nucleo_iks4a1;
			accel2 = &lis2duxs12_1e_x_nucleo_iks4a1;
		};
	};

Then build for this shield and run with:

.. zephyr-app-commands::
   :zephyr-app: samples/sensor/accel_polling
   :board: <board to use>
   :shield: x_nucleo_iks4a1
   :goals: build flash
   :compact:

Sample Output
=============

.. code-block:: console

       lsm6dso16is@6a [m/s^2]:    (    0.923629,    -0.084945,     9.891330)
        lsm6dsv16x@6b [m/s^2]:    (   -0.059820,     0.862612,     9.827322)
        lis2duxs12@19 [m/s^2]:    (    0.851844,    -0.028713,     9.896714)
       lsm6dso16is@6a [m/s^2]:    (    0.924825,    -0.072981,     9.894919)
        lsm6dsv16x@6b [m/s^2]:    (   -0.061615,     0.864407,     9.825527)
        lis2duxs12@19 [m/s^2]:    (    0.823131,    -0.057427,     9.915857)
       lsm6dso16is@6a [m/s^2]:    (    0.928415,    -0.081355,     9.898508)
        lsm6dsv16x@6b [m/s^2]:    (   -0.061615,     0.864407,     9.829117)
        lis2duxs12@19 [m/s^2]:    (    0.832702,    -0.047856,     9.935000)
       lsm6dso16is@6a [m/s^2]:    (    0.922433,    -0.078963,     9.917053)
        lsm6dsv16x@6b [m/s^2]:    (   -0.063409,     0.864407,     9.825527)
        lis2duxs12@19 [m/s^2]:    (    0.832702,    -0.057427,     9.906286)
