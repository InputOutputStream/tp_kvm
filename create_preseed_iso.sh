# #!/bin/bash
# # Usage: sudo ./create_preseed_iso.sh \         
# #   /home/thot/Desktop/TP_KVM/iso/ubuntu-22.04.5-live-server-amd64.iso \
# #   /mnt/iso \
# #   /home/thot/ubuntu-preseed \
# #   /home/thot/Desktop/TP_KVM/presseed.cfg \
# #   /home/thot/Desktop/TP_KVM/ubuntu-preseed.iso

# ISO_FILE=$1
# MOUNT_POINT=$2
# DEST_POINT=$3
# ABS_PATH_TO_PRESEED=$4
# ISO_NAME=$5

# # 1. Monter l'ISO originale
# mkdir -p "$MOUNT_POINT"
# sudo mount -o loop "$ISO_FILE" "$MOUNT_POINT"

# # 2. Copier le contenu dans un dossier de travail
# mkdir -p "$DEST_POINT"
# cp -r "$MOUNT_POINT"/* "$DEST_POINT"

# # 3. Copier le fichier preseed.cfg dans l'ISO
# cp "$ABS_PATH_TO_PRESEED" "$DEST_POINT/preseed.cfg"

# # 4. Modifier le menu GRUB pour inclure le preseed (boot automatique)
# GRUB_CFG="$DEST_POINT/boot/grub/grub.cfg"
# if ! grep -q "file=/cdrom/preseed.cfg" "$GRUB_CFG"; then
#     sed -i '/set default=0/a\
#     linux /casper/vmlinuz file=/cdrom/preseed.cfg auto=true priority=critical quiet splash ---
#     ' "$GRUB_CFG"
# fi

# # 5. Créer la nouvelle ISO preseeded (AVEC -joliet-long)
# # cd "$DEST_POINT" || exit
# # mkisofs -D -r -V "Ubuntu Preseed" -cache-inodes -J -joliet-long -l \
# # -b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 \
# # -boot-info-table -o "$ISO_NAME" .

# # # 6. Nettoyage
# # sudo umount "$MOUNT_POINT"
# # echo "ISO preseeded générée : $ISO_NAME"


#!/bin/bash
# Usage: ./create_preseed_iso.sh ubuntu-24.04-live-server-amd64.iso /mnt/iso ~/ubuntu-preseed ./preseed.cfg ubuntu-preseed.iso

ISO_FILE=$1
MOUNT_POINT=$2
DEST_POINT=$3
ABS_PATH_TO_PRESEED=$4
ISO_NAME=$5

# 1. Monter l'ISO originale
mkdir -p "$MOUNT_POINT"
sudo mount -o loop "$ISO_FILE" "$MOUNT_POINT"

# 2. Copier le contenu dans un dossier de travail
mkdir -p "$DEST_POINT"
cp -r "$MOUNT_POINT"/* "$DEST_POINT"

# 3. Copier le fichier preseed.cfg dans l'ISO
cp "$ABS_PATH_TO_PRESEED" "$DEST_POINT/preseed.cfg"

# 4. Modifier le menu GRUB pour inclure le preseed (boot automatique)
GRUB_CFG="$DEST_POINT/boot/grub/grub.cfg"
if ! grep -q "file=/cdrom/preseed.cfg" "$GRUB_CFG"; then
    sed -i '/set default=0/a\
    linux /casper/vmlinuz file=/cdrom/preseed.cfg auto=true priority=critical quiet splash ---
    ' "$GRUB_CFG"
fi

# 5. Créer la nouvelle ISO preseeded avec xorriso
cd "$DEST_POINT" || exit

# Extraire MBR du fichier ISO original
dd if="$ISO_FILE" bs=1 count=446 of=/tmp/isohdpfx.bin 2>/dev/null

# Détecter le chemin de l'image EFI
EFI_IMG=""
if [ -f "boot/grub/efi.img" ]; then
    EFI_IMG="boot/grub/efi.img"
elif [ -f "EFI/BOOT/BOOTX64.EFI" ]; then
    # Pas d'image EFI séparée, utiliser approche alternative
    EFI_IMG=""
fi

# Créer ISO avec ou sans boot EFI selon disponibilité
if [ -n "$EFI_IMG" ]; then
    echo "Creating ISO with UEFI support using $EFI_IMG..."
    xorriso -as mkisofs \
      -r -V "Ubuntu_Preseed" \
      -o "$ISO_NAME" \
      -J -joliet-long \
      -b boot/grub/i386-pc/eltorito.img \
      -c boot.catalog \
      -no-emul-boot -boot-load-size 4 -boot-info-table \
      -eltorito-alt-boot \
      -e "$EFI_IMG" \
      -no-emul-boot \
      -isohybrid-mbr /tmp/isohdpfx.bin \
      -isohybrid-gpt-basdat \
      .
else
    echo "Creating ISO with BIOS-only boot (no EFI image found)..."
    xorriso -as mkisofs \
      -r -V "Ubuntu_Preseed" \
      -o "$ISO_NAME" \
      -J -joliet-long \
      -b boot/grub/i386-pc/eltorito.img \
      -c boot.catalog \
      -no-emul-boot -boot-load-size 4 -boot-info-table \
      -isohybrid-mbr /tmp/isohdpfx.bin \
      .
fi

# 6. Nettoyage
sudo umount "$MOUNT_POINT"
rm -f /tmp/isohdpfx.bin
echo "ISO preseeded générée : $ISO_NAME"