// ==========================================
// UNIFIED STATE MANAGEMENT SYSTEM
// Fixes: VM deletion polling, state synchronization, redundancy
// ==========================================

class StateManager {
    constructor() {
        this.state = {
            currentVM: null,
            vms: [],
            paasApps: [],
            swarmClusters: [],
            networks: [],
            users: [],
            activePollers: new Map(), // Track active polling intervals
            viewState: 'dashboard'
        };
        
        this.listeners = new Map();
        this.pollingIntervals = new Map();
    }

    // ==========================================
    // State Getters/Setters
    // ==========================================
    
    setState(key, value) {
        const oldValue = this.state[key];
        this.state[key] = value;
        this.notify(key, value, oldValue);
    }

    getState(key) {
        return this.state[key];
    }

    // ==========================================
    // Observer Pattern for State Changes
    // ==========================================
    
    subscribe(key, callback) {
        if (!this.listeners.has(key)) {
            this.listeners.set(key, new Set());
        }
        this.listeners.get(key).add(callback);
        
        // Return unsubscribe function
        return () => {
            this.listeners.get(key)?.delete(callback);
        };
    }

    notify(key, newValue, oldValue) {
        this.listeners.get(key)?.forEach(callback => {
            callback(newValue, oldValue);
        });
    }

    // ==========================================
    // VM State Management
    // ==========================================
    
    setCurrentVM(vmName) {
        const oldVM = this.state.currentVM;
        
        // Stop polling for old VM
        if (oldVM) {
            this.stopVMPolling(oldVM);
        }
        
        this.setState('currentVM', vmName);
        
        // Start polling for new VM if it exists
        if (vmName && this.vmExists(vmName)) {
            this.startVMPolling(vmName);
        }
    }

    vmExists(vmName) {
        return this.state.vms.some(vm => vm.name === vmName);
    }

    updateVMsList(vms) {
        const oldVMs = this.state.vms;
        this.setState('vms', vms);
        
        // Check if current VM was deleted
        if (this.state.currentVM && !this.vmExists(this.state.currentVM)) {
            console.log(`Current VM ${this.state.currentVM} was deleted, clearing selection`);
            this.clearCurrentVM();
        }
        
        // Notify about deleted VMs
        const deletedVMs = oldVMs.filter(oldVM => 
            !vms.some(vm => vm.name === oldVM.name)
        );
        
        deletedVMs.forEach(vm => {
            this.notify('vmDeleted', vm.name);
        });
    }

    clearCurrentVM() {
        if (this.state.currentVM) {
            this.stopVMPolling(this.state.currentVM);
        }
        this.setState('currentVM', null);
        
        // Close details panel if open
        const panel = document.getElementById('vm-details-panel');
        if (panel) {
            panel.classList.remove('open');
        }
    }

    // ==========================================
    // Polling Management (Prevents stale data fetches)
    // ==========================================
    
    startVMPolling(vmName) {
        // Clear any existing poller for this VM
        this.stopVMPolling(vmName);
        
        const pollerId = `vm-${vmName}`;
        
        // Start stats polling
        const interval = setInterval(async () => {
            // Double-check VM still exists before polling
            if (!this.vmExists(vmName)) {
                console.log(`Stopping polling for deleted VM: ${vmName}`);
                this.stopVMPolling(vmName);
                return;
            }
            
            try {
                const data = await window.apiService.getVMStats(vmName);
                if (data.stats) {
                    this.notify(`vm-stats-${vmName}`, data.stats);
                }
            } catch (error) {
                // VM might have been deleted
                if (error.message.includes('404') || error.message.includes('not found')) {
                    console.log(`VM ${vmName} not found during polling, stopping`);
                    this.stopVMPolling(vmName);
                    await this.refreshVMsList();
                }
            }
        }, 3000);
        
        this.pollingIntervals.set(pollerId, interval);
        console.log(`Started polling for VM: ${vmName}`);
    }

    stopVMPolling(vmName) {
        const pollerId = `vm-${vmName}`;
        const interval = this.pollingIntervals.get(pollerId);
        
        if (interval) {
            clearInterval(interval);
            this.pollingIntervals.delete(pollerId);
            console.log(`Stopped polling for VM: ${vmName}`);
        }
    }

    stopAllPolling() {
        this.pollingIntervals.forEach((interval, key) => {
            clearInterval(interval);
            console.log(`Stopped polling: ${key}`);
        });
        this.pollingIntervals.clear();
    }

    // ==========================================
    // Data Refresh Methods
    // ==========================================
    
    async refreshVMsList() {
        try {
            const data = await window.apiService.getVMs();
            if (data.success && data.vms) {
                this.updateVMsList(data.vms);
                return data.vms;
            }
        } catch (error) {
            console.error('Error refreshing VMs list:', error);
            throw error;
        }
    }

    async refreshPaaSApps() {
        try {
            const data = await window.apiService.getPaasApplications();
            if (data.success && data.applications) {
                this.setState('paasApps', data.applications);
                return data.applications;
            }
        } catch (error) {
            console.error('Error refreshing PaaS apps:', error);
            throw error;
        }
    }

    async refreshSwarmClusters() {
        try {
            const username = window.authService?.currentUser?.username;
            if (!username) return [];
            
            const data = await window.apiService.getSwarmClusters(username);
            if (data.success && data.clusters) {
                this.setState('swarmClusters', data.clusters);
                return data.clusters;
            }
        } catch (error) {
            console.error('Error refreshing Swarm clusters:', error);
            throw error;
        }
    }

    async refreshNetworks() {
        try {
            const data = await window.apiService.getNetworks();
            if (data.success) {
                this.setState('networks', data);
                return data;
            }
        } catch (error) {
            console.error('Error refreshing networks:', error);
            throw error;
        }
    }

    // ==========================================
    // View State Management
    // ==========================================
    
    setView(viewName) {
        const oldView = this.state.viewState;
        this.setState('viewState', viewName);
        
        // Stop unnecessary polling when leaving views
        if (oldView === 'vms' && viewName !== 'vms') {
            this.clearCurrentVM();
        }
        
        if (oldView === 'monitoring' && viewName !== 'monitoring') {
            this.notify('stopMonitoring');
        }
    }

    // ==========================================
    // Cleanup
    // ==========================================
    
    cleanup() {
        this.stopAllPolling();
        this.listeners.clear();
        this.state = {
            currentVM: null,
            vms: [],
            paasApps: [],
            swarmClusters: [],
            networks: [],
            users: [],
            activePollers: new Map(),
            viewState: 'dashboard'
        };
    }
}

// Create global instance
window.stateManager = new StateManager();

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
    window.stateManager.cleanup();
});