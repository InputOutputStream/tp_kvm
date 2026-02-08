class VMController {
    constructor() {
        this.searchDebounce = null;
        this.setupEventListeners();
    }

    // ==========================================
    // Event Listeners Setup
    // ==========================================
    
    setupEventListeners() {
        // Subscribe to state changes
        window.stateManager.subscribe('vms', (vms) => {
            this.renderVMsList(vms);
            this.updateVMCounts(vms);
        });

        window.stateManager.subscribe('currentVM', (vmName, oldVMName) => {
            if (vmName) {
                this.showVMDetails(vmName);
            } else {
                this.hideVMDetails();
            }
        });

        window.stateManager.subscribe('vmDeleted', (vmName) => {
            this.handleVMDeleted(vmName);
        });

        // VM search
        const searchInput = document.getElementById('vm-search');
        if (searchInput) {
            searchInput.addEventListener('input', (e) => this.handleSearch(e));
        }

        // VM filters
        document.querySelectorAll('.filter-chips .chip').forEach(chip => {
            chip.addEventListener('click', (e) => this.handleFilter(e));
        });

        // Stats updates
        window.stateManager.subscribe('vm-stats-*', (stats, vmName) => {
            this.updateVMStats(stats);
        });
    }

    // ==========================================
    // Load VMs
    // ==========================================
    
    async loadVMs() {
        const vmsList = document.getElementById('vms-list');
        const dashboardList = document.getElementById('dashboard-vms-list');
        
        if (vmsList) {
            vmsList.innerHTML = '<p class="loading-text">Loading virtual machines...</p>';
        }
        
        try {
            const vms = await window.stateManager.refreshVMsList();
            
            // Update counts
            this.updateVMCounts(vms);
            
            // Render lists
            this.renderVMsList(vms);
            
            // Update dashboard
            if (dashboardList) {
                this.renderDashboardVMs(vms);
            }
            
            return vms;
        } catch (error) {
            console.error('Error loading VMs:', error);
            if (vmsList) {
                vmsList.innerHTML = `<p class="error-text">Error: ${error.message}</p>`;
            }
            window.showToast?.(`Error loading VMs: ${error.message}`, 'error');
            throw error;
        }
    }

    // ==========================================
    // Render VMs List
    // ==========================================
    
    renderVMsList(vms) {
        const vmsList = document.getElementById('vms-list');
        if (!vmsList) return;
        
        vmsList.innerHTML = '';
        
        if (!vms || vms.length === 0) {
            vmsList.innerHTML = '<p class="empty-state">No VMs found. Deploy your first VM!</p>';
            return;
        }
        
        vms.forEach(vm => {
            vmsList.appendChild(this.createVMCard(vm));
        });
    }

    renderDashboardVMs(vms) {
        const dashboardList = document.getElementById('dashboard-vms-list');
        if (!dashboardList) return;
        
        dashboardList.innerHTML = '';
        
        if (!vms || vms.length === 0) {
            dashboardList.innerHTML = '<p class="empty-state">No VMs deployed yet</p>';
            return;
        }
        
        // Show first 4 VMs
        vms.slice(0, 4).forEach(vm => {
            dashboardList.appendChild(this.createVMCard(vm));
        });
    }

    // ==========================================
    // Create VM Card
    // ==========================================
    
    createVMCard(vm) {
        const card = document.createElement('div');
        card.className = 'vm-card';
        card.dataset.vmName = vm.name;
        card.onclick = () => this.selectVM(vm.name);
        
        const statusClass = vm.running ? 'running' : 'stopped';
        const statusText = vm.running ? '🟢 Running' : '🔴 Stopped';
        const displayName = vm.displayName || vm.name;
        
        let statsHTML = '';
        if (vm.stats && vm.running) {
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
        
        let ownerBadge = '';
        if (window.authService?.currentUser?.role === 'admin' && vm.owner) {
            ownerBadge = `<span class="owner-badge">👤 ${vm.owner}</span>`;
        }
        
        card.innerHTML = `
            <div class="vm-card-header">
                <div>
                    <h3 class="vm-card-title">🖥️ ${displayName}</h3>
                    <div class="vm-card-meta">
                        <span>State: ${vm.state}</span>
                        ${ownerBadge}
                    </div>
                </div>
                <span class="vm-card-status ${statusClass}">${statusText}</span>
            </div>
            ${statsHTML}
        `;
        
        return card;
    }

    // ==========================================
    // Select VM
    // ==========================================
    
    async selectVM(vmName) {
        // Verify VM exists before selecting
        if (!window.stateManager.vmExists(vmName)) {
            window.showToast?.('VM no longer exists', 'error');
            await this.loadVMs();
            return;
        }
        
        // Update state (will trigger details panel)
        window.stateManager.setCurrentVM(vmName);
        
        // Highlight selected card
        document.querySelectorAll('.vm-card').forEach(card => {
            card.classList.remove('selected');
        });
        
        const selectedCard = document.querySelector(`[data-vm-name="${vmName}"]`);
        if (selectedCard) {
            selectedCard.classList.add('selected');
        }
    }

    // ==========================================
    // Show/Hide VM Details
    // ==========================================
    
    async showVMDetails(vmName) {
        const panel = document.getElementById('vm-details-panel');
        const panelTitle = document.getElementById('vm-panel-title');
        
        if (panel) {
            panel.classList.add('open');
        }
        
        if (panelTitle) {
            const vm = window.stateManager.state.vms.find(v => v.name === vmName);
            const displayName = vm?.displayName || vmName;
            panelTitle.textContent = `VM: ${displayName}`;
        }
        
        // Load VM info and snapshots
        await Promise.all([
            this.loadVMInfo(vmName),
            this.loadSnapshots(vmName)
        ]);
        
        // Show stats section if VM is running
        try {
            const data = await window.api.getVMDetails(vmName);
            const statsSection = document.getElementById('vm-stats');
            if (statsSection) {
                statsSection.style.display = data.running ? 'grid' : 'none';
            }
        } catch (error) {
            console.error('Error checking VM status:', error);
        }
    }

    hideVMDetails() {
        const panel = document.getElementById('vm-details-panel');
        if (panel) {
            panel.classList.remove('open');
        }
        
        // Clear selection
        document.querySelectorAll('.vm-card').forEach(card => {
            card.classList.remove('selected');
        });
    }

    // ==========================================
    // Load VM Info
    // ==========================================
    
    async loadVMInfo(vmName) {
        const vmInfo = document.getElementById('vm-info');
        if (!vmInfo) return;
        
        vmInfo.innerHTML = '<p>Loading information...</p>';
        
        try {
            const data = await window.api.getVMDetails(vmName);
            vmInfo.textContent = data.info || 'No information available';
        } catch (error) {
            vmInfo.innerHTML = '<p class="error-text">Error loading information</p>';
        }
    }

    // ==========================================
    // VM Actions
    // ==========================================
    
    async startVM() {
        const vmName = window.stateManager.getState('currentVM');
        if (!vmName) return;
        
        try {
            window.showToast?.('Starting VM...', 'info');
            await window.api.startVM(vmName);
            window.showToast?.('✅ VM started successfully', 'success');
            
            // Refresh VM list and details
            await this.loadVMs();
            await this.loadVMInfo(vmName);
            
            // Show stats section
            const statsSection = document.getElementById('vm-stats');
            if (statsSection) {
                statsSection.style.display = 'grid';
            }
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    async shutdownVM() {
        const vmName = window.stateManager.getState('currentVM');
        if (!vmName) return;
        
        if (!confirm(`Are you sure you want to shut down ${vmName}?`)) {
            return;
        }
        
        try {
            window.showToast?.('Shutting down VM...', 'info');
            await window.api.shutdownVM(vmName);
            window.showToast?.('✅ VM shut down successfully', 'success');
            
            await this.loadVMs();
            await this.loadVMInfo(vmName);
            
            // Hide stats section
            const statsSection = document.getElementById('vm-stats');
            if (statsSection) {
                statsSection.style.display = 'none';
            }
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    async rebootVM() {
        const vmName = window.stateManager.getState('currentVM');
        if (!vmName) return;
        
        if (!confirm(`Reboot ${vmName}?`)) {
            return;
        }
        
        try {
            window.showToast?.('Rebooting VM...', 'info');
            await window.api.rebootVM(vmName);
            window.showToast?.('✅ VM rebooted', 'success');
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    async pauseVM() {
        const vmName = window.stateManager.getState('currentVM');
        if (!vmName) return;
        
        try {
            window.showToast?.('Pausing VM...', 'info');
            await window.api.pauseVM(vmName);
            window.showToast?.('✅ VM paused', 'success');
            await this.loadVMInfo(vmName);
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    async resumeVM() {
        const vmName = window.stateManager.getState('currentVM');
        if (!vmName) return;
        
        try {
            window.showToast?.('Resuming VM...', 'info');
            await window.api.resumeVM(vmName);
            window.showToast?.('✅ VM resumed', 'success');
            await this.loadVMInfo(vmName);
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    async deleteVM(vmName, removeDisks = true) {
        if (!vmName) {
            vmName = window.stateManager.getState('currentVM');
        }
        
        if (!vmName) return;
        
        try {
            window.showToast?.('Deleting VM...', 'info');
            
            // Delete the VM
            await window.api.deleteVM(vmName, removeDisks);
            
            window.showToast?.('✅ VM deleted successfully', 'success');
            
            // Clear current VM (this will stop polling automatically)
            window.stateManager.clearCurrentVM();
            
            // Refresh VMs list (will auto-detect deletion)
            await this.loadVMs();
            
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    // ==========================================
    // Snapshots Management
    // ==========================================
    
    async loadSnapshots(vmName) {
        const snapshotsList = document.getElementById('snapshots-list');
        if (!snapshotsList) return;
        
        snapshotsList.innerHTML = '<p>Loading snapshots...</p>';
        
        try {
            const data = await window.api.getSnapshots(vmName);
            
            if (!data.snapshots || data.snapshots.length === 0) {
                snapshotsList.innerHTML = '<p class="empty-state">No snapshots found</p>';
                return;
            }
            
            snapshotsList.innerHTML = '';
            data.snapshots.forEach(snapshot => {
                snapshotsList.appendChild(this.createSnapshotItem(snapshot, vmName));
            });
        } catch (error) {
            snapshotsList.innerHTML = '<p class="error-text">Error loading snapshots</p>';
        }
    }

    createSnapshotItem(snapshot, vmName) {
        const item = document.createElement('div');
        item.className = 'snapshot-item';
        
        item.innerHTML = `
            <div class="snapshot-info">
                <h4>📸 ${snapshot.name}</h4>
                <p>🕒 ${snapshot.creationTime}</p>
                ${snapshot.description ? `<p class="snapshot-desc">${snapshot.description}</p>` : ''}
            </div>
            <div class="snapshot-actions">
                <button class="btn btn-sm btn-primary" onclick="vmController.revertSnapshot('${snapshot.name}')">
                    ↩️ Restore
                </button>
                <button class="btn btn-sm btn-danger" onclick="vmController.deleteSnapshot('${snapshot.name}')">
                    🗑️
                </button>
            </div>
        `;
        
        return item;
    }

    async createSnapshot(snapshotName, description) {
        const vmName = window.stateManager.getState('currentVM');
        if (!vmName) return;
        
        try {
            window.showToast?.('Creating snapshot...', 'info');
            await window.api.createSnapshot(vmName, snapshotName, description);
            window.showToast?.('✅ Snapshot created successfully', 'success');
            await this.loadSnapshots(vmName);
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    async revertSnapshot(snapshotName) {
        const vmName = window.stateManager.getState('currentVM');
        if (!vmName) return;
        
        if (!confirm(`Restore snapshot "${snapshotName}"?\n\n⚠️ VM will be stopped if running.`)) {
            return;
        }
        
        try {
            window.showToast?.('Restoring snapshot...', 'info');
            await window.api.revertSnapshot(vmName, snapshotName);
            window.showToast?.('✅ Snapshot restored successfully', 'success');
            
            await this.loadVMInfo(vmName);
            await this.loadVMs();
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    async deleteSnapshot(snapshotName) {
        const vmName = window.stateManager.getState('currentVM');
        if (!vmName) return;
        
        if (!confirm(`Delete snapshot "${snapshotName}" permanently?`)) {
            return;
        }
        
        try {
            window.showToast?.('Deleting snapshot...', 'info');
            await window.api.deleteSnapshot(vmName, snapshotName);
            window.showToast?.('✅ Snapshot deleted', 'success');
            await this.loadSnapshots(vmName);
        } catch (error) {
            window.showToast?.(`❌ Error: ${error.message}`, 'error');
        }
    }

    // ==========================================
    // Stats Updates
    // ==========================================
    
    updateVMStats(stats) {
        if (!stats) return;
        
        const cpuUsage = document.getElementById('cpu-usage');
        const memoryUsage = document.getElementById('memory-usage');
        const diskIO = document.getElementById('disk-io');
        const networkIO = document.getElementById('network-io');
        
        if (cpuUsage) cpuUsage.textContent = `${stats.cpu}%`;
        if (memoryUsage) {
            memoryUsage.textContent = `${stats.memory.used} KB / ${stats.memory.max} KB (${stats.memory.percent}%)`;
        }
        if (diskIO) {
            diskIO.textContent = `R: ${stats.disk.readMB} MB | W: ${stats.disk.writeMB} MB`;
        }
        if (networkIO) {
            networkIO.textContent = `RX: ${stats.network.rxMB} MB | TX: ${stats.network.txMB} MB`;
        }
    }

    // ==========================================
    // Search and Filter
    // ==========================================
    
    handleSearch(event) {
        clearTimeout(this.searchDebounce);
        
        this.searchDebounce = setTimeout(() => {
            const searchTerm = event.target.value.toLowerCase();
            const vmCards = document.querySelectorAll('#vms-list .vm-card');
            
            vmCards.forEach(card => {
                const vmName = card.querySelector('.vm-card-title').textContent.toLowerCase();
                card.style.display = vmName.includes(searchTerm) ? '' : 'none';
            });
        }, 300);
    }

    handleFilter(event) {
        const chip = event.target.closest('.chip');
        if (!chip) return;
        
        // Update active chip
        document.querySelectorAll('.filter-chips .chip').forEach(c => {
            c.classList.remove('active');
        });
        chip.classList.add('active');
        
        const filter = chip.dataset.filter;
        const vmCards = document.querySelectorAll('#vms-list .vm-card');
        
        vmCards.forEach(card => {
            if (filter === 'all') {
                card.style.display = '';
            } else {
                const status = card.querySelector('.vm-card-status');
                card.style.display = status?.classList.contains(filter) ? '' : 'none';
            }
        });
    }

    // ==========================================
    // Helpers
    // ==========================================
    
    updateVMCounts(vms) {
        const runningCount = vms.filter(vm => vm.running).length;
        
        const runningEl = document.getElementById('running-count');
        const totalEl = document.getElementById('total-count');
        const badgeEl = document.getElementById('vm-count-badge');
        
        if (runningEl) runningEl.textContent = runningCount;
        if (totalEl) totalEl.textContent = vms.length;
        if (badgeEl) badgeEl.textContent = vms.length;
    }

    handleVMDeleted(vmName) {
        console.log(`VM ${vmName} was deleted`);
        
        // Remove card from UI immediately
        const card = document.querySelector(`[data-vm-name="${vmName}"]`);
        if (card) {
            card.style.opacity = '0';
            setTimeout(() => card.remove(), 300);
        }
    }

    closeDetailsPanel() {
        window.stateManager.clearCurrentVM();
    }
}

// Create global instance
window.vmController = new VMController();

// Export functions for backward compatibility
window.loadVMs = () => window.vmController.loadVMs();
window.selectVM = (vmName) => window.vmController.selectVM(vmName);
window.startVM = () => window.vmController.startVM();
window.shutdownVM = () => window.vmController.shutdownVM();
window.rebootVM = () => window.vmController.rebootVM();
window.pauseVM = () => window.vmController.pauseVM();
window.resumeVM = () => window.vmController.resumeVM();
window.closeDetailsPanel = () => window.vmController.closeDetailsPanel();