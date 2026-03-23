import sys
with open('PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/drv_nvme.c', 'r') as f:
    text = f.read()

import re

# Fix the broken map_physmem check
text = text.replace("""if (!ndev->bar || ndev->bar == (void *)0xffffffffffffffffULL) {
printf("Error: NVMe probe provided an invalid pointer! \\n");
return -ENODEV;
}

if (0) {""", """if (!ndev->bar) {""")

# Add massive debug before readl in nvme_init
target = """if (readl(&ndev->bar->csts) == -1) {"""

replacement = """printf("NVME DBG: ------- NVME INIT DEBUG --------\\n");
printf("NVME DBG: ndev->bar pointer = %p\\n", ndev->bar);
printf("NVME DBG: udev->name = %s\\n", udev->name);

if (!ndev->bar || (unsigned long)ndev->bar < 0x1000) {
printf("NVME DBG: CRITICAL ERROR! ndev->bar is NULL or suspiciously low! Aborting to prevent SError!\\n");
ret = -ENODEV;
goto free_nvme;
}

if ((unsigned long)ndev->bar == 0xffffffffffffffffULL) {
printf("NVME DBG: CRITICAL ERROR! ndev->bar is -1! Aborting!\\n");
ret = -ENODEV;
goto free_nvme;
}

printf("NVME DBG: About to readl from %p...\\n", &ndev->bar->csts);

/* Actually do the read cautiously if possible, but U-Boot has no trap handling so we just pray */
u32 csts_val = readl(&ndev->bar->csts);
printf("NVME DBG: csts_val = 0x%08x\\n", csts_val);

if (csts_val == -1) {"""

if target in text:
    text = text.replace(target, replacement)
    print("Patched nvme_init readl")
else:
    print("Could not find Target in text")

with open('PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/drv_nvme.c', 'w') as f:
    f.write(text)

