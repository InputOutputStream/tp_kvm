// ==========================================
// VNC CONSOLE MANAGEMENT
// ==========================================

const VNCConsole = {
    currentVM: null,
    vncWindow: null,
    
    async openConsole(vmName) {
        try {
            showToast('🖥️ Connecting to VM console...', 'info');
            
            const data = await window.fetchAPI(`/vms/${vmName}/vnc`);
            
            if (!data.success) {
                if (data.error.includes('not running')) {
                    showToast('⚠️ VM must be running to access console', 'warning');
                } else if (data.error.includes('not configured')) {
                    // VNC not enabled, ask to enable it
                    if (confirm('VNC is not enabled for this VM. Enable it now?\n\n⚠️ VM will be restarted.')) {
                        await this.enableVNC(vmName);
                        return;
                    }
                } else {
                    showToast(`❌ ${data.error}`, 'error');
                }
                return;
            }
            
            // Open VNC console in new window
            const consoleUrl = data.novncUrl;
            const windowFeatures = 'width=1024,height=768,menubar=no,toolbar=no,location=no,status=no';
            
            this.vncWindow = window.open(consoleUrl, `vnc_${vmName}`, windowFeatures);
            this.currentVM = vmName;
            
            if (this.vncWindow) {
                showToast('✅ Console opened in new window', 'success');
            } else {
                showToast('⚠️ Popup blocked. Please allow popups for this site.', 'warning');
                // Show the URL so user can open manually
                this.showConsoleModal(vmName, consoleUrl, data);
            }
            
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    showConsoleModal(vmName, consoleUrl, vncData) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'vnc-console-modal';
        
        modal.innerHTML = `
            <div class="modal modal-large">
                <div class="modal-header">
                    <h3>🖥️ VNC Console - ${vmName}</h3>
                    <button class="modal-close" onclick="VNCConsole.closeModal()">✖</button>
                </div>
                <div class="modal-body">
                    <div class="info-banner">
                        <span class="info-icon">ℹ️</span>
                        <div>
                            <p><strong>Console Access</strong></p>
                            <p>VNC Port: ${vncData.vncPort}</p>
                            ${vncData.hasPassword ? '<p>⚠️ Password required (see VM details)</p>' : ''}
                        </div>
                    </div>
                    
                    <div class="console-url-section">
                        <label>Console URL:</label>
                        <div class="url-copy-box">
                            <input type="text" value="${consoleUrl}" readonly id="console-url-input">
                            <button class="btn btn-secondary" onclick="VNCConsole.copyConsoleUrl()">
                                📋 Copy
                            </button>
                        </div>
                    </div>
                    
                    <div class="console-embed" id="vnc-embed">
                        <iframe src="${consoleUrl}" 
                                style="width: 100%; height: 600px; border: 1px solid #ddd; border-radius: 4px;">
                        </iframe>
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-primary" onclick="window.open('${consoleUrl}', '_blank')">
                        🔗 Open in New Window
                    </button>
                    <button class="btn btn-secondary" onclick="VNCConsole.closeModal()">
                        Close
                    </button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
    },
    
    async enableVNC(vmName, password = '') {
        try {
            showToast('Enabling VNC...', 'info');
            
            const result = await window.fetchAPI(`/vms/${vmName}/vnc/enable`, {
                method: 'POST',
                body: JSON.stringify({ password })
            });
            
            if (result.success) {
                showToast('✅ VNC enabled successfully', 'success');
                
                if (result.restarted) {
                    showToast('⏳ VM restarted. Waiting for boot...', 'info');
                    await new Promise(resolve => setTimeout(resolve, 5000));
                }
                
                // Try opening console again
                await this.openConsole(vmName);
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
            
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    copyConsoleUrl() {
        const input = document.getElementById('console-url-input');
        if (input) {
            input.select();
            document.execCommand('copy');
            showToast('✅ Console URL copied!', 'success');
        }
    },
    
    closeModal() {
        const modal = document.getElementById('vnc-console-modal');
        if (modal) modal.remove();
    },
    
    async checkNoVNCStatus() {
        try {
            const data = await window.fetchAPI('/vnc/status');
            
            if (!data.novncRunning) {
                console.warn('noVNC service is not running');
                return false;
            }
            
            return true;
        } catch (error) {
            console.error('Error checking noVNC status:', error);
            return false;
        }
    }
};

// ==========================================
// PORT FORWARDING & NETWORK ACCESS
// ==========================================

const NetworkAccess = {
    async getVMIP(vmName) {
        try {
            const data = await window.fetchAPI(`/vms/${vmName}/ip`);
            
            if (data.success && data.ip) {
                return data;
            }
            
            return null;
        } catch (error) {
            console.error('Error getting VM IP:', error);
            return null;
        }
    },
    
    async showNetworkInfo(vmName) {
        try {
            const ipData = await this.getVMIP(vmName);
            
            if (!ipData) {
                showToast('❌ Could not retrieve network information', 'error');
                return;
            }
            
            const modal = document.createElement('div');
            modal.className = 'modal-overlay active';
            modal.id = 'network-info-modal';
            
            let interfacesHTML = '';
            if (ipData.interfaces && ipData.interfaces.length > 0) {
                interfacesHTML = ipData.interfaces.map(iface => `
                    <div class="network-interface">
                        <h4>${iface.name}</h4>
                        <div class="interface-details">
                            <div><strong>IP:</strong> <code>${iface.ip || 'N/A'}</code></div>
                            <div><strong>MAC:</strong> <code>${iface.mac || 'N/A'}</code></div>
                            <div><strong>Network:</strong> ${iface.network || 'N/A'}</div>
                        </div>
                    </div>
                `).join('');
            }
            
            modal.innerHTML = `
                <div class="modal">
                    <div class="modal-header">
                        <h3>🌐 Network Information - ${vmName}</h3>
                        <button class="modal-close" onclick="NetworkAccess.closeModal()">✖</button>
                    </div>
                    <div class="modal-body">
                        <div class="network-summary">
                            <div class="info-item">
                                <strong>Primary IP:</strong>
                                <code>${ipData.primaryIP || 'Not available'}</code>
                            </div>
                        </div>
                        
                        ${interfacesHTML}
                        
                        <div class="port-forwarding-section">
                            <h4>Port Forwarding</h4>
                            <button class="btn btn-primary" onclick="NetworkAccess.showAddForwardModal('${vmName}')">
                                ➕ Add Port Forward
                            </button>
                            <div id="forwards-list-${vmName}"></div>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button class="btn btn-secondary" onclick="NetworkAccess.closeModal()">Close</button>
                    </div>
                </div>
            `;
            
            document.body.appendChild(modal);
            
            // Load existing port forwards
            await this.loadPortForwards(vmName);
            
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    async showAddForwardModal(vmName) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'add-forward-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>➕ Add Port Forward</h3>
                    <button class="modal-close" onclick="NetworkAccess.closeAddForwardModal()">✖</button>
                </div>
                <form onsubmit="NetworkAccess.createPortForward(event, '${vmName}')">
                    <div class="modal-body">
                        <div class="form-group">
                            <label>VM Port *</label>
                            <input type="number" id="vm-port" required min="1" max="65535" 
                                   placeholder="e.g., 80 for HTTP, 22 for SSH">
                        </div>
                        
                        <div class="form-group">
                            <label>Host Port (leave empty for auto-assign)</label>
                            <input type="number" id="host-port" min="1024" max="65535" 
                                   placeholder="Auto-assigned if empty">
                        </div>
                        
                        <div class="form-group">
                            <label>Protocol</label>
                            <select id="forward-protocol">
                                <option value="tcp">TCP</option>
                                <option value="udp">UDP</option>
                            </select>
                        </div>
                        
                        <div class="info-banner">
                            <span class="info-icon">ℹ️</span>
                            <p>This will create a NAT rule to forward traffic from the host to your VM.</p>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" onclick="NetworkAccess.closeAddForwardModal()">
                            Cancel
                        </button>
                        <button type="submit" class="btn btn-primary">
                            Create Forward
                        </button>
                    </div>
                </form>
            </div>
        `;
        
        document.body.appendChild(modal);
    },
    
    async createPortForward(event, vmName) {
        event.preventDefault();
        
        const vmPort = parseInt(document.getElementById('vm-port').value);
        const hostPort = parseInt(document.getElementById('host-port').value) || 0;
        const protocol = document.getElementById('forward-protocol').value;
        
        try {
            showToast('Creating port forward...', 'info');
            
            const result = await window.fetchAPI(`/vms/${vmName}/forward`, {
                method: 'POST',
                body: JSON.stringify({ vmPort, hostPort, protocol })
            });
            
            if (result.success) {
                showToast(`✅ Port forward created: ${result.hostPort} → ${vmPort}`, 'success');
                this.closeAddForwardModal();
                await this.loadPortForwards(vmName);
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
            
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    async loadPortForwards(vmName) {
        try {
            const data = await window.fetchAPI(`/vms/${vmName}/forwards`);
            const container = document.getElementById(`forwards-list-${vmName}`);
            
            if (!container) return;
            
            if (!data.success || data.forwards.length === 0) {
                container.innerHTML = '<p class="empty-state">No port forwards configured</p>';
                return;
            }
            
            container.innerHTML = data.forwards.map(fwd => `
                <div class="forward-item">
                    <div class="forward-info">
                        <strong>${fwd.hostPort}</strong> → <strong>${fwd.vmPort}</strong>
                        <span class="protocol-badge">${fwd.protocol.toUpperCase()}</span>
                    </div>
                    <button class="btn btn-sm btn-danger" 
                            onclick="NetworkAccess.deletePortForward('${vmName}', '${fwd.id}')">
                        🗑️
                    </button>
                </div>
            `).join('');
            
        } catch (error) {
            console.error('Error loading port forwards:', error);
        }
    },
    
    async deletePortForward(vmName, forwardId) {
        if (!confirm('Delete this port forward?')) return;
        
        try {
            const result = await window.fetchAPI(`/vms/${vmName}/forward/${forwardId}`, {
                method: 'DELETE'
            });
            
            if (result.success) {
                showToast('✅ Port forward deleted', 'success');
                await this.loadPortForwards(vmName);
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
            
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    closeModal() {
        const modal = document.getElementById('network-info-modal');
        if (modal) modal.remove();
    },
    
    closeAddForwardModal() {
        const modal = document.getElementById('add-forward-modal');
        if (modal) modal.remove();
    }
};

// Export functions
window.VNCConsole = VNCConsole;
window.NetworkAccess = NetworkAccess;