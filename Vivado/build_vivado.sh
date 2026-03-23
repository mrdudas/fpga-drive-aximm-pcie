#!/usr/bin/env bash
set -euo pipefail

TARGET="${1:-vck190_fmcp1}"
JOBS="${JOBS:-2}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
VIVADO_DIR="${REPO_DIR}/Vivado"

source ./setenv

if [[ -z "${XILINX_VIVADO:-}" ]]; then
  echo "XILINX_VIVADO nincs beállítva"
  echo "Példa:"
  echo "  source /tools/Xilinx/Vivado/2024.2/settings64.sh"
  exit 1
fi

cd "${VIVADO_DIR}"

echo "Teljes Vivado rebuild indul: ${TARGET}"
make clean TARGET="${TARGET}"
make xsa TARGET="${TARGET}" JOBS="${JOBS}"

echo
echo "Kesz:"
echo "  XSA: ${VIVADO_DIR}/${TARGET}/fpgadrv_wrapper.xsa"