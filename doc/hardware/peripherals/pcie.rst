.. _pcie_api:

Peripheral Component Interconnect express Bus (PCIe)
####################################################

Overview
********

Peripheral Component Interconnect Express (PCIe) is a high-speed serial computer expansion bus
standard used to connect peripheral devices to a host system. PCIe provides point-to-point
connections between devices, with each connection called a "link" consisting of one or more lanes
for data transfer.

The PCIe API provides a generic interface for PCIe host controllers and endpoints. Key features include:

**Device Discovery**
  Enumerate PCIe devices on the bus.
  Identify devices by vendor ID, device ID, and class code.
  Access device configuration space.

**Memory and I/O Access**
  Map PCIe device memory regions (Base Address Registers - BARs).
  Access device registers and memory-mapped I/O.

**Interrupt Management**
  Configure and manage PCIe interrupts (INTx, MSI, MSI-X).
  Register interrupt handlers for device events.

**Bus Configuration**
  Read and write PCIe configuration space registers.
  Configure device capabilities and features.
  Support for PCIe endpoint and host modes.

Devicetree Configuration
************************

PCIe devices are defined in the Devicetree with PCIe-specific properties
including bus/device/function (BDF) identifiers and vendor/device IDs.

Example of a PCIe host controller definition:

.. code-block:: devicetree

   pcie0: pcie@d0070000 {
       compatible = "pci-host-ecam-generic";
       reg = <0xd0070000 0x10000>;
       #address-cells = <3>;
       #size-cells = <2>;
       device_type = "pci";
       bus-range = <0x00 0xff>;
       ranges = <0x02000000 0x0 0x10000000 0x10000000 0x0 0x10000000>;
   };

Example of a PCIe endpoint device:

.. code-block:: devicetree

   pcie_device: pcie@0,0,0 {
       compatible = "vendor,pcie-device";
       reg = <0x00000000 0x0 0x0 0x0 0x0>;
       vendor-id = <0x8086>;
       device-id = <0x5845>;
   };

Basic Operation
***************

PCIe operations typically involve discovering devices, mapping their memory regions,
and configuring interrupts.

.. code-block:: c
   :caption: Enumerate and configure a PCIe device

   #include <zephyr/drivers/pcie/pcie.h>

   void setup_pcie_device(void)
   {
       pcie_bdf_t bdf;
       struct pcie_mbar mbar;
       unsigned int irq;

       /* Scan for device with specific vendor/device ID */
       pcie_id_t device_id = PCIE_ID(0x8086, 0x5845);
       
       if (!pcie_probe(PCIE_BDF(0, 0, 0), device_id)) {
           printk("PCIe device not found\n");
           return;
       }

       bdf = PCIE_BDF(0, 0, 0);

       /* Get Base Address Register (BAR) 0 */
       if (!pcie_get_mbar(bdf, 0, &mbar)) {
           printk("Failed to get BAR 0\n");
           return;
       }

       /* Note: Memory mapping would be done here using appropriate functions
        * based on your specific hardware and memory management setup */

       /* Configure MSI interrupt (example - actual implementation varies) */
       irq = pcie_alloc_irq(bdf);
       if (irq == PCIE_CONF_INTR_IRQ_NONE) {
           printk("Failed to allocate IRQ\n");
           return;
       }

       /* Note: Connect IRQ handler using your application's ISR function */
       /* pcie_connect_dynamic_irq(bdf, irq, 0, your_isr_handler, NULL, 0); */
       /* pcie_irq_enable(bdf, irq); */

       printk("PCIe device configured successfully\n");
   }

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_PCIE`
* :kconfig:option:`CONFIG_PCIE_MSI`
* :kconfig:option:`CONFIG_PCIE_MSI_X`
* :kconfig:option:`CONFIG_PCIE_SHELL`
* :kconfig:option:`CONFIG_PCIE_CONTROLLER`

API Reference
*************

.. doxygengroup:: pcie_host_interface
