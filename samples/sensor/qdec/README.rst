.. zephyr:code-sample:: qdec
   :name: Quadrature Decoder Sensor
   :relevant-api: sensor_interface

   Get rotation data from a quadrature decoder sensor.

Overview
********

This sample reads the value of the counter which has been configured in
quadrature decoder mode.

It requires:

* an external mechanical encoder
* pin to be properly configured in the device tree

Requirements
************

The sample runs on any board whose devicetree provides a ``qdec0`` alias pointing to an enabled
quadrature decoder. It is tested on the following boards, which provide that alias in their own
devicetree or in an overlay in the sample's :file:`boards` directory:

* ``sam_e70_xplained/same70q21`` and ``sam_e70_xplained/same70q21b``
* ``mimxrt1050_evk/mimxrt1052/hyperflash`` and ``mimxrt1170_evk@A/mimxrt1176/cm7``
* ``nrf52840dk/nrf52840``, ``nrf5340dk/nrf5340/cpuapp``, ``nrf54h20dk/nrf54h20/cpuapp``,
  ``nrf54l15dk/nrf54l15/cpuapp``, ``nrf54lm20dk/nrf54lm20a/cpuapp``,
  ``nrf54lm20dk/nrf54lm20b/cpuapp``, ``nrf7120dk/nrf7120/cpuapp`` and
  ``ophelia4ev/nrf54l15/cpuapp``
* ``bl54l15_dvk/nrf54l15/cpuapp``
* ``esp32c6_devkitc/esp32c6/hpcore``, ``esp32s3_devkitc/esp32s3/procpu``,
  ``esp32s3_luatos_core/esp32s3/procpu``, ``esp32s3_luatos_core/esp32s3/procpu/usb`` and
  ``matouch_mtro128g/esp32s3/procpu``
* ``frdm_imxrt1186/mimxrt1186/cm33``, ``frdm_imxrt1186/mimxrt1186/cm7``,
  ``mimxrt1180_evk/mimxrt1189/cm33`` and ``mimxrt1180_evk/mimxrt1189/cm7``
* ``frdm_mcxa153``, ``frdm_mcxa156``, ``frdm_mcxa266``, ``frdm_mcxa344``, ``frdm_mcxa346`` and
  ``frdm_mcxa366``
* ``frdm_mcxn236``, ``frdm_mcxn947/mcxn947/cpu0`` and ``mcx_n5xx_evk/mcxn547/cpu0``
* ``mr_canhubk3``
* ``rtl8752h_evb/rtl8752hjl`` and ``rtl87x2g_evb_a/rtl8762gku``
* ``b_u585i_iot02a``, ``disco_l475_iot1``, ``stm32f3_disco``, ``stm32h573i_dk``,
  ``stm32h7s78_dk``, ``stm32l562e_dk``, ``stm32n6570_dk/stm32n657xx/sb`` and ``stm32u083c_dk``
* ``nucleo_c071rb``, ``nucleo_c5a3zg``, ``nucleo_f091rc``, ``nucleo_f103rb``, ``nucleo_f207zg``,
  ``nucleo_f401re``, ``nucleo_f429zi``, ``nucleo_f746zg``, ``nucleo_g071rb``, ``nucleo_g474re``,
  ``nucleo_h753zi``, ``nucleo_l073rz``, ``nucleo_l152re``, ``nucleo_u385rg_q``, ``nucleo_wb09ke``,
  ``nucleo_wb55rg``, ``nucleo_wba55cg`` and ``nucleo_wl55jc``

The NXP eQDC trigger variant is tested on the FRDM-MCXA, ``frdm_imxrt1186`` and
``mimxrt1180_evk`` boards listed above.

When the devicetree also defines ``qenca`` and ``qencb`` aliases for two GPIO outputs, the sample
toggles them to emulate an encoder. On the nRF boards listed above, these outputs must be wired
to the decoder's A and B inputs (see the board overlay for the pins).

Building and Running
********************

In order to run this sample you need to:

* enable the quadrature decoder device in your board's DT file or board overlay
* add a new alias property named ``qdec0`` and make it point to the decoder
  device you just enabled

For example, here's how the overlay file of an STM32F401 board looks like when
using decoder from TIM3 through pins PA6 and PA7:

.. code-block:: dts

    / {
        aliases {
            qdec0 = &qdec;
        };
    };

    &timers3 {
        status = "okay";

        qdec: qdec {
            status = "okay";
            pinctrl-0 = <&tim3_ch1_pa6 &tim3_ch2_pa7>;
            pinctrl-names = "default";
            st,input-polarity-inverted;
            st,input-filter-level = <FDIV32_N8>;
            st,counts-per-revolution = <16>;
        };
    };

Sample Output
=============

Once the MCU is started it prints the counter value every second on the
console

.. code-block:: console

    Quadrature decoder sensor test
    Position = 0 degrees
    Position = 15 degrees
    Position = 30 degrees
    ...

If the driver supports getting speed (RPM) and revolution count channels, these
data will be displayed on the console.

When ``CONFIG_EQDC_MCUX_TRIGGER=y`` (e.g. on ``frdm_mcxa153``), the sample also
registers the ``SENSOR_TRIG_OVERFLOW`` trigger on the revolution channel and
prints the trigger count each cycle:

.. code-block:: console

    Quadrature decoder sensor test
    Registered SENSOR_TRIG_OVERFLOW on SENSOR_CHAN_ENCODER_REVOLUTIONS
    Position = 0 degrees
    Revolutions = 0
    Triggers = 0
    Position = 180 degrees
    Revolutions = 1
    Triggers = 1
    ...

Of course the read value changes once the user manually rotates the mechanical
encoder.

.. note::

    The reported increment/decrement can be larger/smaller than the one shown
    in the above example. This depends on the mechanical encoder being used and
    ``st,counts-per-revolution`` value.
