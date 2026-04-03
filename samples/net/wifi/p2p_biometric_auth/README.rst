.. zephyr:code-sample:: wifi-p2p-biometric-auth
   :name: WiFi Direct P2P Biometric Auth
   :relevant-api: biometrics_interface wifi_mgmt gpio_interface

   Gate WiFi Direct peer connections with biometric authentication.

Overview
********

This sample demonstrates a WiFi Direct (P2P) hotspot that requires biometric
authentication before granting network access to connecting peers.

When the application starts it:

1. Initializes the GPIO-based LED (``led0`` alias) and the biometric
   fingerprint sensor (``fingerprint`` alias).
2. Pre-enrolls a fingerprint template using the biometrics API.
3. Starts a WiFi Direct Group Owner (hotspot).  On platforms without full
   P2P support, a standard SoftAP is used as a fallback.
4. Waits for a peer device to connect.
5. On peer detection the LED starts blinking rapidly to indicate that
   authentication is required.
6. The application performs a fingerprint verification against the
   pre-enrolled template.
7. On success the LED is turned on steady and the peer is authorised;
   on failure the LED is turned off.

The emulated biometrics driver (``zephyr,biometrics-emul``) can be used for
initial development and testing on ``native_sim``.

Requirements
************

* A board with GPIO-controlled LED (``led0`` devicetree alias).
* A biometric fingerprint sensor, or the emulated driver for testing.
* For WiFi Direct: a WiFi chipset with P2P support and
  ``CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P=y``.

Building and Running
********************

For ``native_sim`` (uses the emulated biometrics driver)::

   west build -b native_sim samples/net/wifi/p2p_biometric_auth
   west build -t run

On a board with real WiFi and fingerprint hardware, provide a board-specific
overlay that defines the ``fingerprint`` alias pointing to the sensor node::

   west build -b <board> samples/net/wifi/p2p_biometric_auth

Sample Output
*************

.. code-block:: console

   [00:00:00.000,000] <inf> p2p_bio_auth: === WiFi Direct Biometric Auth Demo ===
   [00:00:00.000,000] <inf> p2p_bio_auth: Sensor: max_templates=100, samples_required=2
   [00:00:00.000,000] <inf> p2p_bio_auth: Enrolling template 1 – place finger on sensor
   [00:00:00.050,000] <inf> p2p_bio_auth:   captured 1/2 (quality 60)
   [00:00:00.050,000] <inf> p2p_bio_auth:   lift and place finger again
   [00:00:00.600,000] <inf> p2p_bio_auth:   captured 2/2 (quality 60)
   [00:00:00.600,000] <inf> p2p_bio_auth: Enrollment complete for template 1
   [00:00:00.600,000] <inf> p2p_bio_auth: Waiting for a peer to connect...
   [00:00:05.000,000] <inf> p2p_bio_auth: Peer detected – blinking LED, awaiting biometric auth
   [00:00:05.000,000] <inf> p2p_bio_auth: Place finger on sensor to authenticate...
   [00:00:05.150,000] <inf> p2p_bio_auth: Authentication SUCCESS (confidence 80, quality 60)
   [00:00:05.150,000] <inf> p2p_bio_auth: Access GRANTED – peer authorised
