OCPP CitrineOS Quick Start Guide
==================================

This guide provides step-by-step instructions to get started with the OCPP
CitrineOS demo on Zephyr.

Prerequisites
-------------

1. Docker and Docker Compose installed on your development machine
2. Zephyr development environment set up
3. A supported Zephyr board with network capability (Ethernet or WiFi)

Step 1: Set Up CitrineOS
-------------------------

1. Clone and start CitrineOS:

   ```bash
   git clone https://github.com/citrineos/citrineos-core.git
   cd citrineos-core
   docker-compose up -d
   ```

2. Verify CitrineOS is running:

   ```bash
   docker ps
   ```

   You should see the CitrineOS containers running.

3. Access the web interface at http://localhost:8080

4. Note your machine's IP address (where CitrineOS is running):

   ```bash
   # Linux
   ip addr show
   
   # macOS
   ifconfig
   
   # Windows
   ipconfig
   ```

Step 2: Build the Zephyr OCPP Demo
-----------------------------------

### Option A: Using the Standard Demo (main.c)

Build with CitrineOS overlay configuration:

```bash
cd /path/to/zephyr
west build -b <your_board> samples/net/ocpp -- \
  -DOVERLAY_CONFIG=overlay-citrineos.conf \
  -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"<citrineos_ip>\" \
  -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"pool.ntp.org\"
```

### Option B: Using the CitrineOS-Specific Demo (main_citrineos.c)

Build with the dedicated CitrineOS demo source:

```bash
cd /path/to/zephyr
west build -b <your_board> samples/net/ocpp -- \
  -DOVERLAY_CONFIG=overlay-citrineos.conf \
  -DUSE_CITRINEOS_DEMO=y \
  -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"<citrineos_ip>\" \
  -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"pool.ntp.org\"
```

### Example for STM32F769I Discovery Board

```bash
west build -b stm32f769i_disco samples/net/ocpp -- \
  -DOVERLAY_CONFIG=overlay-citrineos.conf \
  -DUSE_CITRINEOS_DEMO=y \
  -DCONFIG_NET_SAMPLE_OCPP_SERVER=\"192.168.1.100\" \
  -DCONFIG_NET_SAMPLE_SNTP_SERVER=\"pool.ntp.org\"
```

Step 3: Flash and Run
---------------------

1. Flash the board:

   ```bash
   west flash
   ```

2. Monitor the serial output:

   ```bash
   # Linux/macOS
   screen /dev/ttyACM0 115200
   
   # Or use minicom
   minicom -D /dev/ttyACM0
   ```

3. You should see output similar to:

   ```
   *** Booting Zephyr OS ***
   OCPP CitrineOS Demo stm32f769i_disco
   Connecting to CitrineOS Central System
   [00:00:02.642,000] <inf> net_dhcpv4: Received: 192.168.1.101
   [00:00:07.011,000] <inf> citrineos_demo: cs server 192.168.1.100 8080
   [00:00:07.025,000] <inf> ocpp: ocpp init success
   [00:00:17.066,000] <inf> citrineos_demo: ocpp auth 0> idcon 1 status 1
   [00:00:17.197,000] <inf> citrineos_demo: ocpp start charging connector id 1
   ```

Step 4: Monitor in CitrineOS
-----------------------------

1. Open the CitrineOS web interface: http://localhost:8080

2. Navigate to the Charge Points section

3. Look for your charge point with ID "CitrineOSDemo" or "zephyr"

4. Monitor the connection status, transactions, and meter values

5. Try sending remote commands (e.g., Remote Start/Stop Transaction)

Differences from SteVe
----------------------

| Feature           | CitrineOS         | SteVe                               |
|-------------------|-------------------|-------------------------------------|
| Default Port      | 8080              | 8180                                |
| WebSocket Path    | /ocpp             | /steve/websocket/CentralSystemService/<id> |
| Setup             | Docker Compose    | Java WAR deployment                 |
| UI                | Modern React UI   | Traditional JSP interface           |

Troubleshooting
---------------

### Connection Refused

- Ensure CitrineOS is running: `docker ps`
- Check firewall allows port 8080
- Verify the IP address is correct

### DHCP Failure

- Check network cable connection
- Verify DHCP server is available on the network
- Try static IP configuration if needed

### Authorization Rejected

- Check CitrineOS logs: `docker logs citrineos`
- Some CitrineOS configurations require pre-registration of ID tags
- Verify the ID tag in the demo code matches CitrineOS configuration

### Time Synchronization Issues

- Verify SNTP server is reachable
- Check network connectivity
- Try using a different SNTP server (e.g., "time.google.com", "0.pool.ntp.org")

Next Steps
----------

1. Customize the charge point configuration in the source code
2. Implement actual meter reading from hardware
3. Add support for additional OCPP features
4. Integrate with real charging hardware
5. Explore OCPP 2.0.1 support (CitrineOS supports both 1.6 and 2.0.1)

Resources
---------

- CitrineOS Documentation: https://github.com/citrineos/citrineos-core
- OCPP Specification: https://www.openchargealliance.org/
- Zephyr OCPP API: https://docs.zephyrproject.org/latest/connectivity/networking/api/ocpp.html
- Zephyr Getting Started: https://docs.zephyrproject.org/latest/develop/getting_started/
