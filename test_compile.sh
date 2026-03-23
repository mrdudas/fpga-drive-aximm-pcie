#!/bin/bash
cd PetaLinux/vck190_fmcp1
petalinux-build -c u-boot
echo $? > ../../build_result.txt
