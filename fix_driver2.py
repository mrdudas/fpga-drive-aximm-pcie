import re
import os

file_path = "PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/drv_nvme.c"

with open(file_path, "r") as f:
    text = f.read()

orig_init_match = re.compile(r'\s*ndev->udev = udev;\s*INIT_LIST_HEAD\(&ndev->namespaces\);\s*if \(readl\(&ndev->bar->csts\) == -1\) \{')

new_init_code = """
ndev->udev = udev;
INIT_LIST_HEAD(&ndev->namespaces);

printf("\\n\\n================================\\n");
printf("NVME DBG: ENTERING nvme_init for %s\\n", udev->name);
printf("NVME DBG: UCLASS passed ndev->bar = %p\\n", ndev->bar);

{
u32 bar0 = 0, bar1 = 0;
u64 bar_phys;
dm_pci_read_config32(udev, 0x10, &bar0);
dm_pci_read_config32(udev, 0x14, &bar1);
printf("NVME DBG: Raw PCIe config space read: BAR0=0x%08x, BAR1=0x%08x\\n", bar0, bar1);

bar_phys = bar0 & 0xfffffff0;
if ((bar0 & 0x6) == 4) bar_phys |= ((u64)bar1 << 32);
printf("NVME DBG: Derived Physical Address from Config Space = 0x%llx\\n", bar_phys);

if (bar_phys == 0 || bar_phys == 0xfffffff0 || ((bar_phys & 0xff000000ULL) == 0)) {
printf("NVME DBG: Device physically unmapped! Cannot proceed safely.\\n");
ndev->bar = NULL; // Force fallback OR fail
}
}

if (!ndev->bar || (unsigned long)ndev->bar == 0xffffffffffffffffULL || (unsigned long)ndev->bar < 0x2000) {
printf("NVME DBG: Calling fallback...\\n");
ret = nvme_map_bar_fallback(udev, ndev);
if (ret) {
printf("Error: BAR mapping is NULL and fallback failed (%d)\\n", ret);
goto free_nvme;
}
}

printf("NVME DBG: Final chosen ndev->bar = %p\\n", ndev->bar);

/* Extra safety guard */
if (!ndev->bar || (unsigned long)ndev->bar < 0x2000 || (unsigned long)ndev->bar == 0xffffffffffffffffULL) {
printf("NVME DBG: FATAL - address is zero or invalid! Aborting to prevent SError!\\n");
ret = -ENODEV;
goto free_nvme;
}

printf("NVME DBG: -> Attempting readl csts...\\n");
u32 csts_val = readl(&ndev->bar->csts);
printf("NVME DBG: -> readl returned 0x%08x\\n", csts_val);

if (csts_val == -1) {"""

if orig_init_match.search(text):
    text = orig_init_match.sub(new_init_code, text)
    print("MATCHED AND REPLACED!")
else:
    print("NOT MATCHED!")

with open(file_path, "w") as f:
    f.write(text)

