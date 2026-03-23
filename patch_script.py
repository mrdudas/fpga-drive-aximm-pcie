import sys

file_path = "PetaLinux/vck190_fmcp1/project-spec/meta-user/recipes-bsp/u-boot/files/opsero_qdma_dm_pci.c"

with open(file_path, "r") as f:
    text = f.read()

replacement = """printf("QDMA DM PCI: host ready\\n");

{
struct pci_controller *hose = dev_get_uclass_priv(dev);
if (hose) {
            /* Fix U-Boot device tree parsing by adding memory regions manually */
hose->region_count = 0;
/* NP Mem */
pci_set_region(hose->regions + hose->region_count++,
       host->np_mem_base, host->np_mem_base,
       host->np_mem_max - host->np_mem_base + 1,
       PCI_REGION_MEM);
/* P Mem */
pci_set_region(hose->regions + hose->region_count++,
       host->p_mem_base, host->p_mem_base,
       host->p_mem_max - host->p_mem_base + 1,
       PCI_REGION_MEM | PCI_REGION_PREFETCH);
/* Sys Mem mapped 1:1 */
pci_set_region(hose->regions + hose->region_count++,
       0, 0,
       0x80000000,
       PCI_REGION_MEM | PCI_REGION_SYS_MEMORY);

printf("QDMA DM PCI: Populated %d regions manually\\n", hose->region_count);
}
}

return 0;"""

new_text = text.replace('printf("QDMA DM PCI: host ready\\n");\n\nreturn 0;', replacement)

if new_text != text:
    with open(file_path, "w") as f:
        f.write(new_text)
    print("Patched successfully.")
else:
    print("Pattern not found! Check file contents.")
