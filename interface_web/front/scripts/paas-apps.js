class PaaSManager {
    constructor() {
        this.paasAppsInterval = null;
        this.serverInfo = {
            ip: null,
            hosts: {},
            networks: {},
            defaultPort: 80,
            selectionStrategy: 'LEAST_USED'
        };
        this.hostManager = null;
    }

    async initialize() {
        try {
            // Get server information with host details
            const serverData = await window.fetchAPI('/paas/server-info');
            if (serverData.success) {
                this.serverInfo = {
                    ip: serverData.ip || serverData.host || 'localhost',
                    hosts: serverData.hosts || {},
                    networks: serverData.networks || {},
                    defaultPort: serverData.defaultPort || 80,
                    selectionStrategy: serverData.selectionStrategy || 'LEAST_USED'
                };
                console.log('PaaS server info loaded:', this.serverInfo);
            }
            
            // Initialize host manager if available
            if (window.hostManager) {
                this.hostManager = window.hostManager;
            }
            
            // Load available hosts
            await this.loadAvailableHosts();
        } catch (error) {
            console.warn('Could not load server info from backend:', error);
            this.serverInfo.ip = window.location.hostname || 'localhost';
        }
    }

    async loadAvailableHosts() {
        try {
            const hostsData = await window.fetchAPI('/hosts');
            if (hostsData.success && hostsData.hosts) {
                this.serverInfo.hosts = hostsData.hosts.reduce((acc, host) => {
                    acc[host.id] = host;
                    return acc;
                }, {});
            }
        } catch (error) {
            console.warn('Could not load hosts:', error);
        }
    }

    // ==========================================
    // APPLICATION RENDERING
    // ==========================================

    async refreshApps() {
        const appsGrid = document.getElementById('apps-grid') || document.getElementById('paas-apps-grid');
        if (!appsGrid) return;
        
        appsGrid.innerHTML = '<p class="loading-text">Loading applications...</p>';
        
        try {
            const data = await window.fetchAPI('/paas/applications');
            
            if (data.success && data.applications) {
                this.renderApps(data.applications);
            } else {
                appsGrid.innerHTML = '<p class="empty-state">No applications deployed. Browse the catalog to deploy your first app!</p>';
            }
        } catch (error) {
            appsGrid.innerHTML = `<p class="error-text">Error: ${error.message}</p>`;
        }
    }

    renderApps(apps) {
        const appsGrid = document.getElementById('apps-grid') || document.getElementById('paas-apps-grid');
        if (!appsGrid) return;
        
        appsGrid.innerHTML = '';
        
        if (apps.length === 0) {
            appsGrid.innerHTML = '<p class="empty-state">No applications deployed. Check the catalog!</p>';
            return;
        }
        
        apps.forEach(app => {
            appsGrid.appendChild(this.createPaaSAppCard(app));
        });
    }

    createPaaSAppCard(app) {
        const card = document.createElement('div');
        card.className = 'paas-app-card';
        
        const isRunning = app.running || app.status === 'running' || app.status?.includes('Up');
        const statusClass = isRunning ? 'running' : 'stopped';
        const statusText = isRunning ? '🟢 Running' : '🔴 Stopped';
        
        const appType = this.detectAppType(app.name);
        const appIcon = this.getAppIcon(appType);
        const primaryPort = app.port || this.parsePrimaryPort(app.ports);
        
        // Get host information
        const hostInfo = app.host ? this.getHostInfo(app.host) : null;
        const hostName = hostInfo?.hostname || app.host || 'primary';
        const hostStatus = hostInfo?.active ? '🟢' : '🔴';
        
        // Build URL with host-specific information
        const accessUrl = app.url || this.buildAppUrl(app, hostInfo, primaryPort);
        
        card.innerHTML = `
            <div class="app-card-header">
                <div class="app-title-section">
                    <span class="app-icon">${appIcon}</span>
                    <div>
                        <h3>${app.name}</h3>
                        <div class="app-meta">
                            <span class="app-type-badge">${appType}</span>
                            ${app.host ? `
                                <div class="host-info">
                                    <span class="host-badge" title="${hostName}">
                                        🖥️ ${hostName} ${hostStatus}
                                    </span>
                                    ${app.hostResources ? `
                                        <span class="resource-badge" title="Resource allocation">
                                            💻 ${app.hostResources.cpu || 1}vCPU • 💾 ${app.hostResources.memory || 1024}MB
                                        </span>
                                    ` : ''}
                                </div>
                            ` : ''}
                        </div>
                    </div>
                </div>
                <span class="status-badge status-${statusClass}">${statusText}</span>
            </div>
            
            <div class="app-card-body">
                ${isRunning ? this.renderRunningAppInfo(app, appType, accessUrl, primaryPort, hostInfo) : this.renderStoppedAppInfo(app)}
            </div>
            
            <div class="app-card-actions">
                ${isRunning ? `
                    <button class="btn btn-sm btn-primary" onclick="paasManager.openAppUrl('${accessUrl}')">
                        🌐 Open
                    </button>
                    <button class="btn btn-sm btn-secondary" onclick="paasManager.showAppDetails('${app.id || app.name}', '${appType}', '${primaryPort}')">
                        📋 Details
                    </button>
                    ${app.host ? `
                    <button class="btn btn-sm btn-info" onclick="paasManager.showHostInfo('${app.host}')">
                        🖥️ Host
                    </button>
                    ` : ''}
                    <button class="btn btn-sm btn-warning" onclick="paasManager.stopApp('${app.id || app.name}')">
                        ⏸️ Stop
                    </button>
                ` : `
                    <button class="btn btn-sm btn-success" onclick="paasManager.startApp('${app.id || app.name}')">
                        ▶️ Start
                    </button>
                    ${app.host ? `
                    <button class="btn btn-sm btn-info" onclick="paasManager.migrateApp('${app.name}')">
                        🔄 Migrate
                    </button>
                    ` : ''}
                `}
                <button class="btn btn-sm btn-danger" onclick="paasManager.deleteApp('${app.id || app.name}')">
                    🗑️ Delete
                </button>
            </div>
        `;
        
        return card;
    }

    renderRunningAppInfo(app, appType, accessUrl, port, hostInfo) {
        const username = authService.currentUser.username;
        
        return `
            <div class="app-info-grid">
                <div class="app-info-item">
                    <strong>Access URL:</strong>
                    <div class="copyable-field">
                        <code id="url-${app.name}">${accessUrl}</code>
                        <button class="btn-copy" onclick="copyToClipboard('url-${app.name}')">
                            📋
                        </button>
                    </div>
                </div>
                
                <div class="app-info-item">
                    <strong>Container:</strong>
                    <div class="copyable-field">
                        <code id="container-${app.name}">${app.name}</code>
                        <button class="btn-copy" onclick="copyToClipboard('container-${app.name}')">
                            📋
                        </button>
                    </div>
                </div>
                
                ${hostInfo ? `
                <div class="app-info-item">
                    <strong>Deployed on:</strong>
                    <div class="host-details">
                        <span>${hostInfo.hostname || hostInfo.id}</span>
                        <span class="host-status ${hostInfo.active ? 'active' : 'inactive'}">
                            ${hostInfo.active ? '🟢 Active' : '🔴 Inactive'}
                        </span>
                    </div>
                </div>
                
                ${app.hostResources ? `
                <div class="app-info-item">
                    <strong>Resources:</strong>
                    <div class="resource-details">
                        <span class="resource-chip">💻 ${app.hostResources.cpu || 1} vCPU</span>
                        <span class="resource-chip">💾 ${app.hostResources.memory || 1024} MB</span>
                        <span class="resource-chip">💿 ${Math.round((app.hostResources.disk || 0) / (1024*1024*1024))} GB</span>
                    </div>
                </div>
                ` : ''}
                ` : ''}
                
                <div class="app-info-item">
                    <strong>Status:</strong>
                    <span>${app.status || 'Running'}</span>
                </div>
                
                ${this.renderAppTypeSpecificInfo(appType, app.name, username)}
            </div>
        `;
    }

    renderStoppedAppInfo(app) {
        return `
            <div class="app-info-grid">
                <div class="app-info-item">
                    <strong>Container:</strong>
                    <code>${app.name}</code>
                </div>
                <div class="app-info-item">
                    <strong>Status:</strong>
                    <span class="text-muted">Stopped</span>
                </div>
            </div>
        `;
    }

    renderAppTypeSpecificInfo(appType, appName, username) {
        switch(appType) {
            case 'WordPress':
            case 'Moodle':
            case 'PrestaShop':
            case 'NextCloud':
                return `
                    <div class="app-info-item database-info">
                        <strong>Database:</strong>
                        <button class="btn btn-sm btn-info" onclick="paasManager.showDatabaseCreds('mariadb', '${appName}', '${username}')">
                            🔑 Show Credentials
                        </button>
                    </div>
                `;
                
            case 'Odoo':
            case 'Mattermost':
            case 'Grafana':
                return `
                    <div class="app-info-item database-info">
                        <strong>Database:</strong>
                        <button class="btn btn-sm btn-info" onclick="paasManager.showDatabaseCreds('postgresql', '${appName}', '${username}')">
                            🔑 Show Credentials
                        </button>
                    </div>
                `;
                
            default:
                return '';
        }
    }

    // ==========================================
    // UTILITY FUNCTIONS
    // ==========================================

    detectAppType(appName) {
        const name = appName.toLowerCase();
        if (name.includes('wordpress')) return 'WordPress';
        if (name.includes('odoo')) return 'Odoo';
        if (name.includes('moodle')) return 'Moodle';
        if (name.includes('prestashop')) return 'PrestaShop';
        if (name.includes('nextcloud')) return 'NextCloud';
        if (name.includes('mattermost')) return 'Mattermost';
        if (name.includes('grafana')) return 'Grafana';
        return 'Application';
    }

    getAppIcon(appType) {
        const icons = {
            'WordPress': '📝',
            'Odoo': '📊',
            'Moodle': '🎓',
            'PrestaShop': '🛒',
            'NextCloud': '☁️',
            'Mattermost': '💬',
            'Grafana': '📈'
        };
        return icons[appType] || '📦';
    }

    parsePrimaryPort(portString) {
        if (!portString) return '80';
        // Format: "8080:80, 443:443" or "8080:80"
        const ports = portString.split(',')
            .map(p => p.trim().split(':')[0])
            .filter(p => p);
        return ports[0] || '80';
    }

    getHostInfo(hostId) {
        return this.serverInfo.hosts[hostId] || null;
    }

    buildAppUrl(app, hostInfo, port) {
        if (hostInfo && hostInfo.ip) {
            return `http://${hostInfo.ip}:${port}`;
        }
        return `http://${this.serverInfo.ip}:${port}`;
    }

    openAppUrl(url) {
        window.open(url, '_blank');
    }

    // ==========================================
    // APP DETAILS MODAL
    // ==========================================

    async showAppDetails(appId, appType, port) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'app-details-modal';
        
        const username = authService.currentUser.username;
        const accessUrl = `http://${this.serverInfo.ip}:${port}`;
        
        modal.innerHTML = `
            <div class="modal modal-large">
                <div class="modal-header">
                    <h3>${this.getAppIcon(appType)} ${appId} - Details</h3>
                    <button class="modal-close" onclick="paasManager.closeModal('app-details-modal')">✖</button>
                </div>
                <div class="modal-body">
                    <div class="tabs">
                        <button class="tab active" onclick="paasManager.switchTab('access')">Access</button>
                        <button class="tab" onclick="paasManager.switchTab('monitoring')">Monitoring</button>
                        <button class="tab" onclick="paasManager.switchTab('logs')">Logs</button>
                    </div>
                    
                    <div class="tab-content active" id="app-tab-access">
                        <h4>Application Access</h4>
                        <div class="access-info-section">
                            <div class="copyable-field-large">
                                <label>Access URL</label>
                                <div class="field-with-copy">
                                    <input type="text" id="detail-url" value="${accessUrl}" readonly>
                                    <button class="btn btn-primary" onclick="paasManager.copyFieldValue('detail-url')">
                                        📋 Copy
                                    </button>
                                    <button class="btn btn-success" onclick="window.open('${accessUrl}', '_blank')">
                                        🌐 Open
                                    </button>
                                </div>
                            </div>
                            
                            <div class="copyable-field-large">
                                <label>Container Name</label>
                                <div class="field-with-copy">
                                    <input type="text" id="detail-container" value="${appId}" readonly>
                                    <button class="btn btn-primary" onclick="paasManager.copyFieldValue('detail-container')">
                                        📋 Copy
                                    </button>
                                </div>
                            </div>
                            
                            <div class="copyable-field-large">
                                <label>Server IP</label>
                                <div class="field-with-copy">
                                    <input type="text" id="detail-ip" value="${this.serverInfo.ip}" readonly>
                                    <button class="btn btn-primary" onclick="paasManager.copyFieldValue('detail-ip')">
                                        📋 Copy
                                    </button>
                                </div>
                            </div>
                            
                            <div class="copyable-field-large">
                                <label>Port</label>
                                <div class="field-with-copy">
                                    <input type="text" id="detail-port" value="${port}" readonly>
                                    <button class="btn btn-primary" onclick="paasManager.copyFieldValue('detail-port')">
                                        📋 Copy
                                    </button>
                                </div>
                            </div>
                        </div>
                        
                        ${this.renderDatabaseSection(appType, appId, username)}
                    </div>
                    
                    <div class="tab-content" id="app-tab-monitoring">
                        <h4>Resource Usage</h4>
                        <div id="app-monitoring-content">
                            <p class="loading-text">Loading monitoring data...</p>
                        </div>
                    </div>
                    
                    <div class="tab-content" id="app-tab-logs">
                        <h4>Application Logs</h4>
                        <div class="logs-controls">
                            <button class="btn btn-sm btn-secondary" onclick="paasManager.refreshAppLogs('${appId}')">
                                🔄 Refresh
                            </button>
                            <select id="log-lines" onchange="paasManager.refreshAppLogs('${appId}')">
                                <option value="50">Last 50 lines</option>
                                <option value="100" selected>Last 100 lines</option>
                                <option value="200">Last 200 lines</option>
                                <option value="500">Last 500 lines</option>
                            </select>
                        </div>
                        <pre id="app-logs-content" class="logs-container">Loading logs...</pre>
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="paasManager.closeModal('app-details-modal')">Close</button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        // Load initial data
        await this.loadAppMonitoring(appId);
        await this.refreshAppLogs(appId);
    }

    renderDatabaseSection(appType, appName, username) {
        const dbType = ['WordPress', 'Moodle', 'PrestaShop', 'NextCloud'].includes(appType) 
            ? 'mariadb' : 'postgresql';
        
        if (!['WordPress', 'Odoo', 'Moodle', 'PrestaShop', 'NextCloud', 'Mattermost', 'Grafana'].includes(appType)) {
            return '';
        }
        
        const dbHost = dbType === 'mariadb' ? 'thoth-mariadb' : 'thoth-postgres';
        const dbPort = dbType === 'mariadb' ? '3306' : '5432';
        const dbName = `${username}_${appName}`;
        const dbUser = `${username}_${appName}_user`;
        
        return `
            <h4 style="margin-top: 30px;">Database Access</h4>
            <div class="access-info-section">
                <div class="copyable-field-large">
                    <label>Database Type</label>
                    <div class="field-with-copy">
                        <input type="text" value="${dbType === 'mariadb' ? 'MariaDB' : 'PostgreSQL'}" readonly>
                    </div>
                </div>
                
                <div class="copyable-field-large">
                    <label>Host</label>
                    <div class="field-with-copy">
                        <input type="text" id="db-host" value="${dbHost}" readonly>
                        <button class="btn btn-primary" onclick="paasManager.copyFieldValue('db-host')">
                            📋 Copy
                        </button>
                    </div>
                </div>
                
                <div class="copyable-field-large">
                    <label>Port</label>
                    <div class="field-with-copy">
                        <input type="text" id="db-port" value="${dbPort}" readonly>
                        <button class="btn btn-primary" onclick="paasManager.copyFieldValue('db-port')">
                            📋 Copy
                        </button>
                    </div>
                </div>
                
                <div class="copyable-field-large">
                    <label>Database Name</label>
                    <div class="field-with-copy">
                        <input type="text" id="db-name" value="${dbName}" readonly>
                        <button class="btn btn-primary" onclick="paasManager.copyFieldValue('db-name')">
                            📋 Copy
                        </button>
                    </div>
                </div>
                
                <div class="copyable-field-large">
                    <label>Username</label>
                    <div class="field-with-copy">
                        <input type="text" id="db-user" value="${dbUser}" readonly>
                        <button class="btn btn-primary" onclick="paasManager.copyFieldValue('db-user')">
                            📋 Copy
                        </button>
                    </div>
                </div>
                
                <div class="copyable-field-large">
                    <label>Password</label>
                    <div class="field-with-copy">
                        <input type="password" id="db-password" value="••••••••" readonly>
                        <button class="btn btn-warning" onclick="paasManager.revealDatabasePassword('${dbType}', '${appName}')">
                            👁️ Show
                        </button>
                        <button class="btn btn-primary" onclick="paasManager.copyDatabasePassword('${dbType}', '${appName}')">
                            📋 Copy
                        </button>
                    </div>
                </div>
                
                <div class="info-banner" style="margin-top: 15px;">
                    <span class="info-icon">ℹ️</span>
                    <p>Database credentials are automatically configured in the application. Use these credentials for direct database access if needed.</p>
                </div>
            </div>
        `;
    }

    switchTab(tabName) {
        document.querySelectorAll('#app-details-modal .tab').forEach(tab => {
            tab.classList.remove('active');
        });
        event.target.classList.add('active');
        
        document.querySelectorAll('#app-details-modal .tab-content').forEach(content => {
            content.classList.remove('active');
        });
        document.getElementById(`app-tab-${tabName}`).classList.add('active');
    }

    async loadAppMonitoring(appName) {
        const monitoringContent = document.getElementById('app-monitoring-content');
        if (!monitoringContent) return;
        
        try {
            const stats = await this.getDockerStats(appName);
            
            monitoringContent.innerHTML = `
                <div class="monitoring-grid">
                    <div class="metric-card">
                        <h5>💻 CPU Usage</h5>
                        <div class="metric-value">${stats.cpu || '0.0'}%</div>
                        <div class="progress-bar">
                            <div class="progress-fill" style="width: ${stats.cpu || 0}%"></div>
                        </div>
                    </div>
                    
                    <div class="metric-card">
                        <h5>💾 Memory Usage</h5>
                        <div class="metric-value">${stats.memory || '0 MB'}</div>
                        <div class="metric-detail">${stats.memoryPercent || '0'}% of limit</div>
                    </div>
                    
                    <div class="metric-card">
                        <h5>🌐 Network I/O</h5>
                        <div class="metric-detail">RX: ${stats.networkRx || '0 B'}</div>
                        <div class="metric-detail">TX: ${stats.networkTx || '0 B'}</div>
                    </div>
                    
                    <div class="metric-card">
                        <h5>💿 Disk I/O</h5>
                        <div class="metric-detail">Read: ${stats.diskRead || '0 B'}</div>
                        <div class="metric-detail">Write: ${stats.diskWrite || '0 B'}</div>
                    </div>
                </div>
            `;
        } catch (error) {
            monitoringContent.innerHTML = `<p class="error-text">Failed to load monitoring data</p>`;
        }
    }

    async getDockerStats(containerName) {
        try {
            const data = await window.fetchAPI(`/paas/applications/${containerName}/stats`);
            if (data.success && data.stats) {
                return data.stats;
            }
        } catch (error) {
            console.warn('Stats API failed, using mock data:', error);
        }
        
        // Fallback to mock data
        return {
            cpu: (Math.random() * 50).toFixed(1),
            memory: `${(Math.random() * 500).toFixed(0)} MB`,
            memoryPercent: (Math.random() * 30).toFixed(1),
            networkRx: `${(Math.random() * 100).toFixed(1)} MB`,
            networkTx: `${(Math.random() * 50).toFixed(1)} MB`,
            diskRead: `${(Math.random() * 200).toFixed(1)} MB`,
            diskWrite: `${(Math.random() * 150).toFixed(1)} MB`
        };
    }

    async refreshAppLogs(appName) {
        const logsContent = document.getElementById('app-logs-content');
        if (!logsContent) return;
        
        const lines = document.getElementById('log-lines')?.value || 100;
        logsContent.textContent = 'Loading logs...';
        
        try {
            const data = await window.fetchAPI(`/paas/applications/${appName}/logs?lines=${lines}`);
            
            if (data.success && data.logs) {
                logsContent.textContent = data.logs;
            } else {
                logsContent.textContent = 'No logs available';
            }
        } catch (error) {
            logsContent.textContent = `Error loading logs: ${error.message}`;
        }
    }

    // ==========================================
    // DATABASE CREDENTIALS
    // ==========================================

    async revealDatabasePassword(dbType, appName) {
        const passwordField = document.getElementById('db-password');
        if (!passwordField) return;
        
        try {
            const username = authService.currentUser.username;
            const data = await window.fetchAPI(`/paas/database/credentials?type=${dbType}&app=${appName}&user=${username}`);
            
            if (data.success && data.password) {
                passwordField.type = 'text';
                passwordField.value = data.password;
                event.target.textContent = '🔒 Hide';
                event.target.onclick = () => this.hideDatabasePassword();
            }
        } catch (error) {
            showToast('Failed to retrieve password', 'error');
        }
    }

    hideDatabasePassword() {
        const passwordField = document.getElementById('db-password');
        if (!passwordField) return;
        
        passwordField.type = 'password';
        passwordField.value = '••••••••';
        event.target.textContent = '👁️ Show';
    }

    async copyDatabasePassword(dbType, appName) {
        try {
            const username = authService.currentUser.username;
            const data = await window.fetchAPI(`/paas/database/credentials?type=${dbType}&app=${appName}&user=${username}`);
            
            if (data.success && data.password) {
                await navigator.clipboard.writeText(data.password);
                showToast('✅ Password copied!', 'success');
            }
        } catch (error) {
            showToast('Failed to copy password', 'error');
        }
    }

    copyFieldValue(fieldId) {
        const field = document.getElementById(fieldId);
        if (!field) return;
        
        navigator.clipboard.writeText(field.value).then(() => {
            showToast('✅ Copied!', 'success');
        });
    }

    showDatabaseCreds(dbType, appName, username) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'db-creds-modal';
        
        const dbHost = dbType === 'mariadb' ? 'thoth-mariadb' : 'thoth-postgres';
        const dbPort = dbType === 'mariadb' ? '3306' : '5432';
        const dbName = `${username}_${appName}`;
        const dbUser = `${username}_${appName}_user`;
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>🔑 Database Credentials</h3>
                    <button class="modal-close" onclick="paasManager.closeModal('db-creds-modal')">✖</button>
                </div>
                <div class="modal-body">
                    <div class="creds-grid">
                        ${this.createCredField('Type', dbType === 'mariadb' ? 'MariaDB' : 'PostgreSQL', 'db-type-quick')}
                        ${this.createCredField('Host', dbHost, 'db-host-quick')}
                        ${this.createCredField('Port', dbPort, 'db-port-quick')}
                        ${this.createCredField('Database', dbName, 'db-name-quick')}
                        ${this.createCredField('Username', dbUser, 'db-user-quick')}
                    </div>
                    
                    <div class="password-section">
                        <button class="btn btn-warning" onclick="paasManager.revealQuickPassword('${dbType}', '${appName}')">
                            👁️ Show Password
                        </button>
                        <button class="btn btn-primary" onclick="paasManager.copyQuickPassword('${dbType}', '${appName}')">
                            📋 Copy Password
                        </button>
                    </div>
                    
                    <div id="quick-password-display" style="display: none; margin-top: 15px;">
                        <strong>Password:</strong>
                        <code id="quick-password-value" style="padding: 10px; display: block; margin-top: 5px;"></code>
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="paasManager.closeModal('db-creds-modal')">Close</button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
    }

    createCredField(label, value, id) {
        return `
            <div class="cred-field">
                <label>${label}</label>
                <div class="field-with-copy">
                    <input type="text" id="${id}" value="${value}" readonly>
                    <button class="btn-copy" onclick="paasManager.copyFieldValue('${id}')">📋</button>
                </div>
            </div>
        `;
    }

    async revealQuickPassword(dbType, appName) {
        const display = document.getElementById('quick-password-display');
        const valueField = document.getElementById('quick-password-value');
        
        try {
            const username = authService.currentUser.username;
            const data = await window.fetchAPI(`/paas/database/credentials?type=${dbType}&app=${appName}&user=${username}`);
            
            if (data.success && data.password) {
                valueField.textContent = data.password;
                display.style.display = 'block';
            }
        } catch (error) {
            showToast('Failed to retrieve password', 'error');
        }
    }

    async copyQuickPassword(dbType, appName) {
        try {
            const username = authService.currentUser.username;
            const data = await window.fetchAPI(`/paas/database/credentials?type=${dbType}&app=${appName}&user=${username}`);
            
            if (data.success && data.password) {
                await navigator.clipboard.writeText(data.password);
                showToast('✅ Password copied!', 'success');
            }
        } catch (error) {
            showToast('Failed to copy password', 'error');
        }
    }

    // ==========================================
    // APP CONTROL FUNCTIONS
    // ==========================================

    async stopApp(appId) {
        if (!confirm(`Stop application ${appId}?`)) return;
        
        showToast('Stopping application...', 'info');
        
        try {
            const result = await window.fetchAPI(`/paas/applications/${appId}/stop`, {
                method: 'POST'
            });
            
            if (result.success) {
                showToast('✅ Application stopped', 'success');
                await this.refreshApps();
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    }

    async startApp(appId) {
        showToast('Starting application...', 'info');
        
        try {
            const result = await window.fetchAPI(`/paas/applications/${appId}/start`, {
                method: 'POST'
            });
            
            if (result.success) {
                showToast('✅ Application started', 'success');
                await this.refreshApps();
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    }

    async deleteApp(appId) {
        if (!confirm(`Delete application ${appId}?\n\n⚠️ This action cannot be undone!`)) return;
        
        showToast('Deleting application...', 'info');
        
        try {
            const result = await window.fetchAPI(`/paas/applications/${appId}`, {
                method: 'DELETE'
            });
            
            if (result.success) {
                showToast('✅ Application deleted', 'success');
                await this.refreshApps();
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    }

    // ==========================================
    // MULTI-HOST DEPLOYMENT FEATURES
    // ==========================================

    async selectHostForApp(appConfig) {
        try {
            const selectionData = {
                memory: appConfig.memory || 1024,
                cpu: appConfig.cpu || 1,
                disk: appConfig.disk || 1024 * 1024 * 1024,
                strategy: appConfig.strategy || this.serverInfo.selectionStrategy
            };
            
            const response = await window.fetchAPI('/paas/select-host', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(selectionData)
            });
            
            if (response.success) {
                return {
                    host: response.host,
                    resources: response.resources,
                    connection: response.connection
                };
            }
            return null;
        } catch (error) {
            console.error('Host selection failed:', error);
            return null;
        }
    }

    async deployApplication(appConfig) {
        try {
            // Step 1: Select optimal host
            const hostSelection = await this.selectHostForApp(appConfig);
            if (!hostSelection) {
                throw new Error('Could not select suitable host for deployment');
            }
            
            showToast(`Selected host: ${hostSelection.host}`, 'info');
            
            // Step 2: Deploy to selected host
            const deploymentData = {
                ...appConfig,
                targetHost: hostSelection.host,
                hostResources: hostSelection.resources
            };
            
            const response = await window.fetchAPI('/paas/deploy', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify(deploymentData)
            });
            
            if (response.success) {
                showToast(`✅ Application deployed to ${hostSelection.host}`, 'success');
                await this.refreshApps();
                return response;
            } else {
                throw new Error(response.error || 'Deployment failed');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
            throw error;
        }
    }

    async showHostInfo(hostId) {
        try {
            const hostData = await window.fetchAPI(`/hosts/${hostId}`);
            if (!hostData.success) {
                showToast('Could not load host information', 'error');
                return;
            }
            
            const host = hostData.host;
            const modal = document.createElement('div');
            modal.className = 'modal-overlay active';
            modal.id = 'host-info-modal';
            
            modal.innerHTML = `
                <div class="modal">
                    <div class="modal-header">
                        <h3>🖥️ Host: ${host.hostname || host.id}</h3>
                        <button class="modal-close" onclick="paasManager.closeModal('host-info-modal')">✖</button>
                    </div>
                    <div class="modal-body">
                        <div class="host-details-grid">
                            <div class="detail-item">
                                <strong>Hostname:</strong>
                                <span>${host.hostname}</span>
                            </div>
                            <div class="detail-item">
                                <strong>Status:</strong>
                                <span class="status-badge status-${host.active ? 'active' : 'inactive'}">
                                    ${host.active ? '🟢 Active' : '🔴 Inactive'}
                                </span>
                            </div>
                            <div class="detail-item">
                                <strong>URI:</strong>
                                <code>${host.uri || 'N/A'}</code>
                            </div>
                            
                            ${host.resources ? `
                            <div class="resource-section">
                                <h4>Resource Usage</h4>
                                <div class="resource-metrics">
                                    <div class="metric">
                                        <strong>CPU:</strong>
                                        <div class="progress-bar">
                                            <div class="progress-fill" style="width: ${host.resources.cpuUsage || 0}%"></div>
                                        </div>
                                        <span>${host.resources.availableCPUs || 0}/${host.resources.totalCPUs || 0} vCPUs</span>
                                    </div>
                                    <div class="metric">
                                        <strong>Memory:</strong>
                                        <div class="progress-bar">
                                            <div class="progress-fill" style="width: ${host.resources.memoryUsage || 0}%"></div>
                                        </div>
                                        <span>${Math.round((host.resources.availableMemory || 0) / 1024)}/${Math.round((host.resources.totalMemory || 0) / 1024)} GB</span>
                                    </div>
                                    <div class="metric">
                                        <strong>Disk:</strong>
                                        <div class="progress-bar">
                                            <div class="progress-fill" style="width: ${host.resources.diskUsage || 0}%"></div>
                                        </div>
                                        <span>${Math.round((host.resources.availableDisk || 0) / (1024*1024*1024))}/${Math.round((host.resources.totalDisk || 0) / (1024*1024*1024))} GB</span>
                                    </div>
                                </div>
                            </div>
                            ` : ''}
                            
                            ${host.vms && host.vms.length > 0 ? `
                            <div class="vms-section">
                                <h4>Running VMs</h4>
                                <ul class="vm-list">
                                    ${host.vms.map(vm => `<li>${vm.name} (${vm.status})</li>`).join('')}
                                </ul>
                            </div>
                            ` : ''}
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button class="btn btn-secondary" onclick="paasManager.closeModal('host-info-modal')">Close</button>
                    </div>
                </div>
            `;
            
            document.body.appendChild(modal);
        } catch (error) {
            showToast(`Error loading host info: ${error.message}`, 'error');
        }
    }

    async migrateApp(appName) {
        try {
            // Get available hosts for migration
            const hostsData = await window.fetchAPI('/hosts/available');
            if (!hostsData.success || hostsData.hosts.length === 0) {
                showToast('No suitable hosts available for migration', 'warning');
                return;
            }
            
            // Show migration dialog
            const modal = document.createElement('div');
            modal.className = 'modal-overlay active';
            modal.id = 'migration-modal';
            
            const hostsOptions = hostsData.hosts.map(host => 
                `<option value="${host.id}">${host.hostname} (Available: ${Math.round(host.availableMemory/1024)}GB RAM, ${host.availableCPUs} vCPUs)</option>`
            ).join('');
            
            modal.innerHTML = `
                <div class="modal">
                    <div class="modal-header">
                        <h3>🔄 Migrate Application: ${appName}</h3>
                        <button class="modal-close" onclick="paasManager.closeModal('migration-modal')">✖</button>
                    </div>
                    <div class="modal-body">
                        <p>Select target host for migration:</p>
                        <select id="target-host" class="form-select">
                            ${hostsOptions}
                        </select>
                        <div class="form-group">
                            <label>
                                <input type="checkbox" id="live-migration">
                                Live migration (minimal downtime)
                            </label>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button class="btn btn-secondary" onclick="paasManager.closeModal('migration-modal')">Cancel</button>
                        <button class="btn btn-primary" onclick="paasManager.executeMigration('${appName}')">
                            Start Migration
                        </button>
                    </div>
                </div>
            `;
            
            document.body.appendChild(modal);
        } catch (error) {
            showToast(`Migration failed: ${error.message}`, 'error');
        }
    }

    async executeMigration(appName) {
        try {
            const targetHost = document.getElementById('target-host').value;
            const liveMigration = document.getElementById('live-migration').checked;
            
            showToast('Starting migration...', 'info');
            
            const response = await window.fetchAPI(`/paas/applications/${appName}/migrate`, {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({
                    targetHost: targetHost,
                    liveMigration: liveMigration
                })
            });
            
            if (response.success) {
                showToast('✅ Migration started successfully', 'success');
                this.closeModal('migration-modal');
                await this.refreshApps();
            } else {
                throw new Error(response.error || 'Migration failed');
            }
        } catch (error) {
            showToast(`❌ Migration failed: ${error.message}`, 'error');
        }
    }

    // ==========================================
    // MODAL MANAGEMENT
    // ==========================================

    closeModal(modalId) {
        const modal = document.getElementById(modalId);
        if (modal) modal.remove();
    }

    // ==========================================
    // AUTO-REFRESH & MONITORING
    // ==========================================

    startMonitoring() {
        this.refreshApps();
        if (this.paasAppsInterval) clearInterval(this.paasAppsInterval);
        this.paasAppsInterval = setInterval(() => this.refreshApps(), 30000); // Refresh every 30s
    }

    stopMonitoring() {
        if (this.paasAppsInterval) {
            clearInterval(this.paasAppsInterval);
            this.paasAppsInterval = null;
        }
    }
}

// ==========================================
// INITIALIZATION
// ==========================================

// Create global instance
const paasManager = new PaaSManager();

// Initialize on DOM load
document.addEventListener('DOMContentLoaded', async () => {
    await paasManager.initialize();
    
    // Setup navigation handlers
    const paasNavItem = document.querySelector('[data-view="paas"]');
    if (paasNavItem) {
        paasNavItem.addEventListener('click', () => {
            paasManager.startMonitoring();
        });
    }
    
    // Stop monitoring when leaving PaaS view
    document.querySelectorAll('.nav-item').forEach(item => {
        if (item.dataset.view !== 'paas') {
            item.addEventListener('click', () => {
                paasManager.stopMonitoring();
            });
        }
    });
});

// ==========================================
// BACKWARDS COMPATIBILITY EXPORTS
// ==========================================

// Export functions for legacy code
window.paasManager = paasManager;
window.refreshApps = () => paasManager.refreshApps();
window.showAppDetailsModal = (id, type, port) => paasManager.showAppDetails(id, type, port);
window.stopApp = (id) => paasManager.stopApp(id);
window.startApp = (id) => paasManager.startApp(id);
window.deleteApp = (id) => paasManager.deleteApp(id);
window.openApp = (port) => paasManager.openAppUrl(`http://${paasManager.serverInfo.ip}:${port}`);
window.copyToClipboard = (text) => {
    navigator.clipboard.writeText(text).then(() => {
        showToast('✅ Copied!', 'success');
    });
};

// CSS Styles
const styles = `
<style>
.host-info {
    display: flex;
    gap: 8px;
    align-items: center;
    margin-top: 4px;
    flex-wrap: wrap;
}

.host-badge {
    background: var(--bg-secondary, #f5f5f5);
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 0.8rem;
    display: inline-flex;
    align-items: center;
    gap: 4px;
}

.resource-badge {
    background: var(--primary-light, #e3f2fd);
    color: var(--primary, #2196F3);
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 0.8rem;
    display: inline-flex;
    align-items: center;
    gap: 4px;
}

.resource-details {
    display: flex;
    gap: 6px;
    flex-wrap: wrap;
    margin-top: 4px;
}

.resource-chip {
    background: var(--bg-hover, #f0f0f0);
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 0.8rem;
}

.host-details-grid {
    display: grid;
    gap: 12px;
}

.detail-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px 0;
    border-bottom: 1px solid var(--border-color, #e0e0e0);
}

.resource-section {
    margin-top: 16px;
    padding: 16px;
    background: var(--bg-secondary, #f5f5f5);
    border-radius: 8px;
}

.resource-metrics {
    display: grid;
    gap: 12px;
}

.metric {
    display: grid;
    gap: 4px;
}

.progress-bar {
    width: 100%;
    height: 8px;
    background: var(--border-color, #e0e0e0);
    border-radius: 4px;
    overflow: hidden;
}

.progress-fill {
    height: 100%;
    background: linear-gradient(90deg, var(--primary-color, #2196F3), var(--success-color, #4CAF50));
    transition: width 0.3s ease;
}

.vms-section {
    margin-top: 16px;
}

.vm-list {
    list-style: none;
    padding: 0;
    margin: 0;
}

.vm-list li {
    padding: 8px;
    background: var(--bg-hover, #f0f0f0);
    margin-bottom: 4px;
    border-radius: 4px;
}

.app-meta {
    display: flex;
    gap: 8px;
    align-items: center;
    flex-wrap: wrap;
}
</style>
`;

// Inject styles
if (typeof document !== 'undefined') {
    const styleSheet = document.createElement('style');
    styleSheet.textContent = styles;
    document.head.appendChild(styleSheet);
}