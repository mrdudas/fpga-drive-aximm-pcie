import re

file_path = "PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/drv_nvme.c"

with open(file_path, "r") as f:
    orig_text = f.read()

# Let's find the start of nvme_map_bar_fallback and replace everything up to the end of nvme_init's start

# Split text
start_fallback = orig_text.find("static int nvme_map_bar_fallback(struct udevice *udev, struct nvme_dev *ndev)")
if start_fallback == -1:
    print("Could not find nvme_map_bar_fallback")
    exit(1)

end_fallback = orig_text.find("int nvme_init(struct udevice *udev)")

# Let's just find exactly what we need.
# nvme_init goes until: ndev->queues = malloc(NVME_Q_NUM * sizeof(struct nvme_queue *));
end_init = orig_text.find("ndev->queues = malloc(NVME_Q_NUM * sizeof(struct nvme_queue *));")

if end_init == -1:
    print("Could not find end of nvme_init")
    exit(1)

new_fallback = """static int nvme_map_bar_fallback(struct udevice *udev, struct nvme_dev *ndev)
{
u32 bar0 = 0;
u32 bar1 = 0;
u64 bar_phys;
int ret;

printf("NVME DBG: --- Fallback Mapping --- \\n");

ret = dm_pci_read_config32(udev, PCI_BASE_ADDRESS_0, &bar0);
if (ret)
return ret;

printf("NVME DBG: Fallback read BAR0: 0x%08x\\n", bar0);

if (bar0 == 0 || bar0 == 0xffffffff)
return -ENODEV;

if (bar0 & PCI_BASE_ADDRESS_SPACE)
return -ENOTSUPP;

bar_phys = (u64)(bar0 & PCI_BASE_ADDRESS_MEM_MASK);

if ((bar0 & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64) {
ret = dm_pci_read_config32(udev, PCI_BASE_ADDRESS_1, &bar1);
if (ret)
return ret;
printf("NVME DBG: Fallback read BAR1: 0x%08x\\n", bar1);
bar_phys |= ((u64)bar1 << 32);
}

printf("NVME DBG: Fallback Bar Phys Address: 0x%llx\\n", bar_phys);
if (bar_phys == 0 || bar_phys > 0x1000000000ULL || (bar_phys & 0xff000000ULL) == 0) {
printf("NVME DBG: Rejected phys address! %llx\\n", bar_phys);
return -ENODEV;
}

ndev->bar = map_physmem(bar_phys, 0x2000, MAP_NOCACHE);
printf("NVME DBG: Fallback map_physmem returned ndev->bar = %p\\n", ndev->bar);

if (!ndev->bar || (unsigned long)ndev->bar == 0xffffffffffffffffULL || (unsigned long)ndev->bar < 0x2000) {
printf("Error: NVMe map_physmem invalid! %p \\n", ndev->bar);
return -ENODEV;
}

return 0;
}
"""

new_init = """int nvme_init(struct udevice *udev)
{
struct nvme_dev *ndev = dev_get_priv(udev);
struct nvme_id_ns *id;
int ret;

printf("\\n\\n================================\\n");
printf("NVME DBG: ENTERING nvme_init for %s\\n", udev->name);
printf("NVME DBG: UCLASS passed ndev->bar = %p\\n", ndev->bar);

ndev->udev = udev;
INIT_LIST_HEAD(&ndev->namespaces);

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

if (csts_val == -1) {
ret = -ENODEV;
printf("Error: %s: controller register access failed (BAR=%p)\\n", udev->name, ndev->bar);
goto free_nvme;
}

/* continue existing block */
"""

# Replace the text piecewise.
# Wait, let's just make the script rebuild the file text exactly.
# It's safer to use regex substitution if possible, but manual slicing is ok.

pre_fallback_text = orig_text[:start_fallback]
# Find where nvme_map_bar_fallback ends:
# It's right before: static int nvme_setup_io_queues
next_func = orig_text.find("static int nvme_setup_io_queues", start_fallback)
mid_text = orig_text[next_func:end_fallback] # this might contain other functions between fallback and init!
if mid_text.strip() == "":
    # Fallback and init are adjacent? No, fallback was at line 32. Init is at line 937.
    pass

# So let's replace FALLBACK specifically
end_of_fallback = orig_text.find("}", start_fallback)
# Actually, the fallback has multiple braces. Let's find "return 0;" followed by "}" for fallback.
fallback_return = orig_text.find("return 0;\n}", start_fallback)
if fallback_return != -1:
    end_of_fallback = fallback_return + 10
else:
    # Just search for the next function which is static int nvme_setup_prps
    end_of_fallback = orig_text.find("static int nvme_setup_prps", start_fallback)

# Similarly, replace INIT
end_of_init_intro = orig_text.find("ndev->queues = malloc(NVME_Q_NUM * sizeof(struct nvme_queue *));")

text1 = orig_text[:start_fallback] + new_fallback + "\n" + orig_text[end_of_fallback:end_fallback] + new_init + orig_text[end_of_init_intro:]

with open(file_path, "w") as f:
    f.write(text1)

print("SUCCESS")
