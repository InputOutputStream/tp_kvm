// ==========================================
// NETWORK MANAGEMENT
// ==========================================

async function loadNetworks() {
    const networksList = document.getElementById('networks-list');
    if (!networksList) return;
    
    networksList.innerHTML = '<p class="loading-text">Loading networks...</p>';
    
    try {
        const data = await window.fetchAPI('/networks');
        
        if (!data.success) {
            networksList.innerHTML = '<p class="error-text">Failed to load networks</p>';
            return;
        }
        
        renderNetworks(data);
    } catch (error) {
        networksList.innerHTML = `<p class="error-text">Error: ${error.message}</p>`;
        showToast(`Error loading networks: ${error.message}`, 'error');
    }
}

function renderNetworks(data) {
    const networksList = document.getElementById('networks-list');
    if (!networksList) return;
    
    networksList.innerHTML = '';
    
    // User Networks Section
    const userSection = document.createElement('div');
    userSection.className = 'network-section';
    
    const currentUser = authService.currentUser.username;
    const userNetworks = data.userNetworks[currentUser] || [];
    
    userSection.innerHTML = `
        <div class="section-header">
            <h3>👤 My Networks</h3>
            <button class="btn btn-primary" onclick="createUserNetwork()">
                ➕ Create Network
            </button>
        </div>
        <div class="network-info-banner">
            <span>Networks: ${userNetworks.length} / 10</span>
        </div>
        <div class="network-cards" id="user-networks"></div>
    `;
    networksList.appendChild(userSection);
    
    const userNetworksDiv = document.getElementById('user-networks');
    
    if (userNetworks.length === 0) {
        userNetworksDiv.innerHTML = '<p class="empty-state">No networks created yet. Click "Create Network" to get started.</p>';
    } else {
        userNetworks.forEach(network => {
            userNetworksDiv.appendChild(createNetworkCard(network, 'user'));
        });
    }
    
    // Swarm Networks Section (Admin only or user's own swarms)
    if (authService.isAdmin()) {
        const swarmSection = document.createElement('div');
        swarmSection.className = 'network-section';
        swarmSection.innerHTML = `
            <h3>🐳 Swarm Networks</h3>
            <div class="network-cards" id="swarm-networks"></div>
        `;
        networksList.appendChild(swarmSection);
        
        const swarmNetworksDiv = document.getElementById('swarm-networks');
        const swarmNetworks = Object.values(data.swarmNetworks || {});
        
        if (swarmNetworks.length === 0) {
            swarmNetworksDiv.innerHTML = '<p class="empty-state">No swarm networks created</p>';
        } else {
            swarmNetworks.forEach(network => {
                swarmNetworksDiv.appendChild(createNetworkCard(network, 'swarm'));
            });
        }
    }
}

function createNetworkCard(network, type) {
    const card = document.createElement('div');
    card.className = 'network-card';
    card.dataset.networkId = network.networkId;
    
    const typeIcon = type === 'user' ? '👤' : '🐳';
    const typeLabel = type === 'user' ? 'User Network' : 'Swarm Network';
    const isActive = network.active !== false;
    
    const createdDate = network.created 
        ? new Date(network.created * 1000).toLocaleDateString()
        : 'Unknown';
    
    card.innerHTML = `
        <div class="network-card-header">
            <div>
                <h4>${typeIcon} ${network.displayName || network.networkName}</h4>
                <span class="network-status ${isActive ? 'active' : 'inactive'}">
                    ${isActive ? '● Active' : '○ Inactive'}
                </span>
            </div>
            <span class="network-type-badge">${typeLabel}</span>
        </div>
        <div class="network-card-body">
            <div class="network-info-grid">
                <div class="network-info-item">
                    <strong>Network ID:</strong>
                    <code class="network-id">${network.networkId}</code>
                </div>
                <div class="network-info-item">
                    <strong>Subnet:</strong>
                    <code>${network.subnet}/24</code>
                </div>
                ${type === 'swarm' && network.clusterName ? `
                    <div class="network-info-item">
                        <strong>Cluster:</strong>
                        <span>${network.clusterName}</span>
                    </div>
                ` : ''}
                <div class="network-info-item">
                    <strong>Created:</strong>
                    <span>${createdDate}</span>
                </div>
                ${network.bridge ? `
                    <div class="network-info-item">
                        <strong>Bridge:</strong>
                        <code>${network.bridge}</code>
                    </div>
                ` : ''}
            </div>
        </div>
        ${type === 'user' ? `
            <div class="network-card-actions">
                <button class="btn btn-sm btn-secondary" onclick="renameNetwork('${network.networkId}')">
                    ✏️ Rename
                </button>
                <button class="btn btn-sm btn-danger" onclick="deleteNetwork('${network.networkId}')">
                    🗑️ Delete
                </button>
            </div>
        ` : ''}
    `;
    
    return card;
}

async function createUserNetwork() {
    const username = authService.currentUser.username;
    
    // Ask for network name
    const networkName = prompt('Enter network name (optional):');
    if (networkName === null) return; // User cancelled
    
    try {
        showToast('Creating user network...', 'info');
        
        const result = await window.fetchAPI('/networks/user', {
            method: 'POST',
            body: JSON.stringify({ 
                username,
                networkName: networkName.trim()
            })
        });
        
        if (result.success) {
            showToast(`✅ Network created: ${result.displayName}`, 'success');
            await loadNetworks();
        } else {
            showToast(`❌ ${result.error}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function deleteNetwork(networkId) {
    if (!confirm('Are you sure you want to delete this network? This action cannot be undone.')) {
        return;
    }
    
    const username = authService.currentUser.username;
    
    try {
        showToast('Deleting network...', 'info');
        
        const result = await window.fetchAPI(`/networks/user/${networkId}`, {
            method: 'DELETE',
            body: JSON.stringify({ username })
        });
        
        if (result.success) {
            showToast('✅ Network deleted successfully', 'success');
            await loadNetworks();
        } else {
            showToast(`❌ ${result.error}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function renameNetwork(networkId) {
    const newName = prompt('Enter new network name:');
    if (!newName || newName.trim() === '') return;
    
    try {
        showToast('Renaming network...', 'info');
        
        const result = await window.fetchAPI(`/networks/${networkId}`, {
            method: 'PATCH',
            body: JSON.stringify({ 
                displayName: newName.trim()
            })
        });
        
        if (result.success) {
            showToast('✅ Network renamed successfully', 'success');
            await loadNetworks();
        } else {
            showToast(`❌ ${result.error}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function loadUserNetworks() {
    const response = await fetch('/api/networks/mine', {
        headers: { 'Authorization': `Bearer ${token}` }
    });
    
    const data = await response.json();
    const select = document.getElementById('network-select');
    
    data.networks.forEach(net => {
        const option = document.createElement('option');
        option.value = net.networkName;
        option.textContent = net.displayName;
        select.appendChild(option);
    });
}

async function getNetworkInfo(networkId) {
    try {
        const result = await window.fetchAPI(`/networks/${networkId}`);
        
        if (result.success) {
            return result.network;
        } else {
            console.error('Failed to get network info:', result.error);
            return null;
        }
    } catch (error) {
        console.error('Error getting network info:', error);
        return null;
    }
}

// Export functions
window.loadUserNetworks = loadUserNetworks;
window.loadNetworks = loadNetworks;
window.createUserNetwork = createUserNetwork;
window.deleteNetwork = deleteNetwork;
window.renameNetwork = renameNetwork;
window.getNetworkInfo = getNetworkInfo;