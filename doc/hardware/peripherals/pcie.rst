.. _pcie_api:

Peripheral Component Interconnect express Bus (PCIe)
####################################################

Overview
********

PCI Express (PCIe) is a packet based, point-to-point serial interconnect. Every function attached
to the bus exposes a standardized configuration space through which a host identifies it, assigns
the memory or I/O windows it requests through its Base Address Registers (BARs), and selects how
it signals interrupts: over a wired INTx line, or as Message Signaled Interrupts (MSI and MSI-X)
written to an address owned by the interrupt controller.

Zephyr can operate on either side of the link. As a **host** (:kconfig:option:`CONFIG_PCIE`), the
drivers of PCIe functions (UART, Ethernet, NVMe, VirtIO and others) use the API of
:zephyr_file:`include/zephyr/drivers/pcie/pcie.h` to locate their function, access its
configuration space and BARs, and connect its interrupts; where no boot firmware has enumerated
the bus, a host controller driver implementing
:zephyr_file:`include/zephyr/drivers/pcie/controller.h` does it. As an **endpoint**
(:kconfig:option:`CONFIG_PCIE_ENDPOINT`), the API of
:zephyr_file:`include/zephyr/drivers/pcie/endpoint/pcie_ep.h` lets Zephyr act as a function of a
remote host, mapping host memory into outbound windows and raising interrupts towards the host.
The host API is consumed by drivers rather than by applications, which reach a PCIe function
through its driver class, for example the :ref:`NVMe <disk_nvme>` disk driver.

Key concepts include:

**Bus, device, function (BDF)**
  Every function is addressed by its position in the hierarchy, packed into a
  :c:type:`pcie_bdf_t` with :c:macro:`PCIE_BDF` and unpacked with :c:macro:`PCIE_BDF_TO_BUS`,
  :c:macro:`PCIE_BDF_TO_DEV` and :c:macro:`PCIE_BDF_TO_FUNC`. :c:macro:`PCIE_BDF_NONE` denotes a
  function that has not been found.

**PCIe context**
  :c:struct:`pcie_dev` ties a devicetree node to a function through its vendor and device IDs, a
  :c:type:`pcie_id_t` built with :c:macro:`PCIE_ID`. A driver declares the context with
  :c:macro:`DEVICE_PCIE_INST_DECLARE`, points its configuration structure at it with
  :c:macro:`DEVICE_PCIE_INST_INIT`, and reads the :c:member:`pcie_dev.bdf` resolved by the bus
  scan when it initializes.

**Configuration space**
  :c:func:`pcie_conf_read` and :c:func:`pcie_conf_write` access the 32-bit configuration words of
  a function by word index. The ``PCIE_CONF_*`` macros name the standard words and their fields,
  and :c:func:`pcie_get_cap` and :c:func:`pcie_get_ext_cap` walk the capability lists using the
  identifiers of :zephyr_file:`include/zephyr/drivers/pcie/cap.h`.

**Base Address Registers**
  :c:struct:`pcie_bar` describes one memory or I/O window of a function as a physical address and
  a size, retrieved with :c:func:`pcie_get_mbar`, :c:func:`pcie_probe_mbar`,
  :c:func:`pcie_get_iobar` or :c:func:`pcie_probe_iobar`. :c:func:`pcie_set_cmd` sets or clears
  bits of the command register, such as those enabling memory and I/O decoding.

**Interrupts**
  A function raises either a wired interrupt, whose line is read with :c:func:`pcie_get_irq` or
  allocated with :c:func:`pcie_alloc_irq`, or message signaled interrupts managed with the API of
  :zephyr_file:`include/zephyr/drivers/pcie/msi.h`. :c:func:`pcie_connect_dynamic_irq` and
  :c:func:`pcie_irq_enable` hide the difference from the driver.

**Host controller and endpoint**
  With :kconfig:option:`CONFIG_PCIE_CONTROLLER`, the device selected by the
  ``zephyr,pcie-controller`` chosen node implements :c:struct:`pcie_ctrl_driver_api` and provides
  configuration space access, BAR allocation and MSI setup for the whole hierarchy. On the device
  side of the link, an endpoint controller driver implements :c:struct:`pcie_ep_driver_api`.

Bus enumeration and driver binding
**********************************

On x86, :c:func:`pcie_conf_read` and :c:func:`pcie_conf_write` are provided by the architecture
code: they use the traditional ``0xCF8``/``0xCFC`` I/O ports, serialized with a spinlock, or the
memory mapped configuration space described by the ACPI ``MCFG`` table when
:kconfig:option:`CONFIG_PCIE_MMIO_CFG` is enabled. The firmware has already enumerated the bus, so
BARs and wired interrupt lines are read back as assigned.

On other platforms, :kconfig:option:`CONFIG_PCIE_CONTROLLER` (enabled by default when the
``zephyr,pcie-controller`` chosen node exists) routes the same calls to the chosen controller
device through :c:func:`pcie_ctrl_conf_read` and :c:func:`pcie_ctrl_conf_write`. The controller
driver initializes at ``PRE_KERNEL_1`` with priority :kconfig:option:`CONFIG_PCIE_INIT_PRIORITY`
and sets up the hierarchy. Two controller drivers exist. :kconfig:option:`CONFIG_PCIE_ECAM`, for
generic ECAM controllers (:dtcompatible:`pci-host-ecam-generic`), enumerates the bus with
:c:func:`pcie_generic_ctrl_enumerate`, which sizes every BAR, assigns it a bus address from the
windows of the ``ranges`` property with :c:func:`pcie_ctrl_region_allocate`, and programs the bus
numbers and forwarding windows of PCI-to-PCI bridges with
:c:func:`pcie_ctrl_region_get_allocate_base`. :kconfig:option:`CONFIG_PCIE_BRCMSTB`, for Broadcom
set-top box SoCs (:dtcompatible:`brcm,brcmstb-pcie`), instead programs the BARs of its endpoint
from additional ``reg`` entries of the controller node. In both cases
:c:func:`pcie_ctrl_region_translate` converts bus addresses to CPU addresses and is applied by
:c:func:`pcie_get_mbar`, so that drivers receive addresses they can map directly.

The PCIe layer then registers a :c:macro:`SYS_INIT` hook at ``PRE_KERNEL_1``, or at
``PRE_KERNEL_2`` when a controller driver must enumerate the bus first, with priority
:kconfig:option:`CONFIG_PCIE_INIT_PRIORITY`. When at least one :c:struct:`pcie_dev` context has
been placed in an :ref:`iterable section <iterable_sections_api>` by
:c:macro:`DEVICE_PCIE_DECLARE`, the hook scans bus 0 and the buses listed in the ``scan-buses``
property of the controller, following bridges recursively (a multifunction host controller at
``0:0.0`` counts as one root bus per function, see :c:macro:`PCIE_HOST_CONTROLLER`). Every
function found is compared with the contexts: the first unbound context whose ID matches, and whose
``class-rev`` value matches the class and revision word of the function under
``class-rev-mask``, receives the BDF. Functions that no context describes are ignored. Drivers and
tools can run their own scan with :c:func:`pcie_scan`, whose :c:struct:`pcie_scan_opt` selects the
starting bus, the :c:type:`pcie_scan_cb_t` callback invoked per function, and flags such as
:c:enumerator:`PCIE_SCAN_RECURSIVE` and :c:enumerator:`PCIE_SCAN_CB_ALL`.

Devicetree Configuration
************************

A host controller node uses a binding that declares ``bus: pcie`` (see :ref:`dt-bindings-bus`).
On x86 the generic :dtcompatible:`pcie-controller` node is sufficient because the firmware has
enumerated the bus; its ``acpi-hid`` property identifies the root complex whose ACPI interrupt
routing table :kconfig:option:`CONFIG_PCIE_PRT` loads, and the optional ``scan-buses`` property
lists additional root buses. PCIe functions are child nodes of the controller. Their bindings
include ``pcie-device.yaml``, which defines ``vendor-id``, ``device-id`` and the optional
``class-rev`` and ``class-rev-mask`` filters; no ``reg`` property is needed since the BDF is
resolved at run time. The ``interrupts`` property carries the wired line assigned by the firmware,
or :c:macro:`PCIE_IRQ_DETECT` from :zephyr_file:`include/zephyr/dt-bindings/pcie/pcie.h` when the
driver must determine the line at run time, as in
``interrupts = <PCIE_IRQ_DETECT IRQ_TYPE_LOWEST_LEVEL_LOW 3>;``.

.. code-block:: devicetree
   :caption: PCIe function on an x86 host (from :zephyr_file:`boards/qemu/x86/qemu_x86.dts`)

   pcie0: pcie0 {
       #address-cells = <1>;
       #size-cells = <1>;
       compatible = "pcie-controller";
       acpi-hid = "PNP0A08";
       ranges;

       eth0: eth0 {
           compatible = "intel,e1000";
           vendor-id = <0x8086>;
           device-id = <0x100e>;
           interrupts = <11 IRQ_TYPE_LOWEST_EDGE_RISING 3>;
           interrupt-parent = <&intc>;
           status = "okay";
       };
   };

When Zephyr enumerates the bus itself, the controller node describes the hardware and is selected
with the ``zephyr,pcie-controller`` :ref:`chosen node <devicetree-zephyr-chosen-nodes>`. For an
ECAM controller, ``reg`` is the memory mapped configuration space, ``ranges`` lists the bus
address windows available for BARs with their CPU addresses (see
:ref:`devicetree-ranges-property`), and the optional ``msi-parent`` phandle designates the MSI
controller (for example a GIC ITS) that provides the vectors:

.. code-block:: devicetree
   :caption: ECAM host controller (from :zephyr_file:`dts/riscv/qemu/virt-riscv.dtsi`)

   chosen {
       zephyr,pcie-controller = &pcie;
   };

   pcie: pcie@30000000 {
       compatible = "pci-host-ecam-generic";
       device_type = "pci";
       reg = <0x30000000 0x10000000>;
       #size-cells = <0x02>;
       #address-cells = <0x03>;
       ranges = <0x1000000 0x00 0x00 0x3000000 0x00 0x10000
                 0x2000000 0x00 0x40000000 0x40000000 0x00 0x40000000>;
       #interrupt-cells = <0x01>;
       bus-range = <0x00 0xff>;
   };

Typical driver flow
*******************

A driver for a PCIe function typically:

#. Declares a PCIe context per devicetree instance with :c:macro:`DEVICE_PCIE_INST_DECLARE` and
   stores a pointer to it in its configuration structure with :c:macro:`DEVICE_PCIE_INST_INIT`.
#. Registers its device at an initialization level and priority later than the PCIe scan, and
   checks in its initialization function that :c:member:`pcie_dev.bdf` differs from
   :c:macro:`PCIE_BDF_NONE`.
#. Retrieves its register window with :c:func:`pcie_probe_mbar` or :c:func:`pcie_get_mbar`,
   enables memory decoding (and bus mastering when the function performs DMA) with
   :c:func:`pcie_set_cmd`, and maps the window with :c:func:`device_map`.
#. Connects its interrupt, statically from the ``interrupts`` property or dynamically with
   :c:func:`pcie_alloc_irq` and :c:func:`pcie_connect_dynamic_irq`, then calls
   :c:func:`pcie_irq_enable`.
#. Optionally enables several MSI-X vectors, Precision Time Measurement or Virtual Channels.

Basic Operation
***************

The following skeleton, modeled on the in-tree Ethernet and UART drivers, binds a driver instance
to its PCIe function and maps its first memory BAR:

.. code-block:: c
   :caption: Locating a PCIe function and mapping its registers

   #define DT_DRV_COMPAT vendor_device

   #include <zephyr/device.h>
   #include <zephyr/drivers/pcie/pcie.h>
   #include <zephyr/sys/device_mmio.h>

   struct foo_config {
       struct pcie_dev *pcie;
   };

   struct foo_data {
       mm_reg_t regs;
   };

   static int foo_init(const struct device *dev)
   {
       const struct foo_config *cfg = dev->config;
       struct foo_data *data = dev->data;
       struct pcie_bar mbar;

       /* PCIE_BDF_NONE: the bus scan found no function with the configured IDs */
       if (cfg->pcie->bdf == PCIE_BDF_NONE) {
           return -ENODEV;
       }

       /* Lowest numbered memory BAR, already translated to a CPU physical address */
       if (!pcie_probe_mbar(cfg->pcie->bdf, 0, &mbar)) {
           return -ENODEV;
       }

       pcie_set_cmd(cfg->pcie->bdf, PCIE_CONF_CMDSTAT_MEM | PCIE_CONF_CMDSTAT_MASTER, true);
       device_map(&data->regs, mbar.phys_addr, mbar.size, K_MEM_CACHE_NONE);

       return 0;
   }

   #define FOO_PCIE_INIT(inst)                                                  \
       DEVICE_PCIE_INST_DECLARE(inst);                                          \
       static const struct foo_config foo_config_##inst = {                     \
           DEVICE_PCIE_INST_INIT(inst, pcie),                                   \
       };                                                                       \
       static struct foo_data foo_data_##inst;                                  \
       DEVICE_DT_INST_DEFINE(inst, foo_init, NULL, &foo_data_##inst,            \
                             &foo_config_##inst, POST_KERNEL,                   \
                             CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

   DT_INST_FOREACH_STATUS_OKAY(FOO_PCIE_INIT)

Interrupt handling
==================

A fixed line from the ``interrupts`` property is connected with :c:macro:`IRQ_CONNECT`, or with
:c:macro:`PCIE_IRQ_CONNECT`, which also receives the BDF so that the architecture can reserve the
line and route it through MSI where supported, and enabled with :c:func:`pcie_irq_enable`. A line
declared as :c:macro:`PCIE_IRQ_DETECT` is discovered at run time, which generally requires
:kconfig:option:`CONFIG_DYNAMIC_INTERRUPTS`:
:c:func:`pcie_alloc_irq`, available only without :kconfig:option:`CONFIG_PCIE_CONTROLLER`, returns
the line stored in the interrupt register of the function when it is valid and unused, and
otherwise obtains one from the ACPI routing table or allocates a free line and writes it back to
the function.

.. code-block:: c
   :caption: Run time interrupt setup of a PCIe function

   static void foo_isr(const void *arg);

   static void foo_irq_config(const struct device *dev)
   {
       const struct foo_config *cfg = dev->config;
       unsigned int irq = pcie_alloc_irq(cfg->pcie->bdf);

       if (irq == PCIE_CONF_INTR_IRQ_NONE) {
           return;
       }

       if (pcie_connect_dynamic_irq(cfg->pcie->bdf, irq, DT_INST_IRQ(0, priority),
                                    foo_isr, dev, 0)) {
           pcie_irq_enable(cfg->pcie->bdf, irq);
       }
   }

:c:func:`pcie_irq_enable` configures the function for MSI when :kconfig:option:`CONFIG_PCIE_MSI`
is enabled and the function supports it, and enables the wired line otherwise. Likewise, with
:kconfig:option:`CONFIG_PCIE_MSI_MULTI_VECTOR`, :c:func:`pcie_connect_dynamic_irq` allocates and
connects a single MSI vector instead of the line when the function supports MSI.

MSI and MSI-X
=============

:kconfig:option:`CONFIG_PCIE_MSI` selects message signaled interrupts, which PCIe functions
typically require to raise interrupts at all. :c:func:`pcie_is_msi` reports whether a function
exposes the MSI or MSI-X capability. With :kconfig:option:`CONFIG_PCIE_MSI_X`, a function offering
both is configured for MSI-X, whose vector table lives in one of its BARs and is mapped by the
PCIe layer when vectors are allocated. :c:func:`pcie_msi_enable` programs the capability and also
sets the bus master bit, because messages are memory writes issued by the function. With
:kconfig:option:`CONFIG_PCIE_MSI_MULTI_VECTOR`, a driver can request several vectors, each with
its own handler, using :c:type:`msi_vector_t` entries:

.. code-block:: c
   :caption: Multi-vector MSI-X setup

   static msi_vector_t vectors[FOO_MSIX_VECTORS];
   uint8_t n_vectors;

   n_vectors = pcie_msi_vectors_allocate(bdf, FOO_IRQ_PRIORITY, vectors, FOO_MSIX_VECTORS);
   if (n_vectors == 0) {
       return -EIO;
   }

   for (uint8_t i = 0; i < n_vectors; i++) {
       if (!pcie_msi_vector_connect(bdf, &vectors[i], foo_isr, &params[i], 0)) {
           return -EIO;
       }
   }

   if (!pcie_msi_enable(bdf, vectors, n_vectors, 0)) {
       return -EIO;
   }

The number of vectors granted can be lower than requested: it is bounded by the multiple message
capability of the function or the size of its MSI-X table, and by what the platform provides. On
x86 the architecture code allocates vectors only when :kconfig:option:`CONFIG_PCIE_MSI_X` or
:kconfig:option:`CONFIG_INTEL_VTD_ICTL` is enabled, the latter adding interrupt remapping;
otherwise the allocation returns 0. With a host controller the vectors come from its
:c:func:`pcie_ctrl_msi_device_setup` implementation, backed by the ``msi-parent`` device.

Capabilities, PTM and Virtual Channels
**************************************

:c:func:`pcie_get_cap` returns the configuration word index of a standard capability and
:c:func:`pcie_get_ext_cap` that of a PCI Express extended capability, or 0 when the function does
not expose it. A driver then reads the registers of the capability relative to that index, for
example ``pcie_conf_read(bdf, base + PCIE_MSI_MCR)``, using identifiers such as
:c:macro:`PCI_CAP_ID_MSI` or :c:macro:`PCIE_EXT_CAP_ID_PTM`. Two extended capabilities have
dedicated helpers:

* **Precision Time Measurement** (:kconfig:option:`CONFIG_PCIE_PTM`): a ``ptm-root`` node placed
  under the controller, with the vendor and device IDs of the function acting as PTM root, turns
  on the root role at boot, and a driver whose function is a PTM requester calls
  :c:func:`pcie_ptm_enable` with its BDF.
* **Virtual Channels**: :c:func:`pcie_vc_enable` and :c:func:`pcie_vc_disable` switch the
  extended VCs of a function on and off (VC0 is always active), and :c:func:`pcie_vc_map_tc`
  applies a :c:struct:`pcie_vctc_map` assigning each Traffic Class to a VC through the
  ``PCIE_VC_SET_TC*`` bits. VCs must be disabled before the map is applied and enabled afterward,
  Traffic Class 0 must stay on VC0, and ``-ENOTSUP`` on a function without extended VCs is
  expected and non-fatal.

Endpoint mode
*************

With :kconfig:option:`CONFIG_PCIE_ENDPOINT`, the endpoint controller is an ordinary device
implementing :c:struct:`pcie_ep_driver_api`. :c:func:`pcie_ep_conf_read` and
:c:func:`pcie_ep_conf_write` access the endpoint's own configuration space by offset.
:c:func:`pcie_ep_map_addr` maps a host memory buffer into a PCIe outbound window and returns the
size actually mapped, which can be smaller than requested because of alignment constraints; the
:c:enum:`pcie_ob_mem_type` hint selects a window below or above the 32-bit boundary for bus
masters limited to 32-bit addresses, and :c:func:`pcie_ep_unmap_addr` releases the window.
:c:func:`pcie_ep_raise_irq` signals the host with a legacy, MSI or MSI-X interrupt
(:c:enum:`pci_ep_irq_type`), and :c:func:`pcie_ep_register_reset_cb` registers a
:c:type:`pcie_ep_reset_callback_t` for a :c:enum:`pcie_reset` event such as
:c:enumerator:`PCIE_PERST` or :c:enumerator:`PCIE_FLR`. :c:func:`pcie_ep_dma_xfer` moves data
between a mapped host buffer and local memory with the system DMA engine; it and reset callbacks
are optional in the driver API and return ``-ENOTSUP`` when not implemented. The helpers
:c:func:`pcie_ep_xfer_data_memcpy` and :c:func:`pcie_ep_xfer_data_dma` combine mapping, copying
and unmapping for a whole buffer, mapping the remainder a second time when the first mapping is
partial, and issue a dummy read after a :c:enumerator:`DEVICE_TO_HOST` transfer to flush the
posted writes. The in-tree endpoint driver supports the Broadcom iProc PCIe endpoint controller
(:dtcompatible:`brcm,iproc-pcie-ep`, :kconfig:option:`CONFIG_PCIE_EP_IPROC`).

Usage constraints
*****************

* A driver that dereferences its :c:struct:`pcie_dev` context must initialize after the PCIe scan
  and treat :c:macro:`PCIE_BDF_NONE` as "function absent".
* BAR retrieval sizes the window by writing all ones to the register, with memory and I/O decoding
  temporarily disabled for the function: call :c:func:`pcie_get_mbar` and the related functions
  from the driver initialization, before the function is in use.
* :c:func:`pcie_conf_read` returns ``0xFFFFFFFF`` for a nonexistent function or word, the BAR
  helpers return ``false``, :c:func:`pcie_alloc_irq` returns :c:macro:`PCIE_CONF_INTR_IRQ_NONE`
  on failure, and :c:func:`pcie_scan` returns ``-EINVAL`` when no callback is given.
* :c:func:`pcie_vc_disable` waits for pending VC negotiations with :c:func:`k_msleep` and must be
  called from a thread. Endpoint reset callbacks execute in interrupt context and may only use
  interrupt-safe APIs.

Shell commands
**************

When :kconfig:option:`CONFIG_PCIE_SHELL` is enabled (it requires :kconfig:option:`CONFIG_SHELL`),
the ``pcie`` command inspects the functions visible from the host through the same configuration
space accessors as the drivers. Running ``pcie`` alone is equivalent to ``pcie ls``.

``pcie ls [bus:device.function] [dump]``
  Without a function identifier, scan bus 0, following bridges recursively, and print one line
  per function: its BDF, vendor and device IDs, class, subclass, programming interface and
  revision, followed by ``[bridge]`` for bridges, or for endpoints by their BARs with type
  (memory or I/O, 64-bit or not) and address, their MSI and MSI-X capabilities when
  :kconfig:option:`CONFIG_PCIE_MSI` is enabled, and their wired interrupt line if one is assigned.
  With a function identifier, written in hexadecimal in the ``bus:device.function`` form printed
  by the listing (for example ``0:3.0``; the ``bus:device:function`` form shown by the built-in
  help is not accepted), show only that function together with the names of its standard and
  extended capabilities and its Virtual Channel resources. ``dump`` additionally prints the first
  64 bytes of the configuration space of each function shown.

Configuration Options
*********************

Related configuration options:

* :kconfig:option:`CONFIG_PCIE`
* :kconfig:option:`CONFIG_PCIE_INIT_PRIORITY`
* :kconfig:option:`CONFIG_PCIE_CONTROLLER`
* :kconfig:option:`CONFIG_PCIE_ECAM`
* :kconfig:option:`CONFIG_PCIE_BRCMSTB`
* :kconfig:option:`CONFIG_PCIE_MSI`
* :kconfig:option:`CONFIG_PCIE_MSI_MULTI_VECTOR`
* :kconfig:option:`CONFIG_PCIE_MSI_X`
* :kconfig:option:`CONFIG_PCIE_PTM`
* :kconfig:option:`CONFIG_PCIE_PRT`
* :kconfig:option:`CONFIG_PCIE_MMIO_CFG`
* :kconfig:option:`CONFIG_PCIE_SHELL`
* :kconfig:option:`CONFIG_PCIE_ENDPOINT`
* :kconfig:option:`CONFIG_PCIE_EP_IPROC`

API Reference
*************

Host interface
==============

.. doxygengroup:: pcie_host_interface

Host controller interface
=========================

.. doxygengroup:: pcie_controller_interface

MSI and MSI-X interface
=======================

.. doxygengroup:: pcie_host_msi_interface

Capabilities
============

.. doxygengroup:: pcie_capabilities

Precision Time Measurement
==========================

.. doxygengroup:: pcie_host_ptm_interface

Virtual Channels
================

.. doxygengroup:: pcie_vc_host_interface
