.. _biometrics_api:

Biometrics
##########

Overview
********

The biometrics API provides a unified interface for biometric sensors such as
fingerprint scanners, iris scanners, face recognition, and palm / palm-vein
modules. These sensors are commonly used for secure authentication in embedded
systems, access control devices, and IoT applications.

The API supports the full lifecycle of biometric operations including enrollment,
template management, and matching. Sensors can store templates on-device or on
the host system depending on hardware capabilities.

:c:member:`biometric_capabilities.type` describes the primary modality; multimodal
devices also set :c:member:`biometric_capabilities.supported_modalities` as a bitmask
(for example face and palm on the DFRobot AI10 UART module: see
`DFRobot_AI10`_ and the :dtcompatible:`dfrobot,ai10` binding).

.. _DFRobot_AI10: https://github.com/DFRobot/DFRobot_AI10/tree/master/src

A typical fingerprint enrollment process requires capturing multiple samples
of the same finger to create a reliable template. The matching process compares
a captured sample against stored templates to verify identity.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_BIOMETRICS`
* :kconfig:option:`CONFIG_BIOMETRICS_INIT_PRIORITY`
* :kconfig:option:`CONFIG_BIOMETRICS_DFROBOT_AI10`

API Reference
*************

.. doxygengroup:: biometrics_interface
