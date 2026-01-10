#!/bin/bash
# Run this on the remote host  X.X.X.X

# Create custom VM images directory
mkdir -p /home/thoth/vm-images
chmod 755 /home/thoth/vm-images

# Add user to libvirt groups
sudo usermod -a -G libvirt thoth
sudo usermod -a -G kvm thoth
sudo usermod -a -G libvirt-qemu thoth

# Restart libvirtd
sudo systemctl restart libvirtd

# Set proper permissions on default directory (if you have sudo)
sudo mkdir -p /var/lib/libvirt/images
sudo chown libvirt-qemu:kvm /var/lib/libvirt/images
sudo chmod 775 /var/lib/libvirt/images

echo "Setup complete! Log out and back in for group changes to take effect"