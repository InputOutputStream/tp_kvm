// ==========================================
// Modal Management Functions
// ==========================================

// Show Console Modal
function showConsoleModal() {
    if (!window.currentVM) {
        showToast('⚠️ Please select a VM first', 'warning');
        return;
    }
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'console-modal';
    
    modal.innerHTML = `
        <div class="modal-content modal-large">
            <div class="modal-header">
                <h3>🖥️ VM Console - ${window.currentVM}</h3>
                <button class="btn-close" onclick="closeConsoleModal()">✖</button>
            </div>
            <div class="modal-body">
                <div class="console-info">
                    <p><strong>VNC Console Access</strong></p>
                    <p>To access the console, you need to connect via VNC.</p>
                </div>
                <div class="console-details" id="console-details">
                    <p class="loading-text">Loading console information...</p>
                </div>
            </div>
            <div class="modal-footer">
                <button class="btn btn-secondary" onclick="closeConsoleModal()">Close</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    
    // Load VNC info
    loadConsoleInfo();
}

async function openVNCConsole(vmName) {
    const response = await fetch(`/api/vms/${vmName}/vnc`, {
        headers: {
            'Authorization': `Bearer ${token}`
        }
    });
    
    const vncInfo = await response.json();
    
    if (!vncInfo.success) {
        alert(`Error: ${vncInfo.error}`);
        return;
    }
    
    // Open noVNC in new window or iframe
    const vncUrl = `http://${vncInfo.vnc.host}:6080/vnc.html?` +
                   `host=${vncInfo.vnc.host}&` +
                   `port=${vncInfo.vnc.port}&` +
                   `autoconnect=true`;
    
    window.open(vncUrl, 'vnc_' + vmName, 'width=1024,height=768');
}

function closeConsoleModal() {
    const modal = document.getElementById('console-modal');
    if (modal) {
        modal.remove();
    }
}

async function loadConsoleInfo() {
    const consoleDetails = document.getElementById('console-details');
    if (!consoleDetails) return;
    
    try {
        const data = await window.fetchAPI(`/vms/${window.currentVM}/vnc`);
        
        if (data.success) {
            consoleDetails.innerHTML = `
                <div class="info-grid">
                    <div class="info-item">
                        <strong>Host:</strong>
                        <code>${data.host}</code>
                    </div>
                    <div class="info-item">
                        <strong>Port:</strong>
                        <code>${data.port}</code>
                    </div>
                    <div class="info-item">
                        <strong>Display:</strong>
                        <code>${data.display}</code>
                    </div>
                    <div class="info-item">
                        <strong>Connection String:</strong>
                        <code>${data.host}:${data.display}</code>
                        <button class="btn btn-sm btn-secondary" onclick="copyToClipboard('${data.host}:${data.display}')">
                            📋 Copy
                        </button>
                    </div>
                </div>
            `;
        } else {
            consoleDetails.innerHTML = `
                <p class="error-text">❌ ${data.error || 'Failed to get console information'}</p>
                <p>Make sure the VM is running and VNC is configured.</p>
            `;
        }
    } catch (error) {
        consoleDetails.innerHTML = `
            <p class="error-text">❌ Error ${error.message} </p>
        `;
    }
}

// Show Snapshot Modal
function showSnapshotModal() {
    if (!window.currentVM) {
        showToast('⚠️ Please select a VM first', 'warning');
        return;
    }
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'snapshot-modal';
    
    modal.innerHTML = `
        <div class="modal-content">
            <div class="modal-header">
                <h3>📸 Create Snapshot</h3>
                <button class="btn-close" onclick="closeSnapshotModal()">✖</button>
            </div>
            <form onsubmit="createSnapshot(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Snapshot Name *</label>
                        <input type="text" id="snapshot-name" required 
                               placeholder="e.g., before-update" 
                               pattern="[a-zA-Z0-9-]+" 
                               title="Only letters, numbers, hyphens and underscores">
                    </div>
                    <div class="form-group">
                        <label>Description</label>
                        <textarea id="snapshot-description" rows="3" 
                                  placeholder="Optional description of this snapshot"></textarea>
                    </div>
                    <div class="info-banner">
                        <span class="info-icon">ℹ️</span>
                        <p>Creating a snapshot will save the current state of the VM. You can revert to this state later.</p>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeSnapshotModal()">Cancel</button>
                    <button type="submit" class="btn btn-primary">
                        <span>📸</span>
                        Create Snapshot
                    </button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeSnapshotModal() {
    const modal = document.getElementById('snapshot-modal');
    if (modal) {
        modal.remove();
    }
}

async function createSnapshot(event) {
    event.preventDefault();
    
    const snapshotName = document.getElementById('snapshot-name').value;
    const description = document.getElementById('snapshot-description').value || 'Created via web interface';
    
    closeSnapshotModal();
    showToast('Creating snapshot...', 'info');
    
    try {
        const result = await window.fetchAPI(`/vms/${window.currentVM}/snapshots`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                snapshotName: snapshotName,
                description: description
            })
        });
        
        if (result.success) {
            showToast('✅ Snapshot created successfully!', 'success');
            await loadSnapshots();
        } else {
            showToast(`❌ Failed to create snapshot: ${result.error || 'Unknown error'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

// Show Clone Modal
function showCloneModal() {
    if (!window.currentVM) {
        showToast('⚠️ Please select a VM first', 'warning');
        return;
    }
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'clone-modal';
    
    modal.innerHTML = `
        <div class="modal-content">
            <div class="modal-header">
                <h3>📋 Clone VM</h3>
                <button class="btn-close" onclick="closeCloneModal()">✖</button>
            </div>
            <form onsubmit="cloneVM(event)">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Source VM</label>
                        <input type="text" value="${window.currentVM}" disabled>
                    </div>
                    <div class="form-group">
                        <label>Clone Name *</label>
                        <input type="text" id="clone-name" required 
                               placeholder="e.g., ${window.currentVM}-clone" 
                               pattern="[a-zA-Z0-9-]+" 
                               title="Only letters, numbers, hyphens and underscores">
                    </div>
                    <div class="info-banner">
                        <span class="info-icon">ℹ️</span>
                        <div>
                            <p><strong>This will create a full clone of the VM</strong></p>
                            <p>All disks will be copied. The VM must be stopped to clone.</p>
                        </div>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeCloneModal()">Cancel</button>
                    <button type="submit" class="btn btn-primary">
                        <span>📋</span>
                        Clone VM
                    </button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeCloneModal() {
    const modal = document.getElementById('clone-modal');
    if (modal) {
        modal.remove();
    }
}

async function cloneVM(event) {
    event.preventDefault();
    
    const cloneName = document.getElementById('clone-name').value;
    
    closeCloneModal();
    showToast('Cloning VM...', 'info');
    
    try {
        const result = await window.fetchAPI(`/vms/${window.currentVM}/clone`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                cloneName: cloneName
            })
        });
        
        if (result.success) {
            showToast('✅ VM cloned successfully!', 'success');
            await loadVMs();
        } else {
            showToast(`❌ Failed to clone VM: ${result.error || 'Unknown error'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

// Show Delete VM Modal
function showDeleteVMModal() {
    if (!window.currentVM) {
        showToast('⚠️ Please select a VM first', 'warning');
        return;
    }
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'delete-modal';
    
    modal.innerHTML = `
        <div class="modal-content">
            <div class="modal-header">
                <h3>🗑️ Delete VM</h3>
                <button class="btn-close" onclick="closeDeleteVMModal()">✖</button>
            </div>
            <form onsubmit="deleteVMConfirmed(event)">
                <div class="modal-body">
                    <div class="warning-banner">
                        <span class="warning-icon">⚠️</span>
                        <div>
                            <h4>Warning: This action cannot be undone!</h4>
                            <p>You are about to delete VM: <strong>${window.currentVM}</strong></p>
                        </div>
                    </div>
                    <div class="form-group" style="margin-top: 20px;">
                        <label class="checkbox-label">
                            <input type="checkbox" id="remove-disks" checked>
                            <span>Also delete disk files (recommended)</span>
                        </label>
                        <p style="font-size: 0.9em; color: var(--text-secondary); margin-top: 5px;">
                            If unchecked, only the VM definition will be removed. Disk files will remain on the host.
                        </p>
                    </div>
                    <div class="form-group">
                        <label>Type VM name to confirm:</label>
                        <input type="text" id="delete-confirm" required 
                               placeholder="${window.currentVM}">
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeDeleteVMModal()">Cancel</button>
                    <button type="submit" class="btn btn-danger">
                        <span>🗑️</span>
                        Delete VM
                    </button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeDeleteVMModal() {
    const modal = document.getElementById('delete-modal');
    if (modal) {
        modal.remove();
    }
}

function closeModal(id) {
    const modal = document.getElementById(id);
    if (modal) {
        modal.remove();
    }
}

async function deleteVMConfirmed(event) {
    event.preventDefault();
    
    const confirmName = document.getElementById('delete-confirm').value;
    const removeDisks = document.getElementById('remove-disks').checked;
    
    if (confirmName !== window.currentVM) {
        showToast('❌ VM name does not match', 'error');
        return;
    }
    
    closeDeleteVMModal();
    closeDetailsPanel();
    showToast('Deleting VM...', 'info');
    
    try {
        const result = await window.fetchAPI(`/vms/${window.currentVM}?removeDisks=${removeDisks}`, {
            method: 'DELETE'
        });
        
        if (result.success) {
            showToast('✅ VM deleted successfully!', 'success');
            window.currentVM = null;
            await loadVMs();
        } else {
            showToast(`❌ Failed to delete VM: ${result.error || 'Unknown error'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ Error: ${error.message}`, 'error');
    }
}

// Utility function to copy text to clipboard
function copyToClipboard(text) {
    navigator.clipboard.writeText(text).then(() => {
        showToast('✅ Copied to clipboard!', 'success');
    }).catch(() => {
        showToast('❌ Failed to copy', 'error');
    });
}


window.showConsoleModal = showConsoleModal;
window.closeConsoleModal = closeConsoleModal;
window.openVNCConsole = openVNCConsole;
window.loadConsoleInfo = loadConsoleInfo;
window.showSnapshotModal = showSnapshotModal;
window.closeSnapshotModal = closeSnapshotModal;
window.createSnapshot = createSnapshot;
window.showCloneModal = showCloneModal;
window.closeModal = closeModal;
window.closeCloneModal = closeCloneModal;
window.cloneVM = cloneVM;
window.showDeleteVMModal = showDeleteVMModal;
window.closeDeleteVMModal = closeDeleteVMModal;
window.deleteVMConfirmed = deleteVMConfirmed;
window.copyToClipboard = copyToClipboard;