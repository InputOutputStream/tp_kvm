// ==========================================
// VM CLONING MODULE
// ==========================================

const VMCloning = {
    // Show clone VM modal
    showCloneModal(vmName) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'clone-vm-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>📋 Clone VM: ${vmName}</h3>
                    <button class="modal-close" onclick="VMCloning.closeModal()">✖</button>
                </div>
                <form onsubmit="VMCloning.cloneVM(event, '${vmName}')">
                    <div class="modal-body">
                        <div class="info-banner">
                            <span class="info-icon">ℹ️</span>
                            <div>
                                <p><strong>Cloning Process:</strong></p>
                                <ul style="margin: 10px 0; padding-left: 20px;">
                                    <li>Creates an exact copy of the VM</li>
                                    <li>Copies all disks and configuration</li>
                                    <li>Clone will be in 'shut off' state</li>
                                    <li>You can start it after cloning completes</li>
                                </ul>
                            </div>
                        </div>
                        
                        <div class="form-group" style="margin-top: 20px;">
                            <label>Clone Name *</label>
                            <input type="text" id="clone-name" required 
                                   pattern="[a-zA-Z0-9_-]+" 
                                   placeholder="${vmName}_clone">
                            <small>Letters, numbers, underscore, and hyphen only</small>
                        </div>
                        
                        <div class="warning-banner" style="margin-top: 15px;">
                            <span class="warning-icon">⚠️</span>
                            <div>
                                <p><strong>Important:</strong></p>
                                <p>Cloning can take several minutes depending on disk size. The VM will remain accessible during cloning.</p>
                            </div>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" onclick="VMCloning.closeModal()">Cancel</button>
                        <button type="submit" class="btn btn-primary">
                            📋 Clone VM
                        </button>
                    </div>
                </form>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        // Focus on input
        setTimeout(() => {
            document.getElementById('clone-name').focus();
        }, 100);
    },
    
    // Clone VM
    async cloneVM(event, sourceVMName) {
        event.preventDefault();
        
        const cloneName = document.getElementById('clone-name').value;
        
        this.closeModal();
        showToast('🔄 Cloning VM... This may take several minutes.', 'info');
        
        try {
            const result = await window.apiService.cloneVM(sourceVMName, cloneName);
            
            if (result.success) {
                showToast('✅ VM cloned successfully!', 'success');
                
                // Show success details
                this.showCloneSuccessModal(sourceVMName, cloneName);
                
                // Reload VMs list
                if (window.loadVMs) {
                    await window.loadVMs();
                }
            } else {
                showToast(`❌ ${result.error || 'Failed to clone VM'}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    // Show clone success modal
    showCloneSuccessModal(sourceVM, cloneName) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'clone-success-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>✅ Clone Created Successfully</h3>
                    <button class="modal-close" onclick="VMCloning.closeModal()">✖</button>
                </div>
                <div class="modal-body">
                    <div class="success-content">
                        <div class="success-icon">✅</div>
                        <h4>VM Cloned Successfully!</h4>
                        
                        <div class="clone-details">
                            <div class="detail-item">
                                <strong>Source VM:</strong>
                                <code>${sourceVM}</code>
                            </div>
                            <div class="detail-item">
                                <strong>Clone Name:</strong>
                                <code>${cloneName}</code>
                            </div>
                            <div class="detail-item">
                                <strong>Status:</strong>
                                <span class="status-badge status-stopped">🔴 Shut Off</span>
                            </div>
                        </div>
                        
                        <div class="info-banner" style="margin-top: 20px;">
                            <span class="info-icon">ℹ️</span>
                            <div>
                                <p><strong>Next Steps:</strong></p>
                                <ol style="margin: 10px 0; padding-left: 20px;">
                                    <li>The cloned VM is ready to use</li>
                                    <li>You can start it from the VMs list</li>
                                    <li>Consider changing hostname and network settings inside the VM</li>
                                </ol>
                            </div>
                        </div>
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="VMCloning.closeModal()">Close</button>
                    <button class="btn btn-primary" onclick="VMCloning.startClonedVM('${cloneName}')">
                        ▶️ Start Clone
                    </button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
    },
    
    // Start cloned VM
    async startClonedVM(vmName) {
        this.closeModal();
        
        try {
            showToast('Starting VM...', 'info');
            await window.apiService.startVM(vmName);
            showToast('✅ VM started successfully', 'success');
            
            if (window.loadVMs) {
                await window.loadVMs();
            }
            
            if (window.selectVM) {
                window.selectVM(vmName);
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    // Close modal
    closeModal() {
        const modals = document.querySelectorAll('.modal-overlay');
        modals.forEach(modal => modal.remove());
    }
};

// Export to global scope
window.VMCloning = VMCloning;