.. zephyr:code-sample:: ocpp_demo_zephyr43
   :name: Zephyr 4.3 Comprehensive Demo - OCPP with GUI
   :relevant-api: ocpp_api display_interface lvgl

   Comprehensive demo showcasing Zephyr 4.3 highlights with OCPP, GUI, USB, CPU monitoring,
   and instrumentation features.

Overview
********

This comprehensive demo application showcases the major highlights of Zephyr 4.3 in a
single application. It builds upon the OCPP (Open Charge Point Protocol) sample and adds:

**Zephyr 4.3 Highlights Demonstrated:**

1. **OCPP 1.6 Library**: EV charging station implementation with Central System communication
2. **USB Device "Next" Stack**: Modern USB device implementation with runtime configuration
3. **CPU Load Subsystem**: Real-time CPU usage metrics display
4. **CPU Frequency Scaling**: Dynamic frequency adjustment (when supported)
5. **Instrumentation Subsystem**: Function tracing and profiling capabilities
6. **LVGL GUI**: Interactive touchscreen display showing all metrics

Features
********

* **GUI Dashboard**:
  - Real-time charging status for multiple connectors
  - CPU load percentage visualization
  - Network connection status
  - Active transactions display
  - Meter value readings
  - System information panel

* **OCPP Integration**:
  - Multi-connector support (2 connectors)
  - Authorization and transaction management
  - Meter value reporting
  - Remote start/stop support

* **System Monitoring**:
  - CPU usage tracking
  - Frequency scaling visualization
  - USB connection status

* **Interactive Controls**:
  - Touch-based connector selection
  - Manual start/stop charging
  - Status monitoring

Requirements
************

Hardware:

- STM32F746G Discovery Kit (recommended)
- Network connection (Ethernet)
- OCPP Central System server (e.g., SteVe)

Software:

- Zephyr 4.3 or later
- LVGL graphics library
- Network stack with WebSocket support

Building and Running
********************

1. Set up the OCPP server details:

.. code-block:: console

   # Edit prj.conf or use build-time configuration
   CONFIG_NET_SAMPLE_SNTP_SERVER="pool.ntp.org"
   CONFIG_NET_SAMPLE_OCPP_SERVER="your-server-ip"
   CONFIG_NET_SAMPLE_OCPP_PORT=8180

2. Build the application:

.. zephyr-app-commands::
   :zephyr-app: samples/net/ocpp_demo_zephyr43
   :board: stm32f746g_disco
   :goals: build flash
   :compact:

3. Connect the board to your network via Ethernet

4. Start your OCPP Central System server (e.g., SteVe)

5. Interact with the touchscreen display to monitor and control charging

Sample Output
*************

The display shows:

- Header: "Zephyr 4.3 OCPP Demo"
- Connection status indicator
- CPU load gauge
- Two connector panels with status
- Current meter readings
- USB device status
- System uptime

Console output:

.. code-block:: console

   *** Booting Zephyr OS build v4.3.0 ***
   [00:00:00.010] Zephyr 4.3 OCPP Demo - STM32F746G Discovery
   [00:00:00.100] Display initialized: 480x272
   [00:00:00.150] USB Device stack initialized
   [00:00:01.000] CPU load monitoring started
   [00:00:02.642] Network: 192.168.1.101
   [00:00:07.024] OCPP: Connected to Central System
   [00:00:17.066] Connector 1: Authorized
   [00:00:17.197] Connector 1: Charging started

Using the Demo
**************

1. **Touchscreen Navigation**:
   - Tap connector panels to view details
   - Use buttons to start/stop charging
   - Monitor real-time metrics

2. **OCPP Operations**:
   - Automatic boot notification
   - Remote start/stop from Central System
   - Periodic meter value reporting
   - Status notifications

3. **Performance Monitoring**:
   - Watch CPU load in real-time
   - Observe frequency scaling effects
   - Monitor system resources

4. **USB Features**:
   - USB device enumeration
   - Runtime configuration
   - Status indication

Instrumentation
***************

The application includes instrumentation hooks for profiling:

.. code-block:: console

   # On host, after building with CONFIG_INSTRUMENTATION=y
   scripts/instrumentation/zaru.py trace -v

This allows capturing call graphs and performance profiles for analysis.

Developer Experience Features
******************************

This demo prominently showcases Zephyr 4.3's new developer experience improvement tools.

**See DX_TOOLS_DEMO.md for comprehensive demonstrations with examples!**

Quick reference:

1. **DTDoctor**: Diagnose devicetree issues with helpful error messages
   
   .. code-block:: console
   
      west dtdoctor

2. **Trace Kconfig**: Interactive configuration dependency analysis
   
   .. code-block:: console
   
      west build -t traceconfig

3. **Footprint Analysis**: Interactive memory usage visualization
   
   .. code-block:: console
   
      west build -t footprint

4. **Size Reports**: Detailed ROM/RAM breakdowns
   
   .. code-block:: console
   
      west build -t rom_report
      west build -t ram_report

For detailed examples, use cases, and sample outputs, see the **DX_TOOLS_DEMO.md** guide
included with this sample

References
**********

- :ref:`ocpp_interface`
- :ref:`usb_device_stack_next`
- :ref:`cpu_load`
- :ref:`cpu_freq`
- :zephyr:code-sample:`instrumentation`
- LVGL Documentation: https://docs.lvgl.io
