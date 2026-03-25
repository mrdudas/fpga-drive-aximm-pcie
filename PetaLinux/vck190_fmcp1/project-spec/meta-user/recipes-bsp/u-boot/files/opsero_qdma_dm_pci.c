/*
 * Opsero Electronic Design Inc. 2024
 *
 * QDMA PCIe Root Complex Driver for U-Boot Device Model
 *
 * This driver registers QDMA PCIe controllers as U-Boot DM PCI host controllers,
 * enabling automatic enumeration via 'pci enum' and device discovery.
 */

#include <common.h>
#include <dm.h>
#include <pci.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <linux/delay.h>
#include "opsero_qdma.h"

#define QDMA_BRIDGE_BASE_OFF           0xCD8
#define XILINX_PCIE_DMA_REG_PSCR       0x144
#define XILINX_PCIE_DMA_REG_PSCR_LNKUP 0x800
#define QDMA_LINK_LOG_THROTTLE_MS      2000

struct qdma_pcie_host {
	void *ecam_base;
	phys_addr_t csr_phys;
	phys_addr_t ecam_phys;
	phys_addr_t np_mem_base;
	phys_addr_t np_mem_max;
	phys_addr_t p_mem_base;
	phys_addr_t p_mem_max;
	int initialized;
	int link_state_valid;
	int last_link_up;
	ulong last_link_down_log_ms;
};

static int qdma_dm_pci_config_address(const struct udevice *dev, pci_dev_t bdf,
				      uint offset, void **paddress);

static int qdma_dm_pci_fill_no_device(enum pci_size_t size, ulong *valuep)
{
	switch (size) {
	case PCI_SIZE_8:
		*valuep = 0xff;
		break;
	case PCI_SIZE_16:
		*valuep = 0xffff;
		break;
	case PCI_SIZE_32:
		*valuep = 0xffffffff;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int qdma_dm_pci_is_link_up(const struct udevice *dev,
				  struct qdma_pcie_host *host)
{
	u32 pscr;
	int link_up;
	ulong now_ms;

	if (!host)
		return 0;

	pscr = readl((void *)(ulong)(host->csr_phys +
		QDMA_BRIDGE_BASE_OFF + XILINX_PCIE_DMA_REG_PSCR));
	link_up = (pscr & XILINX_PCIE_DMA_REG_PSCR_LNKUP) ? 1 : 0;
	now_ms = get_timer(0);

	if (!host->link_state_valid || host->last_link_up != link_up) {
		host->link_state_valid = 1;
		host->last_link_up = link_up;
		host->last_link_down_log_ms = now_ms;
		printf("QDMA DM PCI: link %s dev=%s csr=0x%llx PSCR=0x%08x\n",
		       link_up ? "UP" : "DOWN",
		       dev ? dev->name : "?",
		       host->csr_phys,
		       pscr);
	} else if (!link_up &&
		   get_timer(host->last_link_down_log_ms) >= QDMA_LINK_LOG_THROTTLE_MS) {
		host->last_link_down_log_ms = now_ms;
		printf("QDMA DM PCI: link still DOWN dev=%s csr=0x%llx PSCR=0x%08x (cfg accesses suppressed)\n",
		       dev ? dev->name : "?",
		       host->csr_phys,
		       pscr);
	}

	return link_up;
}

static int qdma_dm_pci_read32(const struct udevice *dev, pci_dev_t bdf,
				      uint offset, u32 *val)
{
	void *addr;
	int ret;

	ret = qdma_dm_pci_config_address(dev, bdf, offset & ~0x3, &addr);
	if (ret)
		return ret;

	*val = readl(addr);
	return 0;
}

static int qdma_dm_pci_write32(const struct udevice *dev, pci_dev_t bdf,
				       uint offset, u32 val)
{
	void *addr;
	int ret;

	ret = qdma_dm_pci_config_address(dev, bdf, offset & ~0x3, &addr);
	if (ret)
		return ret;

	writel(val, addr);
	return 0;
}

static void qdma_dm_pci_dump_bar_route_state(const struct udevice *dev,
					     pci_dev_t bdf,
					     const char *tag)
{
	u32 ep_bar0 = 0, ep_bar1 = 0, ep_cmd = 0;
	u32 rp_np = 0, rp_p = 0;
	pci_dev_t rp_bdf;

	if (qdma_dm_pci_read32(dev, bdf, PCI_BASE_ADDRESS_0, &ep_bar0) ||
	    qdma_dm_pci_read32(dev, bdf, PCI_BASE_ADDRESS_1, &ep_bar1) ||
	    qdma_dm_pci_read32(dev, bdf, PCI_COMMAND, &ep_cmd))
		return;

	rp_bdf = PCI_BDF(PCI_BUS(bdf) & ~0x1, 0, 0);
	if (qdma_dm_pci_read32(dev, rp_bdf, PCI_MEMORY_BASE, &rp_np) ||
	    qdma_dm_pci_read32(dev, rp_bdf, PCI_PREF_MEMORY_BASE, &rp_p))
		return;

	printf("QDMA DM PCI TRACE[%s]: ep=%02x:%02x.%x BAR0=%08x BAR1=%08x CMD=%08x | rp=%02x:00.0 NP=%08x P=%08x\n",
	       tag,
	       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf),
	       ep_bar0, ep_bar1, ep_cmd,
	       PCI_BUS(rp_bdf), rp_np, rp_p);
}

static int qdma_dm_pci_config_address(const struct udevice *dev, pci_dev_t bdf,
				      uint offset, void **paddress)
{
	struct qdma_pcie_host *host = dev_get_priv(dev);
	struct pci_controller *hose = dev_get_uclass_priv(dev);
	unsigned int bus = PCI_BUS(bdf);
	unsigned int slot = PCI_DEV(bdf);
	unsigned int func = PCI_FUNC(bdf);
	unsigned int local_bus;
	void *addr;
	static int unsupported_bdf_warned;

	if (!host || !hose || !host->initialized)
		return -ENODEV;

	if (bus < hose->first_busno)
		return -ENODEV;

	local_bus = bus - hose->first_busno;

	/*
	 * If the downstream PCIe link drops (endpoint unplug/power-off/cable issue),
	 * suppress non-root config accesses to avoid system lockups.
	 */
	if (local_bus > 0 && !qdma_dm_pci_is_link_up(dev, host))
		return -ENOLINK;

	/*
	 * On local root-bus (0), only the RC itself at 00:00.0 is valid.
	 * Downstream buses may contain multiple slots/functions (switches/endpoints),
	 * so allow generic probing there.
	 */
	if (local_bus == 0 && (slot != 0 || func != 0)) {
		if (!unsupported_bdf_warned) {
			printf("QDMA DM PCI DBG: filtering unsupported BDF %02x:%02x.%x (local bus %u), returning ENODEV\n",
			       bus, slot, func, local_bus);
			unsupported_bdf_warned = 1;
		}
		return -ENODEV;
	}


	addr = host->ecam_base;
	addr += PCIE_ECAM_OFFSET(local_bus, slot, func, offset);
	*paddress = addr;

	if ((offset == PCI_COMMAND) ||
	    (offset == PCI_PRIMARY_BUS) || (offset == PCI_BASE_ADDRESS_0) ||
	    (offset == PCI_BASE_ADDRESS_1)) {
		printf("QDMA DM PCI DBG: cfg_addr dev=%s bdf=%02x:%02x.%x off=0x%x local_bus=%u addr=%p\n",
		       dev->name, bus, slot, func, offset, local_bus, addr);
	}

	return 0;
}

/**
 * qdma_dm_pci_read_config - Read PCI config via ECAM
 */
static int qdma_dm_pci_read_config(const struct udevice *dev, pci_dev_t bdf,
				   uint offset, ulong *valuep,
				   enum pci_size_t size)
{
	struct pci_controller *hose = dev_get_uclass_priv(dev);
	struct qdma_pcie_host *host = dev_get_priv(dev);
	int is_root_port;
	int link_up = 1;
	u32 dword_off = offset & ~0x3;
	u32 data32;
	u32 shift;
	int ret;

	is_root_port = hose && PCI_BUS(bdf) == hose->first_busno &&
		PCI_DEV(bdf) == 0 && PCI_FUNC(bdf) == 0;

	if (host)
		link_up = qdma_dm_pci_is_link_up(dev, host);

	if (!is_root_port && !link_up)
		return qdma_dm_pci_fill_no_device(size, valuep);

	if (is_root_port) {
		switch (dword_off) {
		case PCI_VENDOR_ID:
			data32 = 0xB0B410EE;
			break;
		case PCI_CLASS_REVISION:
			data32 = 0x06040000;
			break;
		case (PCI_HEADER_TYPE & ~0x3):
			data32 = 0x00010000;
			break;
		case PCI_COMMAND:
			data32 = 0x00000006;
			break;
		case PCI_PRIMARY_BUS:
			data32 = 0x00FF0100;
			break;
		case PCI_BASE_ADDRESS_0:
		case PCI_BASE_ADDRESS_1:
			data32 = 0x00000000;
			break;
		default:
			/*
			 * Hot-unplug can make root-port ECAM reads unreliable on some boards.
			 * Return synthetic-safe value for unsupported root dwords.
			 */
			data32 = 0x00000000;
			break;
		}

		shift = (offset & 0x3) * 8;
		switch (size) {
		case PCI_SIZE_8:
			*valuep = (data32 >> shift) & 0xff;
			break;
		case PCI_SIZE_16:
			*valuep = (data32 >> shift) & 0xffff;
			break;
		case PCI_SIZE_32:
			*valuep = data32;
			break;
		default:
			return -EINVAL;
		}

		return 0;
	}

	/*
	 * Endpoint config dword 0x04 (Command/Status) can stall on some boards.
	 * Use a safe synthetic read for non-root BDFs to keep nvme/pci scans alive.
	 */
	if (hose && !is_root_port &&
	    dword_off == PCI_COMMAND) {
		data32 = PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
		shift = (offset & 0x3) * 8;
		switch (size) {
		case PCI_SIZE_8:
			*valuep = (data32 >> shift) & 0xff;
			break;
		case PCI_SIZE_16:
			*valuep = (data32 >> shift) & 0xffff;
			break;
		case PCI_SIZE_32:
			*valuep = data32;
			break;
		default:
			return -EINVAL;
		}

		return 0;
	}

read_from_ecam:

	if (is_root_port &&
	    (offset == PCI_BASE_ADDRESS_0 || offset == PCI_BASE_ADDRESS_1)) {
		*valuep = 0;
		printf("QDMA DM PCI DBG: read quirk root-port BAR bdf=%02x:%02x.%x off=0x%x -> 0x%lx\n",
		       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf), offset, *valuep);
		return 0;
	}

	ret = qdma_dm_pci_read32(dev, bdf, offset, &data32);
	if (ret == -ENOLINK)
		return qdma_dm_pci_fill_no_device(size, valuep);
	if (ret)
		return ret;

	shift = (offset & 0x3) * 8;
	switch (size) {
	case PCI_SIZE_8:
		*valuep = (data32 >> shift) & 0xff;
		break;
	case PCI_SIZE_16:
		*valuep = (data32 >> shift) & 0xffff;
		break;
	case PCI_SIZE_32:
		*valuep = data32;
		break;
	default:
		return -EINVAL;
	}

	if ((offset == PCI_COMMAND) ||
	    (offset == PCI_PRIMARY_BUS) || (offset == PCI_BASE_ADDRESS_0) ||
	    (offset == PCI_BASE_ADDRESS_1)) {
		printf("QDMA DM PCI DBG: read cfg bdf=%02x:%02x.%x off=0x%x len=%d ret=%d val=0x%lx\n",
		       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf), offset, size, ret,
		       ret ? 0UL : *valuep);
	}

	return ret;
}

/**
 * qdma_dm_pci_write_config - Write PCI config via ECAM
 */
static int qdma_dm_pci_write_config(struct udevice *dev, pci_dev_t bdf,
                                    uint offset, ulong value,
                                    enum pci_size_t size)
{
	struct pci_controller *hose = dev_get_uclass_priv(dev);
	struct qdma_pcie_host *host = dev_get_priv(dev);
	unsigned int bus = PCI_BUS(bdf);
	int is_root_port = 0;
	int link_up = 1;
	u32 data32;
	u32 shift;
	u32 mask;
	int ret;

	if (hose && bus == hose->first_busno &&
	    PCI_DEV(bdf) == 0 && PCI_FUNC(bdf) == 0)
		is_root_port = 1;

	if (host)
		link_up = qdma_dm_pci_is_link_up(dev, host);

	if (!is_root_port && !link_up)
		return 0;

	/*
	 * Root-port writes are non-essential once link is down; skip to avoid stalls.
	 */
	if (is_root_port && !link_up)
		return 0;

	if ((offset == PCI_COMMAND) || (offset == PCI_PRIMARY_BUS) ||
	    (offset == PCI_BASE_ADDRESS_0) || (offset == PCI_BASE_ADDRESS_1) ||
	    (offset == PCI_MEMORY_BASE) || (offset == PCI_MEMORY_LIMIT) ||
	    (offset == PCI_PREF_MEMORY_BASE) || (offset == PCI_PREF_MEMORY_LIMIT)) {
		printf("QDMA DM PCI DBG: write req bdf=%02x:%02x.%x off=0x%x len=%d val=0x%lx\n",
		       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf), offset, size, value);
	}

	if (PCI_DEV(bdf) == 0 && PCI_FUNC(bdf) == 0 &&
	    PCI_BUS(bdf) != (hose ? hose->first_busno : 0) &&
	    (offset == PCI_BASE_ADDRESS_0 || offset == PCI_BASE_ADDRESS_1 ||
	     offset == PCI_COMMAND))
		qdma_dm_pci_dump_bar_route_state(dev, bdf, "before-ep-write");

	/*
	 * Keep only BAR quirk protection on the RC root port.
	 * Bus/window routing is owned by U-Boot PCI autoconfig.
	 */
	if (hose && bus == hose->first_busno && PCI_DEV(bdf) == 0 && PCI_FUNC(bdf) == 0) {
		switch (offset) {
		case PCI_BASE_ADDRESS_0:
		case PCI_BASE_ADDRESS_1:
			printf("QDMA DM PCI DBG: blocked write to root port offset 0x%x len %d\n",
			       offset, size);
			return 0;
		}
	}

	if (size == PCI_SIZE_32)
		ret = qdma_dm_pci_write32(dev, bdf, offset,
			(!is_root_port && ((offset & ~0x3) == PCI_COMMAND))
			? (((u32)value) | PCI_COMMAND_MEMORY)
			: (u32)value);
	else {
		ret = qdma_dm_pci_read32(dev, bdf, offset, &data32);
		if (ret)
			return ret;

		shift = (offset & 0x3) * 8;
		if (size == PCI_SIZE_8)
			mask = 0xff << shift;
		else if (size == PCI_SIZE_16)
			mask = 0xffff << shift;
		else
			return -EINVAL;

		data32 = (data32 & ~mask) | (((u32)value << shift) & mask);
		if (!is_root_port && ((offset & ~0x3) == PCI_COMMAND))
			data32 |= PCI_COMMAND_MEMORY;
		ret = qdma_dm_pci_write32(dev, bdf, offset, data32);
	}

	if ((offset == PCI_COMMAND) || (offset == PCI_PRIMARY_BUS) ||
	    (offset == PCI_BASE_ADDRESS_0) || (offset == PCI_BASE_ADDRESS_1) ||
	    (offset == PCI_MEMORY_BASE) || (offset == PCI_MEMORY_LIMIT) ||
	    (offset == PCI_PREF_MEMORY_BASE) || (offset == PCI_PREF_MEMORY_LIMIT)) {
		printf("QDMA DM PCI DBG: write done bdf=%02x:%02x.%x off=0x%x ret=%d\n",
		       PCI_BUS(bdf), PCI_DEV(bdf), PCI_FUNC(bdf), offset, ret);
	}

	if (!ret && PCI_DEV(bdf) == 0 && PCI_FUNC(bdf) == 0 &&
	    PCI_BUS(bdf) != (hose ? hose->first_busno : 0) &&
	    (offset == PCI_BASE_ADDRESS_0 || offset == PCI_BASE_ADDRESS_1 ||
	     offset == PCI_COMMAND))
		qdma_dm_pci_dump_bar_route_state(dev, bdf, "after-ep-write");

	return ret;
}

static const struct dm_pci_ops qdma_pci_ops = {
	.read_config	= qdma_dm_pci_read_config,
	.write_config	= qdma_dm_pci_write_config,
};

static int qdma_dm_pci_of_to_plat(struct udevice *dev)
{
	struct qdma_pcie_host *host = dev_get_priv(dev);
	struct fdt_resource reg0, reg1;
	DECLARE_GLOBAL_DATA_PTR;
	int ret;

	printf("QDMA DM PCI DBG: of_to_plat STEP 1/7 enter dev=%s\n", dev->name);

	if (!host) {
		printf("QDMA DM PCI: of_to_plat - no priv buffer!\n");
		return -ENOMEM;
	}

	printf("QDMA DM PCI: of_to_plat called for %s (offset=%d)\n", 
	       dev->name, dev_of_offset(dev));
	printf("QDMA DM PCI DBG: of_to_plat STEP 2/7 read reg[0]\n");

	ret = fdt_get_resource(gd->fdt_blob, dev_of_offset(dev), "reg", 0, &reg0);
	if (ret < 0) {
		printf("QDMA DM PCI: failed to read reg[0] (%d)\n", ret);
		return ret;
	}

	printf("QDMA DM PCI DBG: of_to_plat STEP 3/7 read reg[1]\n");

	ret = fdt_get_resource(gd->fdt_blob, dev_of_offset(dev), "reg", 1, &reg1);
	if (ret < 0) {
		printf("QDMA DM PCI: failed to read reg[1] (%d)\n", ret);
		return ret;
	}

	printf("QDMA DM PCI DBG: of_to_plat STEP 4/7 map ECAM\n");

	host->csr_phys = reg0.start;
	host->ecam_phys = reg1.start;
	host->ecam_base = map_physmem(reg1.start, fdt_resource_size(&reg1), MAP_NOCACHE);

	if (!host->ecam_base) {
		printf("QDMA DM PCI: map_physmem failed for ECAM 0x%llx\n", reg1.start);
		return -ENOMEM;
	}

	printf("QDMA DM PCI DBG: of_to_plat STEP 5/7 set memory windows\n");

	if (host->csr_phys == 0x80000000) {
		host->np_mem_base = 0xA8000000;
		host->np_mem_max = 0xAFFFFFFF;
		host->p_mem_base = 0xC0000000;
		host->p_mem_max = 0xDEFFFFFF;
	} else {
		host->np_mem_base = 0xB0000000;
		host->np_mem_max = 0xBFFFFFFF;
		host->p_mem_base = 0xE0000000;
		host->p_mem_max = 0xFEFFFFFF;
	}

	printf("QDMA DM PCI DBG: of_to_plat STEP 6/7 values csr=0x%llx ecam=0x%llx\n",
	       host->csr_phys, host->ecam_phys);

	printf("QDMA DM PCI: of_to_plat done - CSR=0x%llx ECAM=0x%llx (mapped=%p)\n",
	       host->csr_phys, host->ecam_phys, host->ecam_base);

	printf("QDMA DM PCI DBG: of_to_plat STEP 7/7 exit\n");

	return 0;
}

/**
 * qdma_dm_pci_probe - Device Model probe for QDMA PCIe host
 */
static int qdma_dm_pci_probe(struct udevice *dev)
{
	struct qdma_pcie_host *host = dev_get_priv(dev);
	struct pci_controller *hose;
	int ret;

	printf("QDMA DM PCI DBG: probe STEP 1/7 enter dev=%s\n", dev->name);

	if (!host) {
		printf("QDMA DM PCI: probe - ERROR: host priv is NULL\n");
		return -ENODEV;
	}

	printf("QDMA DM PCI: probe START - dev=%s priv@%p\n", dev->name, host);

	printf("QDMA DM PCI: CSR=0x%llx ECAM=0x%llx NP=0x%llx-0x%llx P=0x%llx-0x%llx\n",
	       host->csr_phys, host->ecam_phys,
	       host->np_mem_base, host->np_mem_max,
	       host->p_mem_base, host->p_mem_max);

	printf("QDMA DM PCI DBG: probe STEP 2/7 check already-initialized\n");

	/* Check if already initialized (e.g., by PREBOOT) */
	if (host->initialized ||
	    qdma_is_instance_initialized(host->csr_phys, host->ecam_phys)) {
		host->initialized = 1;
		printf("QDMA DM PCI: already initialized, skipping init\n");
		ret = qdma_resync_instance(host->csr_phys, host->ecam_phys,
					 host->np_mem_base, host->np_mem_max,
					 host->p_mem_base, host->p_mem_max);
		if (ret)
			printf("QDMA DM PCI: resync warning (%d), continuing\n", ret);
		else
			printf("QDMA DM PCI: resync done\n");
		printf("QDMA DM PCI DBG: probe STEP 3/7 init skipped\n");
	} else {
		printf("QDMA DM PCI: calling qdma_init_instance...\n");
		printf("QDMA DM PCI DBG: probe STEP 3/7 calling qdma_init_instance\n");
		/* Initialize QDMA if not already done */
		ret = qdma_init_instance(host->csr_phys, host->ecam_phys,
					 host->np_mem_base, host->np_mem_max,
					 host->p_mem_base, host->p_mem_max);
		if (ret) {
			printf("QDMA DM PCI: init failed (%d)\n", ret);
			printf("QDMA DM PCI DBG: probe FAIL at STEP 3/7 ret=%d\n", ret);
			return ret;
		}

		host->initialized = 1;
		printf("QDMA DM PCI DBG: probe STEP 4/7 init done\n");
	}

	printf("QDMA DM PCI DBG: probe STEP 5/7 finalize host\n");

	hose = dev_get_uclass_priv(dev);
	if (hose) {
		hose->region_count = 0;

		pci_set_region(hose->regions + hose->region_count++,
			       host->np_mem_base, host->np_mem_base,
			       host->np_mem_max - host->np_mem_base + 1,
			       PCI_REGION_MEM);

		pci_set_region(hose->regions + hose->region_count++,
			       host->p_mem_base, host->p_mem_base,
			       host->p_mem_max - host->p_mem_base + 1,
			       PCI_REGION_MEM | PCI_REGION_PREFETCH);

		printf("QDMA DM PCI DBG: probe host regions populated count=%d\n",
		       hose->region_count);
	} else {
		printf("QDMA DM PCI DBG: probe warning - no hose uclass priv\n");
	}

	printf("QDMA DM PCI: host ready\n");
	qdma_dm_pci_is_link_up(dev, host);
	printf("QDMA DM PCI DBG: probe STEP 6/7 host ready printed\n");
	printf("QDMA DM PCI DBG: probe STEP 7/7 exit\n");

	return 0;
}

static const struct udevice_id qdma_pci_ids[] = {
	{ .compatible = "xlnx,qdma-host-3.00" },
	{ }
};

U_BOOT_DRIVER(qdma_pci) = {
	.name = "qdma_pci",
	.id = UCLASS_PCI,
	.of_match = qdma_pci_ids,
	.probe = qdma_dm_pci_probe,
	.of_to_plat = qdma_dm_pci_of_to_plat,
	.priv_auto = sizeof(struct qdma_pcie_host),
	.ops = &qdma_pci_ops,
};
