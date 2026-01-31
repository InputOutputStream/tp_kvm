// ==========================================
// ENHANCED SWARM CLUSTERS MANAGEMENT
// ==========================================

async function loadSwarmClusters() {
    const clustersGrid = document.getElementById('clusters-grid');
    if (!clustersGrid) return;
    
    clustersGrid.innerHTML = '<p class="loading-text">Loading clusters...</p>';
    
    try {
        const username = authService.currentUser.username;
        const data = await window.fetchAPI(`/swarm/clusters?owner=${username}`);
        
        if (data.success && data.clusters) {
            renderSwarmClusters(data.clusters);
        } else {
            clustersGrid.innerHTML = '<p>No clusters found</p>';
        }
    } catch (error) {
        clustersGrid.innerHTML = `<p class="error-text">Error: ${error.message}</p>`;
        showToast(`Error: ${error.message}`, 'error');
    }
}

function renderSwarmClusters(clusters) {
    const clustersGrid = document.getElementById('clusters-grid');
    if (!clustersGrid) return;
    
    clustersGrid.innerHTML = '';
    
    if (clusters.length === 0) {
        clustersGrid.innerHTML = '<p>No clusters deployed yet. Create your first cluster!</p>';
        return;
    }
    
    clusters.forEach(cluster => {
        clustersGrid.appendChild(createClusterCard(cluster));
    });
}

function createClusterCard(cluster) {
    const card = document.createElement('div');
    card.className = 'cluster-card';
    
    const statusClass = cluster.status === 'ready' ? 'active' : 'inactive';
    const statusText = cluster.status === 'ready' ? '🟢 Ready' : '🟡 ' + cluster.status;
    
    card.innerHTML = `
        <div class="cluster-header">
            <h3>🐳 ${cluster.clusterName}</h3>
            <span class="status-badge status-${statusClass}">${statusText}</span>
        </div>
        
        <div class="cluster-body">
            <div class="cluster-info">
                <div class="info-item">
                    <span class="info-icon">👑</span>
                    <span>Managers: ${cluster.managers || 0}</span>
                </div>
                <div class="info-item">
                    <span class="info-icon">⚙️</span>
                    <span>Workers: ${cluster.workers || 0}</span>
                </div>
                <div class="info-item">
                    <span class="info-icon">🌐</span>
                    <span>Network: ${cluster.networkName || 'N/A'}</span>
                </div>
                <div class="info-item">
                    <span class="info-icon">📦</span>
                    <span>Subnet: ${cluster.subnet || 'N/A'}</span>
                </div>
                <div class="info-item">
                    <span class="info-icon">📅</span>
                    <span>Created: ${new Date(cluster.created * 1000).toLocaleDateString()}</span>
                </div>
            </div>
        </div>
        
        <div class="cluster-actions">
            <button class="btn btn-sm btn-primary" onclick="showClusterDetails('${cluster.clusterId}')">
                📋 Details
            </button>
            <button class="btn btn-sm btn-secondary" onclick="scaleCluster('${cluster.clusterId}')">
                📈 Scale
            </button>
            <button class="btn btn-sm btn-danger" onclick="deleteCluster('${cluster.clusterId}')">
                🗑️ Delete
            </button>
        </div>
    `;
    
    return card;
}

function showCreateSwarmModal() {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'create-swarm-modal';
    
    modal.innerHTML = `
        <div class="modal">
            <div class="modal-header">
                <h3>🐳 Create Docker Swarm Cluster</h3>
                <button class="modal-close" onclick="closeCreateSwarmModal()">✖</button>
            </div>
            <form onsubmit="deploySwarmCluster(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Cluster Name *</label>
                        <input type="text" id="swarm-name" required 
                               pattern="[a-z0-9-]+" 
                               placeholder="my-cluster">
                        <small>Lowercase letters, numbers, and hyphens only</small>
                    </div>
                    
                    <div class="form-row">
                        <div class="form-group">
                            <label>Manager Nodes (odd number) *</label>
                            <select id="swarm-managers" required>
                                <option value="1">1 Manager</option>
                                <option value="3" selected>3 Managers (Recommended)</option>
                                <option value="5">5 Managers</option>
                                <option value="7">7 Managers</option>
                            </select>
                        </div>
                        
                        <div class="form-group">
                            <label>Worker Nodes *</label>
                            <input type="number" id="swarm-workers" 
                                   value="2" min="0" max="10" required>
                        </div>
                    </div>
                    
                    <div class="info-banner">
                        <span class="info-icon">ℹ️</span>
                        <div>
                            <p><strong>Deployment Time:</strong> ~5-10 minutes</p>
                            <p>Each node will be a full VM with Docker pre-installed.</p>
                            <p>Manager nodes must be odd (1, 3, 5) for quorum.</p>
                        </div>
                    </div>
                    
                    <div class="warning-banner" style="margin-top: 15px;">
                        <span class="warning-icon">⚠️</span>
                        <div>
                            <p><strong>Resource Requirements:</strong></p>
                            <p>Each node: 2 vCPU, 2GB RAM, 20GB Disk</p>
                            <p id="total-requirements"></p>
                        </div>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeCreateSwarmModal()">Cancel</button>
                    <button type="submit" class="btn btn-primary">
                        🚀 Deploy Cluster
                    </button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
    
    // Update requirements on change
    const managersSelect = document.getElementById('swarm-managers');
    const workersInput = document.getElementById('swarm-workers');
    const updateReqs = () => {
        const total = parseInt(managersSelect.value) + parseInt(workersInput.value);
        document.getElementById('total-requirements').textContent = 
            `Total: ${total} nodes (${total * 2} vCPU, ${total * 2}GB RAM, ${total * 20}GB Disk)`;
    };
    managersSelect.addEventListener('change', updateReqs);
    workersInput.addEventListener('input', updateReqs);
    updateReqs();
}

function closeCreateSwarmModal() {
    const modal = document.getElementById('create-swarm-modal');
    if (modal) modal.remove();
}

async function deploySwarmCluster(event) {
    event.preventDefault();
    
    const clusterData = {
        clusterName: document.getElementById('swarm-name').value,
        user: authService.currentUser.username,
        numManagers: parseInt(document.getElementById('swarm-managers').value),
        numWorkers: parseInt(document.getElementById('swarm-workers').value)
    };
    
    closeCreateSwarmModal();
    showToast('🐳 Creating Swarm cluster... This will take several minutes.', 'info');
    
    try {
        const result = await window.fetchAPI('/swarm/clusters', {
            method: 'POST',
            body: JSON.stringify(clusterData)
        });
        
        if (result.success) {
            showToast('✅ Cluster created successfully!', 'success');
            
            // Show setup instructions
            if (result.instructions) {
                showClusterInstructions(result.instructions, result.clusterId);
            }
            
            await loadSwarmClusters();
        } else {
            showToast(`❌ ${result.error || 'Failed to create cluster'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

function showClusterInstructions(instructions, clusterId) {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'instructions-modal';
    
    modal.innerHTML = `
        <div class="modal modal-large">
            <div class="modal-header">
                <h3>📋 Cluster Setup Instructions</h3>
                <button class="modal-close" onclick="closeInstructionsModal()">✖</button>
            </div>
            <div class="modal-body">
                <pre class="instructions-text">${instructions}</pre>
            </div>
            <div class="modal-footer">
                <button class="btn btn-secondary" onclick="copyInstructions()">
                    📋 Copy Instructions
                </button>
                <button class="btn btn-primary" onclick="closeInstructionsModal()">
                    Done
                </button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeInstructionsModal() {
    const modal = document.getElementById('instructions-modal');
    if (modal) modal.remove();
}

function copyInstructions() {
    const instructions = document.querySelector('.instructions-text').textContent;
    navigator.clipboard.writeText(instructions).then(() => {
        showToast('✅ Instructions copied!', 'success');
    });
}

async function showClusterDetails(clusterId) {
    try {
        const data = await window.fetchAPI(`/swarm/clusters/${clusterId}`);
        
        if (!data.success) {
            showToast(`❌ ${data.error}`, 'error');
            return;
        }
        
        const cluster = data.cluster;
        
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'cluster-details-modal';
        
        let nodesHTML = '';
        if (cluster.nodes && cluster.nodes.length > 0) {
            nodesHTML = cluster.nodes.map(node => `
                <div class="node-item">
                    <div class="node-info">
                        <strong>${node.vmName}</strong>
                        <span class="node-role-badge">${node.role}</span>
                    </div>
                    <div class="node-details">
                        <span>IP: ${node.ip || 'N/A'}</span>
                        <span class="status-badge status-${node.status === 'ready' ? 'active' : 'inactive'}">
                            ${node.status}
                        </span>
                    </div>
                </div>
            `).join('');
        }
        
        modal.innerHTML = `
            <div class="modal modal-large">
                <div class="modal-header">
                    <h3>🐳 ${cluster.clusterName} - Details</h3>
                    <button class="modal-close" onclick="closeClusterDetailsModal()">✖</button>
                </div>
                <div class="modal-body">
                    <div class="cluster-details-grid">
                        <div class="detail-item">
                            <strong>Cluster ID:</strong>
                            <code>${cluster.clusterId}</code>
                        </div>
                        <div class="detail-item">
                            <strong>Network:</strong>
                            <span>${cluster.networkName}</span>
                        </div>
                        <div class="detail-item">
                            <strong>Subnet:</strong>
                            <code>${cluster.subnet}</code>
                        </div>
                        <div class="detail-item">
                            <strong>Manager IP:</strong>
                            <code>${cluster.managerIP || 'N/A'}</code>
                        </div>
                    </div>
                    
                    <h4 style="margin-top: 20px;">Nodes:</h4>
                    <div class="nodes-list">
                        ${nodesHTML || '<p>No nodes information available</p>'}
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="closeClusterDetailsModal()">Close</button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

function closeClusterDetailsModal() {
    const modal = document.getElementById('cluster-details-modal');
    if (modal) modal.remove();
}

function scaleCluster(clusterId) {
    showToast('🚧 Cluster scaling - Coming soon', 'info');
}

async function deleteCluster(clusterId) {
    if (!confirm('Delete this cluster?\n\n⚠️ All nodes will be destroyed.')) {
        return;
    }
    
    try {
        showToast('Deleting cluster...', 'info');
        
        const result = await window.fetchAPI(`/swarm/clusters/${clusterId}`, {
            method: 'DELETE'
        });
        
        if (result.success) {
            showToast('✅ Cluster deleted', 'success');
            await loadSwarmClusters();
        } else {
            showToast(`❌ ${result.error}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}


// Export
window.loadSwarmClusters = loadSwarmClusters;
window.showCreateSwarmModal = showCreateSwarmModal;
window.closeCreateSwarmModal = closeCreateSwarmModal;
window.deploySwarmCluster = deploySwarmCluster;
window.showClusterDetails = showClusterDetails;
window.closeClusterDetailsModal = closeClusterDetailsModal;
window.scaleCluster = scaleCluster;
window.deleteCluster = deleteCluster;
window.closeInstructionsModal = closeInstructionsModal;
window.copyInstructions = copyInstructions;