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

Sensors report the modalities they support in the
:c:member:`biometric_capabilities.supported_modalities` bitmask, where each
bit position corresponds to a :c:enum:`biometric_sensor_type` enumerator.
Single-modality sensors set exactly one bit, while multimodal devices (for
example a combined face and palm-vein module) set one bit per supported
modality.

A typical fingerprint enrollment process requires capturing multiple samples
of the same finger to create a reliable template. The matching process compares
a captured sample against stored templates to verify identity.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_BIOMETRICS`
* :kconfig:option:`CONFIG_BIOMETRICS_INIT_PRIORITY`

API Reference
*************

.. doxygengroup:: biometrics_interface
