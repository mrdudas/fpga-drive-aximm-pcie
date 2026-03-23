import re

file_path = "PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/drv_nvme.c"

with open(file_path, "r") as f:
    text = f.read()

fallback = r"""
static int nvme_map_bar_fallback(struct udevice *udev, struct nvme_dev *ndev) {
    u32 bar0 = 0, bar1 = 0;
    u64 bar_phys;
    int ret;

    printf("NVME DBG: --- Fallback Mapping ---\n");
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
        ndev->bar = NULL;
        return -ENODEV;
    }
    return 0;
}

"""

new_init_full = r"""ndev->udev = udev;
INIT_LIST_HEAD(&ndev->namespaces);

printf("NVME DBG: UCLASS initially passed ndev->bar = %p\n", ndev->bar);

if (!ndev->bar || (unsigned long)ndev->bar == 0xffffffffffffffffULL || (unsigned long)ndev->bar < 0x2000) {
printf("NVME DBG: Initially mapped BAR is invalid, calling fallback...\n");
ret = nvme_map_bar_fallback(udev, ndev);
if (ret) {
printf("Error: Fallback BAR mapping failed (%d)\n", ret);
goto free_nvme;
}
}

/* Extra safety guard */
if (!ndev->bar || (unsigned long)ndev->bar < 0x2000 || (unsigned long)ndev->bar == 0xffffffffffffffffULL) {
printf("NVME DBG: FATAL - address is zero or invalid! Aborting to prevent SError!\n");
ret = -ENODEV;
goto free_nvme;
}

printf("NVME DBG: -> Attempting readl csts at BAR %p ...\n", ndev->bar);

if (readl(&ndev->bar->csts) == -1) {"""

if "nvme_map_bar_fallback" not in text:
    text = text.replace("int nvme_init(struct udevice *udev)", fallback + "int nvme_init(struct udevice *udev)")

# Find the exact text block and slice it!
match_regex = re.compile(r"ndev->udev = udev;[\s\S]*?if \(readl\(&ndev->bar->csts\) == -1\) \{")
match = match_regex.search(text)
if match:
    # use Python slice rather than re.sub to avoid escape bugs!
    start, end = match.span()
    text = text[:start] + new_init_full + text[end:]
    
    with open(file_path, "w") as f:
        f.write(text)
    print("Success")
else:
    print("Failed to match")
