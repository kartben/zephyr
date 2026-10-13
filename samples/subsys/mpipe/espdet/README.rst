.. zephyr:code-sample:: mpipe-espdet
   :name: ESPDet-Pico object detection pipeline
   :relevant-api: mpipe_framework mpipe_transform

   Detect objects in the camera stream with ESPDet-Pico and draw them on the display.

Overview
********

This sample runs `ESPDet-Pico`_, Espressif's tiny YOLO-style detector (0.36 M parameters,
0.17 GFLOPs at 224x224), on the camera stream of an ESP32-S3 board, and draws the detected
objects on the live video. It is built with the :ref:`Multimedia Pipeline <mpipe>` and runs the
model with Espressif's `ESP-DL`_ inference library.

.. graphviz::

   digraph pipeline {
     rankdir=LR;
     node [shape=box, style=filled, fillcolor="#e8e8e8"];
     camera  [label="Camera\nSource"];
     caps    [label="Caps\nFilter"];
     espdet  [label="ESPDet\nOverlay"];
     display [label="Display\nSink"];
     detector [label="Detection\nthread", style="filled,dashed"];
     camera -> caps -> espdet -> display;
     espdet -> detector [label=" frame copy", style=dashed];
     detector -> espdet [label=" boxes", style=dashed];
   }

- **Camera source** captures 320x240 RGB565 frames.
- **Caps filter** fixes the frame format shared by the detector and the display.
- **ESPDet overlay** is an in-place transform written in the sample. When its detection thread is
  idle, it hands it a copy of the frame. It then draws the latest detections, their label and
  score, and a status line on every frame going through.
- **Display sink** writes the frames to the display.

The video therefore never waits for the detector: it runs at the camera and display rate, while
the boxes refresh at the rate of the model. The detection thread letterboxes the frame to the
model input, runs the model and maps the boxes back to the frame.

ESPDet-Pico is trained for the objects a given application needs. The class labels drawn by the
sample come from ``CONFIG_ESPDET_CLASS_LABELS``, ``person,phone`` by default: see
`Training a person and phone detector`_.

Requirements
************

- An ESP32-S3 board with PSRAM, a camera and a display:

  - :zephyr:board:`m5stack_stackchan`
  - :zephyr:board:`m5stack_cores3` (not the SE variant, which has no camera)

- An ESP-DL source tree, version 3.3.11:

  .. code-block:: console

     git clone --depth 1 --branch v3.3.11 --filter=blob:none --sparse \
         https://github.com/espressif/esp-dl
     git -C esp-dl sparse-checkout set esp-dl models/cat_detect/models/s3

  The ``modules/esp-dl`` directory of the sample builds it from source as a Zephyr module, with a
  small compatibility layer for the ESP-IDF services it expects. ESP-DL distributes its model
  reader in binary form only (``libfbs_model.a``): the module does not link it and provides an
  open implementation instead.

- An ESPDet-Pico model in ESP-DL format (``.espdl``) quantized for the ESP32-S3.

Building and running
********************

Pass the ESP-DL tree and the model to the build. To try the sample right away with the cat
detector of the ESP-DL model zoo:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/mpipe/espdet
   :board: m5stack_stackchan/esp32s3/procpu
   :gen-args: -DESP_DL_DIR=<esp-dl>/esp-dl
              -DESPDET_MODEL=<esp-dl>/models/cat_detect/models/s3/espdet_pico_224_224_cat.espdl
              -DCONFIG_ESPDET_CLASS_LABELS=\"cat\"
   :goals: build flash
   :compact:

The ``ESP_DL_DIR`` environment variable can be used instead of the CMake variable.

The display shows the camera stream with a box and a label around every detected object, and the
inference time and video frame rate at the bottom. With
``CONFIG_ESPDET_LOG_DETECTIONS``, the detections are also logged:

.. code-block:: console

   <inf> detector: Model loaded, input 224x224
   <inf> espdet_overlay: Detecting on 320x240 RGB565X frames
   <inf> espdet_overlay: cat 88% (0,85)-(222,239)
   <inf> espdet_overlay: cat 87% (142,42)-(319,239)

Espressif measures ESPDet-Pico at 224x224 at about 126 ms per frame on the ESP32-S3, including
pre- and post-processing.

Training a person and phone detector
************************************

ESPDet-Pico models are trained with `ESP-Detection`_, which builds on Ultralytics YOLO and exports
``.espdl`` models. To detect people and phones, train it on the ``person`` and ``cell phone``
classes of COCO 2017 (classes 0 and 67).

#. Download COCO 2017 in the Ultralytics YOLO layout, for instance with the Ultralytics
   ``coco.yaml`` dataset, and set up ESP-Detection as its documentation describes.

#. Carve the two-class dataset out of it with the helper of the sample. Only the images holding
   a person or a phone are kept, and their labels are renumbered from 0. A validation subset is
   copied to ``calib/`` for the quantization of the model:

   .. code-block:: console

      python samples/subsys/mpipe/espdet/train/coco_subset.py \
          --coco datasets/coco --output datasets/person_phone \
          --classes 0:person 67:phone

#. Train, export and quantize the model from the ESP-Detection directory. The training settings,
   the number of epochs and the device in particular, are in its ``train.py``:

   .. code-block:: console

      python espdet_run.py --class_name person_phone --pretrained_path None \
          --dataset datasets/person_phone/dataset.yaml --size 224 224 \
          --target esp32s3 --calib_data datasets/person_phone/calib \
          --espdl espdet_pico_224_224_person_phone.espdl --img espdet.jpg

#. Build the sample with the model. The class labels default to ``person,phone``, in the order
   given to ``coco_subset.py``:

   .. code-block:: console

      west build -b m5stack_stackchan/esp32s3/procpu samples/subsys/mpipe/espdet -- \
          -DESP_DL_DIR=<esp-dl>/esp-dl \
          -DESPDET_MODEL=<esp-detection>/espdet_pico_224_224_person_phone.espdl

Notes
*****

- ESP-DL uses the FPU and the SIMD instructions (PIE) of the ESP32-S3. The ESP-DL module enables
  both coprocessors at boot. Zephyr does not preserve the PIE registers across context switches,
  so the sample runs ESP-DL from a single thread.
- The model reader of the ESP-DL module, ``compat/src/fbs_model.cpp``, reads ``.espdl`` files
  in place following the ``fbs_loader/espdl.fbs`` schema of ESP-DL, behind the ``fbs::FbsModel``
  interface ESP-DL expects.
- ESP-DL allocates from two heaps of its own: one in internal SRAM for the activations
  (``CONFIG_ESP_DL_INTERNAL_HEAP_SIZE``), one in PSRAM for the rest
  (``CONFIG_ESP_DL_PSRAM_HEAP_SIZE``).

.. _ESPDet-Pico: https://github.com/espressif/esp-detection
.. _ESP-DL: https://github.com/espressif/esp-dl
.. _ESP-Detection: https://github.com/espressif/esp-detection
