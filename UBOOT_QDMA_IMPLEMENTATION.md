# U-Boot PCIe QDMA Driver Implementation Summary

## Project Overview

This project adds **QDMA (Queueing and DMA) PCIe Root Complex controller support** to U-Boot bootloader for Xilinx Versal designs (VCK190 with FMCP1 module). This enables PCIe enumeration and configuration space access directly from U-Boot, independent of Linux.

### Why This Matters

Previously:
- **Linux**: HAD full QDMA driver support (patches + xdmapcie driver)
- **U-Boot**: NO QDMA support - only generic Xilinx PCIe support

Now:
- **U-Boot**: Can enumerate and configure PCIe devices via QDMA
- **Use Cases**: 
  - FPGA testing/validation in bootloader
  - Device enumeration before Linux boot
  - Alternative control path without Linux dependency
  - Hardware bring-up and debugging

## Implementation Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    U-Boot (Bootloader)                       │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌──────────────────────────────────────────────────────┐   │
│  │         QDMA PCIe Driver                             │   │
│  │  (EmbeddedSw/XilinxProcessorIPLib/drivers/...)      │   │
│  │                                                       │   │
│  │  • drivers/pci/opsero_qdma.c                         │   │
│  │  • drivers/pci/opsero_qdma.h                         │   │
│  └──────────────────────────────────────────────────────┘   │
│         ▲              ▲               ▲                     │
│         │              │               │                     │
│    ┌────┴──┐    ┌──────┴────┐   ┌────┴────────┐            │
│    │ pcie_ │    │ qdma_     │   │ board_      │            │
│    │qdma_  │    │ setup_    │   │ pcie_qdma_  │            │
│    │ init  │    │ command_  │   │ init        │            │
│    │cmd    │    │ regs      │   │ hook        │            │
│    └───────┘    └───────────┘   └─────────────┘            │
│                                                               │
│    ┌──────────────────────────────────────────────────────┐  │
│    │  Generic PCI/PCIe Support (CONFIG_PCI_XILINX)       │  │
│    └──────────────────────────────────────────────────────┘  │
│                                                               │
└─────────────────────────────────────────────────────────────┘
         ▼
┌─────────────────────────────────────────────────────────────┐
│                 Hardware (PCIe Controller)                    │
│                     QDMA 0 / QDMA 1                          │
└─────────────────────────────────────────────────────────────┘
```

## Files Created/Modified

### New Driver Files
```
PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/
├── opsero_qdma.c                   (386 lines - main driver)
├── opsero_qdma.h                   (37 lines - public API header)
├── opsero_qdma_board_init.c        (32 lines - board init hook)
├── 0002-Opsero-QDMA-driver.patch   (60 lines - U-Boot source patch)
├── QDMA_DRIVER_README.md           (Full technical documentation)
└── test_qdma_integration.sh        (Validation test script)
```

### Modified Files
```
PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/
└── u-boot-xlnx_%.bbappend         (Updated SRC_URI, added do_patch hook)

PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/
└── user_2026-03-17-12-31-00.cfg   (Added CONFIG_OPSERO_QDMA options)
```

## Driver Features

### Core Functionality
1. **PCIe Link Detection** - Waits for link establishment with timeout
2. **Command Register Setup** - Enables IO, Memory, and Bus Master access
3. **QDMA Bridge BAR Programming** - Configures 8 access windows per instance
4. **Multi-Instance Support** - Handles both QDMA_0 and QDMA_1
5. **Auto-Initialization** - Optional boot-time automatic init

### Configuration Options
```kconfig
config OPSERO_QDMA
  bool "Opsero QDMA PCIe Root Complex Support"
  depends on PCI && ARCH_VERSAL

config OPSERO_QDMA_AUTO
  bool "Auto-initialize QDMA PCIe during boot"
  depends on OPSERO_QDMA
```

### U-Boot Command
```bash
U-Boot# help pcie_qdma_init
pcie_qdma_init  - Initialize QDMA PCIe Root Complex
    Initialize QDMA controllers for PCIe enumeration and access

U-Boot# pcie_qdma_init
U-Boot: Auto-initializing QDMA PCIe...
QDMA: Initializing instance at CSR=0x80000000 ECAM=0x...
  NP Memory: 0xa8000000 - 0xafffffff
  P Memory:  0xc0000000 - 0xdf000000
QDMA: PCIe link is UP
QDMA: Setting command register: 0x00000147
QDMA DBG: qdma_init_instance STEP 9/9 done
QDMA: Instance initialization complete
```

## QDMA Address Configuration

For VCK190 FMCP1 Versal Design:

| Parameter | QDMA_0 | QDMA_1 |
|-----------|--------|--------|
| CSR Base Address | 0x80000000 | 0x90000000 |
| ECAM Base Address | XPAR_QDMA_0_BASEADDR | XPAR_QDMA_1_BASEADDR |
| NP Mem Range | 0xA8000000-0xAFFFFFFF | 0xB0000000-0xBFFFFFFF |
| Prefetchable Mem | 0xC0000000-0xDF000000 | 0xC0000000-0xDF000000 |

## Building

### 1. Configure PetaLinux
```bash
cd PetaLinux/vck190_fmcp1
petalinux-config -c u-boot
# Ensure these are enabled:
# CONFIG_PCI=y
# CONFIG_OPSERO_QDMA=y
# CONFIG_OPSERO_QDMA_AUTO=y
```

### 2. Build U-Boot with QDMA Support
```bash
petalinux-build -c u-boot
```

The build process will:
- Apply the 0002-Opsero-QDMA-driver.patch to U-Boot source
- Copy opsero_qdma.c and opsero_qdma.h to drivers/pci/
- Compile QDMA driver as part of U-Boot

### 3. Build Full System
```bash
petalinux-build
```

## Testing

### Validation Script
```bash
bash PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/test_qdma_integration.sh
```

Checks:
- All required files present
- Configuration options set correctly
- Patch file format valid
- Source files syntactically correct

### Runtime Testing
```bash
U-Boot# pcie_qdma_init          # Manual initialization
U-Boot# pcie_qdma_scan 1 250    # Enumerate PCIe functions via QDMA ECAM
U-Boot# nvme scan               # Trigger NVMe discovery
```

For dual-NVMe hardware population, `pcie_qdma_init` now attempts both QDMA instances by default.
If QDMA1 is not present/ready, initialization continues with QDMA0 (best-effort behavior).

Expected on dual-drive setup:
- `pcie_qdma_scan 1 250` shows one endpoint per QDMA root complex (typically 2 NVMe endpoints total).

### Debug Trace Interpretation
The driver now prints step-by-step markers for each critical stage.

- QDMA init/scan path markers start with: `QDMA DBG:`
- DM PCI of_to_plat/probe markers start with: `QDMA DM PCI DBG:`
- If boot stops, the last printed `STEP X/Y` line identifies the failing stage.

Typical successful sequence excerpt:
```text
QDMA DBG: qdma_init_all STEP 1/4 reset instance table
QDMA DBG: qdma_init_instance STEP 1/9 enter csr=...
QDMA DBG: qdma_init_all STEP 3.5/4 attempt QDMA1 init (best-effort)
QDMA DBG: qdma_init_instance STEP 9/9 done
QDMA DM PCI DBG: probe STEP 7/7 exit
```

### Preboot Recommendation
For VCK190 RevA in this project, use QDMA-native scan in preboot and avoid generic `pci enum; pci` in preboot.

Recommended preboot:
```text
echo PREBOOT-QDMA-START; pcie_qdma_init; pcie_qdma_scan 1 250; echo PREBOOT-NVME-START; if nvme scan; then echo PREBOOT-NVME-SCAN-OK; else echo PREBOOT-NVME-SCAN-FAIL; fi; echo PREBOOT-NVME-END; echo QDMA-PCI-PREBOOT-DONE
```

## Comparison: Linux vs. U-Boot QDMA Support

| Feature | Linux | U-Boot |
|---------|-------|--------|
| Kernel Driver | Yes (patched) | N/A |
| DMA Enumeration | Yes | Yes |
| NVMe Support | Yes | Partial |
| Device Interrupts | Yes | No (not needed) |
| Boot-time Init | Yes | Yes (optional) |
| Post-boot Config | Yes | N/A |

## Integration with Existing Code

### Reference Implementations
- **Vitis Example**: `Vitis/common/src/xdmapcie_rc_enumerate_example.c`
- **Linux Driver**: `PetaLinux/vck190_fmcp1/...recipes-kernel/linux/linux-xlnx_%.bbappend`
- **Xilinx Driver Library**: `EmbeddedSw/XilinxProcessorIPLib/drivers/xdmapcie_v1_7/src/xdmapcie.c`

The U-Boot driver was adapted from these resources and maintains compatibility with the existing Versal hardware design and parameter configuration.

## Future Enhancements

1. **Device Tree Support** - Parse QDMA nodes from device tree
2. **MSI Interrupt Support** - Add message signaled interrupt handling
3. **Power Management** - PM state management and wake events
4. **Performance Monitoring** - Link speed/width statistics
5. **Error Recovery** - Link reset and recovery mechanisms

## Troubleshooting

### "pcie_qdma_init command not found"
- Check `CONFIG_OPSERO_QDMA=y` is set
- Rebuild U-Boot: `petalinux-build -c u-boot`

### "PCIe link is DOWN"
- Verify QDMA IP is enabled in Vivado design
- Check xparameters.h for correct memory addresses
- Verify physical PCIe connections

### "QDMA initialization failed"
- Enable debug output in opsero_qdma.c
- Check U-Boot console output for detailed error messages
- Verify CSR and ECAM base addresses match hardware design

## Documentation

- **Full Technical Reference**: [QDMA_DRIVER_README.md](PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/QDMA_DRIVER_README.md)
- **Source Code Documentation**: Comments in opsero_qdma.c and opsero_qdma.h
- **Integration Guide**: Comments in u-boot-xlnx_%.bbappend

## License

These modifications follow the Xilinx/FPGA-Drive project licensing and are provided as-is for use with the fpga-drive-aximm-pcie project.

---

**Implementation Date**: March 17-18, 2026  
**Supported Hardware**: Xilinx Versal VCK190 with FMCP1 module  
**Device**: QDMA PCIe Root Complex (QDMA_0 and QDMA_1)
