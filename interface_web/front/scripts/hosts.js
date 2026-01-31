// ==========================================
// HOST MANAGEMENT (Admin Only)
// ==========================================

async function loadHostsManagement() {
    if (!authService.isAdmin()) {
        showToast('⛔ Admin access required', 'error');
        return;
    }
    
    const hostsList = document.getElementById('hosts-list');
    if (!hostsList) return;
    
    hostsList.innerHTML = '<p class="loading-text">Loading hosts...</p>';
    
    try {
        const data = await window.fetchAPI('/hosts/stats');
        
        if (data.success && data.hosts) {
            renderHosts(data.hosts);
        } else {
            hostsList.innerHTML = '<p class="error-text">Failed to load hosts</p>';
        }
    } catch (error) {
        hostsList.innerHTML = `<p class="error-text">Error: ${error.message}</p>`;
        showToast(`Error: ${error.message}`, 'error');
    }
}

function renderHosts(hosts) {
    const hostsList = document.getElementById('hosts-list');
    if (!hostsList) return;
    
    hostsList.innerHTML = '';
    
    if (hosts.length === 0) {
        hostsList.innerHTML = '<p>No hosts configured</p>';
        return;
    }
    
    hosts.forEach(host => {
        hostsList.appendChild(createHostCard(host));
    });
}

function createHostCard(host) {
    const card = document.createElement('div');
    card.className = 'host-card';
    
    const cpuPercent = ((host.totalCPUs - host.availableCPUs) / host.totalCPUs * 100).toFixed(1);
    const memPercent = host.memoryUsage.toFixed(1);
    const diskPercent = ((host.totalDisk - host.availableDisk) / host.totalDisk * 100).toFixed(1);
    
    card.innerHTML = `
        <div class="host-card-header">
            <div>
                <h3>🖥️ ${host.hostname}</h3>
                <p class="host-uri">${host.uri}</p>
            </div>
            <span class="status-badge ${host.active ? 'status-active' : 'status-inactive'}">
                ${host.active ? '🟢 Active' : '🔴 Inactive'}
            </span>
        </div>
        
        <div class="host-stats-grid">
            <div class="host-stat">
                <div class="stat-header">
                    <span>💻 CPU</span>
                    <span>${cpuPercent}%</span>
                </div>
                <div class="progress-bar">
                    <div class="progress-fill" style="width: ${cpuPercent}%"></div>
                </div>
                <small>${host.totalCPUs - host.availableCPUs} / ${host.totalCPUs} used</small>
            </div>
            
            <div class="host-stat">
                <div class="stat-header">
                    <span>💾 Memory</span>
                    <span>${memPercent}%</span>
                </div>
                <div class="progress-bar">
                    <div class="progress-fill" style="width: ${memPercent}%"></div>
                </div>
                <small>${(host.totalMemory - host.availableMemory) / 1024} / ${host.totalMemory / 1024} MB</small>
            </div>
            
            <div class="host-stat">
                <div class="stat-header">
                    <span>💿 Disk</span>
                    <span>${diskPercent}%</span>
                </div>
                <div class="progress-bar">
                    <div class="progress-fill" style="width: ${diskPercent}%"></div>
                </div>
                <small>${host.availableDisk} GB free</small>
            </div>
            
            <div class="host-stat">
                <div class="stat-header">
                    <span>🖥️ VMs</span>
                    <span>${host.activeVMs}</span>
                </div>
                <p class="host-vm-count">${host.activeVMs} active / ${host.totalVMs} total</p>
            </div>
        </div>
        
        <div class="host-actions">
            <button class="btn btn-sm btn-secondary" onclick="refreshHost('${host.id}')">
                🔄 Refresh
            </button>
            <button class="btn btn-sm btn-danger" onclick="removeHost('${host.id}')">
                🗑️ Remove
            </button>
        </div>
    `;
    
    return card;
}

function showAddHostModal() {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'add-host-modal';
    
    modal.innerHTML = `
        <div class="modal">
            <div class="modal-header">
                <h3>➕ Add New Host</h3>
                <button class="modal-close" onclick="closeAddHostModal()">✖</button>
            </div>
            <form onsubmit="addHost(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Connection URI *</label>
                        <input type="text" id="host-uri" required 
                               placeholder="qemu+ssh://user@hostname/system">
                        <small>Format: qemu+ssh://user@host/system or qemu:///system (local)</small>
                    </div>
                    
                    <div class="info-banner">
                        <span class="info-icon">ℹ️</span>
                        <div>
                            <p><strong>Requirements:</strong></p>
                            <ul style="margin: 5px 0; padding-left: 20px;">
                                <li>SSH key-based authentication configured</li>
                                <li>libvirt installed on target host</li>
                                <li>Proper permissions for VM management</li>
                            </ul>
                        </div>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeAddHostModal()">Cancel</button>
                    <button type="submit" class="btn btn-primary">Add Host</button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeAddHostModal() {
    const modal = document.getElementById('add-host-modal');
    if (modal) modal.remove();
}

async function addHost(event) {
    event.preventDefault();
    
    const uri = document.getElementById('host-uri').value;
    
    closeAddHostModal();
    showToast('Adding host...', 'info');
    
    try {
        const result = await window.fetchAPI('/hosts', {
            method: 'POST',
            body: JSON.stringify({ uri })
        });
        
        if (result.success) {
            showToast('✅ Host added successfully', 'success');
            await loadHostsManagement();
        } else {
            showToast(`❌ ${result.error || 'Failed to add host'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function refreshHost(hostId) {
    showToast('Refreshing host stats...', 'info');
    await loadHostsManagement();
}

async function removeHost(hostId) {
    if (!confirm(`Remove host ${hostId}?\n\nVMs on this host will become unmanageable.`)) {
        return;
    }
    
    try {
        showToast('Removing host...', 'info');
        await window.fetchAPI(`/hosts/${hostId}`, { method: 'DELETE' });
        showToast('✅ Host removed', 'success');
        await loadHostsManagement();
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function showHostStrategyModal() {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'strategy-modal';
    
    modal.innerHTML = `
        <div class="modal">
            <div class="modal-header">
                <h3>⚙️ Host Selection Strategy</h3>
                <button class="modal-close" onclick="closeHostStrategyModal()">✖</button>
            </div>
            <form onsubmit="updateHostStrategy(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Selection Strategy *</label>
                        <select id="strategy-select" required>
                            <option value="0">Least Used - Most free resources</option>
                            <option value="1">Round Robin - Distribute evenly</option>
                            <option value="2">Best Fit - Minimize waste</option>
                        </select>
                    </div>
                    
                    <div class="info-banner">
                        <span class="info-icon">ℹ️</span>
                        <div>
                            <p><strong>Strategies:</strong></p>
                            <ul style="margin: 5px 0; padding-left: 20px;">
                                <li><strong>Least Used:</strong> Select host with most available resources</li>
                                <li><strong>Round Robin:</strong> Cycle through hosts evenly</li>
                                <li><strong>Best Fit:</strong> Select host that best matches requirements</li>
                            </ul>
                        </div>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeHostStrategyModal()">Cancel</button>
                    <button type="submit" class="btn btn-primary">Update Strategy</button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeHostStrategyModal() {
    const modal = document.getElementById('strategy-modal');
    if (modal) modal.remove();
}

async function updateHostStrategy(event) {
    event.preventDefault();
    
    const strategy = parseInt(document.getElementById('strategy-select').value);
    
    closeHostStrategyModal();
    showToast('Updating strategy...', 'info');
    
    try {
        const result = await window.fetchAPI('/hosts/strategy', {
            method: 'PUT',
            body: JSON.stringify({ HostSelectionStrategy: strategy })
        });
        
        if (result.success) {
            showToast('✅ Strategy updated', 'success');
        } else {
            showToast('❌ Failed to update strategy', 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

// Export
window.loadHostsManagement = loadHostsManagement;
window.showAddHostModal = showAddHostModal;
window.closeAddHostModal = closeAddHostModal;
window.addHost = addHost;
window.refreshHost = refreshHost;
window.removeHost = removeHost;
window.showHostStrategyModal = showHostStrategyModal;
window.closeHostStrategyModal = closeHostStrategyModal;
window.updateHostStrategy = updateHostStrategy;