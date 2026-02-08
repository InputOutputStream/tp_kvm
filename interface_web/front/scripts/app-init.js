// scripts/app-init.js
class AppInitializer {
    constructor() {
        this.modules = {
            api: null,
            auth: null,
            state: null,
            vmController: null,
            paasController: null,
            monitoring: null,
            userManagement: null,
            billing: null,
            network: null,
            swarm: null,
            theme: null
        };
    }

    async init() {
        try {
            // 1. Initialize Theme First
            if (window.themeManager) {
                window.themeManager.init();
            }

            // 2. Initialize API Client
            if (window.apiService) {
                this.modules.api = window.apiService;
            }

            // 3. Initialize State Manager
            if (window.stateManager) {
                this.modules.state = window.stateManager;
            }

            // 4. Initialize Auth and wait for user
            if (window.authService) {
                await this.waitForAuth();
                await this.initializeRoleBasedModules();
            }

            // 5. Setup global event listeners
            this.setupGlobalEventListeners();

            console.log('✅ All modules initialized successfully');
        } catch (error) {
            console.error('❌ Failed to initialize app:', error);
        }
    }
    async waitForAuth() {
            return new Promise((resolve) => {
                let attempts = 0;
                const maxAttempts = 50; // Timeout after 5 seconds

                const checkAuth = () => {
                    // If we have a token, we are good
                    if (window.authService && window.authService.token) {
                        resolve(true);
                        return;
                    }
                    
                    // If we are on login/register page, stop waiting
                    if (window.location.pathname.includes('login.html') || 
                        window.location.pathname.includes('register.html')) {
                        resolve(false);
                        return;
                    }

                    // Stop after max attempts to prevent infinite loop
                    attempts++;
                    if (attempts >= maxAttempts) {
                        console.warn('Auth wait timed out');
                        resolve(false);
                        return;
                    }

                    setTimeout(checkAuth, 100);
                };
                checkAuth();
            });
        }
        
    async initializeRoleBasedModules() {
        const user = window.authService.currentUser;
        const isAdmin = user.role === 'admin';

        // Initialize VM Controller
        if (window.vmController) {
            this.modules.vmController = window.vmController;
            await window.vmController.loadVMs();
        }

        // Initialize PaaS Controller
        if (window.paasController) {
            this.modules.paasController = window.paasController;
            // Only show user's apps, not catalog
            document.querySelector('[data-view="paas"]')?.addEventListener('click', () => {
                window.paasController.loadDeployedApps();
            });
        }

        // Initialize User Management (Admin only)
        if (window.UserManagement && isAdmin) {
            this.modules.userManagement = window.UserManagement;
        }

        // Initialize Billing Dashboard
        if (window.BillingDashboard) {
            this.modules.billing = window.BillingDashboard;
            // Add price constants to window for easy access
            window.PRICING = {
                VM: {
                    SMALL: 2500,    // FCFA/month
                    MEDIUM: 3500,
                    LARGE: 6500
                },
                PAAS: {
                    WORDPRESS: 3000,
                    ODOO: 4000,
                    MOODLE: 3700,
                    PRESTASHOP: 4200,
                    NEXCLOUD: 4000,
                    MATTERMOST: 3750,
                    GRAFANA: 2000
                },
                STORAGE: 100,       // FCFA/GB/month
                NETWORK: 500,       // FCFA/network/month
                SWARM_NODE: 1500    // FCFA/node/month
            };
        }

        // Initialize Monitoring Dashboard
        if (window.monitoringDashboard) {
            this.modules.monitoring = window.monitoringDashboard;
        }

        // Initialize Network Service
        if (window.NetworkService) {
            this.modules.network = window.NetworkService;
        }

        // Initialize Swarm
        if (window.loadSwarmClusters) {
            this.modules.swarm = {
                load: window.loadSwarmClusters,
                create: window.showCreateSwarmModal
            };
        }
    }

    setupGlobalEventListeners() {
        // Toast notifications
        window.showToast = function(message, type = 'info') {
            const toast = document.getElementById('toast');
            if (!toast) return;
            
            toast.textContent = message;
            toast.className = `toast ${type} show`;
            
            setTimeout(() => {
                toast.classList.remove('show');
            }, 3000);
        };

        // Connect VM details panel buttons
        this.connectVMActions();
        
        // Connect PaaS actions
        this.connectPaasActions();
        
        // Connect Billing actions
        this.connectBillingActions();
        
        // Connect Swarm actions
        this.connectSwarmActions();
    }

    connectVMActions() {
        // Map HTML buttons to VM controller methods
        const vmActionMap = {
            'startVM': 'startVM',
            'shutdownVM': 'shutdownVM', 
            'rebootVM': 'rebootVM',
            'pauseVM': 'pauseVM',
            'resumeVM': 'resumeVM',
            'closeDetailsPanel': 'closeDetailsPanel'
        };

        Object.entries(vmActionMap).forEach(([action, method]) => {
            if (window[action] && window.vmController?.[method]) {
                const original = window[action];
                window[action] = function() {
                    return window.vmController[method]();
                };
            }
        });

        // Connect clone modal
        if (window.VMCloning) {
            window.showCloneModal = function() {
                const vmName = window.stateManager?.getState('currentVM');
                if (vmName) {
                    window.VMCloning.showCloneModal(vmName);
                } else {
                    showToast('Please select a VM first', 'warning');
                }
            };
        }

        // Connect console modal
        if (window.VNCConsole) {
            window.showConsoleModal = function() {
                const vmName = window.stateManager?.getState('currentVM');
                if (vmName) {
                    window.VNCConsole.openConsole(vmName);
                } else {
                    showToast('Please select a VM first', 'warning');
                }
            };
        }

        // Connect delete modal
        if (window.showDeleteVMModal) {
            // Keep original but add current VM check
            const originalDelete = window.showDeleteVMModal;
            window.showDeleteVMModal = function() {
                const vmName = window.stateManager?.getState('currentVM');
                if (vmName) {
                    originalDelete();
                } else {
                    showToast('Please select a VM first', 'warning');
                }
            };
        }
    }

    connectPaasActions() {
        // Replace old functions with controller methods
        if (window.paasController) {
            window.refreshApps = () => window.paasController.loadDeployedApps();
            window.deployApp = (appId) => window.paasController.deployApp(appId);
            window.deleteApp = (appId) => window.paasController.deleteApp(appId);
            window.switchToPaasCatalog = () => window.paasController.showCatalogView();
            
            // Connect quick deploy cards
            document.querySelectorAll('.deploy-card').forEach(card => {
                const appId = card.getAttribute('onclick')?.match(/'([^']+)'/)?.[1];
                if (appId) {
                    card.onclick = () => window.paasController.deployApp(appId);
                }
            });
        }
    }

    connectBillingActions() {
        if (window.BillingDashboard) {
            window.updateBilling = () => window.BillingDashboard.loadBillingData();
            
            // Initialize billing when view is shown
            document.querySelector('[data-view="billing"]')?.addEventListener('click', () => {
                setTimeout(() => {
                    if (window.BillingDashboard && !window.BillingDashboard.initialized) {
                        window.BillingDashboard.init();
                        window.BillingDashboard.initialized = true;
                    }
                }, 100);
            });
        }
    }

    connectSwarmActions() {
        // Connect swarm deployment form
        const swarmForm = document.querySelector('.swarm-form');
        if (swarmForm) {
            swarmForm.querySelector('button[type="button"]').onclick = (e) => {
                e.preventDefault();
                const managers = swarmForm.querySelector('input[placeholder="1, 3, 5..."]').value;
                const workers = swarmForm.querySelector('input[placeholder="2"]').value;
                
                if (window.deploySwarmCluster) {
                    // Create a mock event and call the function
                    const event = { preventDefault: () => {} };
                    window.deploySwarmCluster(event);
                }
            };
        }
    }

    // Price calculation helper
    calculateVMPrice(flavor, storageGB = 20) {
        const basePrice = window.PRICING?.VM[flavor?.toUpperCase()] || 3500;
        const storagePrice = (storageGB * window.PRICING?.STORAGE) || 0;
        return basePrice + storagePrice;
    }

    calculatePaaSPrice(appId) {
        return window.PRICING?.PAAS[appId?.toUpperCase()] || 3000;
    }
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.appInitializer = new AppInitializer();
    
    // Short delay to ensure all scripts are loaded
    setTimeout(() => {
        window.appInitializer.init();
    }, 500);
});