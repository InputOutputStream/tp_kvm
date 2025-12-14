#!/bin/bash

echo "Installation des packages système..."
sudo apt update
sudo apt install -y qemu-kvm libvirt-daemon-system libvirt-clients bridge-utils virt-manager virtinst cloud-init genisoimage qemu-utils

# Configuration libvirt
echo "Configuration de libvirt..."
CURRENT_USER=$(whoami)
sudo usermod -aG libvirt,kvm $CURRENT_USER
sudo systemctl enable --now libvirtd
sudo systemctl start libvirtd

# Configuration sudo
echo "Configuration des permissions sudo..."
echo "$CURRENT_USER ALL=(ALL) NOPASSWD: /usr/bin/virsh, /usr/bin/virt-clone, /usr/bin/virt-install, /usr/bin/qemu-img" | sudo tee /etc/sudoers.d/vm-manager
sudo chmod 440 /etc/sudoers.d/vm-manager

# Installation npm
echo "Installation des dépendances npm..."
npm install

# Création du répertoire images
sudo mkdir -p /var/lib/libvirt/images
sudo chown root:libvirt /var/lib/libvirt/images
sudo chmod 770 /var/lib/libvirt/images

echo ""
echo "Installation terminée!"
echo ""
echo "Prochaines étapes:"
echo "1. Redémarrez votre session: newgrp libvirt"
echo "2. Lancez le serveur: nnpm run start:open"
echo "3. Ouvrez http://localhost:3000"