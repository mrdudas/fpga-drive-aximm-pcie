import re

file_path = "PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/drv_nvme.c"

with open(file_path, "r") as f:
    text = f.read()

# Instead of multiline strings, carefully insert safely
insert_fallback = r"""
static int nvme_map_bar_fallback(struct udevice *udev, struct nvme_dev *ndev)
{
u32 bar0 = 0, bar1 = 0;
u64 bar_phys;
int ret;
printf("NVME DBG: --- Fallback Mapping --- \n");
ret = dm_pci_read_config32(udev, 0x10, &bar0);
if (ret) return ret;
if (bar0 == 0 || bar0 == 0xffffffff) return -ENODEV;
if (bar0 & 0x1) return -ENOTSUPP;
bar_phys = (u64)(bar0 & 0xfffffff0);
if ((bar0 & 0x6) == 4) {
ret = dm_pci_read_config32(udev, 0x14, &bar1);
if (ret) return ret;
bar_phys |= ((u64)bar1 << 32);
}
printf("NVME DBG: Fallback Bar Phys Address: 0x%llx\n", bar_phys);
if (bar_phys == 0 || bar_phys > 0x1000000000ULL || (bar_phys & 0xff000000ULL) == 0) {
printf("NVME DBG: Rejected phys address! %llx\n", bar_phys);
return -ENODEV;
}
ndev->bar = map_physmem(bar_phys, 0x2000, MAP_NOCACHE);
printf("NVME DBG: Fallback map_physmem returned ndev->bar = %p\n", ndev->bar);
if (!ndev->bar || (unsigned long)ndev->bar == 0xffffffffffffffffULL || (unsigned long)ndev->bar < 0x2000) {
printf("Error: NVMe map_physmem invalid! %p \n", ndev->bar);
return -ENODEV;
}
return 0;
}
"""

if "nvme_map_bar_fallback" not in text:
    text = text.replace("int nvme_init(struct udevice *udev)", insert_fallback + "\nint nvme_init(struct udevice *udev)")

insert_init = r"""ndev->udev = udev;
INIT_LIST_HEAD(&ndev->namespaces);

printf("\n\n================================\n");
printf("NVME DBG: ENTERING nvme_init for %s\n", udev->name);
printf("NVME DBG: UCLASS passed ndev->bar = %p\n", ndev->bar);

{
u32 bar0 = 0, bar1 = 0;
u64 bar_phys;
dm_pci_read_config32(udev, 0x10, &bar0);
dm_pci_read_config32(udev, 0x14, &bar1);
printf("NVME DBG: Raw PCIe config space read: BAR0=0x%08x, BAR1=0x%08x\n", bar0, bar1);
bar_phys = bar0 & 0xfffffff0;
if ((bar0 & 0x6) == 4) bar_phys |= ((u64)bar1 << 32);
printf("NVME DBG: Derived Physical Address from Config Space = 0x%llx\n", bar_phys);
if (bar_phys == 0 || bar_phys == 0xfffffff0 || ((bar_phys & 0xff000000ULL) == 0)) {
printf("NVME DBG: Device physically unmapped! Cannot proceed safely.\n");
ndev->bar = NULL;
}
}

if (!ndev->bar || (unsigned long)ndev->bar == 0xffffffffffffffffULL || (unsigned long)ndev->bar < 0x2000) {
printf("NVME DBG: Calling fallback...\n");
ret = nvme_map_bar_fallback(udev, ndev);
if (ret) {
printf("Error: BAR mapping is NULL and fallback failed (%d)\n", ret);
goto free_nvme;
}
}

printf("NVME DBG: Final chosen ndev->bar = %p\n", ndev->bar);
if (!ndev->bar || (unsigned long)ndev->bar < 0x2000 || (unsigned long)ndev->bar == 0xffffffffffffffffULL) {
printf("NVME DBG: FATAL - address is zero or invalid! Aborting to prevent SError!\n");
ret = -ENODEV;
goto free_nvme;
}

printf("NVME DBG: -> Attempting readl csts...\n");
u32 csts_val = readl(&ndev->bar->csts);
printf("NVME DBG: -> readl returned 0x%08x\n", csts_val);

if (csts_val == -1) {"""

# Replace inside nvme_init
find_str = """ndev->udev = udev;
INIT_LIST_HEAD(&ndev->namespaces);
if (readl(&ndev->bar->csts) == -1) {"""

if find_str in text:
    text = text.replace(find_str, insert_init)
    print("Replaced nvme_init successfully.")
else:
    print("Could not find orig_init_match in text!")

with open(file_path, "w") as f:
    f.write(text)

