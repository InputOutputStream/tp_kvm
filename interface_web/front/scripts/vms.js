
const API_URL = 'http://localhost:3000/api';
let currentVM = null;
let statsInterval = null;

// Fetch API Wrapper
async function fetchAPI(endpoint, options = {}) {
    try {
        const response = await fetch(`${API_URL}${endpoint}`, options);
        const contentType = response.headers.get('content-type');
        
        if (contentType && contentType.includes('application/json')) {
            const data = await response.json();
            
            if (!data.success && response.status >= 400) {
                throw new Error(data.error || 'An error occurred');
            }
            
            return data;
        } else {
            throw new Error('Non-JSON response from server');
        }
    } catch (error) {
        if (error.message.includes('Failed to fetch') || error.message.includes('ERR_CONNECTION_REFUSED')) {
            console.warn('Backend server not available:', error.message);
            throw new Error('Backend server unavailable');
        }
        
        showToast(error.message, 'error');
        throw error;
    }
}

// Load VMs
async function loadVMs() {
    const vmsList = document.getElementById('vms-list');
    if (!vmsList) return;
    
    vmsList.innerHTML = '<p class="loading-text">Loading virtual machines...</p>';
    
    try {
        const data = await fetchAPI('/vms');
        
        if (data.vms.length === 0) {
            vmsList.innerHTML = '<p class="loading-text">No VMs found</p>';
            return;
        }
        
        const runningCount = data.vms.filter(vm => vm.running).length;
        document.getElementById('running-count').textContent = runningCount;
        document.getElementById('total-count').textContent = data.vms.length;
        
        const vmCountBadge = document.getElementById('vm-count-badge');
        if (vmCountBadge) {
            vmCountBadge.textContent = data.vms.length;
        }
        
        vmsList.innerHTML = '';
        data.vms.forEach(vm => {
            const vmCard = createVMCard(vm);
            vmsList.appendChild(vmCard);
        });
    } catch (error) {
        vmsList.innerHTML = '<p class="loading-text">Error loading VMs</p>';
    }
}

// Create VM Card Element
function createVMCard(vm) {
    const card = document.createElement('div');
    card.className = 'vm-card';
    card.onclick = () => selectVM(vm.name);
    
    const statusClass = vm.running ? 'running' : 'stopped';
    const statusText = vm.running ? '🟢 Running' : '🔴 Stopped';
    
    let statsHTML = '';
    if (vm.stats) {
        statsHTML = `
            <div class="vm-card-stats">
                <div class="vm-stat-item">
                    <strong>CPU:</strong> ${vm.stats.cpu}%
                </div>
                <div class="vm-stat-item">
                    <strong>Memory:</strong> ${vm.stats.memory.percent}%
                </div>
            </div>
        `;
    }
    
    card.innerHTML = `
        <div class="vm-card-header">
            <div>
                <h3 class="vm-card-title">🖥️ ${vm.name}</h3>
                <div class="vm-card-meta">
                    <span>State: ${vm.state}</span>
                </div>
            </div>
            <span class="vm-card-status ${statusClass}">${statusText}</span>
        </div>
        ${statsHTML}
    `;
    
    return card;
}

// Select VM and Show Details
async function selectVM(vmName) {
    currentVM = vmName;
    
    const panel = document.getElementById('vm-details-panel');
    const panelTitle = document.getElementById('vm-panel-title');
    
    if (panel) {
        panel.classList.add('open');
    }
    
    if (panelTitle) {
        panelTitle.textContent = `VM: ${vmName}`;
    }
    
    if (statsInterval) {
        clearInterval(statsInterval);
    }
    
    await loadVMInfo();
    await loadSnapshots();
    
    const statusData = await fetchAPI(`/vms/${currentVM}/status`);
    if (statusData.running) {
        document.getElementById('vm-stats').style.display = 'grid';
        startStatsUpdate();
    } else {
        document.getElementById('vm-stats').style.display = 'none';
    }
}

// Load VM Info
async function loadVMInfo() {
    const vmInfo = document.getElementById('vm-info');
    if (!vmInfo) return;
    
    vmInfo.innerHTML = '<p>Loading information...</p>';
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}`);
        vmInfo.textContent = data.info;
    } catch (error) {
        vmInfo.innerHTML = '<p>Error loading information</p>';
    }
}

// Start Stats Update
function startStatsUpdate() {
    updateStats();
    statsInterval = setInterval(updateStats, 3000);
}

// Update Stats
async function updateStats() {
    if (!currentVM) return;
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}/stats`);
        const stats = data.stats;
        
        if (!stats) return;
        
        document.getElementById('cpu-usage').textContent = `${stats.cpu}%`;
        document.getElementById('memory-usage').textContent = 
            `${stats.memory.used} KB / ${stats.memory.max} KB (${stats.memory.percent}%)`;
        document.getElementById('disk-io').textContent = 
            `R: ${stats.disk.readMB} MB | W: ${stats.disk.writeMB} MB`;
        document.getElementById('network-io').textContent = 
            `RX: ${stats.network.rxMB} MB | TX: ${stats.network.txMB} MB`;
    } catch (error) {
        console.error('Error updating stats:', error);
    }
}

// VM Actions
async function startVM() {
    if (!currentVM) return;
    
    try {
        showToast('Starting VM...', 'info');
        await fetchAPI(`/vms/${currentVM}/start`, { method: 'POST' });
        showToast('✅ VM started successfully', 'success');
        await loadVMInfo();
        await loadVMs();
        document.getElementById('vm-stats').style.display = 'grid';
        startStatsUpdate();
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

async function shutdownVM() {
    if (!currentVM) return;
    
    if (!confirm(`Are you sure you want to shut down ${currentVM}?`)) {
        return;
    }
    
    try {
        showToast('Shutting down VM...', 'info');
        await fetchAPI(`/vms/${currentVM}/shutdown`, { method: 'POST' });
        showToast('✅ VM shut down successfully', 'success');
        await loadVMInfo();
        await loadVMs();
        document.getElementById('vm-stats').style.display = 'none';
        if (statsInterval) clearInterval(statsInterval);
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

async function rebootVM() {
    if (!currentVM) return;
    
    if (!confirm(`Reboot ${currentVM}?`)) {
        return;
    }
    
    try {
        showToast('Rebooting VM...', 'info');
        await fetchAPI(`/vms/${currentVM}/reboot`, { method: 'POST' });
        showToast('✅ VM rebooted', 'success');
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

async function pauseVM() {
    if (!currentVM) return;
    
    try {
        showToast('Pausing VM...', 'info');
        await fetchAPI(`/vms/${currentVM}/pause`, { method: 'POST' });
        showToast('✅ VM paused', 'success');
        await loadVMInfo();
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

async function resumeVM() {
    if (!currentVM) return;
    
    try {
        showToast('Resuming VM...', 'info');
        await fetchAPI(`/vms/${currentVM}/resume`, { method: 'POST' });
        showToast('✅ VM resumed', 'success');
        await loadVMInfo();
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

// Load Snapshots
async function loadSnapshots() {
    const snapshotsList = document.getElementById('snapshots-list');
    if (!snapshotsList) return;
    
    snapshotsList.innerHTML = '<p>Loading snapshots...</p>';
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}/snapshots`);
        
        if (data.snapshots.length === 0) {
            snapshotsList.innerHTML = '<p>No snapshots found</p>';
            return;
        }
        
        snapshotsList.innerHTML = '';
        data.snapshots.forEach(snapshot => {
            const snapshotItem = createSnapshotItem(snapshot);
            snapshotsList.appendChild(snapshotItem);
        });
    } catch (error) {
        snapshotsList.innerHTML = '<p>Error loading snapshots</p>';
    }
}

// Create Snapshot Item
function createSnapshotItem(snapshot) {
    const item = document.createElement('div');
    item.className = 'snapshot-item';
    
    item.innerHTML = `
        <div class="snapshot-info">
            <h4>📸 ${snapshot.name}</h4>
            <p>🕒 ${snapshot.creationTime}</p>
        </div>
        <div class="snapshot-actions">
            <button class="btn btn-sm btn-primary" onclick="revertSnapshot('${snapshot.name}')">
                ↩️ Restore
            </button>
            <button class="btn btn-sm btn-danger" onclick="deleteSnapshot('${snapshot.name}')">
                🗑️
            </button>
        </div>
    `;
    
    return item;
}

// Revert Snapshot
async function revertSnapshot(snapshotName) {
    if (!confirm(`Restore snapshot "${snapshotName}"?\n\n⚠️ VM will be stopped if running.`)) {
        return;
    }
    
    try {
        showToast('Restoring snapshot...', 'info');
        await fetchAPI(`/vms/${currentVM}/snapshots/${snapshotName}/revert`, {
            method: 'POST'
        });
        
        showToast('✅ Snapshot restored successfully', 'success');
        await loadVMInfo();
        await loadVMs();
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

// Delete Snapshot
async function deleteSnapshot(snapshotName) {
    if (!confirm(`Delete snapshot "${snapshotName}" permanently?`)) {
        return;
    }
    
    try {
        showToast('Deleting snapshot...', 'info');
        await fetchAPI(`/vms/${currentVM}/snapshots/${snapshotName}`, {
            method: 'DELETE'
        });
        
        showToast('✅ Snapshot deleted', 'success');
        await loadSnapshots();
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

// VM Search Filter
const vmSearchInput = document.getElementById('vm-search');
if (vmSearchInput) {
    vmSearchInput.addEventListener('input', debounce((e) => {
        const searchTerm = e.target.value.toLowerCase();
        const vmCards = document.querySelectorAll('#vms-list .vm-card');
        
        vmCards.forEach(card => {
            const vmName = card.querySelector('.vm-card-title').textContent.toLowerCase();
            if (vmName.includes(searchTerm)) {
                card.style.display = '';
            } else {
                card.style.display = 'none';
            }
        });
    }, 300));
}

// VM Status Filter
const filterChips = document.querySelectorAll('.filter-chips .chip');
filterChips.forEach(chip => {
    chip.addEventListener('click', () => {
        // Update active chip
        filterChips.forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        
        const filter = chip.dataset.filter;
        const vmCards = document.querySelectorAll('#vms-list .vm-card');
        
        vmCards.forEach(card => {
            if (filter === 'all') {
                card.style.display = '';
            } else {
                const status = card.querySelector('.vm-card-status');
                if (status && status.classList.contains(filter)) {
                    card.style.display = '';
                } else {
                    card.style.display = 'none';
                }
            }
        });
    });
});