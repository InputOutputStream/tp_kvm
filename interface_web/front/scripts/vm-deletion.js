// ==========================================
// VM DELETION MODULE
// ==========================================

const VMDeletion = {
    // Show delete VM confirmation modal
    showDeleteConfirmation(vmName, vmDisplayName = null) {
        const displayName = vmDisplayName || vmName;
        
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'delete-vm-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>⚠️ Delete Virtual Machine</h3>
                    <button class="modal-close" onclick="VMDeletion.closeModal()">✖</button>
                </div>
                <div class="modal-body">
                    <div class="warning-banner danger">
                        <span class="warning-icon">⚠️</span>
                        <div>
                            <p><strong>Warning: This action cannot be undone!</strong></p>
                            <p>You are about to permanently delete the virtual machine:</p>
                            <p class="vm-name-highlight">${displayName}</p>
                        </div>
                    </div>
                    
                    <div class="deletion-options" style="margin-top: 20px;">
                        <h4>Deletion Options:</h4>
                        
                        <div class="form-group">
                            <label class="checkbox-label">
                                <input type="checkbox" id="remove-disks" checked>
                                <span>
                                    <strong>Remove all disk storage</strong>
                                    <small>This will permanently delete all VM data and cannot be recovered</small>
                                </span>
                            </label>
                        </div>
                        
                        <div class="form-group">
                            <label class="checkbox-label">
                                <input type="checkbox" id="remove-snapshots" checked>
                                <span>
                                    <strong>Remove all snapshots</strong>
                                    <small>Delete all snapshots associated with this VM</small>
                                </span>
                            </label>
                        </div>
                    </div>
                    
                    <div class="info-banner" style="margin-top: 20px;">
                        <span class="info-icon">ℹ️</span>
                        <div>
                            <p><strong>What will be deleted:</strong></p>
                            <ul style="margin: 10px 0; padding-left: 20px;">
                                <li>VM configuration</li>
                                <li>Virtual disk files (if selected)</li>
                                <li>All snapshots (if selected)</li>
                                <li>Network configuration</li>
                            </ul>
                        </div>
                    </div>
                    
                    <div class="confirmation-input" style="margin-top: 20px;">
                        <label>
                            <strong>Type the VM name to confirm deletion:</strong>
                        </label>
                        <input type="text" id="delete-confirmation-input" 
                               placeholder="${displayName}"
                               class="confirmation-field">
                        <small>This is a safety measure to prevent accidental deletion</small>
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="VMDeletion.closeModal()">
                        Cancel
                    </button>
                    <button class="btn btn-danger" onclick="VMDeletion.confirmDelete('${vmName}', '${displayName}')">
                        🗑️ Delete VM Permanently
                    </button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        // Focus on confirmation input
        setTimeout(() => {
            document.getElementById('delete-confirmation-input').focus();
        }, 100);
    },
    
    // Confirm and execute deletion
    async confirmDelete(vmName, displayName) {
        const confirmationInput = document.getElementById('delete-confirmation-input');
        const inputValue = confirmationInput.value.trim();
        
        // Verify confirmation input
        if (inputValue !== displayName) {
            showToast('❌ VM name does not match. Please type the exact name.', 'error');
            confirmationInput.classList.add('error-shake');
            setTimeout(() => {
                confirmationInput.classList.remove('error-shake');
            }, 500);
            return;
        }
        
        const removeDisks = document.getElementById('remove-disks').checked;
        const removeSnapshots = document.getElementById('remove-snapshots').checked;
        
        this.closeModal();
        
        // Show deletion progress
        this.showDeletionProgress(vmName, displayName, removeDisks, removeSnapshots);
    },
    
    // Show deletion progress modal
    async showDeletionProgress(vmName, displayName, removeDisks, removeSnapshots) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'deletion-progress-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>🗑️ Deleting VM: ${displayName}</h3>
                </div>
                <div class="modal-body">
                    <div class="deletion-steps">
                        <div class="deletion-step" id="step-shutdown">
                            <span class="step-icon">⏳</span>
                            <span class="step-text">Shutting down VM...</span>
                        </div>
                        ${removeSnapshots ? `
                        <div class="deletion-step" id="step-snapshots">
                            <span class="step-icon">⏳</span>
                            <span class="step-text">Removing snapshots...</span>
                        </div>
                        ` : ''}
                        <div class="deletion-step" id="step-vm-config">
                            <span class="step-icon">⏳</span>
                            <span class="step-text">Removing VM configuration...</span>
                        </div>
                        ${removeDisks ? `
                        <div class="deletion-step" id="step-disks">
                            <span class="step-icon">⏳</span>
                            <span class="step-text">Removing disk storage...</span>
                        </div>
                        ` : ''}
                        <div class="deletion-step" id="step-complete">
                            <span class="step-icon">⏳</span>
                            <span class="step-text">Finalizing deletion...</span>
                        </div>
                    </div>
                    
                    <div class="progress-bar-container">
                        <div class="progress-bar">
                            <div class="progress-fill" id="deletion-progress" style="width: 0%"></div>
                        </div>
                        <span class="progress-text" id="progress-percentage">0%</span>
                    </div>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        // Execute deletion
        await this.executeDeleteion(vmName, removeDisks, removeSnapshots);
    },
    
    // Execute deletion with progress updates
    async executeDeleteion(vmName, removeDisks, removeSnapshots) {
        try {
            let progress = 0;
            const totalSteps = 3 + (removeSnapshots ? 1 : 0) + (removeDisks ? 1 : 0);
            const stepIncrement = 100 / totalSteps;
            
            // Step 1: Shutdown VM
            this.updateStep('step-shutdown', 'loading');
            await this.shutdownVMIfRunning(vmName);
            this.updateStep('step-shutdown', 'success');
            progress += stepIncrement;
            this.updateProgress(progress);
            
            // Step 2: Remove snapshots (if selected)
            if (removeSnapshots) {
                this.updateStep('step-snapshots', 'loading');
                await this.removeAllSnapshots(vmName);
                this.updateStep('step-snapshots', 'success');
                progress += stepIncrement;
                this.updateProgress(progress);
            }
            
            // Step 3: Remove VM configuration
            this.updateStep('step-vm-config', 'loading');
            await window.apiService.deleteVM(vmName, removeDisks);
            this.updateStep('step-vm-config', 'success');
            progress += stepIncrement;
            this.updateProgress(progress);
            
            // Step 4: Remove disks (handled by backend)
            if (removeDisks) {
                this.updateStep('step-disks', 'loading');
                await new Promise(resolve => setTimeout(resolve, 1000)); // Visual delay
                this.updateStep('step-disks', 'success');
                progress += stepIncrement;
                this.updateProgress(progress);
            }
            
            // Step 5: Complete
            this.updateStep('step-complete', 'loading');
            await new Promise(resolve => setTimeout(resolve, 500));
            this.updateStep('step-complete', 'success');
            this.updateProgress(100);
            
            // Show success and cleanup
            setTimeout(() => {
                this.closeModal();
                showToast('✅ VM deleted successfully', 'success');
                
                // Reload VMs list
                if (window.loadVMs) {
                    window.loadVMs();
                }
                
                // Close details panel if open
                const detailsPanel = document.getElementById('vm-details-panel');
                if (detailsPanel) {
                    detailsPanel.classList.remove('open');
                }
            }, 1000);
            
        } catch (error) {
            this.closeModal();
            showToast(`❌ Failed to delete VM: ${error.message}`, 'error');
            console.error('VM deletion error:', error);
        }
    },
    
    // Shutdown VM if running
    async shutdownVMIfRunning(vmName) {
        try {
            const status = await window.apiService.getVMStatus(vmName);
            if (status.running) {
                await window.apiService.shutdownVM(vmName);
                // Wait for shutdown
                await new Promise(resolve => setTimeout(resolve, 3000));
            }
        } catch (error) {
            console.warn('Shutdown error (continuing):', error);
        }
    },
    
    // Remove all snapshots
    async removeAllSnapshots(vmName) {
        try {
            const snapshotsData = await window.apiService.getSnapshots(vmName);
            if (snapshotsData.snapshots && snapshotsData.snapshots.length > 0) {
                for (const snapshot of snapshotsData.snapshots) {
                    await window.apiService.deleteSnapshot(vmName, snapshot.name);
                }
            }
        } catch (error) {
            console.warn('Snapshot removal error (continuing):', error);
        }
    },
    
    // Update deletion step status
    updateStep(stepId, status) {
        const step = document.getElementById(stepId);
        if (!step) return;
        
        const icon = step.querySelector('.step-icon');
        
        if (status === 'loading') {
            icon.textContent = '⏳';
            step.classList.add('active');
        } else if (status === 'success') {
            icon.textContent = '✅';
            step.classList.add('complete');
            step.classList.remove('active');
        } else if (status === 'error') {
            icon.textContent = '❌';
            step.classList.add('error');
            step.classList.remove('active');
        }
    },
    
    // Update progress bar
    updateProgress(percentage) {
        const progressBar = document.getElementById('deletion-progress');
        const progressText = document.getElementById('progress-percentage');
        
        if (progressBar) {
            progressBar.style.width = `${percentage}%`;
        }
        
        if (progressText) {
            progressText.textContent = `${Math.round(percentage)}%`;
        }
    },
    
    // Close modal
    closeModal() {
        const modals = document.querySelectorAll('.modal-overlay');
        modals.forEach(modal => modal.remove());
    }
};

// Add deletion-specific CSS
const deletionStyle = document.createElement('style');
deletionStyle.textContent = `
    .warning-banner.danger {
        background: rgba(220, 38, 38, 0.1);
        border-color: rgba(220, 38, 38, 0.3);
    }
    
    .vm-name-highlight {
        font-size: 1.2em;
        font-weight: 700;
        color: var(--danger-color);
        margin: 10px 0;
        padding: 10px;
        background: rgba(220, 38, 38, 0.05);
        border-radius: 6px;
        text-align: center;
    }
    
    .deletion-options h4 {
        margin-bottom: 15px;
    }
    
    .checkbox-label {
        display: flex;
        align-items: flex-start;
        gap: 12px;
        padding: 12px;
        background: var(--bg-color);
        border-radius: 8px;
        cursor: pointer;
        transition: background 0.2s;
    }
    
    .checkbox-label:hover {
        background: rgba(0, 0, 0, 0.02);
    }
    
    .checkbox-label input[type="checkbox"] {
        margin-top: 2px;
    }
    
    .checkbox-label span {
        flex: 1;
    }
    
    .checkbox-label small {
        display: block;
        color: var(--text-secondary);
        margin-top: 4px;
    }
    
    .confirmation-field {
        width: 100%;
        padding: 12px;
        border: 2px solid var(--border-color);
        border-radius: 8px;
        font-size: 1em;
        margin-top: 8px;
        transition: border-color 0.2s;
    }
    
    .confirmation-field:focus {
        outline: none;
        border-color: var(--danger-color);
    }
    
    .error-shake {
        animation: shake 0.3s;
    }
    
    @keyframes shake {
        0%, 100% { transform: translateX(0); }
        25% { transform: translateX(-10px); }
        75% { transform: translateX(10px); }
    }
    
    .deletion-steps {
        margin: 20px 0;
    }
    
    .deletion-step {
        display: flex;
        align-items: center;
        gap: 12px;
        padding: 12px;
        margin-bottom: 8px;
        border-radius: 8px;
        background: var(--bg-color);
        transition: all 0.3s;
    }
    
    .deletion-step.active {
        background: rgba(0, 132, 61, 0.1);
        border-left: 3px solid var(--primary-color);
    }
    
    .deletion-step.complete {
        background: rgba(0, 132, 61, 0.05);
    }
    
    .deletion-step.error {
        background: rgba(220, 38, 38, 0.1);
        border-left: 3px solid var(--danger-color);
    }
    
    .deletion-step .step-icon {
        font-size: 1.3em;
    }
    
    .deletion-step .step-text {
        flex: 1;
    }
    
    .progress-bar-container {
        margin-top: 20px;
        display: flex;
        align-items: center;
        gap: 15px;
    }
    
    .progress-bar {
        flex: 1;
        height: 8px;
        background: var(--bg-color);
        border-radius: 4px;
        overflow: hidden;
    }
    
    .progress-text {
        font-weight: 600;
        min-width: 50px;
        text-align: right;
    }
`;
document.head.appendChild(deletionStyle);

// Export to global scope
window.VMDeletion = VMDeletion;