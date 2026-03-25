/*
 * Opsero Electronic Design Inc. 2024
 *
 * QDMA PCIe Root Complex Initialization for U-Boot
 * 
 * This file implements QDMA PCIe initialization for Xilinx Versal designs.
 * Adapted from the standalone XDMA driver for use in U-Boot bootloader.
 *
 * Based on: EmbeddedSw/XilinxProcessorIPLib/drivers/xdmapcie_v1_7/src/xdmapcie.c
 */

#include <common.h>
#include <command.h>
#include <pci.h>
#include <asm/io.h>
#include <cpu_func.h>
#include <asm/system.h>
#include <linux/delay.h>

/* QDMA PCIe Bridge Specific Definitions */
#define QDMA_PCIE_BRIDGE

/* Register Offsets for QDMA Bridge */
#define BDF_ENTRY_ADDR_LO              0x2420
#define BDF_ENTRY_ADDR_HI              0x2424
#define BDF_ENTRY_PASID                0x2428
#define BDF_ENTRY_FUNCTION             0x242C
#define BDF_ENTRY_WINDOW               0x2430
#define BDF_ENTRY_REG                  0x2434

#define BDF_NUM_WINDOWS                8
#define BDF_ADDR_BOUNDARY              4096
#define BDF_TABLE_ENTRY_OFF            0x20
#define BDF_ACCESS_PERM                0xC0000000

/* PCIe Configuration Register Offsets */
#define PCIE_CFG_CMD_IO_EN             0x00000001  /* I/O access enable */
#define PCIE_CFG_CMD_MEM_EN            0x00000002  /* Memory access enable */
#define PCIE_CFG_CMD_BUSM_EN           0x00000004  /* Bus master enable */
#define PCIE_CFG_CMD_PARITY            0x00000040  /* Parity errors response */
#define PCIE_CFG_CMD_SERR_EN           0x00000100  /* SERR report enable */

#define PCIE_CFG_CMD_STATUS_REG        0x0001      /* Command/Status Register */
#define PCIE_CFG_ID_REG                0x0000      /* Vendor ID/Device ID */
#define PCIE_CFG_BUS_NUMBERS_REG       0x0006      /* Primary/Secondary/Subordinate */
#define PCIE_CFG_CLASS_REV_REG         0x0002      /* Class Code/Revision ID */
#define PCIE_CFG_HDRTYPE_REG           0x0003      /* Header Type */
#define PCIE_CFG_BAR0_REG              0x0004      /* BAR0 */
#define PCIE_CFG_BAR1_REG              0x0005      /* BAR1 */
#define PCIE_CFG_MEM_BASE_LIMIT_REG    0x0008      /* Memory Base/Limit Register (Type-1, offset 0x20) */
#define PCIE_CFG_PREFETCH_BASE_LIMIT_REG 0x0009   /* Prefetchable Memory Base/Limit */
#define PCIE_CFG_PREFETCH_BASE_UPPER_REG 0x000A   /* Prefetchable Base Upper 32 bits */
#define PCIE_CFG_PREFETCH_LIMIT_UPPER_REG 0x000B  /* Prefetchable Limit Upper 32 bits */

#define PCIE_CFG_CAP_PTR_OFF           0x34        /* Capabilities pointer byte offset */
#define PCIE_CAP_ID_PCI_EXP            0x10
#define PCIE_CAP_LINK_CAP_OFF          0x0C
#define PCIE_CAP_LINK_CONTROL_OFF      0x10
#define PCIE_CAP_LINK_STATUS_OFF       0x12
#define PCIE_CAP_LINK_CONTROL2_OFF     0x30

#define PCIE_LINK_CONTROL_RETRAIN      0x0020
#define PCIE_LINK_STATUS_SPEED_MASK    0x000f
#define PCIE_LINK_STATUS_WIDTH_MASK    0x03f0
#define PCIE_LINK_STATUS_TRAINING      0x0800
#define PCIE_LINK_CONTROL2_SPEED_MASK  0x000f

#define PCIE_CFG_PRIMARY_BUS_NUM       0x00
#define PCIE_CFG_SECONDARY_BUS_NUM     0x01
#define PCIE_CFG_SUBORDINATE_BUS_NUM   0xFF
#define PCIE_CFG_BUS_NUMBERS_VALUE     ((PCIE_CFG_SUBORDINATE_BUS_NUM << 16) | \
                                        (PCIE_CFG_SECONDARY_BUS_NUM << 8)  | \
                                         PCIE_CFG_PRIMARY_BUS_NUM)

/* QDMA Configuration */
#define OPSERO_QDMA_0_BASEADDR         0x80000000ULL
#define OPSERO_QDMA_1_BASEADDR         0x90000000ULL

#define OPSERO_QDMA_0_ECAMADDR         0x400000000ULL
#define OPSERO_QDMA_1_ECAMADDR         0x440000000ULL

#ifndef XPAR_QDMA_0_BASEADDR
#define XPAR_QDMA_0_BASEADDR OPSERO_QDMA_0_ECAMADDR
#endif

#ifndef XPAR_QDMA_1_BASEADDR
#define XPAR_QDMA_1_BASEADDR OPSERO_QDMA_1_ECAMADDR
#endif

#define XDMAPCIE_ECAM_MEMSIZE          0x10000000  /* 256MB ECAM size */
#define XDMAPCIE_NUM_BUSES             256

/* QDMA bridge link status (aligned with Linux pcie-xilinx-dma-pl.c) */
#define QDMA_BRIDGE_BASE_OFF           0xCD8
#define XILINX_PCIE_DMA_REG_PSCR       0x144
#define XILINX_PCIE_DMA_REG_PSCR_LNKUP 0x800
#define XDMAPCIE_LINK_WAIT_MAX_RETRIES 10
#define XDMAPCIE_LINK_WAIT_USLEEP_MIN  90000

#define QDMA_SCAN_DEFAULT_MAX_BUS      255
#define QDMA_SCAN_DEFAULT_TIMEOUT_MS   1000

/* Struct to hold QDMA instance data */
struct qdma_pcie_instance {
        phys_addr_t csr_base;        /* S_AXI_LITE_CSR base address */
        phys_addr_t ecam_base;       /* ECAM configuration space base */
        phys_addr_t np_mem_base;     /* Non-prefetchable memory base */
        phys_addr_t np_mem_max;      /* Non-prefetchable memory max */
        phys_addr_t p_mem_base;      /* Prefetchable memory base (64-bit) */
        phys_addr_t p_mem_max;       /* Prefetchable memory max (64-bit) */
        int initialized;
};

static struct qdma_pcie_instance qdma_instances[2];
static int num_qdma_instances = 0;
static int qdma_cfg_write_lockdown;

static int qdma_count_initialized_instances(void)
{
        int i;
        int initialized = 0;

        for (i = 0; i < num_qdma_instances; i++) {
                if (qdma_instances[i].initialized)
                        initialized++;
        }

        return initialized;
}

int qdma_is_instance_initialized(phys_addr_t csr_base, phys_addr_t ecam_base)
{
        int i;

        for (i = 0; i < num_qdma_instances; i++) {
                if (!qdma_instances[i].initialized)
                        continue;

                if (qdma_instances[i].csr_base == csr_base &&
                    qdma_instances[i].ecam_base == ecam_base)
                        return 1;
        }

        return 0;
}

static void qdma_setup_command_registers(struct qdma_pcie_instance *qdma);
static void qdma_setup_bridge_bus_numbers(struct qdma_pcie_instance *qdma);
static void qdma_setup_memory_apertures(struct qdma_pcie_instance *qdma);
static void qdma_enable_bridge_access(struct qdma_pcie_instance *qdma);
static void qdma_dump_root_port_cfg(struct qdma_pcie_instance *qdma,
                                      const char *tag);
static void qdma_cfg_writel_guarded(struct qdma_pcie_instance *qdma,
                                    u8 bus, u8 dev, u8 fn, u16 reg, u32 value);
static int qdma_check_link(struct qdma_pcie_instance *qdma);
static inline void qdma_write_reg(phys_addr_t base, u32 offset, u32 value);

int qdma_resync_instance(phys_addr_t csr_base, phys_addr_t ecam_base,
                               phys_addr_t np_mem_base, phys_addr_t np_mem_max,
                               phys_addr_t p_mem_base, phys_addr_t p_mem_max)
{
        struct qdma_pcie_instance qdma;

        printf("QDMA DBG: qdma_resync_instance enter csr=0x%llx ecam=0x%llx\n",
               csr_base, ecam_base);

        memset(&qdma, 0, sizeof(qdma));
        qdma.csr_base = csr_base;
        qdma.ecam_base = ecam_base;
        qdma.np_mem_base = np_mem_base;
        qdma.np_mem_max = np_mem_max;
        qdma.p_mem_base = p_mem_base;
        qdma.p_mem_max = p_mem_max;
        qdma.initialized = 1;

        mmu_set_region_dcache_behaviour(csr_base, 0x10000000, DCACHE_OFF);
        mmu_set_region_dcache_behaviour(ecam_base, 0x10000000, DCACHE_OFF);
        mmu_set_region_dcache_behaviour(np_mem_base,
                                            np_mem_max - np_mem_base + 1,
                                            DCACHE_OFF);

        qdma_cfg_write_lockdown = 0;

        qdma_setup_command_registers(&qdma);
        qdma_setup_bridge_bus_numbers(&qdma);
        qdma_setup_memory_apertures(&qdma);
        qdma_enable_bridge_access(&qdma);
        qdma_dump_root_port_cfg(&qdma, "resync");

        qdma_cfg_write_lockdown = 1;

        printf("QDMA DBG: qdma_resync_instance done\n");

        return 0;
}

static inline u32 qdma_read_reg(phys_addr_t base, u32 offset);

static inline u32 qdma_ecam_offset(u8 bus, u8 dev, u8 fn, u16 reg)
{
        return ((u32)bus << 20) | ((u32)dev << 15) | ((u32)fn << 12) |
               ((u32)reg << 2);
}

static inline u32 qdma_cfg_readl(struct qdma_pcie_instance *qdma,
                                 u8 bus, u8 dev, u8 fn, u16 reg)
{
        return qdma_read_reg(qdma->ecam_base, qdma_ecam_offset(bus, dev, fn, reg));
}

static inline u8 qdma_cfg_readb(struct qdma_pcie_instance *qdma,
                                u8 bus, u8 dev, u8 fn, u16 byte_off)
{
        u32 dword;
        u8 shift;

        dword = qdma_cfg_readl(qdma, bus, dev, fn, byte_off >> 2);
        shift = (byte_off & 0x3) * 8;

        return (u8)((dword >> shift) & 0xff);
}

static inline u16 qdma_cfg_readw(struct qdma_pcie_instance *qdma,
                                 u8 bus, u8 dev, u8 fn, u16 byte_off)
{
        u32 dword;
        u8 shift;

        dword = qdma_cfg_readl(qdma, bus, dev, fn, byte_off >> 2);
        shift = (byte_off & 0x2) * 8;

        return (u16)((dword >> shift) & 0xffff);
}

static void qdma_cfg_writew(struct qdma_pcie_instance *qdma,
                            u8 bus, u8 dev, u8 fn, u16 byte_off, u16 value)
{
        u16 reg = byte_off >> 2;
        u32 dword;
        u8 shift;
        u32 mask;

        dword = qdma_cfg_readl(qdma, bus, dev, fn, reg);
        shift = (byte_off & 0x2) * 8;
        mask = 0xffffu << shift;
        dword = (dword & ~mask) | ((u32)value << shift);

        qdma_cfg_writel_guarded(qdma, bus, dev, fn, reg, dword);
}

static int qdma_find_pcie_cap_off(struct qdma_pcie_instance *qdma,
                                  u8 bus, u8 dev, u8 fn, u16 *cap_off)
{
        u8 ptr;
        int hops;

        ptr = qdma_cfg_readb(qdma, bus, dev, fn, PCIE_CFG_CAP_PTR_OFF);
        ptr &= ~0x3;

        for (hops = 0; hops < 48 && ptr >= 0x40; hops++) {
                u8 cap_id = qdma_cfg_readb(qdma, bus, dev, fn, ptr);
                u8 next = qdma_cfg_readb(qdma, bus, dev, fn, ptr + 1);

                if (cap_id == PCIE_CAP_ID_PCI_EXP) {
                        *cap_off = ptr;
                        return 0;
                }

                ptr = next & ~0x3;
                if (!ptr)
                        break;
        }

        return -ENOENT;
}

static const char *qdma_pcie_speed_to_str(u32 speed)
{
        switch (speed) {
        case 1:
                return "2.5";
        case 2:
                return "5.0";
        case 3:
                return "8.0";
        case 4:
                return "16.0";
        case 5:
                return "32.0";
        default:
                return "?";
        }
}

static void qdma_print_root_link_info(struct qdma_pcie_instance *qdma)
{
        u16 cap_off;
        u16 link_sta;
        u32 link_cap;
        u32 speed;
        u32 width;
        u32 max_speed;
        u32 max_width;

        if (qdma_find_pcie_cap_off(qdma, 0, 0, 0, &cap_off)) {
                printf("QDMA: PCIe capability not found on root port (ECAM=0x%llx)\n",
                       qdma->ecam_base);
                return;
        }

        link_cap = qdma_cfg_readl(qdma, 0, 0, 0, (cap_off + PCIE_CAP_LINK_CAP_OFF) >> 2);
        link_sta = (u16)(qdma_cfg_readl(qdma, 0, 0, 0,
                                        (cap_off + PCIE_CAP_LINK_STATUS_OFF) >> 2) >> 16);

        speed = link_sta & 0xF;
        width = (link_sta >> 4) & 0x3F;
        max_speed = link_cap & 0xF;
        max_width = (link_cap >> 4) & 0x3F;

        printf("QDMA: LINK UP, Gen%u x%u lanes (%.4s GT/s), max Gen%u x%u\n",
               speed, width, qdma_pcie_speed_to_str(speed), max_speed, max_width);
}

static int qdma_retrain_link(struct qdma_pcie_instance *qdma, u16 cap_off,
                             ulong timeout_ms)
{
        ulong start_ms;
        u16 lnkctl;

        lnkctl = qdma_cfg_readw(qdma, 0, 0, 0, cap_off + PCIE_CAP_LINK_CONTROL_OFF);
        lnkctl |= PCIE_LINK_CONTROL_RETRAIN;
        qdma_cfg_writew(qdma, 0, 0, 0, cap_off + PCIE_CAP_LINK_CONTROL_OFF, lnkctl);

        start_ms = get_timer(0);
        while (get_timer(start_ms) < timeout_ms) {
                u16 lnksta = qdma_cfg_readw(qdma, 0, 0, 0,
                                            cap_off + PCIE_CAP_LINK_STATUS_OFF);
                if (!(lnksta & PCIE_LINK_STATUS_TRAINING))
                        return 0;
                udelay(1000);
        }

        return -ETIMEDOUT;
}

static int qdma_set_target_gen(struct qdma_pcie_instance *qdma, u32 gen,
                               int retrain, ulong timeout_ms)
{
        u16 cap_off;
        u16 lnkctl2;
        u16 lnksta;
        int ret;

        if (gen < 1 || gen > 5)
                return -EINVAL;

        ret = qdma_find_pcie_cap_off(qdma, 0, 0, 0, &cap_off);
        if (ret)
                return ret;

        lnkctl2 = qdma_cfg_readw(qdma, 0, 0, 0, cap_off + PCIE_CAP_LINK_CONTROL2_OFF);
        lnkctl2 &= ~PCIE_LINK_CONTROL2_SPEED_MASK;
        lnkctl2 |= (u16)gen;
        qdma_cfg_writew(qdma, 0, 0, 0, cap_off + PCIE_CAP_LINK_CONTROL2_OFF, lnkctl2);

        printf("QDMA: set target link speed Gen%u on ECAM=0x%llx\n",
               gen, qdma->ecam_base);

        if (retrain) {
                ret = qdma_retrain_link(qdma, cap_off, timeout_ms);
                if (ret)
                        return ret;
        }

        if (qdma_check_link(qdma))
                return -ENOLINK;

        lnksta = qdma_cfg_readw(qdma, 0, 0, 0, cap_off + PCIE_CAP_LINK_STATUS_OFF);
        printf("QDMA: negotiated after update Gen%u x%u\n",
               lnksta & PCIE_LINK_STATUS_SPEED_MASK,
               (lnksta & PCIE_LINK_STATUS_WIDTH_MASK) >> 4);

        return 0;
}

static int qdma_resync_hw_instance(struct qdma_pcie_instance *qdma)
{
        return qdma_resync_instance(qdma->csr_base, qdma->ecam_base,
                                    qdma->np_mem_base, qdma->np_mem_max,
                                    qdma->p_mem_base, qdma->p_mem_max);
}

static int qdma_apply_link_cmd_one(struct qdma_pcie_instance *qdma,
                                   const char *mode, u32 val, ulong timeout_ms)
{
        int ret;

        if (!strcmp(mode, "gen")) {
                ret = qdma_set_target_gen(qdma, val, 1, timeout_ms);
                if (ret)
                        return ret;
        } else if (!strcmp(mode, "eq")) {
                u16 cap_off;

                ret = qdma_find_pcie_cap_off(qdma, 0, 0, 0, &cap_off);
                if (ret)
                        return ret;

                ret = qdma_retrain_link(qdma, cap_off, timeout_ms);
                if (ret)
                        return ret;

                if (qdma_check_link(qdma))
                        return -ENOLINK;

                printf("QDMA: EQ/retrain done on ECAM=0x%llx\n", qdma->ecam_base);
                qdma_print_root_link_info(qdma);
        } else {
                return -EINVAL;
        }

        ret = qdma_resync_hw_instance(qdma);
        if (ret)
                return ret;

        printf("QDMA: instance reinitialized after link update (CSR=0x%llx)\n",
               qdma->csr_base);

        return 0;
}

static bool qdma_is_disabled_cfg_write_reg(u16 reg)
{
        switch (reg) {
        case PCIE_CFG_CMD_STATUS_REG:
        case PCIE_CFG_BUS_NUMBERS_REG:
        case PCIE_CFG_BAR0_REG:
        case PCIE_CFG_BAR1_REG:
        case PCIE_CFG_MEM_BASE_LIMIT_REG:
        case PCIE_CFG_PREFETCH_BASE_LIMIT_REG:
        case PCIE_CFG_PREFETCH_BASE_UPPER_REG:
        case PCIE_CFG_PREFETCH_LIMIT_UPPER_REG:
                return true;
        default:
                return false;
        }
}

static void qdma_cfg_writel_guarded(struct qdma_pcie_instance *qdma,
                                    u8 bus, u8 dev, u8 fn, u16 reg, u32 value)
{
        u32 offset = qdma_ecam_offset(bus, dev, fn, reg);

        if (qdma_cfg_write_lockdown && qdma_is_disabled_cfg_write_reg(reg)) {
                printf("QDMA DM PCI: disabled register write ecam=0x%llx bdf=%02x:%02x.%x reg=0x%04x offset=0x%08x value=0x%08x\n",
                       qdma->ecam_base, bus, dev, fn, reg * 4, offset, value);
                return;
        }

        qdma_write_reg(qdma->ecam_base, offset, value);
}

static void qdma_dump_root_port_cfg(struct qdma_pcie_instance *qdma,
                                    const char *tag)
{
        u32 id;
        u32 cmd;
        u32 busn;
        u32 class_rev;
        u32 hdr;
        u32 bar0;
        u32 bar1;

        id = qdma_cfg_readl(qdma, 0, 0, 0, PCIE_CFG_ID_REG);
        cmd = qdma_cfg_readl(qdma, 0, 0, 0, PCIE_CFG_CMD_STATUS_REG);
        busn = qdma_cfg_readl(qdma, 0, 0, 0, PCIE_CFG_BUS_NUMBERS_REG);
        class_rev = qdma_cfg_readl(qdma, 0, 0, 0, PCIE_CFG_CLASS_REV_REG);
        hdr = qdma_cfg_readl(qdma, 0, 0, 0, PCIE_CFG_HDRTYPE_REG);
        bar0 = qdma_cfg_readl(qdma, 0, 0, 0, PCIE_CFG_BAR0_REG);
        bar1 = qdma_cfg_readl(qdma, 0, 0, 0, PCIE_CFG_BAR1_REG);

        printf("QDMA CFG DUMP[%s]: ecam=0x%llx 00:00.0 ID=%08x CMD=%08x BUS=%08x CLASS=%08x HDR=%08x BAR0=%08x BAR1=%08x\n",
               tag, qdma->ecam_base, id, cmd, busn, class_rev, hdr, bar0, bar1);
}

static void qdma_dump_bdf_window(struct qdma_pcie_instance *qdma, u32 i,
                                 const char *tag)
{
        u32 off = i * BDF_TABLE_ENTRY_OFF;
        u32 lo = qdma_read_reg(qdma->csr_base, BDF_ENTRY_ADDR_LO + off);
        u32 hi = qdma_read_reg(qdma->csr_base, BDF_ENTRY_ADDR_HI + off);
        u32 pasid = qdma_read_reg(qdma->csr_base, BDF_ENTRY_PASID + off);
        u32 fn = qdma_read_reg(qdma->csr_base, BDF_ENTRY_FUNCTION + off);
        u32 win = qdma_read_reg(qdma->csr_base, BDF_ENTRY_WINDOW + off);
        u32 reg = qdma_read_reg(qdma->csr_base, BDF_ENTRY_REG + off);

        printf("QDMA WIN DUMP[%s]: csr=0x%llx idx=%u LO=%08x HI=%08x PASID=%08x FN=%08x WIN=%08x REG=%08x\n",
               tag, qdma->csr_base, i, lo, hi, pasid, fn, win, reg);
}

/**
 * qdma_write_reg - Write to QDMA register
 */
static inline void qdma_write_reg(phys_addr_t base, u32 offset, u32 value)
{
        void *addr = (void *)(ulong)(base + offset);
        writel(value, addr);
}

/**
 * qdma_read_reg - Read from QDMA register
 */
static inline u32 qdma_read_reg(phys_addr_t base, u32 offset)
{
        void *addr = (void *)(ulong)(base + offset);
        return readl(addr);
}

/**
 * qdma_is_link_up - Check if PCIe link is up
 */
static int qdma_is_link_up(struct qdma_pcie_instance *qdma)
{
        u32 pscr;

        pscr = qdma_read_reg(qdma->csr_base,
                            QDMA_BRIDGE_BASE_OFF + XILINX_PCIE_DMA_REG_PSCR);
        printf("QDMA DBG: qdma_is_link_up csr=0x%llx PSCR=0x%08x LNKUP=%u\n",
               qdma->csr_base, pscr,
               (pscr & XILINX_PCIE_DMA_REG_PSCR_LNKUP) ? 1 : 0);
        return (pscr & XILINX_PCIE_DMA_REG_PSCR_LNKUP) != 0 ? 1 : 0;
}

/**
 * qdma_enable_bridge_access - Configure QDMA bridge BAR access windows
 * 
 * This programs the BDF entry registers to enable access through BAR windows
 */
static void qdma_enable_bridge_access(struct qdma_pcie_instance *qdma)
{
        u32 i;
        u32 size;
        phys_addr_t np_base = qdma->np_mem_base;

        printf("QDMA DBG: qdma_enable_bridge_access STEP 1/4 enter csr=0x%llx\n",
               qdma->csr_base);

        /* Non-prefetchable memory windows */
        size = qdma->np_mem_max - qdma->np_mem_base + 1;

        printf("QDMA DBG: qdma_enable_bridge_access STEP 2/4 NP size=0x%x\n", size);

        for (i = 0; i < BDF_NUM_WINDOWS; i++) {
                phys_addr_t window_addr = np_base + (i * (size / BDF_NUM_WINDOWS));
                u32 window_size = size / (BDF_NUM_WINDOWS * BDF_ADDR_BOUNDARY);

                printf("QDMA: Configuring NP window %u: addr=0x%llx size=%uK\n",
                       i, window_addr, (window_size * BDF_ADDR_BOUNDARY) / 1024);

                qdma_write_reg(qdma->csr_base, 
                              BDF_ENTRY_ADDR_LO + (i * BDF_TABLE_ENTRY_OFF),
                              (u32)window_addr);
                qdma_write_reg(qdma->csr_base,
                              BDF_ENTRY_ADDR_HI + (i * BDF_TABLE_ENTRY_OFF),
                              (u32)(window_addr >> 32));
                qdma_write_reg(qdma->csr_base,
                              BDF_ENTRY_PASID + (i * BDF_TABLE_ENTRY_OFF),
                              0x0);
                qdma_write_reg(qdma->csr_base,
                              BDF_ENTRY_FUNCTION + (i * BDF_TABLE_ENTRY_OFF),
                              0x0);
                qdma_write_reg(qdma->csr_base,
                              BDF_ENTRY_WINDOW + (i * BDF_TABLE_ENTRY_OFF),
                              BDF_ACCESS_PERM + window_size);
                qdma_write_reg(qdma->csr_base,
                              BDF_ENTRY_REG + (i * BDF_TABLE_ENTRY_OFF),
                              0x0);
        }

        printf("QDMA DBG: qdma_enable_bridge_access STEP 3/4 NP windows done\n");
        qdma_dump_bdf_window(qdma, 0, "np-first");
        qdma_dump_bdf_window(qdma, BDF_NUM_WINDOWS - 1, "np-last");

        /*
         * Keep NP window table intact for endpoint BAR0 MMIO access.
         * The BDF table has a single set of entries; programming P windows here
         * overwrites NP entries and can break BAR0 access (e.g. 0xA8000000).
         */
        if (qdma->p_mem_base == 0x0)
                printf("QDMA DBG: qdma_enable_bridge_access STEP 4/4 skip P windows (base=0)\n");
        else
                printf("QDMA DBG: qdma_enable_bridge_access STEP 4/4 skip P windows (preserve NP routing)\n");
}

/**
 * qdma_setup_command_registers - Setup PCIe command/status registers
 */
static void qdma_setup_command_registers(struct qdma_pcie_instance *qdma)
{
        u32 cmd_status;

        printf("QDMA DBG: qdma_setup_command_registers STEP 1/3 read current cmd/status\n");

        /* Read current command/status register */
        cmd_status = qdma_read_reg(qdma->ecam_base, PCIE_CFG_CMD_STATUS_REG * 4);

        printf("QDMA DBG: qdma_setup_command_registers STEP 2/3 current=0x%08x\n",
               cmd_status);

        /* Enable IO, Memory, and Bus Master */
        cmd_status |= (PCIE_CFG_CMD_BUSM_EN | PCIE_CFG_CMD_MEM_EN |
                       PCIE_CFG_CMD_IO_EN | PCIE_CFG_CMD_PARITY |
                       PCIE_CFG_CMD_SERR_EN);

        printf("QDMA: Setting command register: 0x%08x\n", cmd_status);
        qdma_cfg_writel_guarded(qdma, 0, 0, 0, PCIE_CFG_CMD_STATUS_REG,
                                cmd_status);

         cmd_status = qdma_read_reg(qdma->ecam_base, PCIE_CFG_CMD_STATUS_REG * 4);
         printf("QDMA DBG: qdma_setup_command_registers verify readback=0x%08x\n",
                 cmd_status);

        printf("QDMA DBG: qdma_setup_command_registers STEP 3/3 write done\n");
}

/**
 * qdma_setup_bridge_bus_numbers - Setup bridge primary/secondary/subordinate buses
 */
static void qdma_setup_bridge_bus_numbers(struct qdma_pcie_instance *qdma)
{
        u32 bus_numbers;

        printf("QDMA DBG: qdma_setup_bridge_bus_numbers STEP 1/3 read current\n");

        bus_numbers = qdma_read_reg(qdma->ecam_base, PCIE_CFG_BUS_NUMBERS_REG * 4);
        printf("QDMA: Current bus numbers: 0x%08x\n", bus_numbers);

        if (bus_numbers == 0) {
                printf("QDMA DBG: qdma_setup_bridge_bus_numbers STEP 2/3 apply safe fallback (firmware left zero)\n");
                qdma_cfg_writel_guarded(qdma, 0, 0, 0,
                                        PCIE_CFG_BUS_NUMBERS_REG,
                                        PCIE_CFG_BUS_NUMBERS_VALUE);
                bus_numbers = qdma_read_reg(qdma->ecam_base,
                                            PCIE_CFG_BUS_NUMBERS_REG * 4);
                printf("QDMA: Programmed fallback bus numbers: 0x%08x\n",
                       bus_numbers);
        } else {
                printf("QDMA DBG: qdma_setup_bridge_bus_numbers STEP 2/3 keep firmware/DM-provided routing\n");
                printf("QDMA: Keeping bus numbers: 0x%08x\n", bus_numbers);
        }

        printf("QDMA DBG: qdma_setup_bridge_bus_numbers decode pri=%u sec=%u sub=%u\n",
               bus_numbers & 0xFF,
               (bus_numbers >> 8) & 0xFF,
               (bus_numbers >> 16) & 0xFF);

        printf("QDMA DBG: qdma_setup_bridge_bus_numbers STEP 3/3 done\n");
}

/**
 * qdma_setup_memory_apertures - Configure Memory Base/Limit registers on root complex
 * 
 * This programs the non-prefetchable and prefetchable memory apertures on the 
 * root complex bridge so that CPU can access PCIe memory space.
 */
static void qdma_setup_memory_apertures(struct qdma_pcie_instance *qdma)
{
        u32 mem_val;
        u32 prefetch_lo, prefetch_hi;
        u32 prefetch_lim_lo, prefetch_lim_hi;
        u32 np_base_16 = (qdma->np_mem_base >> 16) & 0xFFF0;
        u32 np_limit_16 = (qdma->np_mem_max >> 16) & 0xFFF0;
        u32 p_base_64_low, p_base_64_high;
        u32 p_limit_64_low, p_limit_64_high;

        printf("QDMA DBG: qdma_setup_memory_apertures STEP 1/4 configure NP aperture\n");

        /* Non-prefetchable memory base/limit (16-bit granularity, 4KB aligned) */
        mem_val = (np_limit_16 << 16) | np_base_16;
        
        printf("QDMA: Setting NP Memory Base/Limit: 0x%08x (base=0x%llx limit=0x%llx)\n",
               mem_val, qdma->np_mem_base, qdma->np_mem_max);
        
        qdma_cfg_writel_guarded(qdma, 0, 0, 0,
                                PCIE_CFG_MEM_BASE_LIMIT_REG, mem_val);
        mem_val = qdma_read_reg(qdma->ecam_base, PCIE_CFG_MEM_BASE_LIMIT_REG * 4);
        printf("QDMA DBG: NP aperture write verify: 0x%08x\n", mem_val);

        printf("QDMA DBG: qdma_setup_memory_apertures STEP 2/4 configure P aperture\n");

        /* Prefetchable memory aperture (if enabled) */
        if (qdma->p_mem_base == 0x0) {
                printf("QDMA DBG: qdma_setup_memory_apertures STEP 3/4 skip P aperture (base=0)\n");
                return;
        }

        /*
         * Prefetchable Memory Base/Limit low register (Type-1 header):
         *   bits [15:4]  = address bits [31:20]
         *   bits [3:0]   = type flags (bit0=1 for 64-bit)
         */
        p_base_64_low = (u32)((qdma->p_mem_base >> 16) & 0xFFF0) | 0x0001;
        p_limit_64_low = (u32)((qdma->p_mem_max >> 16) & 0xFFF0) | 0x0001;
        
        printf("QDMA: Setting P Memory Base/Limit: low_base=0x%08x low_limit=0x%08x (base=0x%llx limit=0x%llx)\n",
               p_base_64_low, p_limit_64_low, qdma->p_mem_base, qdma->p_mem_max);
        
        qdma_cfg_writel_guarded(qdma, 0, 0, 0,
                                PCIE_CFG_PREFETCH_BASE_LIMIT_REG,
                                (p_limit_64_low << 16) | p_base_64_low);

        /* Upper 32 bits of prefetchable base/limit */
        p_base_64_high = (u32)(qdma->p_mem_base >> 32);
        p_limit_64_high = (u32)(qdma->p_mem_max >> 32);
        
        printf("QDMA: Setting P Memory Upper Base/Limit: high_base=0x%08x high_limit=0x%08x\n",
               p_base_64_high, p_limit_64_high);
        
        qdma_cfg_writel_guarded(qdma, 0, 0, 0,
                                PCIE_CFG_PREFETCH_BASE_UPPER_REG,
                                p_base_64_high);
        qdma_cfg_writel_guarded(qdma, 0, 0, 0,
                                PCIE_CFG_PREFETCH_LIMIT_UPPER_REG,
                                p_limit_64_high);

        prefetch_lo = qdma_read_reg(qdma->ecam_base, PCIE_CFG_PREFETCH_BASE_LIMIT_REG * 4);
        prefetch_hi = qdma_read_reg(qdma->ecam_base, PCIE_CFG_PREFETCH_BASE_UPPER_REG * 4);
        prefetch_lim_lo = prefetch_lo >> 16;
        prefetch_lim_hi = qdma_read_reg(qdma->ecam_base, PCIE_CFG_PREFETCH_LIMIT_UPPER_REG * 4);
        printf("QDMA DBG: P aperture write verify: lo_base=0x%04x lo_limit=0x%04x hi_base=0x%08x hi_limit=0x%08x\n",
               (u16)prefetch_lo, prefetch_lim_lo, prefetch_hi, prefetch_lim_hi);

        printf("QDMA DBG: qdma_setup_memory_apertures STEP 4/4 done\n");
}

/**
 * qdma_check_link - Wait for PCIe link to be established
 */
static int qdma_check_link(struct qdma_pcie_instance *qdma)
{
        int retries;
        u32 pscr;

        for (retries = 0; retries < XDMAPCIE_LINK_WAIT_MAX_RETRIES; retries++) {
                pscr = qdma_read_reg(qdma->csr_base,
                                    QDMA_BRIDGE_BASE_OFF + XILINX_PCIE_DMA_REG_PSCR);
                if ((pscr & XILINX_PCIE_DMA_REG_PSCR_LNKUP) != 0) {
                        printf("QDMA: PCIe link is UP\n");
                        return 0;
                }
                printf("QDMA: link retry %d/%d, PSCR=0x%08x\n",
                       retries + 1, XDMAPCIE_LINK_WAIT_MAX_RETRIES, pscr);
                udelay(XDMAPCIE_LINK_WAIT_USLEEP_MIN);
        }

        printf("QDMA: ERROR - PCIe link is DOWN after retries\n");
        return -ENODEV;
}

/**
 * qdma_init_instance - Initialize a single QDMA instance
 */
int qdma_init_instance(phys_addr_t csr_base, phys_addr_t ecam_base,
                       phys_addr_t np_mem_base, phys_addr_t np_mem_max,
                       phys_addr_t p_mem_base, phys_addr_t p_mem_max)
{
        struct qdma_pcie_instance *qdma;
        int idx;
        int ret;

        printf("QDMA DBG: qdma_init_instance STEP 1/9 enter csr=0x%llx ecam=0x%llx\n",
               csr_base, ecam_base);

        if (num_qdma_instances >= 2) {
                printf("QDMA: Maximum number of instances (2) reached\n");
                return -EINVAL;
        }

        printf("QDMA DBG: qdma_init_instance STEP 2/9 allocate slot idx=%d\n",
               num_qdma_instances);

         idx = num_qdma_instances;
         qdma = &qdma_instances[idx];
         memset(qdma, 0, sizeof(*qdma));

        qdma->csr_base = csr_base;
        qdma->ecam_base = ecam_base;
        qdma->np_mem_base = np_mem_base;
        qdma->np_mem_max = np_mem_max;
        qdma->p_mem_base = p_mem_base;
        qdma->p_mem_max = p_mem_max;

        printf("QDMA: Initializing instance at CSR=0x%llx ECAM=0x%llx\n",
               csr_base, ecam_base);
        printf("  NP Memory: 0x%llx - 0x%llx\n", np_mem_base, np_mem_max);
        printf("  P Memory:  0x%llx - 0x%llx\n", p_mem_base, p_mem_max);

        printf("QDMA DBG: qdma_init_instance STEP 3/9 configure MMU attrs\n");

         /* Configure non-cacheable mapping for PCIe config space */
        mmu_set_region_dcache_behaviour(csr_base, 0x10000000, DCACHE_OFF);
        mmu_set_region_dcache_behaviour(ecam_base, 0x10000000, DCACHE_OFF);

         /*
          * CPU accesses NVMe MMIO through NP aperture (BAR0 -> 0xA8000000 / 0xB0000000).
          * Keep NP non-cacheable; do not touch full P aperture here (too broad on Versal).
          */
         mmu_set_region_dcache_behaviour(np_mem_base,
                                             np_mem_max - np_mem_base + 1,
                                             DCACHE_OFF);

         printf("QDMA DBG: MMU ATTR csr=[0x%llx..0x%llx] ecam=[0x%llx..0x%llx] np=[0x%llx..0x%llx] (P unchanged)\n",
                 csr_base, csr_base + 0x10000000 - 1,
                 ecam_base, ecam_base + 0x10000000 - 1,
                 np_mem_base, np_mem_max);

        printf("QDMA DBG: qdma_init_instance STEP 4/9 check link\n");

        /* Wait for PCIe link */
        ret = qdma_check_link(qdma);
        if (ret != 0) {
                printf("QDMA DBG: qdma_init_instance FAIL at STEP 4/9 ret=%d\n", ret);
                memset(qdma, 0, sizeof(*qdma));
                return ret;
        }

        qdma_print_root_link_info(qdma);

        qdma_cfg_write_lockdown = 0;

        printf("QDMA DBG: qdma_init_instance STEP 5/9 setup command regs\n");

        /* Setup command registers */
        qdma_setup_command_registers(qdma);
        qdma_dump_root_port_cfg(qdma, "after-cmd");

        printf("QDMA DBG: qdma_init_instance STEP 6/9 setup bus numbers\n");

        /* Setup bridge bus numbering for downstream config-space access */
        qdma_setup_bridge_bus_numbers(qdma);
        qdma_dump_root_port_cfg(qdma, "after-busnum");

        printf("QDMA DBG: qdma_init_instance STEP 6.5/9 setup memory apertures\n");

        /* Setup memory base/limit registers on root complex */
        qdma_setup_memory_apertures(qdma);

        printf("QDMA DBG: qdma_init_instance STEP 7/9 enable bridge windows\n");

        /* Enable bridge access windows */
        qdma_enable_bridge_access(qdma);
        qdma_dump_root_port_cfg(qdma, "after-win");

        printf("QDMA DBG: qdma_init_instance STEP 8/9 mark initialized\n");

        qdma->initialized = 1;
        num_qdma_instances = idx + 1;
        qdma_cfg_write_lockdown = 1;
        printf("QDMA: Instance initialization complete\n");

        printf("QDMA DBG: qdma_init_instance STEP 9/9 done\n");

        return 0;
}

/**
 * qdma_init_all - Initialize all QDMA instances with Versal defaults
 */
int qdma_init_all(void)
{
        int ret;
        int ret_qdma1;
        int initialized = 0;

        printf("QDMA DBG: qdma_init_all STEP 1/4 reset instance table\n");

        num_qdma_instances = 0;
        memset(qdma_instances, 0, sizeof(qdma_instances));

        printf("QDMA DBG: qdma_init_all STEP 2/4 init QDMA0\n");

        /* Initialize QDMA 0 */
        ret = qdma_init_instance(
                OPSERO_QDMA_0_BASEADDR,    /* CSR base */
                XPAR_QDMA_0_BASEADDR,      /* ECAM base (from xparameters) */
                0xA8000000,                /* NP mem base */
                0xAFFFFFFF,                /* NP mem max */
                0xC0000000,                /* P mem base */
                0xDEFFFFFF                 /* P mem max */
        );

        if (ret != 0) {
                printf("QDMA: QDMA 0 init failed (%d), trying other instances\n", ret);
        } else {
                initialized++;
                printf("QDMA DBG: qdma_init_all STEP 3/4 QDMA0 done\n");
        }

        /* Initialize QDMA 1 if available */
        printf("QDMA DBG: qdma_init_all STEP 3.5/4 attempt QDMA1 init (best-effort)\n");

        ret_qdma1 = qdma_init_instance(
                OPSERO_QDMA_1_BASEADDR,    /* CSR base */
                XPAR_QDMA_1_BASEADDR,      /* ECAM base (from xparameters) */
                0xB0000000,                /* NP mem base */
                0xBFFFFFFF,                /* NP mem max */
                0xE0000000,                /* P mem base */
                0xFEFFFFFF                 /* P mem max */
        );

        if (ret_qdma1 != 0) {
                printf("QDMA: QDMA 1 init skipped/failed (%d), continuing with available instances\n",
                       ret_qdma1);
        } else {
                initialized++;
                printf("QDMA DBG: qdma_init_all QDMA1 init done\n");
        }

        printf("QDMA DBG: qdma_init_all STEP 4/4 done\n");

        if (!initialized) {
                printf("QDMA: No QDMA instance initialized\n");
                return (ret != 0) ? ret : ret_qdma1;
        }

        return 0;
}

/* U-Boot Command: pcie_qdma_init */
#ifdef CONFIG_CMD_PCI

static int qdma_scan_instance(struct qdma_pcie_instance *qdma, u32 max_bus,
                              ulong timeout_ms, int verbose, int *found)
{
        u32 bus;
        int found_before = *found;
        ulong start_ms = get_timer(0);

        printf("QDMA DBG: qdma_scan_instance STEP 1/3 enter ecam=0x%llx max_bus=%u timeout=%lu\n",
               qdma->ecam_base, max_bus, timeout_ms);

        if (verbose)
                printf("QDMA: Scanning ECAM 0x%llx buses 0..%u (timeout=%lums)\n",
                       qdma->ecam_base, max_bus, timeout_ms);

        if (!qdma_is_link_up(qdma)) {
                if (verbose)
                        printf("QDMA: Link down on ECAM 0x%llx, skip scan\n",
                               qdma->ecam_base);
                return -ENOLINK;
        }

        for (bus = 0; bus <= max_bus; bus++) {
                u32 dev;
                int found_bus_before = *found;

                printf("QDMA SCAN DBG: bus=%u start elapsed=%lums total_found=%d\n",
                       bus, get_timer(start_ms), *found);

                for (dev = 0; dev < 32; dev++) {
                        u32 fn;
                        u32 id0;
                        u32 hdr0;
                        u32 max_fn;

                        if (timeout_ms && get_timer(start_ms) >= timeout_ms) {
                                    printf("QDMA: Scan timeout after %lums at bus=%u dev=%u fn=0 (partial results kept)\n",
                                            get_timer(start_ms), bus, dev);
                                return -ETIMEDOUT;
                        }

                        id0 = qdma_cfg_readl(qdma, bus, dev, 0, PCIE_CFG_ID_REG);
                        if (id0 == 0xFFFFFFFF || id0 == 0x00000000)
                                continue;

                        hdr0 = qdma_cfg_readl(qdma, bus, dev, 0, PCIE_CFG_HDRTYPE_REG);
                        max_fn = ((hdr0 >> 16) & 0x80) ? 8 : 1;

                        for (fn = 0; fn < max_fn; fn++) {
                                u32 id, class_rev, hdr;
                                u16 vendor, device;
                                u32 class_code;
                                u32 bar0, bar1;

                                if (fn == 0) {
                                        id = id0;
                                        hdr = hdr0;
                                } else {
                                        if (timeout_ms && get_timer(start_ms) >= timeout_ms) {
                                                      printf("QDMA: Scan timeout after %lums at bus=%u dev=%u fn=%u (partial results kept)\n",
                                                              get_timer(start_ms), bus, dev, fn);
                                                return -ETIMEDOUT;
                                        }
                                        id = qdma_cfg_readl(qdma, bus, dev, fn,
                                                            PCIE_CFG_ID_REG);
                                        hdr = qdma_cfg_readl(qdma, bus, dev, fn,
                                                             PCIE_CFG_HDRTYPE_REG);
                                }

                                if (id == 0xFFFFFFFF || id == 0x00000000)
                                        continue;

                                vendor = (u16)(id & 0xFFFF);
                                device = (u16)((id >> 16) & 0xFFFF);
                                class_rev = qdma_cfg_readl(qdma, bus, dev, fn,
                                                            PCIE_CFG_CLASS_REV_REG);
                                class_code = (class_rev >> 8) & 0x00FFFFFF;
                                    bar0 = qdma_cfg_readl(qdma, bus, dev, fn,
                                                             PCIE_CFG_BAR0_REG);
                                    bar1 = qdma_cfg_readl(qdma, bus, dev, fn,
                                                             PCIE_CFG_BAR1_REG);

                                    printf("  %02x:%02x.%x  VID:DID %04x:%04x  CLASS %06x  HDR %02x  BAR0 %08x BAR1 %08x\n",
                                       bus, dev, fn, vendor, device, class_code,
                                            (u8)((hdr >> 16) & 0xFF), bar0, bar1);
                                (*found)++;
                        }
                }

                if (*found == found_bus_before)
                        printf("QDMA SCAN DBG: bus=%u done no-functions elapsed=%lums\n",
                               bus, get_timer(start_ms));
                else
                        printf("QDMA SCAN DBG: bus=%u done new-found=%d elapsed=%lums\n",
                               bus, *found - found_bus_before, get_timer(start_ms));
        }

        if (verbose)
                printf("QDMA: Scan complete in %lums, found=%d\n",
                       get_timer(start_ms), *found);

        printf("QDMA DBG: qdma_scan_instance STEP 2/3 scan loops done\n");
         printf("QDMA DBG: qdma_scan_instance STEP 3/3 exit found_total=%d found_this_instance=%d\n",
                 *found, *found - found_before);

        return 0;
}

static int do_pcie_qdma_init(struct cmd_tbl *cmdtp, int flag, int argc,
                             char *const argv[])
{
        int ret;
        int force = 0;

        printf("QDMA DBG: do_pcie_qdma_init STEP 1/3 command enter argc=%d\n", argc);

        if (argc > 2) {
                printf("Usage: pcie_qdma_init [force]\n");
                return CMD_RET_USAGE;
        }

        if (argc == 2) {
                if (!strcmp(argv[1], "force"))
                        force = 1;
                else {
                        printf("Usage: pcie_qdma_init [force]\n");
                        return CMD_RET_USAGE;
                }
        }

        if (!force && qdma_count_initialized_instances() > 0) {
                printf("QDMA: Already initialized, skipping re-init (use 'pcie_qdma_init force' to reinitialize)\n");
                printf("QDMA DBG: do_pcie_qdma_init STEP 2/3 init skipped\n");
                printf("QDMA DBG: do_pcie_qdma_init STEP 3/3 command exit\n");
                return CMD_RET_SUCCESS;
        }

        ret = qdma_init_all();
        if (ret != 0) {
                printf("QDMA initialization failed: %d\n", ret);
                return CMD_RET_FAILURE;
        }

        printf("QDMA DBG: do_pcie_qdma_init STEP 2/3 init done\n");

        printf("QDMA initialization completed successfully\n");
        printf("QDMA DBG: do_pcie_qdma_init STEP 3/3 command exit\n");
        return CMD_RET_SUCCESS;
}

static int do_pcie_qdma_scan(struct cmd_tbl *cmdtp, int flag, int argc,
                             char *const argv[])
{
        int i;
        int found = 0;
        int initialized_count = 0;
        u32 max_bus = QDMA_SCAN_DEFAULT_MAX_BUS;
        ulong timeout_ms = QDMA_SCAN_DEFAULT_TIMEOUT_MS;

        printf("QDMA DBG: do_pcie_qdma_scan STEP 1/4 command enter argc=%d\n", argc);

        if (argc > 3) {
                printf("Usage: pcie_qdma_scan [max_bus] [timeout_ms]\n");
                return CMD_RET_USAGE;
        }

        if (argc == 2)
                max_bus = simple_strtoul(argv[1], NULL, 0);
        else if (argc == 3) {
                max_bus = simple_strtoul(argv[1], NULL, 0);
                timeout_ms = simple_strtoul(argv[2], NULL, 0);
        }

        if (max_bus > 255)
                max_bus = 255;

        if (num_qdma_instances == 0) {
                printf("QDMA: Not initialized, run pcie_qdma_init first\n");
                return CMD_RET_FAILURE;
        }

        for (i = 0; i < num_qdma_instances; i++) {
                if (qdma_instances[i].initialized)
                        initialized_count++;
        }

        if (!initialized_count) {
                printf("QDMA: No initialized QDMA instance, run pcie_qdma_init first\n");
                return CMD_RET_FAILURE;
        }

        for (i = 0; i < num_qdma_instances; i++) {
                struct qdma_pcie_instance *qdma = &qdma_instances[i];
                int ret;

                if (!qdma->initialized)
                        continue;

                printf("QDMA DBG: do_pcie_qdma_scan STEP 2/4 scanning instance=%d csr=0x%llx\n",
                       i, qdma->csr_base);

                ret = qdma_scan_instance(qdma, max_bus, timeout_ms, 1, &found);
                if (ret == -ETIMEDOUT)
                        continue;
                if (ret == -ENOLINK)
                        continue;
        }

        if (!found)
                printf("QDMA: No PCIe functions found in scanned range\n");

        printf("QDMA DBG: do_pcie_qdma_scan STEP 3/4 scan done found=%d\n", found);
        printf("QDMA DBG: do_pcie_qdma_scan STEP 4/4 command exit\n");

        return CMD_RET_SUCCESS;
}

static int do_pcie_qdma_link(struct cmd_tbl *cmdtp, int flag, int argc,
                             char *const argv[])
{
        int i;
        int updated = 0;
        int target_inst = -1;
        u32 gen = 0;
        ulong timeout_ms = 200;

        if (argc < 3 || argc > 5) {
                printf("Usage: pcie_qdma_link <inst|all> <gen|eq|show> [gen] [timeout_ms]\n");
                return CMD_RET_USAGE;
        }

        if (strcmp(argv[1], "all"))
                target_inst = simple_strtoul(argv[1], NULL, 0);

        if (!strcmp(argv[2], "gen")) {
                if (argc < 4) {
                        printf("Usage: pcie_qdma_link <inst|all> gen <1..5> [timeout_ms]\n");
                        return CMD_RET_USAGE;
                }

                gen = simple_strtoul(argv[3], NULL, 0);
                if (argc == 5)
                        timeout_ms = simple_strtoul(argv[4], NULL, 0);
        } else if (!strcmp(argv[2], "eq")) {
                if (argc >= 4)
                        timeout_ms = simple_strtoul(argv[3], NULL, 0);
        } else if (strcmp(argv[2], "show")) {
                printf("Usage: pcie_qdma_link <inst|all> <gen|eq|show> [args]\n");
                return CMD_RET_USAGE;
        }

        if (!num_qdma_instances) {
                printf("QDMA: Not initialized, run pcie_qdma_init first\n");
                return CMD_RET_FAILURE;
        }

        for (i = 0; i < num_qdma_instances; i++) {
                struct qdma_pcie_instance *qdma = &qdma_instances[i];
                int ret;

                if (!qdma->initialized)
                        continue;
                if (target_inst >= 0 && i != target_inst)
                        continue;

                if (!strcmp(argv[2], "show")) {
                        printf("QDMA: instance %d link status:\n", i);
                        qdma_print_root_link_info(qdma);
                        updated++;
                        continue;
                }

                ret = qdma_apply_link_cmd_one(qdma, argv[2], gen, timeout_ms);
                if (ret) {
                        printf("QDMA: link command failed on instance %d (%d)\n", i, ret);
                        return CMD_RET_FAILURE;
                }

                updated++;
        }

        if (!updated) {
                if (target_inst >= 0)
                        printf("QDMA: instance %d not available/initialized\n", target_inst);
                else
                        printf("QDMA: no initialized instance matched\n");
                return CMD_RET_FAILURE;
        }

        return CMD_RET_SUCCESS;
}

U_BOOT_CMD(
        pcie_qdma_init, 2, 1, do_pcie_qdma_init,
        "Initialize QDMA PCIe Root Complex",
        "[force] - initialize QDMA controllers (default: skip if already initialized)"
);

U_BOOT_CMD(
        pcie_qdma_scan, 3, 1, do_pcie_qdma_scan,
        "Enumerate PCIe functions via QDMA ECAM",
        "[max_bus] [timeout_ms] - scan buses 0..max_bus (default: 255), timeout default: 1000ms"
);

U_BOOT_CMD(
        pcie_qdma_link, 5, 1, do_pcie_qdma_link,
        "QDMA PCIe link tuning (Gen/EQ/retrain)",
        "<inst|all> show\n"
        "pcie_qdma_link <inst|all> gen <1..5> [timeout_ms]\n"
        "pcie_qdma_link <inst|all> eq [timeout_ms]"
);

#endif /* CONFIG_CMD_PCI */

/* Optional: Call during U-Boot initialization if auto-init is desired */
int pcie_qdma_init_auto(void)
{
        int ret;
        int i;
        int found = 0;

        printf("QDMA DBG: pcie_qdma_init_auto STEP 1/5 enter\n");

        if (!CONFIG_IS_ENABLED(PCI))
                return 0;

        printf("U-Boot: Auto-initializing QDMA PCIe...\n");
        ret = qdma_init_all();
        if (ret != 0) {
                printf("QDMA: Auto init failed (%d), continuing boot\n", ret);
                return 0;
        }

        printf("QDMA DBG: pcie_qdma_init_auto STEP 2/5 init done\n");

        printf("QDMA: Auto enumeration start (max_bus=%u timeout=%ums)\n",
               QDMA_SCAN_DEFAULT_MAX_BUS, QDMA_SCAN_DEFAULT_TIMEOUT_MS);

        for (i = 0; i < num_qdma_instances; i++) {
                int scan_ret;

                if (!qdma_instances[i].initialized)
                        continue;

                scan_ret = qdma_scan_instance(&qdma_instances[i],
                                              QDMA_SCAN_DEFAULT_MAX_BUS,
                                              QDMA_SCAN_DEFAULT_TIMEOUT_MS,
                                              1, &found);
                if (scan_ret == -ETIMEDOUT)
                        continue;
                if (scan_ret == -ENOLINK)
                        continue;
        }

        printf("QDMA DBG: pcie_qdma_init_auto STEP 3/5 scan loop done\n");

        if (found == 0)
                printf("QDMA: Auto enumeration found no endpoints (drive absent/unresponsive), continuing boot\n");
        else
                printf("QDMA: Auto enumeration found %d function(s)\n", found);

        printf("QDMA DBG: pcie_qdma_init_auto STEP 4/5 summary printed\n");
        printf("QDMA DBG: pcie_qdma_init_auto STEP 5/5 exit\n");

        return 0;
}
