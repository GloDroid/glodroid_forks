/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ARM64_KVM_NVHE_IOMMU_H__
#define __ARM64_KVM_NVHE_IOMMU_H__

#include <linux/types.h>
#include <asm/kvm_host.h>

#include <nvhe/mem_protect.h>

struct pkvm_iommu;

struct pkvm_iommu_ops {
	/*
	 * Global driver initialization called before devices are registered.
	 * Driver-specific arguments are passed in a buffer shared by the host.
	 * The buffer memory has been pinned in EL2 but host retains R/W access.
	 * Extra care must be taken when reading from it to avoid TOCTOU bugs.
	 * Driver initialization lock held during callback.
	 */
	int (*init)(void *data, size_t size);

	/*
	 * Driver-specific validation of device registration inputs.
	 * This should be stateless. No locks are held at entry.
	 */
	int (*validate)(phys_addr_t base, size_t size);

	/*
	 * Callback to apply a host stage-2 mapping change at driver level.
	 * Called before 'host_stage2_idmap_apply' with host lock held.
	 */
	void (*host_stage2_idmap_prepare)(phys_addr_t start, phys_addr_t end,
					  enum kvm_pgtable_prot prot);

	/*
	 * Callback to apply a host stage-2 mapping change at device level.
	 * Called after 'host_stage2_idmap_prepare' with host lock held.
	 */
	void (*host_stage2_idmap_apply)(struct pkvm_iommu *dev,
					phys_addr_t start, phys_addr_t end);

	/* Power management callbacks. Called with host lock held. */
	int (*suspend)(struct pkvm_iommu *dev);
	int (*resume)(struct pkvm_iommu *dev);

	/*
	 * Host data abort handler callback. Called with host lock held.
	 * Returns true if the data abort has been handled.
	 */
	bool (*host_dabt_handler)(struct pkvm_iommu *dev,
				  struct kvm_cpu_context *host_ctxt,
				  u32 esr, size_t off);

	/* Amount of memory allocated per-device for use by the driver. */
	size_t data_size;
};

struct pkvm_iommu {
	struct list_head list;
	unsigned long id;
	const struct pkvm_iommu_ops *ops;
	phys_addr_t pa;
	void *va;
	size_t size;
	bool powered;
	char data[];
};

int __pkvm_iommu_driver_init(enum pkvm_iommu_driver_id id, void *data, size_t size);
int __pkvm_iommu_register(unsigned long dev_id,
			  enum pkvm_iommu_driver_id drv_id,
			  phys_addr_t dev_pa, size_t dev_size,
			  void *kern_mem_va, size_t mem_size);
int __pkvm_iommu_pm_notify(unsigned long dev_id,
			   enum pkvm_iommu_pm_event event);
int pkvm_iommu_host_stage2_adjust_range(phys_addr_t addr, phys_addr_t *start,
					phys_addr_t *end);
bool pkvm_iommu_host_dabt_handler(struct kvm_cpu_context *host_ctxt, u32 esr,
				  phys_addr_t fault_pa);
void pkvm_iommu_host_stage2_idmap(phys_addr_t start, phys_addr_t end,
				  enum kvm_pgtable_prot prot);

extern const struct pkvm_iommu_ops pkvm_s2mpu_ops;

#endif	/* __ARM64_KVM_NVHE_IOMMU_H__ */
