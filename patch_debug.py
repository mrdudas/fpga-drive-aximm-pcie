file_path = "PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/drv_nvme.c"
with open(file_path, "r") as f:
    text = f.read()

# Replace the beginning of nvme_init to include robust debugging
start_str = "int nvme_init(struct udevice *udev)\n{\n\tstruct nvme_dev *ndev = dev_get_priv(udev);\n\tstruct nvme_id_ns *id;\n\tint ret;\n\n\tndev->udev = udev;\n\tINIT_LIST_HEAD(&ndev->namespaces);"

debug_init = """int nvme_init(struct udevice *udev)
{
struct nvme_dev *ndev = dev_get_priv(udev);
struct nvme_id_ns *id;
int ret;

printf("\\n\\n================================\\n");
printf("NVME DBG: ENTERING nvme_init for %s\\n", udev->name);
printf("NVME DBG: ndev->bar initially = %p\\n", ndev->bar);

ndev->udev = udev;
INIT_LIST_HEAD(&ndev->namespaces);

/* Deep check of BARs */
{
u32 bar0 = 0, bar1 = 0;
u64 bar_phys;
dm_pci_read_config32(udev, 0x10, &bar0);
dm_pci_read_config32(udev, 0x14, &bar1);
printf("NVME DBG: Raw PCIe config space read: BAR0=0x%08x, BAR1=0x%08x\\n", bar0, bar1);

bar_phys = bar0 & 0xfffffff0;
if ((bar0 & 0x6) == 4) bar_phys |= ((u64)bar1 << 32);
printf("NVME DBG: Derived Physical Address from Config Space = 0x%llx\\n", bar_phys);
}

if (!ndev->bar || (unsigned long)ndev->bar == 0xffffffffffffffffULL) {
printf("NVME DBG: ndev->bar is invalid (%p), using fallback map\\n", ndev->bar);
ret = nvme_map_bar_fallback(udev, ndev);
printf("NVME DBG: nvme_map_bar_fallback returned %d, new ndev->bar=%p\\n", ret, ndev->bar);
if (ret) {
printf("Error: BAR mapping is NULL and fallback failed (%d)\\n", ret);
goto free_nvme;
}
} else {
printf("NVME DBG: ndev->bar was already valid: %p\\n", ndev->bar);
}

printf("NVME DBG: Final ndev->bar to be used: %p\\n", ndev->bar);

if ((unsigned long)ndev->bar < 0x10000 || (unsigned long)ndev->bar == 0xffffffffffffffffULL || (unsigned long)ndev->bar == 0xfffffffff0ULL) {
printf("NVME DBG: ABORTING!!! ndev->bar %p is unsafe, would cause SError!\\n", ndev->bar);
ret = -ENODEV;
goto free_nvme;
}

/* Also do a safe check on the physical address range for Versal QDMA window (0xa8000000 - 0xbfffffff) */
if (((unsigned long)ndev->bar & 0xff000000) == 0) {
printf("NVME DBG: ABORTING!!! ndev->bar %p seems like a generic bad address!\\n", ndev->bar);
ret = -ENODEV;
goto free_nvme;
}

printf("NVME DBG: Attempting cautious readl(&ndev->bar->csts), address=%p\\n", &ndev->bar->csts);
"""

# Let's clean up the previous manual patches first by reverting to the original file and patching that.
