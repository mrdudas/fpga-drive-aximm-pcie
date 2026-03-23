#!/usr/bin/env bash
set -euo pipefail

TARGET="${1:-vck190_fmcp1}"
JOBS="${JOBS:-2}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/" && pwd)"
PETL_ROOT="${REPO_DIR}/PetaLinux"
PETL_PROJ="${PETL_ROOT}/${TARGET}"
XSA_PATH="${REPO_DIR}/Vivado/${TARGET}/fpgadrv_wrapper.xsa"


# Fontos: a setenv ne vigye el a script paramétereit
ORIG_ARGS=("$@")
set --
source "${REPO_DIR}/setenv"
set -- "${ORIG_ARGS[@]}"

# egy argument, ami  azt mondja meg, hogy clean all vagy csak simán forgassa újfa default false.
CLEAN_ALL=false
if [[ "${1:-}" == "clean" ]]; then
  CLEAN_ALL=true
fi

if [[ -z "${PETALINUX:-}" ]]; then
  echo "PETALINUX nincs beállítva"
  echo "Példa: source /tools/Xilinx/petalinux/2024.2/settings.sh"
  exit 1
fi

if [[ ! -f "${XSA_PATH}" ]]; then
  echo "XSA nem található: ${XSA_PATH}"
  exit 1
fi

if [[ ! -d "${PETL_PROJ}" ]]; then
  echo "PetaLinux projekt nem található: ${PETL_PROJ}"
  exit 1
fi

cd "${PETL_PROJ}"

echo "HW import indul: ${XSA_PATH}"
petalinux-config --get-hw-description "${XSA_PATH}" --silentconfig

echo "Kernel clean..."
if [[ "${CLEAN_ALL}" == true ]]; then
  petalinux-build -c kernel -x cleanall
fi

echo "Kernel + DT újrafordítás..."

petalinux-build -c device-tree
petalinux-build -c kernel 

echo "Image újragenerálás (inkrementális)..."
petalinux-build

echo
echo "Kész."
echo "Ellenőrizd az időbélyeget itt:"
echo "  ${PETL_PROJ}/images/linux/image.ub"
echo "  ${PETL_PROJ}/images/linux/system.dtb"