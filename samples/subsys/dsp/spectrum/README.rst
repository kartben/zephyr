.. zephyr:code-sample:: spectrum
   :name: Audio spectrum and waterfall
   :relevant-api: math_dsp_transform audio_dmic_interface display_interface

   Draw a live, screen-sized spectrum and waterfall with the zDSP real FFT.

Overview
********

This sample captures mono PCM from a digital microphone or generates tones,
applies a Hann window, computes a 1024-point floating-point real FFT with
:c:func:`zdsp_rfft_fast_f32`, and renders 48 logarithmically spaced frequency
bands on a display. The default generated source makes it possible to try the
visualization without a microphone. Set ``CONFIG_SAMPLE_SPECTRUM_DMIC=y`` to
use the DMIC driver instead.

The display must be at least 80 x 160 and support RGB565 or byte-swapped
RGB565X. The spectrum and waterfall fill the screen, including 800 x 480
landscape and 172 x 320 portrait displays. Text and spacing grow by an
integer factor on larger displays. Two 16-row RGB565 strips and a 48-band
waterfall history avoid allocating a full-screen framebuffer. Their maximum
dimensions come from the chosen display's devicetree ``width`` and ``height``
(falling back to 320 x 240 if absent). They use about 73 KiB at 800 x 480
or 26 KiB at 172 x 320. After the first frame, only the rows of the bar
plot and waterfall are redrawn, up to about 31 times per second; boards with
slow SPI displays refresh more slowly. The audio path uses 16 kHz mono,
16-bit samples and an FFT hop of 512 samples. Rendering runs in a separate
low-priority thread that fills one strip while a writer thread sends the
other to the display; stale visual frames are skipped when the display
falls behind. This is a visual demo rather than an audio playback pipeline
with hard real-time guarantees.

The 16 kHz mono PCM enters an mpipe application source linked to a leaky
``mpipe_queue``, which keeps the FFT off the capture thread and drops the
oldest blocks if analysis falls behind. The FFT itself calls the zDSP API
directly, as mpipe does not define a spectrum media format.

Requirements
************

* A board with a ``zephyr,display`` chosen node, an RGB565 or RGB565X display
  of at least 80 x 160, an FPU, sufficient RAM for the framebuffer and audio
  pipeline, a full C library, and the CMSIS-DSP module.
* For microphone capture, a ``dmic0`` devicetree alias referring to a ready
  DMIC device that supports 16 kHz, mono, 16-bit PCM. Some boards need an
  overlay to enable or alias their microphone.

Building and Running
********************

For example, the STM32F769I Discovery board with the B-LCD40-DSI1 shield
provides a display and a DMIC. Build the generated-signal version:

.. code-block:: console

   west build -b stm32f769i_disco --shield st_b_lcd40_dsi1_mb1166 samples/subsys/dsp/spectrum
   west flash

Build with microphone capture instead:

.. code-block:: console

   west build -b stm32f769i_disco --shield st_b_lcd40_dsi1_mb1166 samples/subsys/dsp/spectrum -- -DEXTRA_CONF_FILE=dmic.conf
   west flash

The spectrogram adds new rows at the top. Frequencies run logarithmically
from 50 Hz to 8 kHz. The FFT uses a 1024-sample (64 ms) analysis window and
updates every 512 samples (32 ms), and each analysis requests a new frame.
