#!/bin/bash
# SD kártya írása VCK190 PetaLinux 2024.2 képpel
# Használat: sudo ./write_sd.sh [sd_eszköz]  pl.: sudo ./write_sd.sh sdb

set -e

IMAGES_DIR="PetaLinux/vck190_fmcp1/images/linux"
SD_DEV="${1:-sdb}"
SD_PATH="/dev/$SD_DEV"

# --- Ellenőrzések ---
if [[ $EUID -ne 0 ]]; then
    echo "HIBA: Root jogosultság szükséges. Futtasd: sudo $0 $SD_DEV"
    exit 1
fi

if [[ ! -b "$SD_PATH" ]]; then
    echo "HIBA: Nem található blokk-eszköz: $SD_PATH"
    exit 1
fi

# Megakadályozzuk a rendszerdiszk törlését
for part in "$SD_PATH"?*; do
    mount_point=$(lsblk -no MOUNTPOINT "$part" 2>/dev/null || true)
    if echo "$mount_point" | grep -qE '^/$|^/boot|^/mnt/home|^/home$'; then
        echo "HIBA: $part rendszer mountponton van ($mount_point) – megszakítva!"
        exit 1
    fi
done

SD_SIZE=$(lsblk -bno SIZE "$SD_PATH" | head -1)
SD_GB=$(echo "scale=1; $SD_SIZE/1024/1024/1024" | bc)
echo "SD kártya: $SD_PATH  (${SD_GB} GB)"
echo ""
echo "Képfájlok:"
ls -lh "$IMAGES_DIR/BOOT.BIN" "$IMAGES_DIR/image.ub" "$IMAGES_DIR/boot.scr" "$IMAGES_DIR/rootfs.ext4"
echo ""
read -r -p "Biztos törli az SD kártyát és felírja az új képet? [i/N] " confirm
[[ "${confirm,,}" == "i" ]] || { echo "Megszakítva."; exit 0; }

# --- Unmount ---
echo ""
echo "[1/5] Unmount..."
for part in "$SD_PATH"?*; do
    if mount | grep -q "$part"; then
        echo "  umount $part"
        umount "$part" || true
    fi
done
sleep 1

# --- Particionálás ---
echo "[2/5] Particionálás (FAT32 boot + ext4 rootfs)..."
# Méret: boot = 500MB fix, többi = rootfs
TOTAL_SECTORS=$(blockdev --getsz "$SD_PATH")
SECTOR_SIZE=512
BOOT_SECTORS=$((500 * 1024 * 1024 / SECTOR_SIZE))   # 500 MB

parted -s "$SD_PATH" mklabel msdos
parted -s "$SD_PATH" mkpart primary fat32 4MiB 504MiB
parted -s "$SD_PATH" mkpart primary ext4 504MiB 100%
parted -s "$SD_PATH" set 1 boot on

# Kernel frissítse a partíció táblát
partprobe "$SD_PATH" 2>/dev/null || true
sleep 2

# Partíció neve (p1/p2 MMC-nél, 1/2 USB/SD-nél)
if [[ -b "${SD_PATH}p1" ]]; then
    PART1="${SD_PATH}p1"
    PART2="${SD_PATH}p2"
else
    PART1="${SD_PATH}1"
    PART2="${SD_PATH}2"
fi

# --- Formázás ---
echo "[3/5] Formázás..."
mkfs.vfat -F 32 -n BOOT "$PART1"
mkfs.ext4 -F -L rootfs "$PART2"

# --- Boot partíció feltöltése ---
echo "[4/5] Boot fájlok írása (BOOT.BIN, image.ub, boot.scr)..."
BOOT_MNT=$(mktemp -d)
mount "$PART1" "$BOOT_MNT"
cp "$IMAGES_DIR/BOOT.BIN"   "$BOOT_MNT/"
cp "$IMAGES_DIR/image.ub"   "$BOOT_MNT/"
cp "$IMAGES_DIR/boot.scr"   "$BOOT_MNT/"
sync
umount "$BOOT_MNT"
rmdir "$BOOT_MNT"

# --- Rootfs ---
echo "[5/5] Rootfs írása (rootfs.ext4 dd)..."
dd if="$IMAGES_DIR/rootfs.ext4" of="$PART2" bs=4M status=progress
sync
# Fájlrendszer méret kiterjesztése a teljes partícióra
e2fsck -f -y "$PART2" || true
resize2fs "$PART2"
sync

echo ""
echo "Kész! Az SD kártya ($SD_PATH) bootolható VCK190 PetaLinux 2024.2 képpe van."
