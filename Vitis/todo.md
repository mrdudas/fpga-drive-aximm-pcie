# fpga-drive-aximm-pcie: PCIe Gen3 Targets - No BSP Driver in Vitis 2025.2 SDT Mode

## Problem

Three targets fail to build in Vitis 2025.2 because the BSP does not include a PCIe driver:

| Target | FPGA | PCIe IP | BSP Driver | Status |
|--------|------|---------|------------|--------|
| kcu105_hpc | KCU105 (Kintex UltraScale) | AXI Bridge for PCI Express Gen3 | **NONE** | FAIL |
| kcu105_lpc | KCU105 (Kintex UltraScale) | AXI Bridge for PCI Express Gen3 | **NONE** | FAIL |
| vc709_hpc | VC709 (Virtex-7) | AXI Bridge for PCI Express Gen3 | **NONE** | FAIL |

All other targets build successfully (96/99 across all repos).

## Root Cause

In Vitis 2025.2, the System Device Tree (SDT) BSP generator does not recognize
the "AXI Bridge for PCI Express Gen3" IP and does not include any PCIe driver
(neither `axipcie` nor `xdmapcie`) in the BSP.

The `xparameters.h` for these targets only contains raw address definitions:
```c
#define XPAR_AXI_PCIE3_0_BASEADDR 0x10000000
#define XPAR_AXI_PCIE3_0_HIGHADDR 0x1fffffff
```

No `XPAR_XAXIPCIE_NUM_INSTANCES` or `XPAR_XXDMAPCIE_NUM_INSTANCES` is defined,
and no driver headers (`xaxipcie.h` or `xdmapcie.h`) are present in the BSP.

In previous Vitis versions (non-SDT mode), these targets used the `axipcie`
driver successfully with `pcie_enumerate.c`.

## Comparison with Working Targets

- **axipcie targets** (kc705_hpc/lpc, pz_7015/7030, vc707_hpc1/2, zc706_hpc/lpc):
  BSP includes `axipcie` driver, `xaxipcie.h` is available, `XPAR_XAXIPCIE_0_BASEADDR` defined.
  Build succeeds with SDT-guarded `pcie_enumerate.c`.

- **xdmapcie targets** (auboard, vcu118, uzev, zcu104, zcu106_*, zcu111, zcu208, zcu216):
  BSP includes `xdmapcie` driver, `xdmapcie.h` is available.
  Build succeeds with `xdmapcie_rc_enumerate_example.c`.

## Possible Solutions

### 1. Add axipcie driver to BSP manually
The BSP libsrc can be manually patched to include the axipcie driver.
Copy the driver source from the Vitis installation:
```
/home/jeff/Xilinx/2025.2/data/embeddedsw/XilinxProcessorIPLib/drivers/axipcie_v3_4/
```
into the BSP libsrc, then add the necessary xparameters.h definitions. This could
potentially be automated in `build-vitis.py` as a post-BSP-creation step.

### 2. Use xdmapcie driver instead
The AXI Bridge for PCI Express Gen3 IP might be compatible with the `xdmapcie`
driver. Try:
- Adding `xdmapcie` driver to BSP manually
- Using `xdmapcie_rc_enumerate_example.c` as the source file
- Adding appropriate `XPAR_XXDMAPCIE_*` definitions to xparameters.h

### 3. Write bare-metal register access code
Bypass the driver layer entirely and write direct register access code using
the base addresses already defined in xparameters.h (`XPAR_AXI_PCIE3_0_BASEADDR`).
The PCIe enumeration logic is relatively straightforward register reads/writes.

### 4. Report to AMD / check for patches
This may be a known issue with Vitis 2025.2 SDT mode. Check:
- AMD/Xilinx forums for similar reports
- Whether a newer Vitis patch addresses this
- Whether the device tree needs a `compatible` string to match the axipcie driver

### 5. Modify the Vivado design
Replace the "AXI Bridge for PCI Express Gen3" IP with the "DMA/Bridge Subsystem
for PCI Express" (XDMA) IP in the Vivado block design. The XDMA IP has proper
SDT support and the xdmapcie driver is included automatically.

## Files

- Source: `common/src/pcie_enumerate.c` (for axipcie targets)
- Source: `common/src/xdmapcie_rc_enumerate_example.c` (for xdmapcie targets)
- Config: `py/args.json` (src and src_overrides mappings)
- AMD examples: `/home/jeff/Xilinx/2025.2/data/embeddedsw/XilinxProcessorIPLib/drivers/axipcie_v3_4/examples/`
