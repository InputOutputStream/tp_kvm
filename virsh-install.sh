#!/bin/bash
# Script de gestion de VM avec virsh
# Usage: ./virsh-script.sh <operation>

OPERATION=$1
VM_NAME="vm-cloud-4107"
V_CPU=2
DISK=20
MEMORY=2048
OS_PATH="./iso/ubuntu-22.04.5-live-server-amd64.iso"
OS_VARIANT="ubuntu22.04"
VM_IP="192.168.122.173" 

check_vm_exists() {
    if ! sudo virsh list --all | grep -q "$VM_NAME"; then
        echo "VM '$VM_NAME' n'existe pas"
        return 1
    fi
    return 0
}

check_vm_running() {
    if sudo virsh list --state-running | grep -q "$VM_NAME"; then
        return 0  # Running
    else
        return 1  # Not running
    fi
}

# Afficher le menu si aucune opération n'est spécifiée
if [ -z "$OPERATION" ]; then
    echo "=========================================="
    echo "   Script de gestion VM avec virsh"
    echo "=========================================="
    echo "1 - Créer la VM"
    echo "2 - Gérer les snapshots (avant et après Apache)"
    echo "3 - Cloner la VM"
    echo "4 - Restaurer le snapshot avant-apache"
    echo "=========================================="
    echo "Usage: $0 <numéro_opération>"
    exit 1
fi

# ============================================
# OPÉRATION 1: Création de la VM
# ============================================
if [ "$OPERATION" -eq 1 ]; then
    echo "Création de la VM '$VM_NAME'..."
    
    if check_vm_exists; then
        echo "La VM '$VM_NAME' existe déjà"
        read -p "Voulez-vous la supprimer et la recréer? (o/N): " confirm
        if [ "$confirm" = "o" ] || [ "$confirm" = "O" ]; then
            sudo virsh destroy "$VM_NAME" 2>/dev/null
            sudo virsh undefine "$VM_NAME" --remove-all-storage
            echo "VM supprimée"
        else
            exit 0
        fi
    fi
    
    if [ ! -f "$OS_PATH" ]; then
        echo "Fichier ISO introuvable: $OS_PATH"
        exit 1
    fi
    
    sudo virt-install \
        --name "$VM_NAME" \
        --vcpus "$V_CPU" \
        --memory "$MEMORY" \
        --disk size="$DISK" \
        --cdrom "$OS_PATH" \
        --os-variant "$OS_VARIANT" \
    
    echo "VM créée avec succès"
    echo "Connectez-vous avec: sudo virsh console $VM_NAME ou avec ssh user_name@$VM_IP"

# ============================================
# OPÉRATION 2: Gestion des snapshots
# ============================================
elif [ "$OPERATION" -eq 2 ]; then
    echo "Gestion des snapshots pour '$VM_NAME'..."
    
    if ! check_vm_exists; then
        exit 1
    fi
    
    if ! check_vm_running; then
        echo "La VM n'est pas en cours d'exécution"
        $SLEEP_TIME=20
        read -p "Voulez-vous la démarrer? (o/N): " confirm
        if [ "$confirm" = "o" ] || [ "$confirm" = "O" ]; then
            sudo virsh start "$VM_NAME"
            echo "Attente du démarrage...20 secondes"
            sleep $SLEEP_TIME
        else
            exit 1
        fi
    fi
    
    # Créer le premier snapshot (avant Apache)
    echo "Création du snapshot 'avant-apache'..."
    sudo virsh snapshot-create-as "$VM_NAME" \
        --name "avant-apache" \
        --description "VM propre avant installation Apache"
    
    if [ $? -eq 0 ]; then
        echo "Snapshot 'avant-apache' créé"
    else
        echo "Erreur lors de la création du snapshot"
        exit 1
    fi
    


    # Fadila ca c'est pour question reponse ca ne doit pas etre ici

    # Scénario pour les snapshots
    # echo ""
    # echo "SCÉNARIO D'UTILISATION DES SNAPSHOTS:"
    # echo "Lors du déploiement d'un algorithme pour un système de recommandation,"
    # echo "il est nécessaire de créer un snapshot pour pouvoir restaurer aisément"
    # echo "l'état du système en cas d'erreur. Cela assure la disponibilité du"
    # echo "service en cas de problème."
    # echo ""
    
    # Instructions pour Apache
    echo "1. Connectez-vous à la VM: sudo virsh console $VM_NAME"
    echo "2. Installez Apache:"
    echo "   sudo apt update"
    echo "   sudo apt install apache2 -y"
    echo "3. Testez Apache:"
    echo "   curl localhost"
    echo "   systemctl status apache2"
    echo ""
    
    read -p "Appuyez sur ENTRÉE après avoir installé et testé Apache..." 
    
    # Créer le second snapshot (après Apache)
    echo "Création du snapshot 'avec-apache'..."
    sudo virsh snapshot-create-as "$VM_NAME" \
        --name "avec-apache" \
        --description "VM avec Apache installé et fonctionnel"
    
    if [ $? -eq 0 ]; then
        echo "Snapshot 'avec-apache' créé"
    fi
    
    # Lister les snapshots
    echo ""
    echo "Liste des snapshots:"
    sudo virsh snapshot-list "$VM_NAME"

# ============================================
# OPÉRATION 3: Cloner la VM
# ============================================
elif [ "$OPERATION" -eq 3 ]; then
    echo "Clonage de la VM '$VM_NAME'..."
    
    if ! check_vm_exists; then
        exit 1
    fi
    
    # Vérifier si le clone existe déjà
    if sudo virsh list --all | grep -q "${VM_NAME}-clone"; then
        echo "Le clone '${VM_NAME}-clone' existe déjà"
        read -p "Voulez-vous le supprimer et le recréer? (o/N): " confirm
        if [ "$confirm" = "o" ] || [ "$confirm" = "O" ]; then
            sudo virsh destroy "${VM_NAME}-clone" 2>/dev/null
            sudo virsh undefine "${VM_NAME}-clone" --remove-all-storage
            echo "Clone supprimé"
        else
            exit 0
        fi
    fi
    
    # Arrêter la VM si elle est en cours d'exécution
    if check_vm_running; then
        echo "⏹Arrêt de la VM..."
        sudo virsh shutdown "$VM_NAME"
        echo "Attente de l'arrêt complet (40 secondes)..."
        sleep 40
    fi
    
    # Vérifier que la VM est bien éteinte
    if check_vm_running; then
        echo "La VM est toujours en cours d'exécution. Arrêt forcé..."
        sudo virsh destroy "$VM_NAME"
        sleep 5
    fi
    
    # Créer le clone
    echo "Création du clone..."
    sudo virt-clone \
        --original "$VM_NAME" \
        --name "${VM_NAME}-clone" \
        --auto-clone
    
    if [ $? -eq 0 ]; then
        echo "Clone créé avec succès: ${VM_NAME}-clone"
    else
        echo "Erreur lors du clonage"
        exit 1
    fi
    
# Fadila c'est pour les question reponses

    # Informations sur ce qu'il faut modifier
    # echo ""
    # echo "QU'EST-CE QU'UN CLONE?"
    # echo "Un clone est une copie complète et indépendante d'une VM."
    # echo "C'est une nouvelle VM distincte avec son propre disque dur virtuel."
    # echo ""
    # echo "SCÉNARIOS D'UTILISATION:"
    # echo "- Environnements multiples (dev, test, staging)"
    # echo "- Tests en parallèle de différentes configurations"
    # echo "- Déploiement rapide de serveurs identiques"
    # echo "- Formation (une VM par étudiant)"
    # echo ""
    # echo "PARAMÈTRES À MODIFIER DANS LE CLONE:"
    # echo "1. Adresse MAC (réseau) - virt-clone le fait automatiquement ✅"
    # echo "2. Hostname - Pour identifier facilement chaque machine"
    # echo "3. Adresse IP statique (si configurée)"
    # echo "4. SSH host keys - Sécurité (éviter man-in-the-middle)"
    # echo "5. machine-id - Identification système unique"
    # echo ""
    # echo "COMMANDES À EXÉCUTER DANS LE CLONE:"
    # echo "sudo virsh start ${VM_NAME}-clone"
    # echo "sudo virsh console ${VM_NAME}-clone"
    # echo ""
    # echo "Puis dans la VM:"
    # echo "  sudo hostnamectl set-hostname vm-clone-4107"
    # echo "  sudo rm /etc/machine-id"
    # echo "  sudo systemd-machine-id-setup"
    # echo "  sudo rm /etc/ssh/ssh_host_*"
    # echo "  sudo dpkg-reconfigure openssh-server"
    # echo "  sudo reboot"

# ============================================
# OPÉRATION 4: Restaurer le snapshot avant-apache
# ============================================
elif [ "$OPERATION" -eq 4 ]; then
    echo "Restauration du snapshot 'avant-apache'..."
    
    if ! check_vm_exists; then
        exit 1
    fi
    
    # Vérifier que le snapshot existe
    if ! sudo virsh snapshot-list "$VM_NAME" | grep -q "avant-apache"; then
        echo "Le snapshot 'avant-apache' n'existe pas"
        echo "Snapshots disponibles:"
        sudo virsh snapshot-list "$VM_NAME"
        exit 1
    fi
    
    # Arrêter la VM si nécessaire
    if check_vm_running; then
        echo "Arrêt de la VM..."
        sudo virsh shutdown "$VM_NAME"
        echo "Attente de l'arrêt (30 secondes)..."
        sleep 40
        
        if check_vm_running; then
            echo "Arrêt forcé de la VM..."
            sudo virsh destroy "$VM_NAME"
            sleep 5
        fi
    fi
    
    # Restaurer le snapshot
    echo "Restauration du snapshot 'avant-apache'..."
    sudo virsh snapshot-revert "$VM_NAME" "avant-apache"
    
    if [ $? -eq 0 ]; then
        echo "Snapshot restauré avec succès"
    else
        echo "Erreur lors de la restauration"
        exit 1
    fi
    
    # Démarrer la VM
    echo "Démarrage de la VM..."
    sudo virsh start "$VM_NAME"
    echo "Attente du démarrage (20 secondes)..."
    sleep 20
    
    # Vérifier qu'Apache n'est plus disponible
    echo ""
    echo "Vérification qu'Apache n'est plus installé..."
    echo "Connectez-vous à la VM pour vérifier:"
    echo "   sudo virsh console $VM_NAME"
    echo ""
    echo "Puis exécutez:"
    echo "   systemctl status apache2"
    echo "   (Devrait afficher: apache2.service could not be found)"
    echo ""
    
else
    echo "Opération invalide: $OPERATION"
    echo "Opérations disponibles: 1, 2, 3, 4"
    exit 1
fi

echo ""
echo "Opération $OPERATION terminée"