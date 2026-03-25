FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = " file://platform-top.h file://bsp.cfg"
SRC_URI:append = " file://0001-xilinx_versal.h-ubifs-distroboot-support.patch"
SRC_URI:append = " file://opsero_qdma.c file://opsero_qdma.h file://opsero_qdma_dm_pci.c"
SRC_URI += "file://drv_nvme.c"
SRC_URI += "file://drv_nvme_show.c"
SRC_URI += "file://cmd_nvme.c"
SRC_URI += "file://api_nvme.h"
SRC_URI += "file://local_nvme.h"
SRC_URI += "file://user_2026-03-17-12-31-00.cfg \
            file://user_2026-03-17-12-43-00.cfg \
            "

do_configure:prepend() {
    if [ -f ${WORKDIR}/drv_nvme.c ]; then
        install -m 0644 ${WORKDIR}/drv_nvme.c ${S}/drivers/nvme/nvme.c
        install -m 0644 ${WORKDIR}/drv_nvme_show.c ${S}/drivers/nvme/nvme_show.c
        install -m 0644 ${WORKDIR}/cmd_nvme.c ${S}/cmd/nvme.c
        install -m 0644 ${WORKDIR}/api_nvme.h ${S}/include/nvme.h
        install -m 0644 ${WORKDIR}/local_nvme.h ${S}/drivers/nvme/nvme.h
    fi

    install -m 0644 ${WORKDIR}/opsero_qdma.c ${S}/drivers/pci/opsero_qdma.c
    install -m 0644 ${WORKDIR}/opsero_qdma.h ${S}/drivers/pci/opsero_qdma.h
    install -m 0644 ${WORKDIR}/opsero_qdma_dm_pci.c ${S}/drivers/pci/opsero_qdma_dm_pci.c

    if ! grep -q 'config OPSERO_QDMA' ${S}/drivers/pci/Kconfig; then
        cat >> ${S}/drivers/pci/Kconfig << 'EOF'

config OPSERO_QDMA
    bool "Opsero QDMA PCIe Root Complex Support"
    depends on PCI && ARCH_VERSAL
    help
      Enables support for Opsero QDMA PCIe Root Complex controllers

config OPSERO_QDMA_AUTO
    bool "Auto-initialize QDMA PCIe during boot"
    depends on OPSERO_QDMA
    help
      Automatically initialize QDMA PCIe controllers during U-Boot startup

config OPSERO_QDMA_DM_PCI
    bool "QDMA PCIe Device Model Host Controller"
    depends on OPSERO_QDMA && DM_PCI
    help
      Register QDMA PCIe controllers as Device Model PCI host controllers
      for automatic enumeration via 'pci enum' command
EOF
    fi

    if ! grep -q 'obj-$(CONFIG_OPSERO_QDMA) += opsero_qdma.o' ${S}/drivers/pci/Makefile; then
        echo 'obj-$(CONFIG_OPSERO_QDMA) += opsero_qdma.o' >> ${S}/drivers/pci/Makefile
    fi

    if ! grep -q 'obj-$(CONFIG_OPSERO_QDMA) += opsero_qdma_dm_pci.o' ${S}/drivers/pci/Makefile; then
        echo 'obj-$(CONFIG_OPSERO_QDMA) += opsero_qdma_dm_pci.o' >> ${S}/drivers/pci/Makefile
    fi

}

