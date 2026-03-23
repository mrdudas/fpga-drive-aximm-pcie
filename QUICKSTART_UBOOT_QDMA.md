# Quick Start Guide: U-Boot QDMA Driver

## TL;DR Build Steps

```bash
# 1. Navigate to PetaLinux project
cd PetaLinux/vck190_fmcp1

# 2. Configure U-Boot (optional - verify settings)
petalinux-config -c u-boot

# 3. Build only U-Boot with QDMA support
petalinux-build -c u-boot
# Output: build/tmp/deploy/images/vck190_fmcp1/u-boot.elf

# 4. Or build everything
petalinux-build

# 5. Create SD card image
cd ../../../
./write_sd.sh /dev/sdX  # where X is your SD card device
```

## Verify Installation

### Pre-Build Validation
```bash
# Test that all files are correctly integrated
bash PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/test_qdma_integration.sh
```

Expected output:
```
==========================================
 QDMA PCIe Driver Integration Test
==========================================

[TEST 1] Checking required files...
  ✓ opsero_qdma.c
  ✓ opsero_qdma.h
  ✓ opsero_qdma_board_init.c
  ...
```

### Post-Boot Testing
```bash
# Boot VCK190 with SD card, open U-Boot console

# Option 1: Auto-init (if CONFIG_OPSERO_QDMA_AUTO=y)
# Device boots automatically with QDMA initialized

# Option 2: Manual init
U-Boot# pcie_qdma_init
U-Boot: Auto-initializing QDMA PCIe...
QDMA: PCIe link is UP
...

# Verify PCIe enumeration via QDMA path
U-Boot# pcie_qdma_scan 1 250
U-Boot# nvme scan
```

## What Was Added

| File | Purpose |
|------|---------|
| `opsero_qdma.c` | Main QDMA driver implementation |
| `opsero_qdma.h` | Driver public API header |
| `opsero_qdma_board_init.c` | U-Boot board initialization hook |
| `0002-Opsero-QDMA-driver.patch` | U-Boot source modifications (Kconfig/Makefile) |
| `QDMA_DRIVER_README.md` | Technical documentation |
| `test_qdma_integration.sh` | Integration validation script |
| `user_2026-03-17-12-31-00.cfg` | U-Boot configuration additions |

All files are in:
```
PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/
```

## Configuration Options

Already enabled in `user_2026-03-17-12-31-00.cfg`:
```
CONFIG_OPSERO_QDMA=y          # Enable QDMA driver
CONFIG_OPSERO_QDMA_AUTO=y    # Auto-initialize on boot
CONFIG_CMD_PCI=y              # PCI command support
```

## How It Works

1. **PetaLinux Build Process**:
   - Fetches `opsero_qdma.c` and `opsero_qdma.h`
   - Applies `0002-Opsero-QDMA-driver.patch` to U-Boot source
   - `do_patch:append()` hook copies files to `drivers/pci/`
   - U-Boot compiles with QDMA support

2. **U-Boot Initialization**:
   - If `CONFIG_OPSERO_QDMA_AUTO=y`: Auto-init during boot
   - Otherwise: Manual init via `pcie_qdma_init` command

3. **Runtime**:
   - QDMA controllers enumerate PCIe devices
   - BAR windows enable config space and memory access
  - `pcie_qdma_scan` lists discovered PCIe functions
  - `nvme scan` validates NVMe enumeration path

4. **Debug Tracing**:
  - `QDMA DBG:` shows step-by-step init/scan progress
  - `QDMA DM PCI DBG:` shows DM PCI of_to_plat/probe progress
  - Last printed `STEP X/Y` line indicates where execution stopped

## Troubleshooting

### Build Issues

**Problem**: "patch: **** malformed patch"
- **Solution**: Ensure 0002-Opsero-QDMA-driver.patch is properly formatted

**Problem**: "opsero_qdma.c: No such file"
- **Solution**: Verify file is in `...recipes-bsp/u-boot/files/`

### Runtime Issues

**Problem**: "PCIe link is DOWN"
- **Solution**: 
  1. Check QDMA IP enabled in Vivado design
  2. Verify physical PCIe connections
  3. Check CSR base address: 0x80000000 (QDMA_0)

**Problem**: Boot reset/hang after PREBOOT-PCI
- **Cause**: Generic `pci enum; pci` preboot path may trigger instability on this target.
- **Solution**: Use this preboot sequence instead:
  - `echo PREBOOT-QDMA-START; pcie_qdma_init; pcie_qdma_scan 1 250; echo PREBOOT-NVME-START; if nvme scan; then echo PREBOOT-NVME-SCAN-OK; else echo PREBOOT-NVME-SCAN-FAIL; fi; echo PREBOOT-NVME-END; echo QDMA-PCI-PREBOOT-DONE`
  - Track last printed `QDMA DBG` / `QDMA DM PCI DBG` step.

**Problem**: "pcie_qdma_init: command not found"
- **Solution**: 
  1. Verify `CONFIG_OPSERO_QDMA=y` in U-Boot config
  2. Rebuild: `petalinux-build -c u-boot`

## Next Steps

1. **Build**: `cd PetaLinux/vck190_fmcp1 && petalinux-build`
2. **Test**: Boot on hardware and run `pcie_qdma_init`
3. **Verify**: Run `pcie_qdma_scan 1 250` and `nvme scan`
4. **Reference**: See `QDMA_DRIVER_README.md` for full documentation

## Related Documentation

- Full Technical Docs: [UBOOT_QDMA_IMPLEMENTATION.md](../UBOOT_QDMA_IMPLEMENTATION.md)
- Driver Details: [QDMA_DRIVER_README.md](PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/QDMA_DRIVER_README.md)
- Source Code: See inline comments in `opsero_qdma.c`

## Support & Questions

For issues or questions:
1. Check the troubleshooting section above
2. Review full documentation in files
3. Check U-Boot console output for debug messages
4. Enable debug prints in opsero_qdma.c if needed

---
**Created**: March 18, 2026 | **For**: VCK190 FMCP1 Design
