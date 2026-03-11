FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI:append = " file://bsp.cfg"
SRC_URI:append = " file://Fidus_Versal_QDMA_2GB_Limit_NVMe_Driver_2024_2.patch"
KERNEL_FEATURES:append = " bsp.cfg"
