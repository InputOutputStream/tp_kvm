// ==========================================
// PAAS DEPLOYMENT WIZARD
// ==========================================

class PaaSDeploymentWizard {
    constructor() {
        this.api = window.apiService;
        this.currentStep = 1;
        this.deploymentConfig = {
            app: null,
            resources: null,
            host: null,
            database: null
        };
    }

    async showDeploymentWizard() {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'paas-deployment-wizard';
        
        modal.innerHTML = `
            <div class="modal modal-large">
                <div class="modal-header">
                    <h3>🚀 Deploy Application</h3>
                    <button class="modal-close" onclick="closeModal('paas-deployment-wizard')">✖</button>
                </div>
                <div class="modal-body">
                    <div class="wizard-progress">
                        <div class="wizard-step ${this.currentStep >= 1 ? 'active' : ''}">
                            <div class="step-number">1</div>
                            <div class="step-label">Select App</div>
                        </div>
                        <div class="wizard-step ${this.currentStep >= 2 ? 'active' : ''}">
                            <div class="step-number">2</div>
                            <div class="step-label">Configure</div>
                        </div>
                        <div class="wizard-step ${this.currentStep >= 3 ? 'active' : ''}">
                            <div class="step-number">3</div>
                            <div class="step-label">Select Host</div>
                        </div>
                        <div class="wizard-step ${this.currentStep >= 4 ? 'active' : ''}">
                            <div class="step-number">4</div>
                            <div class="step-label">Review & Deploy</div>
                        </div>
                    </div>
                    
                    <div class="wizard-content">
                        ${await this.renderStep(this.currentStep)}
                    </div>
                </div>
                <div class="modal-footer">
                    ${this.renderWizardButtons()}
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
    }

    async renderStep(step) {
        switch(step) {
            case 1: return await this.renderStep1();
            case 2: return await this.renderStep2();
            case 3: return await this.renderStep3();
            case 4: return await this.renderStep4();
            default: return '<p>Invalid step</p>';
        }
    }

    async renderStep1() {
        const apps = window.PAAS_APPS_EXTENDED || [];
        
        return `
            <h3>📦 Select Application</h3>
            <div class="apps-catalog-grid">
                ${apps.map(app => `
                    <div class="app-select-card" onclick="paasWizard.selectApp('${app.id}')">
                        <div class="app-icon-large">${app.icon}</div>
                        <h4>${app.name}</h4>
                        <p class="app-description">${app.description}</p>
                        <div class="app-meta">
                            <span class="app-category">${app.category}</span>
                            <span class="app-database">${app.database || 'No DB'}</span>
                            <span class="app-price">${app.price} FCFA/mo</span>
                        </div>
                    </div>
                `).join('')}
            </div>
        `;
    }

    async renderStep2() {
        if (!this.deploymentConfig.app) return '<p>No app selected</p>';
        
        const app = window.PAAS_APPS_EXTENDED.find(a => a.id === this.deploymentConfig.app);
        
        return `
            <h3>⚙️ Configure ${app.name}</h3>
            <form id="app-config-form">
                <div class="form-group">
                    <label>Application Name *</label>
                    <input type="text" id="app-name" value="${app.id}-${Date.now().toString().slice(-4)}" required>
                    <small>Unique name for your deployment</small>
                </div>
                
                <div class="form-row">
                    <div class="form-group">
                        <label>CPU Cores</label>
                        <select id="app-cpu">
                            <option value="0.5">0.5 vCPU</option>
                            <option value="1" selected>1 vCPU</option>
                            <option value="2">2 vCPU</option>
                            <option value="4">4 vCPU</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>Memory</label>
                        <select id="app-memory">
                            <option value="512">512 MB</option>
                            <option value="1024" selected>1 GB</option>
                            <option value="2048">2 GB</option>
                            <option value="4096">4 GB</option>
                        </select>
                    </div>
                </div>
                
                <div class="form-group">
                    <label>Storage (GB)</label>
                    <input type="number" id="app-storage" value="10" min="5" max="100">
                </div>
                
                ${app.database ? `
                    <div class="form-group">
                        <label>Database Configuration</label>
                        <div class="database-config">
                            <label class="checkbox-label">
                                <input type="checkbox" id="enable-database" checked>
                                <span>Enable ${app.database} database</span>
                            </label>
                            <small>Automatic credentials will be generated</small>
                        </div>
                    </div>
                ` : ''}
                
                <div class="form-group">
                    <label>Environment Variables</label>
                    <div id="env-vars-container">
                        <div class="env-var-row">
                            <input type="text" placeholder="Variable name" class="env-key">
                            <input type="text" placeholder="Value" class="env-value">
                            <button type="button" class="btn btn-sm btn-danger" onclick="removeEnvVar(this)">🗑️</button>
                        </div>
                    </div>
                    <button type="button" class="btn btn-sm btn-secondary" onclick="addEnvVar()">
                        ➕ Add Variable
                    </button>
                </div>
            </form>
        `;
    }

    async renderStep3() {
        try {
            const hosts = await this.api.getHosts();
            
            return `
                <h3>🖥️ Select Deployment Host</h3>
                <div class="hosts-selection-grid">
                    ${hosts.hosts?.map(host => `
                        <label class="host-select-card">
                            <input type="radio" name="deployment-host" value="${host.id}">
                            <div class="host-card-content">
                                <h4>${host.hostname}</h4>
                                <div class="host-stats">
                                    <span>💻 ${host.availableCPUs} cores free</span>
                                    <span>💾 ${Math.round(host.availableMemory / 1024)} GB free</span>
                                </div>
                                <div class="host-status ${host.active ? 'active' : 'inactive'}">
                                    ${host.active ? '🟢 Active' : '🔴 Inactive'}
                                </div>
                            </div>
                        </label>
                    `).join('') || '<p>No hosts available</p>'}
                </div>
                
                <div class="form-group" style="margin-top: 20px;">
                    <label>Host Selection Strategy</label>
                    <select id="host-strategy">
                        <option value="auto">Auto-select (Recommended)</option>
                        <option value="manual">Manual selection</option>
                    </select>
                </div>
            `;
        } catch (error) {
            return `<p class="error-text">Error loading hosts: ${error.message}</p>`;
        }
    }

    async renderStep4() {
        const app = window.PAAS_APPS_EXTENDED.find(a => a.id === this.deploymentConfig.app);
        
        return `
            <h3>📋 Review Deployment</h3>
            <div class="review-summary">
                <div class="review-section">
                    <h4>Application</h4>
                    <div class="review-item">
                        <strong>Name:</strong> ${document.getElementById('app-name')?.value || 'N/A'}
                    </div>
                    <div class="review-item">
                        <strong>Type:</strong> ${app.name}
                    </div>
                    <div class="review-item">
                        <strong>Image:</strong> ${app.image}
                    </div>
                </div>
                
                <div class="review-section">
                    <h4>Resources</h4>
                    <div class="review-item">
                        <strong>CPU:</strong> ${document.getElementById('app-cpu')?.value || 1} vCPU
                    </div>
                    <div class="review-item">
                        <strong>Memory:</strong> ${document.getElementById('app-memory')?.value || 1024} MB
                    </div>
                    <div class="review-item">
                        <strong>Storage:</strong> ${document.getElementById('app-storage')?.value || 10} GB
                    </div>
                </div>
                
                <div class="review-section">
                    <h4>Database</h4>
                    <div class="review-item">
                        <strong>Type:</strong> ${app.database || 'None'}
                    </div>
                    <div class="review-item">
                        <strong>Status:</strong> ${document.getElementById('enable-database')?.checked ? 'Enabled' : 'Disabled'}
                    </div>
                </div>
                
                <div class="review-section">
                    <h4>Cost Estimate</h4>
                    <div class="review-item">
                        <strong>Monthly:</strong> ${app.price} FCFA
                    </div>
                    <div class="review-item">
                        <strong>Resources:</strong> 500 FCFA
                    </div>
                    <div class="review-item total">
                        <strong>Total:</strong> ${app.price + 500} FCFA/month
                    </div>
                </div>
            </div>
            
            <div class="warning-banner" style="margin-top: 20px;">
                <span class="warning-icon">⚠️</span>
                <p>Deployment will take 2-5 minutes. Your application will be available at a unique URL.</p>
            </div>
        `;
    }

    renderWizardButtons() {
        let buttons = '';
        
        if (this.currentStep > 1) {
            buttons += `<button class="btn btn-secondary" onclick="paasWizard.previousStep()">← Previous</button>`;
        }
        
        if (this.currentStep < 4) {
            buttons += `<button class="btn btn-primary" onclick="paasWizard.nextStep()">Next →</button>`;
        } else {
            buttons += `<button class="btn btn-success" onclick="paasWizard.deployApplication()">🚀 Deploy Now</button>`;
        }
        
        return buttons;
    }

    selectApp(appId) {
        this.deploymentConfig.app = appId;
        this.nextStep();
    }

    nextStep() {
        if (this.currentStep < 4) {
            this.currentStep++;
            this.showDeploymentWizard();
        }
    }

    previousStep() {
        if (this.currentStep > 1) {
            this.currentStep--;
            this.showDeploymentWizard();
        }
    }

    async deployApplication() {
        try {
            showToast('Starting deployment...', 'info');
            
            // Gather all configuration
            const app = window.PAAS_APPS_EXTENDED.find(a => a.id === this.deploymentConfig.app);
            const appName = document.getElementById('app-name')?.value;
            const cpu = document.getElementById('app-cpu')?.value;
            const memory = document.getElementById('app-memory')?.value;
            const storage = document.getElementById('app-storage')?.value;
            const hostId = document.querySelector('input[name="deployment-host"]:checked')?.value;
            
            const deploymentConfig = {
                id: `${authService.currentUser.username}_${appName}`,
                name: appName,
                dockerImage: app.image,
                ports: app.ports,
                resources: {
                    cpu: parseFloat(cpu),
                    memory: parseInt(memory),
                    storage: parseInt(storage)
                },
                database: app.database ? {
                    type: app.database,
                    enabled: document.getElementById('enable-database')?.checked || false
                } : null,
                environment: this.gatherEnvironmentVariables(),
                hostId: hostId || 'auto'
            };
            
            // Call deployment API
            const result = await this.api.deployPaasApplication(deploymentConfig);
            
            if (result.success) {
                showToast('✅ Deployment started successfully!', 'success');
                closeModal('paas-deployment-wizard');
                
                // Show deployment progress
                this.showDeploymentProgress(result.deploymentId);
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ Deployment failed: ${error.message}`, 'error');
        }
    }

    gatherEnvironmentVariables() {
        const envVars = {};
        const rows = document.querySelectorAll('.env-var-row');
        
        rows.forEach(row => {
            const key = row.querySelector('.env-key').value;
            const value = row.querySelector('.env-value').value;
            if (key && value) {
                envVars[key] = value;
            }
        });
        
        return envVars;
    }

    showDeploymentProgress(deploymentId) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'deployment-progress-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>🚀 Deployment in Progress</h3>
                </div>
                <div class="modal-body">
                    <div class="deployment-progress">
                        <div class="progress-step active">
                            <div class="step-icon">⏳</div>
                            <div class="step-text">Initializing deployment</div>
                        </div>
                        <div class="progress-step">
                            <div class="step-icon">📦</div>
                            <div class="step-text">Pulling container image</div>
                        </div>
                        <div class="progress-step">
                            <div class="step-icon">🗄️</div>
                            <div class="step-text">Setting up database</div>
                        </div>
                        <div class="progress-step">
                            <div class="step-icon">🌐</div>
                            <div class="step-text">Configuring network</div>
                        </div>
                        <div class="progress-step">
                            <div class="step-icon">✅</div>
                            <div class="step-text">Deployment complete</div>
                        </div>
                    </div>
                    
                    <div class="deployment-log" id="deployment-log">
                        Starting deployment...
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="closeModal('deployment-progress-modal')">
                        Close (Deployment continues)
                    </button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        // Simulate progress updates (in real app, this would be WebSocket/SSE)
        this.simulateDeploymentProgress(deploymentId);
    }

    simulateDeploymentProgress(deploymentId) {
        const steps = document.querySelectorAll('.progress-step');
        const log = document.getElementById('deployment-log');
        
        const updates = [
            { delay: 2000, step: 1, message: 'Validating configuration...' },
            { delay: 4000, step: 2, message: 'Pulling Docker image...' },
            { delay: 6000, step: 3, message: 'Creating database container...' },
            { delay: 8000, step: 4, message: 'Setting up network routing...' },
            { delay: 10000, step: 5, message: '✅ Deployment completed successfully!' }
        ];
        
        updates.forEach((update, index) => {
            setTimeout(() => {
                // Update step
                steps.forEach((step, i) => {
                    step.classList.toggle('active', i <= update.step);
                    step.classList.toggle('complete', i < update.step);
                });
                
                // Update log
                log.innerHTML = update.message + '<br>' + log.innerHTML;
                
                // If final step, update button
                if (index === updates.length - 1) {
                    const footer = modal.querySelector('.modal-footer');
                    footer.innerHTML = `
                        <button class="btn btn-success" onclick="closeModal('deployment-progress-modal'); window.paasManager.refreshApps()">
                            ✅ View Application
                        </button>
                    `;
                }
            }, update.delay);
        });
    }
}

// Helper functions
function addEnvVar() {
    const container = document.getElementById('env-vars-container');
    const row = document.createElement('div');
    row.className = 'env-var-row';
    row.innerHTML = `
        <input type="text" placeholder="Variable name" class="env-key">
        <input type="text" placeholder="Value" class="env-value">
        <button type="button" class="btn btn-sm btn-danger" onclick="removeEnvVar(this)">🗑️</button>
    `;
    container.appendChild(row);
}

function removeEnvVar(button) {
    button.closest('.env-var-row').remove();
}

// Initialize
window.paasWizard = new PaaSDeploymentWizard();
window.addEnvVar = addEnvVar;
window.removeEnvVar = removeEnvVar;