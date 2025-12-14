#!/bin/bash

# Ajouter l'utilisateur au groupe libvirt
sudo usermod -a -G libvirt $USER

# Redémarrer libvirtd
sudo systemctl restart libvirtd

# Permissions correctes pour /var/lib/libvirt/images/
sudo chown -R root:libvirt /var/lib/libvirt/images/
sudo chmod 775 /var/lib/libvirt/images/  # 775 au lieu de 755 !
sudo chmod g+s /var/lib/libvirt/images/   # SetGID pour hériter du groupe

# Vérifier
sudo ls -la /var/lib/libvirt/images/

