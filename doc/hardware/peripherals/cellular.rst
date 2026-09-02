.. _cellular_api:

Cellular Modem
##############

Overview
********

A cellular modem is a self-contained radio module that attaches to a mobile network (2G, 3G, LTE,
NB-IoT or 5G) and provides IP connectivity to the host. The host controls it over a serial link,
typically a UART, with the AT commands standardized in 3GPP TS 27.007, and exchanges IP packets
with it over PPP once a data call has been dialed.

In Zephyr, data traffic flows through a regular :ref:`PPP <ppp>` network interface and the
:ref:`BSD sockets API <bsd_sockets_interface>`, so applications need no modem-specific code to send
and receive packets. The cellular API defined in :zephyr_file:`include/zephyr/drivers/cellular.h`
covers the control plane. Every operation is optional for a driver: the API functions return
``-ENOSYS`` (:c:func:`cellular_get_stats` returns ``NULL``) when the driver does not implement
them. Key concepts include:

**Registration status**
  The relation between the modem and the network, as reported by the ``+CREG``, ``+CGREG`` and
  ``+CEREG`` unsolicited responses, is described by :c:enum:`cellular_registration_status` and
  read with :c:func:`cellular_get_registration_status` for the technology family of a given
  :c:enum:`cellular_access_technology`.

**Network status**
  :c:struct:`cellular_evt_network_status` is a snapshot of the serving cell: registration status,
  access technology and, for LTE, operator, tracking area, cell identifiers, channel, band and
  signal levels. It is delivered with :c:enumerator:`CELLULAR_EVENT_NETWORK_STATUS_CHANGED` and
  read back with :c:func:`cellular_get_network_status`.

**Signal quality**
  :c:func:`cellular_get_signal` queries one of the measurements of :c:enum:`cellular_signal_type`:
  RSSI and RSRP in dBm, RSRQ in dB.

**Modem information**
  :c:func:`cellular_get_modem_info` copies one of the strings listed in
  :c:enum:`cellular_modem_info_type`: the IMEI, manufacturer, model and firmware version of the
  modem, or the IMSI and ICCID of the SIM card.

**Networks and APN**
  A :c:struct:`cellular_network` pairs an access technology with an optional list of bands;
  :c:func:`cellular_get_supported_networks` and :c:func:`cellular_configure_networks` read and
  restrict the technologies the modem may use. :c:func:`cellular_set_apn` provides the access
  point name of the PDP context when it is not known at build time.

**Events**
  :c:func:`cellular_set_callback` registers a :c:type:`cellular_event_cb_t` for a mask of
  :c:enum:`cellular_event` values, so that the application learns about registration and serving
  cell changes, newly available modem information, the result of the periodic health check and
  modem suspension without polling.

**Statistics**
  :c:func:`cellular_get_stats` returns the cumulative :c:struct:`cellular_stats` counters of a
  link, such as the time spent without registration, deregistrations and command failures.

The main in-tree implementation of this API is the generic ``modem_cellular`` driver, which
operates AT-command modems through the chat, CMUX and PPP modem modules. Its architecture,
connection lifecycle and the steps needed to support a new modem are described in
:ref:`cellular-modem`. The standalone Quectel BC66x driver
(:kconfig:option:`CONFIG_MODEM_QUECTEL_BC66X`), which uses offloaded sockets instead of PPP,
implements the signal, modem information, registration status and APN operations.

Configuration
*************

The generic driver is enabled with :kconfig:option:`CONFIG_MODEM` and
:kconfig:option:`CONFIG_MODEM_CELLULAR`. It needs the UART in interrupt-driven
(:kconfig:option:`CONFIG_UART_INTERRUPT_DRIVEN`) or asynchronous
(:kconfig:option:`CONFIG_UART_ASYNC_API`) mode, and the networking stack with the PPP L2
(:kconfig:option:`CONFIG_NET_L2_PPP`); :zephyr_file:`samples/net/cellular_modem/prj.conf` is a
complete configuration.

:kconfig:option:`CONFIG_MODEM_CELLULAR_APN` sets the static APN, ``internet`` by default; an empty
string makes the driver wait for :c:func:`cellular_set_apn` before it dials. By default the data
call is dialed as part of powering the modem up; with
:kconfig:option:`CONFIG_MODEM_CELLULAR_ON_DEMAND_CONNECT` the driver instead dials when the PPP
interface is brought up with :c:func:`net_if_up` and hangs up on :c:func:`net_if_down`. This
option is only supported by modems whose vendor configuration has no network script; with such a
script the driver dials as soon as the modem registers, and initialization fails with
``-ENOTSUP``. :kconfig:option:`CONFIG_MODEM_CELLULAR_STATS` enables the
:c:struct:`cellular_stats` counters returned by :c:func:`cellular_get_stats`, which accumulate
since boot: time without registration, registration losses, PPP carrier losses, AT command
failures and ``+CME ERROR`` responses, recovery cycles and CMUX disconnects.

Devicetree Configuration
************************

A cellular modem is a child node of the UART it is connected to. Its ``compatible``, for example
:dtcompatible:`quectel,bg95`, :dtcompatible:`quectel,eg25-g`, :dtcompatible:`u-blox,lara-r6` or
:dtcompatible:`nordic,nrf91-slm`, selects the vendor-specific AT command scripts. These bindings
include :zephyr_file:`dts/bindings/modem/zephyr,cellular-modem-device.yaml`, which defines the
properties shared by every modem: the ``mdm-power-gpios`` and ``mdm-reset-gpios`` control lines
and the ``zephyr,mdm-reset-behavior`` reset policy, the optional ``mdm-wake-gpios``,
``mdm-ring-gpios``, ``mdm-dtr-gpios`` and ``mdm-status-gpios`` lines, ``autostarts`` for modems
that boot without a power pulse, ``zephyr,use-default-pdp-ctx`` and ``zephyr,use-default-apn``
for modems that set up the PDP context or the APN by themselves, and the ``cmux-*`` properties
that control :ref:`cmux-power-saving`.

The following overlay, adapted from the RAK5010 board, defines a Quectel BG95 on ``uart0`` and
makes it available to applications through the ``modem`` alias:

.. code-block:: devicetree

   / {
       aliases {
           modem = &modem;
       };
   };

   &uart0 {
       status = "okay";
       current-speed = <115200>;

       modem: modem {
           compatible = "quectel,bg95";
           mdm-power-gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
           status = "okay";
       };
   };

Basic Operation
***************

Typical use of the cellular API is:

#. Get the modem device, usually through the ``modem`` devicetree alias.
#. Register an event callback with :c:func:`cellular_set_callback` as early as possible, so that
   events emitted while the modem initializes are not missed.
#. If no static APN is configured, provide one with :c:func:`cellular_set_apn`.
#. Bring up the PPP interface with :c:func:`net_if_up` and wait for connectivity, using the
   registration events, :ref:`net_mgmt_interface` events or the
   :ref:`Connection Manager <conn_mgr_overview>`.
#. Use sockets for data, and the cellular API to monitor signal quality, serving cell and
   registration, or to read modem and SIM identifiers.
#. Take the interface down with :c:func:`net_if_down` when connectivity is no longer needed.

The example below subscribes to registration changes, brings up the data connection and prints the
IMEI and the current RSSI once the modem is registered:

.. code-block:: c

   #include <zephyr/device.h>
   #include <zephyr/drivers/cellular.h>
   #include <zephyr/kernel.h>
   #include <zephyr/net/net_if.h>

   static const struct device *modem = DEVICE_DT_GET(DT_ALIAS(modem));

   K_SEM_DEFINE(registered_sem, 0, 1);

   static void cellular_event_handler(const struct device *dev, enum cellular_event event,
                                      const void *payload, void *user_data)
   {
       const struct cellular_evt_registration_status *reg = payload;

       if ((event == CELLULAR_EVENT_REGISTRATION_STATUS_CHANGED) &&
           ((reg->status == CELLULAR_REGISTRATION_REGISTERED_HOME) ||
            (reg->status == CELLULAR_REGISTRATION_REGISTERED_ROAMING))) {
           k_sem_give(&registered_sem);
       }
   }

   int main(void)
   {
       char imei[16];
       int16_t rssi;
       int ret;

       ret = cellular_set_callback(modem, CELLULAR_EVENT_REGISTRATION_STATUS_CHANGED,
                                   cellular_event_handler, NULL);
       if (ret < 0) {
           return ret;
       }

       ret = net_if_up(net_if_get_first_by_type(&NET_L2_GET_NAME(PPP)));
       if (ret < 0) {
           return ret;
       }

       if (k_sem_take(&registered_sem, K_SECONDS(120)) != 0) {
           return -ETIMEDOUT;
       }

       if (cellular_get_modem_info(modem, CELLULAR_MODEM_INFO_IMEI, imei, sizeof(imei)) == 0) {
           printk("IMEI: %s\n", imei);
       }

       if (cellular_get_signal(modem, CELLULAR_SIGNAL_RSSI, &rssi) == 0) {
           printk("RSSI: %d dBm\n", rssi);
       }

       return 0;
   }

The :zephyr:code-sample:`cellular-modem` sample extends this flow with DNS resolution, UDP traffic
and run-time APN selection.

Event callbacks
===============

:c:func:`cellular_set_callback` takes a :c:type:`cellular_event_mask_t` built by combining
:c:enum:`cellular_event` values, the callback and a user data pointer; passing ``NULL`` as the
callback unsubscribes. The callback receives the event that occurred and a pointer to its payload:
a :c:struct:`cellular_evt_modem_info` naming the field that became available for
:c:enumerator:`CELLULAR_EVENT_MODEM_INFO_CHANGED`, a :c:struct:`cellular_evt_registration_status`
for :c:enumerator:`CELLULAR_EVENT_REGISTRATION_STATUS_CHANGED`, a
:c:struct:`cellular_evt_network_status` for :c:enumerator:`CELLULAR_EVENT_NETWORK_STATUS_CHANGED`
and a :c:struct:`cellular_evt_modem_comms_check_result` for
:c:enumerator:`CELLULAR_EVENT_MODEM_COMMS_CHECK_RESULT`, which reports whether the periodic script
succeeded. :c:enumerator:`CELLULAR_EVENT_MODEM_SUSPENDED` has no payload and signals that the modem
has been powered down.

The driver invokes the callback directly from its own context, a work queue thread in the generic
driver, so the callback must stay short and hand heavy processing to a work queue or a thread. The
payload is only valid for the duration of the call and must be copied if it is needed later. The
generic driver keeps a single subscriber per device: registering a new callback replaces the
previous one.

Signal quality and network status
=================================

:c:func:`cellular_get_signal` sends a query to the modem and blocks until it is answered; the
generic driver uses ``AT+CSQ`` for RSSI and ``AT+CESQ`` for RSRP and RSRQ. It returns ``-ENODATA``
while the modem is not in a state where it can be polled, for instance before the data call has
been dialed, and ``-EINVAL`` when the modem reports the measurement as unknown.

:c:func:`cellular_get_network_status` does not talk to the modem. It returns the last serving cell
report the driver received, and ``-ENODATA`` before the first report or after a registration
change that leaves the modem registered, until the next periodic poll refreshes the cache; on
deregistration the report only carries the registration status and access technology. Serving
cell reports come from vendor configurations that parse them, in tree the Quectel EG21-G/EG25-G
and Nordic nRF93m1 ones. The :c:enumerator:`CELLULAR_EVENT_NETWORK_STATUS_CHANGED` event is only
emitted when the registration status, access technology, cell identity or radio channel changes;
the signal levels stored in :c:member:`cellular_evt_network_status.cell` are refreshed on every
poll without raising the event.

APN and access technology selection
===================================

When :kconfig:option:`CONFIG_MODEM_CELLULAR_APN` is empty, the generic driver stops after the modem
has been initialized and waits for :c:func:`cellular_set_apn`, which stores the string and resumes
the connection sequence. The function returns ``-EINVAL`` for an empty or too long APN,
``-EALREADY`` when the APN is unchanged and ``-EBUSY`` once the driver has moved past the APN
configuration step, since the APN is only applied before dialing. With
:kconfig:option:`CONFIG_SAMPLE_CELLULAR_MODEM_AUTO_APN`, the :zephyr:code-sample:`cellular-modem`
sample selects the APN at run time: it subscribes to
:c:enumerator:`CELLULAR_EVENT_MODEM_INFO_CHANGED`, waits for the IMSI of the SIM, matches its MCC
and MNC prefix against a table of profiles and calls :c:func:`cellular_set_apn` from the callback.

:c:func:`cellular_get_supported_networks` returns the list of :c:struct:`cellular_network`
entries, access technologies with their bands, that the modem supports.
:c:func:`cellular_configure_networks` restricts the modem to the given list:
the modem uses one technology at a time, prefers the entries in the order given, and enables all
bands of an entry whose ``bands`` list is empty. Unsupported configurations are rejected with
``-EINVAL``. No in-tree driver implements these two operations yet; the generic driver returns
``-ENOSYS`` for both.

Power management and threading
==============================

The driver powers the modem up and starts the connection sequence when the device is resumed.
Without runtime device power management this happens at boot, at the end of driver initialization;
with :kconfig:option:`CONFIG_PM_DEVICE_RUNTIME` and runtime power management enabled on the modem
node, for example with the ``zephyr,pm-device-runtime-auto`` property (see
:ref:`pm-device-runtime`), the modem stays off until a runtime power management reference is
taken on it, by :c:func:`pm_device_runtime_get` or by bringing the PPP interface up, which takes
one on behalf of the application and releases it on :c:func:`net_if_down`. Suspending the device
runs the shutdown script, powers the modem off and emits
:c:enumerator:`CELLULAR_EVENT_MODEM_SUSPENDED`. The suspend action blocks for up to
30 seconds until the modem is down, and fails with ``-EDEADLK`` when requested from the system
work queue, which the driver needs to process the shutdown, for example by calling
:c:func:`net_if_down` or :c:func:`conn_mgr_all_if_down` from there. While connected, the UART and
the modem can sleep between transfers using the mechanism described in :ref:`cmux-power-saving`.

The API is meant to be called from threads, not from interrupt handlers:
:c:func:`cellular_get_signal` waits for the modem, and :c:func:`cellular_get_network_status`,
:c:func:`cellular_set_apn` and :c:func:`cellular_set_callback` take a mutex that serializes them
against the driver.

Shell commands
**************

When :kconfig:option:`CONFIG_MODEM_CELLULAR_SHELL` is enabled, the ``modem_cellular`` command
controls the periodic script of a ``modem_cellular`` driver instance, as
:c:func:`cellular_modem_pause_periodic_script` and :c:func:`cellular_modem_resume_periodic_script`
do from application code. The device name is completed from the instances of that driver, and both
subcommands return ``-ENOTSUP`` for a modem without a periodic script.

``modem_cellular pause <device>``
  Suppress the periodic script from its next scheduled run, for example while a long AT operation
  such as a firmware update runs on the command channel. A run in flight completes. Pausing an
  already paused script fails with ``-EINVAL``.

``modem_cellular resume <device>``
  Re-enable the periodic script. If a scheduled run was skipped while paused, the script runs
  immediately; otherwise the periodic timer restarts. Resuming a script that is not paused fails
  with ``-EINVAL``.

When :kconfig:option:`CONFIG_MODEM_AT_SHELL` is enabled, arbitrary AT commands can be sent to the
modem referenced by the ``modem`` devicetree alias, through the CMUX user pipe selected with
:kconfig:option:`CONFIG_MODEM_AT_USER_PIPE_IDX`:

``modem at <command> [expected_response]``
  Send ``command`` to the modem and print every response line until ``expected_response`` (``OK``
  by default) or ``ERROR`` is received, or until
  :kconfig:option:`CONFIG_MODEM_AT_SHELL_RESPONSE_TIMEOUT_S` elapses. The command reports that the
  modem is not ready until the driver has connected its user pipes, and that a script is already
  running while a previous command is still waiting for its response.

Both commands are mutually exclusive with :kconfig:option:`CONFIG_MODEM_SHELL`, and the AT shell
additionally requires :kconfig:option:`CONFIG_SHELL_WILDCARD` to be disabled.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_MODEM`
* :kconfig:option:`CONFIG_MODEM_CELLULAR`
* :kconfig:option:`CONFIG_MODEM_CELLULAR_INIT_PRIORITY`
* :kconfig:option:`CONFIG_MODEM_CELLULAR_APN`
* :kconfig:option:`CONFIG_MODEM_CELLULAR_PERIODIC_SCRIPT_MS`
* :kconfig:option:`CONFIG_MODEM_CELLULAR_ON_DEMAND_CONNECT`
* :kconfig:option:`CONFIG_MODEM_CELLULAR_STATS`
* :kconfig:option:`CONFIG_MODEM_CELLULAR_MAX_SCRIPT_FAILURES`
* :kconfig:option:`CONFIG_MODEM_CELLULAR_MAX_RECOVERIES`
* :kconfig:option:`CONFIG_MODEM_CELLULAR_SHELL`
* :kconfig:option:`CONFIG_MODEM_AT_SHELL`

API Reference
*************

Cellular API
============

.. doxygengroup:: cellular_interface

Device-specific extensions
==========================

.. doxygengroup:: cellular_interface_ext
