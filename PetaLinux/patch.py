import os
content = """--- a/drivers/pci/controller/pcie-xilinx-dma-pl.c
+++ b/drivers/pci/controller/pcie-xilinx-dma-pl.c
@@ -711,7 +711,7 @@
 \t\treturn port->msi.irq_msi0;
 \t}
 \tret = devm_request_irq(dev, port->msi.irq_msi0, xilinx_pl_dma_pcie_msi_handler_low,
-\t\t\t       IRQF_SHARED | IRQF_NO_THREAD | IRQF_NO_AUTOEN,
+\t\t\t       IRQF_NO_THREAD | IRQF_NO_AUTOEN,
 \t\t\t       "xlnx-pcie-dma-pl", port);
 \tif (ret) {
 \t\tdev_err(dev, "Failed to register interrupt\\n");
@@ -723,7 +723,7 @@
 \t\treturn port->msi.irq_msi1;
 \t}
 \tret = devm_request_irq(dev, port->msi.irq_msi1, xilinx_pl_dma_pcie_msi_handler_high,
-\t\t\t       IRQF_SHARED | IRQF_NO_THREAD | IRQF_NO_AUTOEN,
+\t\t\t       IRQF_NO_THREAD | IRQF_NO_AUTOEN,
 \t\t\t       "xlnx-pcie-dma-pl", port);
 \tif (ret) {
 \t\tdev_err(dev, "Failed to register interrupt\\n");
"""
path = '/mnt/home/pesa101/FPGADEV/fpga-drive-aximm-pcie/PetaLinux/vck190_modern_plnx/project-spec/meta-user/recipes-kernel/linux/files/0001-fix-pcie-xilinx-dma-pl-irq-flags.patch'
os.makedirs(os.path.dirname(path), exist_ok=True)
with open(path, 'w') as f:
    f.write(content)
