/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2023 EPAM Systems
 *
 */

/**
 * @file
 * @brief Xen Dom0 domain control operations.
 * @ingroup xen_dom0_domain_control
 */

#ifndef __XEN_DOM0_DOMCTL_H__
#define __XEN_DOM0_DOMCTL_H__

#include <zephyr/xen/generic.h>
#include <zephyr/xen/public/domctl.h>
#include <zephyr/xen/public/xen.h>

#include <zephyr/kernel.h>

/**
 * @defgroup xen_dom0_domain_control Xen Dom0 domain control
 * @ingroup xen_dom0
 * @brief Control domain lifecycle, resources, and vCPUs from the privileged domain.
 * @{
 */

/**
 * @brief Perform a Xen scheduler operation for a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param[in,out] sched_op Scheduler request structure passed to Xen.
 *
 * @retval 0 If the operation completed successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_scheduler_op(int domid, struct xen_domctl_scheduler_op *sched_op);

/**
 * @brief Pause a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 *
 * @retval 0 If the domain was paused successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_pausedomain(int domid);

/**
 * @brief Unpause a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 *
 * @retval 0 If the domain was unpaused successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_unpausedomain(int domid);

/**
 * @brief Resume a domain that is in the shutdown state.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 *
 * @retval 0 If the domain was resumed successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_resumedomain(int domid);

/**
 * @brief Get the guest context of a domain vCPU.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param vcpu vCPU index to query.
 * @param[out] ctxt Buffer that receives the guest context.
 *
 * @retval 0 If the vCPU context was read successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_getvcpucontext(int domid, int vcpu, vcpu_guest_context_t *ctxt);

/**
 * @brief Set the guest context of a domain vCPU.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param vcpu vCPU index to update.
 * @param ctxt Guest context to write.
 *
 * @retval 0 If the vCPU context was updated successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_setvcpucontext(int domid, int vcpu, vcpu_guest_context_t *ctxt);

/**
 * @brief Get summary information for a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param[out] dom_info Buffer that receives the Xen domain information.
 *
 * @retval 0 If the domain information was read successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_getdomaininfo(int domid, xen_domctl_getdomaininfo_t *dom_info);

/**
 * @brief Get the paging mempool size for a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param[out] size Buffer that receives the paging mempool size in bytes.
 *
 * @retval 0 If the size was read successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_get_paging_mempool_size(int domid, uint64_t *size);

/**
 * @brief Set the paging mempool size for a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param size New paging mempool size in bytes.
 *
 * @retval 0 If the size was updated successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_set_paging_mempool_size(int domid, uint64_t size);

/**
 * @brief Set the maximum memory assigned to a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param max_memkb Maximum memory in KiB.
 *
 * @retval 0 If the limit was updated successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_max_mem(int domid, uint64_t max_memkb);

/**
 * @brief Set the address size used by a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param addr_size Xen address-size selector.
 *
 * @retval 0 If the address size was updated successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_set_address_size(int domid, int addr_size);

/**
 * @brief Allow or deny I/O-memory access for a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param first_mfn First machine frame number in the range.
 * @param nr_mfns Number of machine frames in the range.
 * @param allow_access Set to non-zero to allow access, or zero to revoke it.
 *
 * @retval 0 If the permission change succeeded.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_iomem_permission(int domid, uint64_t first_mfn,
uint64_t nr_mfns, uint8_t allow_access);

/**
 * @brief Map or unmap a machine-memory range into a guest frame range.
 *
 * The implementation automatically retries the operation with smaller chunks if
 * Xen reports that the request is too large.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param first_gfn First guest frame number in the target range.
 * @param first_mfn First machine frame number in the source range.
 * @param nr_mfns Number of frames to process.
 * @param add_mapping Set to non-zero to add the mapping, or zero to remove it.
 *
 * @retval 0 If the requested range was processed successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_memory_mapping(int domid, uint64_t first_gfn, uint64_t first_mfn,
      uint64_t nr_mfns, uint32_t add_mapping);

/**
 * @brief Assign a devicetree-described device to a guest domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param dtdev_path NUL-terminated devicetree path to the device.
 *
 * @retval 0 If the device was assigned successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_assign_dt_device(int domid, char *dtdev_path);

/**
 * @brief Remove a devicetree-described device assignment from a guest domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param dtdev_path NUL-terminated devicetree path to the device.
 *
 * @retval 0 If the device was deassigned successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_deassign_dt_device(int domid, char *dtdev_path);

/**
 * @brief Bind a physical interrupt to a guest domain.
 *
 * Only ``PT_IRQ_TYPE_SPI`` is currently supported.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param machine_irq Machine IRQ number to bind.
 * @param irq_type Xen passthrough IRQ type.
 * @param bus PCI bus number for PCI passthrough modes.
 * @param device PCI device number for PCI passthrough modes.
 * @param intx PCI INTx line for PCI passthrough modes.
 * @param isa_irq ISA IRQ number for ISA passthrough modes.
 * @param spi SPI number used with ``PT_IRQ_TYPE_SPI``.
 *
 * @retval 0 If the binding succeeded.
 * @retval -ENOTSUP If @p irq_type is not supported by the current implementation.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_bind_pt_irq(int domid, uint32_t machine_irq, uint8_t irq_type, uint8_t bus,
   uint8_t device, uint8_t intx, uint8_t isa_irq, uint16_t spi);

/**
 * @brief Set the maximum number of vCPUs available to a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param max_vcpus Maximum number of Xen vCPUs to allocate.
 *
 * @retval 0 If the limit was updated successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_max_vcpus(int domid, int max_vcpus);

/**
 * @brief Create a new domain.
 *
 * Xen may update the value pointed to by @p domid when it allocates the domain
 * identifier on behalf of the caller.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param[in,out] domid Domain identifier request and response buffer.
 * @param config Domain configuration passed to Xen.
 *
 * @retval 0 If the domain was created successfully.
 * @retval -EINVAL If @p domid or @p config is ``NULL``.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_createdomain(int *domid, struct xen_domctl_createdomain *config);

/**
 * @brief Destroy a domain.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 *
 * @retval 0 If the domain was destroyed successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_destroydomain(int domid);

/**
 * @brief Clean and invalidate caches for a guest memory range.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param cacheflush Cache-flush request parameters.
 *
 * @retval 0 If the cache flush completed successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_cacheflush(int domid, struct xen_domctl_cacheflush *cacheflush);

/**
 * @brief Query runtime information for one domain vCPU.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param domid Target domain identifier.
 * @param vcpu vCPU index to query.
 * @param[out] info Buffer that receives the vCPU information.
 *
 * @retval 0 If the vCPU information was read successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_domctl_getvcpu(int domid, uint32_t vcpu, struct xen_domctl_getvcpuinfo *info);

/** @} */

#endif /* __XEN_DOM0_DOMCTL_H__ */
