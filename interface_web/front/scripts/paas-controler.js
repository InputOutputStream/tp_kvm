// ==========================================
// UNIFIED PAAS CONTROLLER
// Consolidates: paas-apps.js, paas-catalog.js, paas-catalog-view.js
// ==========================================

class PaaSController {
    constructor() {
        this.catalog = [
            {
                id: 'wordpress',
                name: 'WordPress',
                category: 'web',
                description: 'CMS for creating websites and blogs',
                icon: '📝',
                ports: ['80:80'],
                database: 'mariadb',
                price: 3000,
                image: 'wordpress:latest'
            },
            {
                id: 'odoo',
                name: 'Odoo',
                category: 'web',
                description: 'Open source ERP and CRM',
                icon: '📊',
                ports: ['8069:8069'],
                database: 'postgresql',
                price: 4000,
                image: 'odoo:16'
            },
            {
                id: 'moodle',
                name: 'Moodle',
                category: 'web',
                description: 'Learning management system',
                icon: '🎓',
                ports: ['80:80'],
                database: 'mariadb',
                price: 3700,
                image: 'moodle/moodle:latest'
            },
            {
                id: 'prestashop',
                name: 'PrestaShop',
                category: 'web',
                description: 'E-commerce platform',
                icon: '🛒',
                ports: ['80:80'],
                database: 'mariadb',
                price: 4200,
                image: 'prestashop/prestashop:latest'
            },
            {
                id: 'nextcloud',
                name: 'NextCloud',
                category: 'collaboration',
                description: 'Private cloud and collaboration',
                icon: '☁️',
                ports: ['80:80'],
                database: 'mariadb',
                price: 4000,
                image: 'nextcloud:latest'
            },
            {
                id: 'mattermost',
                name: 'Mattermost',
                category: 'collaboration',
                description: 'Team messaging platform',
                icon: '💬',
                ports: ['8065:8065'],
                database: 'postgresql',
                price: 3750,
                image: 'mattermost/mattermost-team-edition:latest'
            },
            {
                id: 'grafana',
                name: 'Grafana',
                category: 'monitoring',
                description: 'Monitoring dashboards',
                icon: '📈',
                ports: ['3000:3000'],
                database: 'postgresql',
                price: 2000,
                image: 'grafana/grafana:latest'
            }
        ];
        
        this.setupEventListeners();
    }

    // ==========================================
    // Event Listeners
    // ==========================================
    
    setupEventListeners() {
        // Subscribe to state changes
        window.stateManager.subscribe('paasApps', (apps) => {
            this.renderDeployedApps(apps);
        });

        // Search functionality
        const searchInput = document.getElementById('app-search');
        if (searchInput) {
            searchInput.addEventListener('input', (e) => this.handleSearch(e));
        }

        // Category filters
        document.querySelectorAll('[data-category]').forEach(chip => {
            chip.addEventListener('click', (e) => this.handleCategoryFilter(e));
        });
    }

    // ==========================================
    // View Management
    // ==========================================
    
    showCatalogView() {
        // Hide all views
        document.querySelectorAll('.view').forEach(view => {
            view.classList.remove('active');
        });
        
        // Show catalog view
        const catalogView = document.getElementById('view-paas-catalog');
        if (catalogView) {
            catalogView.classList.add('active');
            this.renderCatalog();
        }
    }

    showDeployedAppsView() {
        // This is the main PaaS view
        this.loadDeployedApps();
    }

    // ==========================================
    // Load Deployed Apps
    // ==========================================
    
    async loadDeployedApps() {
        const appsGrid = document.getElementById('apps-grid');
        if (!appsGrid) return;
        
        appsGrid.innerHTML = '<p class="loading-text">Loading applications...</p>';
        
        try {
            const apps = await window.stateManager.refreshPaaSApps();
            
            if (!apps || apps.length === 0) {
                appsGrid.innerHTML = `
                    <div class="empty-state-large">
                        <div class="empty-icon">📦</div>
                        <h3>No Applications Deployed</h3>
                        <p>Deploy your first application from the catalog</p>
                        <button class="btn btn-primary" onclick="paasController.showCatalogView()">
                            Browse Catalog
                        </button>
                    </div>
                `;
                return;
            }
            
            this.renderDeployedApps(apps);
        } catch (error) {
            console.error('Error loading PaaS apps:', error);
            appsGrid.innerHTML = `<p class="error-text">Error: ${error.message}</p>`;
        }
    }

    // ==========================================
    // Render Deployed Apps
    // ==========================================
    
    renderDeployedApps(apps) {
        const appsGrid = document.getElementById('apps-grid');
        if (!appsGrid) return;
        
        appsGrid.innerHTML = '';
        
        apps.forEach(app => {
            appsGrid.appendChild(this.createDeployedAppCard(app));
        });
    }

    createDeployedAppCard(app) {
        const card = document.createElement('div');
        card.className = 'app-card deployed-app';
        
        const statusClass = app.running ? 'running' : 'stopped';
        const statusText = app.running ? '🟢 Running' : '🔴 Stopped';
        
        // Find catalog info if available
        const catalogApp = this.catalog.find(a => a.id === app.type);
        const icon = catalogApp?.icon || '📦';
        
        card.innerHTML = `
            <div class="app-card-header">
                <div class="app-card-icon">${icon}</div>
                <span class="status-badge status-${statusClass}">${statusText}</span>
            </div>
            <h4 class="app-card-title">${app.name}</h4>
            <p class="app-card-description">${app.status || 'Running'}</p>
            <div class="app-card-meta">
                ${app.url ? `<a href="${app.url}" target="_blank" class="app-url">🌐 Open App</a>` : ''}
            </div>
            <div class="app-card-actions">
                <button class="btn btn-sm btn-secondary" onclick="paasController.showAppDetails('${app.id}')">
                    ℹ️ Details
                </button>
                ${app.running ? 
                    `<button class="btn btn-sm btn-warning" onclick="paasController.stopApp('${app.id}')">⏸️ Stop</button>` :
                    `<button class="btn btn-sm btn-success" onclick="paasController.startApp('${app.id}')">▶️ Start</button>`
                }
                <button class="btn btn-sm btn-danger" onclick="paasController.deleteApp('${app.id}')">
                    🗑️ Delete
                </button>
            </div>
        `;
        
        return card;
    }

    // ==========================================
    // Render Catalog
    // ==========================================
    
    renderCatalog() {
        const catalogGrid = document.getElementById('paas-catalog-grid');
        if (!catalogGrid) {
            console.error('Catalog grid not found');
            return;
        }

        catalogGrid.innerHTML = '';

        this.catalog.forEach(app => {
            catalogGrid.appendChild(this.createCatalogCard(app));
        });
    }

    createCatalogCard(app) {
        const card = document.createElement('div');
        card.className = 'catalog-card';
        
        card.innerHTML = `
            <div class="catalog-card-icon">${app.icon || '📦'}</div>
            <h3 class="catalog-card-title">${app.name}</h3>
            <p class="catalog-card-description">${app.description}</p>
            <div class="catalog-card-meta">
                <span class="catalog-category-badge">${app.category || 'App'}</span>
                <span class="catalog-price">${app.price || 0} FCFA/mo</span>
            </div>
            <div class="catalog-card-footer">
                ${app.database ? `<span class="tech-badge">🗄️ ${app.database}</span>` : ''}
                ${app.ports ? `<span class="tech-badge">🔌 ${app.ports[0]}</span>` : ''}
            </div>
            <div class="catalog-card-actions">
                <button class="btn btn-primary" onclick="paasController.deployApp('${app.id}')" style="flex: 1;">
                    🚀 Deploy Now
                </button>
                <button class="btn btn-secondary" onclick="paasController.showCatalogAppDetails('${app.id}')">
                    ℹ️ Info
                </button>
            </div>
        `;
        
        return card;
    }

    // ==========================================
    // Deploy App
    // ==========================================
    
    async deployApp(appId) {
        const app = this.catalog.find(a => a.id === appId);
        if (!app) {
            window.showToast?.('Application not found', 'error');
            return;
        }
        
        try {
            window.showToast?.(`🚀 Deploying ${app.name}...`, 'info');
            
            let dbCredentials = null;
            
            // Step 1: Provision database if needed
            if (app.database) {
                window.showToast?.(`📊 Creating ${app.database} database...`, 'info');
                dbCredentials = await this.provisionDatabase(app);
                
                if (!dbCredentials) {
                    throw new Error(`Failed to provision ${app.database} database`);
                }
                
                window.showToast?.('✅ Database created', 'success');
            }
            
            // Step 2: Generate configuration
            const config = this.generateDeployConfig(app, dbCredentials);
            
            // Step 3: Deploy via API
            const result = await window.api.deployPaasApplication(config);
            
            if (result.success) {
                window.showToast?.(`✅ ${app.name} deployed successfully!`, 'success');
                
                // Show access info
                this.showAppAccessInfo(app, result);
                
                // Refresh apps list
                await this.loadDeployedApps();
                
                // Switch to deployed apps view
                window.switchView?.('paas');
            } else {
                throw new Error(result.error || 'Deployment failed');
            }
            
        } catch (error) {
            window.showToast?.(`❌ ${error.message}`, 'error');
            console.error('Deployment error:', error);
        }
    }

    // ==========================================
    // Database Provisioning
    // ==========================================
    
    async provisionDatabase(app) {
        const username = window.authService?.currentUser?.username;
        if (!username) {
            throw new Error('User not authenticated');
        }
        
        try {
            const response = await window.api.post('/paas/database', {
                type: app.database,
                username: username,
                appname: app.id
            });
            
            if (response.success) {
                return response.credentials;
            }
            
            // Fallback for testing
            return {
                host: app.database === 'postgresql' ? 'thoth-postgres' : 'thoth-mariadb',
                port: app.database === 'postgresql' ? 5432 : 3306,
                database: `${username}_${app.id}`,
                user: `${username}_${app.id}_user`,
                password: 'temp_' + Math.random().toString(36).substring(7)
            };
            
        } catch (error) {
            console.error('Database provisioning error:', error);
            return null;
        }
    }

    // ==========================================
    // Generate Deploy Config
    // ==========================================
    
    generateDeployConfig(app, dbCredentials) {
        const username = window.authService?.currentUser?.username;
        const appId = `${username}_${app.id}`;
        
        let environment = {};
        
        if (dbCredentials) {
            if (app.database === 'mariadb') {
                environment = {
                    MYSQL_HOST: dbCredentials.host,
                    MYSQL_DATABASE: dbCredentials.database,
                    MYSQL_USER: dbCredentials.user,
                    MYSQL_PASSWORD: dbCredentials.password,
                    MYSQL_PORT: dbCredentials.port.toString()
                };
            } else if (app.database === 'postgresql') {
                environment = {
                    POSTGRES_HOST: dbCredentials.host,
                    POSTGRES_DB: dbCredentials.database,
                    POSTGRES_USER: dbCredentials.user,
                    POSTGRES_PASSWORD: dbCredentials.password,
                    POSTGRES_PORT: dbCredentials.port.toString()
                };
            }
        }
        
        return {
            id: appId,
            name: app.name,
            image: app.image,
            ports: app.ports || [],
            environment: environment,
            category: app.category
        };
    }

    // ==========================================
    // App Actions
    // ==========================================
    
    async stopApp(appId) {
        try {
            window.showToast?.('Stopping application...', 'info');
            await window.api.stopPaasApplication(appId);
            window.showToast?.('✅ Application stopped', 'success');
            await this.loadDeployedApps();
        } catch (error) {
            window.showToast?.(`❌ ${error.message}`, 'error');
        }
    }

    async startApp(appId) {
        try {
            window.showToast?.('Starting application...', 'info');
            await window.api.startPaasApplication(appId);
            window.showToast?.('✅ Application started', 'success');
            await this.loadDeployedApps();
        } catch (error) {
            window.showToast?.(`❌ ${error.message}`, 'error');
        }
    }

    async deleteApp(appId) {
        if (!confirm('Delete this application?\n\n⚠️ This action cannot be undone.')) {
            return;
        }
        
        try {
            window.showToast?.('Deleting application...', 'info');
            await window.api.deletePaasApplication(appId);
            window.showToast?.('✅ Application deleted', 'success');
            await this.loadDeployedApps();
        } catch (error) {
            window.showToast?.(`❌ ${error.message}`, 'error');
        }
    }

    // ==========================================
    // App Details Modal
    // ==========================================
    
    async showAppDetails(appId) {
        try {
            const data = await window.api.getPaasApplicationDetails(appId);
            
            if (!data.success) {
                window.showToast?.('Failed to load app details', 'error');
                return;
            }
            
            const modal = document.createElement('div');
            modal.className = 'modal-overlay active';
            modal.id = 'app-details-modal';
            
            modal.innerHTML = `
                <div class="modal modal-large">
                    <div class="modal-header">
                        <h3>📦 ${data.application.name}</h3>
                        <button class="modal-close" onclick="this.closest('.modal-overlay').remove()">✖</button>
                    </div>
                    <div class="modal-body">
                        <div class="tabs">
                            <div class="tab-buttons">
                                <button class="tab-btn active" onclick="paasController.switchTab('info')">Info</button>
                                <button class="tab-btn" onclick="paasController.switchTab('logs')">Logs</button>
                                <button class="tab-btn" onclick="paasController.switchTab('stats')">Stats</button>
                            </div>
                            <div class="tab-content">
                                <div id="tab-info" class="tab-pane active">
                                    <div class="info-grid">
                                        <div class="info-item">
                                            <strong>Status:</strong>
                                            <span>${data.application.status}</span>
                                        </div>
                                        <div class="info-item">
                                            <strong>Image:</strong>
                                            <code>${data.application.image}</code>
                                        </div>
                                        <div class="info-item">
                                            <strong>Ports:</strong>
                                            <code>${data.application.ports?.join(', ') || 'N/A'}</code>
                                        </div>
                                        ${data.application.url ? `
                                            <div class="info-item">
                                                <strong>URL:</strong>
                                                <a href="${data.application.url}" target="_blank">${data.application.url}</a>
                                            </div>
                                        ` : ''}
                                    </div>
                                </div>
                                <div id="tab-logs" class="tab-pane">
                                    <div id="app-logs-container">Loading logs...</div>
                                </div>
                                <div id="tab-stats" class="tab-pane">
                                    <div id="app-stats-container">Loading stats...</div>
                                </div>
                            </div>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button class="btn btn-secondary" onclick="this.closest('.modal-overlay').remove()">Close</button>
                    </div>
                </div>
            `;
            
            document.body.appendChild(modal);
            
        } catch (error) {
            window.showToast?.(`❌ ${error.message}`, 'error');
        }
    }

    showCatalogAppDetails(appId) {
        const app = this.catalog.find(a => a.id === appId);
        if (!app) return;
        
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'catalog-app-details-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>${app.icon || '📦'} ${app.name}</h3>
                    <button class="modal-close" onclick="this.closest('.modal-overlay').remove()">✖</button>
                </div>
                <div class="modal-body">
                    <p><strong>Description:</strong> ${app.description}</p>
                    <p><strong>Category:</strong> ${app.category}</p>
                    <p><strong>Image:</strong> <code>${app.image}</code></p>
                    <p><strong>Ports:</strong> ${app.ports?.join(', ') || 'Default'}</p>
                    ${app.database ? `<p><strong>Database:</strong> ${app.database}</p>` : ''}
                    <p><strong>Price:</strong> ${app.price || 0} FCFA/month</p>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="this.closest('.modal-overlay').remove()">Close</button>
                    <button class="btn btn-primary" onclick="paasController.deployApp('${app.id}'); this.closest('.modal-overlay').remove();">
                        Deploy
                    </button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
    }

    showAppAccessInfo(app, deployResult) {
        const username = window.authService?.currentUser?.username;
        const appId = `${username}_${app.id}`;
        const port = app.ports?.[0]?.split(':')[0] || '80';
        
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'app-access-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>✅ ${app.name} Deployed Successfully!</h3>
                    <button class="modal-close" onclick="this.closest('.modal-overlay').remove()">✖</button>
                </div>
                <div class="modal-body">
                    <div class="success-banner">
                        <span class="success-icon">🎉</span>
                        <p>Your application is now running!</p>
                    </div>
                    <div class="access-info-grid">
                        <div class="info-item">
                            <strong>Application:</strong>
                            <span>${app.name}</span>
                        </div>
                        <div class="info-item">
                            <strong>Access URL:</strong>
                            <a href="http://localhost:${port}" target="_blank" class="app-url">
                                http://localhost:${port}
                            </a>
                        </div>
                        <div class="info-item">
                            <strong>Container:</strong>
                            <code>${appId}</code>
                        </div>
                        ${app.database ? `
                            <div class="info-item">
                                <strong>Database:</strong>
                                <span>${app.database === 'postgresql' ? 'PostgreSQL' : 'MariaDB'} (Shared)</span>
                            </div>
                        ` : ''}
                    </div>
                    <div class="info-banner" style="margin-top: 20px;">
                        <span class="info-icon">ℹ️</span>
                        <div>
                            <p><strong>Next Steps:</strong></p>
                            <ol>
                                <li>Access the application at the URL above</li>
                                <li>Complete the setup wizard if prompted</li>
                                <li>Database connection is pre-configured</li>
                            </ol>
                        </div>
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-primary" onclick="window.open('http://localhost:${port}', '_blank')">
                        🌐 Open Application
                    </button>
                    <button class="btn btn-secondary" onclick="this.closest('.modal-overlay').remove()">
                        Close
                    </button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
    }

    // ==========================================
    // Tab Switching
    // ==========================================
    
    switchTab(tabName) {
        // Update buttons
        document.querySelectorAll('.tab-btn').forEach(btn => {
            btn.classList.remove('active');
        });
        event.target.classList.add('active');
        
        // Update panes
        document.querySelectorAll('.tab-pane').forEach(pane => {
            pane.classList.remove('active');
        });
        document.getElementById(`tab-${tabName}`).classList.add('active');
        
        // Load content if needed
        if (tabName === 'logs') {
            this.loadAppLogs();
        } else if (tabName === 'stats') {
            this.loadAppStats();
        }
    }

    async loadAppLogs() {
        const container = document.getElementById('app-logs-container');
        if (!container) return;
        
        // Implementation would fetch and display logs
        container.innerHTML = '<pre class="log-viewer">Loading logs...</pre>';
    }

    async loadAppStats() {
        const container = document.getElementById('app-stats-container');
        if (!container) return;
        
        // Implementation would fetch and display stats
        container.innerHTML = '<p>Loading statistics...</p>';
    }

    // ==========================================
    // Search and Filter
    // ==========================================
    
    handleSearch(event) {
        const searchTerm = event.target.value.toLowerCase();
        const filteredApps = this.catalog.filter(app =>
            app.name.toLowerCase().includes(searchTerm) ||
            app.description.toLowerCase().includes(searchTerm) ||
            app.category.includes(searchTerm)
        );
        
        const catalogGrid = document.getElementById('paas-catalog-grid');
        if (catalogGrid) {
            catalogGrid.innerHTML = '';
            filteredApps.forEach(app => {
                catalogGrid.appendChild(this.createCatalogCard(app));
            });
        }
    }

    handleCategoryFilter(event) {
        const chip = event.target.closest('[data-category]');
        if (!chip) return;
        
        const category = chip.dataset.category;
        
        // Update active chip
        document.querySelectorAll('[data-category]').forEach(c => {
            c.classList.remove('active');
        });
        chip.classList.add('active');
        
        const filteredApps = category === 'all' 
            ? this.catalog 
            : this.catalog.filter(app => app.category === category);
        
        const catalogGrid = document.getElementById('paas-catalog-grid');
        if (catalogGrid) {
            catalogGrid.innerHTML = '';
            filteredApps.forEach(app => {
                catalogGrid.appendChild(this.createCatalogCard(app));
            });
        }
    }
}

// Create global instance
window.paasController = new PaaSController();

// Export for backward compatibility
window.PAAS_APPS_EXTENDED = window.paasController.catalog;
window.refreshApps = () => window.paasController.loadDeployedApps();
window.deployApp = (appId) => window.paasController.deployApp(appId);
window.deleteApp = (appId) => window.paasController.deleteApp(appId);
window.switchToPaasCatalog = () => window.paasController.showCatalogView();
window.renderCatalogApps = () => window.paasController.renderCatalog();
window.initPaaSCatalog = () => window.paasController.renderCatalog();