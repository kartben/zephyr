.. zephyr:code-sample:: adc_sequence
   :name: Analog-to-Digital Converter (ADC) sequence sample
   :relevant-api: adc_interface

   Read analog inputs from ADC channels, using a sequence.

Overview
********

This sample demonstrates how to use the :ref:`ADC driver API <adc_api>` using sequences.

Depending on the target board, it reads ADC samples from two channels
and prints the readings on the console, based on the sequence specifications.
Notice how for the whole sequence reading, only one call to the :c:func:`adc_read` API is made.
If voltage of the used reference can be obtained, the raw readings are converted to millivolts.

This example constructs an adc device and setups its channels, according to the
given devicetree configuration.

Requirements
************

The board must provide an ``adc0`` devicetree alias pointing to an enabled ADC node
(``status = "okay";``) that has each channel to sample as a child node, with the desired
settings like gain, reference, acquisition time and oversampling setting (if used). See
:zephyr_file:`samples/drivers/adc/adc_sequence/boards/nrf52840dk_nrf52840.overlay` for an
example of such setup.

The sample has been verified on the following board targets:

- ``cy8cproto_062_4343w``
- ``cy8cproto_063_ble``
- ``frdm_ke15z``
- ``frdm_mcxc242``
- ``frdm_mcxe247``
- ``mck_ra4t1``
- ``nrf52840dk/nrf52840``
- ``nrf5340dk/nrf5340/cpuapp``
- ``nrf54h20dk/nrf54h20/cpuapp``
- ``nrf54h20dk/nrf54h20/cpuppr``
- ``nrf54l15dk/nrf54l15/cpuapp``
- ``nrf54lm20dk/nrf54lm20a/cpuapp``
- ``nrf54lm20dk/nrf54lm20b/cpuapp``
- ``ophelia4ev/nrf54l15/cpuapp``
- ``raytac_an54lq_db_15/nrf54l15/cpuapp``
- ``raytac_an54lv_db_15/nrf54l15/cpuapp``
- ``s32k148_evb``
- ``siwx917_rb4342a``
- ``slwrb4180a``
- ``ucans32k1sic``
- ``xg27_rb4194a``
- ``xg29_rb4412a``

The 8-bit resolution variant (``CONFIG_SEQUENCE_RESOLUTION=8``) has been verified on
the nRF54H20, nRF54L15 and nRF54LM20 development kit targets listed above.

The sample's :file:`boards` directory also provides overlays for other boards, which are not
part of the verified list above.

Building and Running
********************

Building and Running for Nordic nRF52840
========================================

The sample can be built and executed for the
:zephyr:board:`nrf52840dk` as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/adc/adc_sequence
   :board: nrf52840dk/nrf52840
   :goals: build flash
   :compact:

To build for another board, change "nrf52840dk/nrf52840" above to that board's name
and provide a corresponding devicetree overlay.

Sample output
=============

You should get a similar output as below, repeated every second:

.. code-block:: console

   ADC sequence reading [1]:
   - ADC_0, channel 0, 5 sequence samples:
   - - 36 = 65mV
   - - 35 = 63mV
   - - 36 = 65mV
   - - 35 = 63mV
   - - 36 = 65mV
   - ADC_0, channel 1, 5 sequence samples:
   - - 0 = 0mV
   - - 0 = 0mV
   - - 1 = 1mV
   - - 0 = 0mV
   - - 1 = 1mV

.. note:: If the ADC is not supported, the output will be an error message.
