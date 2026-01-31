#!/bin/bash
# install_novnc.sh - Install and configure noVNC for VM console access

set -e

echo "====================================="
echo "noVNC Installation Script"
echo "====================================="

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root (sudo)"
    exit 1
fi

# Install dependencies
echo "[1/5] Installing dependencies..."
apt-get update -qq
apt-get install -y git python3 python3-pip python3-numpy websockify

# Clone noVNC
echo "[2/5] Cloning noVNC..."
cd /opt
if [ -d "noVNC" ]; then
    echo "noVNC directory exists, updating..."
    cd noVNC
    git pull
else
    git clone https://github.com/novnc/noVNC.git
    cd noVNC
fi

# Create systemd service
echo "[3/5] Creating systemd service..."
cat > /etc/systemd/system/novnc.service << 'EOF'
[Unit]
Description=noVNC WebSocket Proxy
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/noVNC
ExecStart=/opt/noVNC/utils/novnc_proxy --vnc localhost:5900 --listen 6080
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF

# Configure firewall (if UFW is active)
echo "[4/5] Configuring firewall..."
if command -v ufw &> /dev/null && ufw status | grep -q active; then
    ufw allow 6080/tcp
    ufw allow 5900:5999/tcp
    echo "UFW rules added"
elif command -v firewall-cmd &> /dev/null; then
    firewall-cmd --permanent --add-port=6080/tcp
    firewall-cmd --permanent --add-port=5900-5999/tcp
    firewall-cmd --reload
    echo "Firewalld rules added"
fi

# Start and enable service
echo "[5/5] Starting noVNC service..."
systemctl daemon-reload
systemctl enable novnc
systemctl start novnc

# Check status
sleep 2
if systemctl is-active --quiet novnc; then
    echo ""
    echo "✅ noVNC installed successfully!"
    echo ""
    echo "Access VNC console at: http://YOUR_SERVER_IP:6080"
    echo ""
    echo "Service management:"
    echo "  Start:   systemctl start novnc"
    echo "  Stop:    systemctl stop novnc"
    echo "  Status:  systemctl status novnc"
    echo "  Logs:    journalctl -u novnc -f"
else
    echo "❌ Failed to start noVNC service"
    systemctl status novnc
    exit 1
fi