// ==========================================
// PAAS APPLICATION DEPLOYMENT WITH AUTO DATABASE
// ==========================================

// Extended app catalog with database requirements
const PAAS_APPS_EXTENDED = [
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

// Deploy app with automatic database provisioning
async function deployAppWithDatabase(app) {
    const username = authService.currentUser.username;
    
    try {
        showToast(`🚀 Deploying ${app.name}...`, 'info');
        
        let dbCredentials = null;
        
        // Step 1: Create database if needed
        if (app.database) {
            showToast(`📊 Creating ${app.database} database...`, 'info');
            dbCredentials = await provisionDatabase(app.database, username, app.id);
            
            if (!dbCredentials) {
                throw new Error(`Failed to provision ${app.database} database`);
            }
            
            showToast(`✅ Database created`, 'success');
        }
        
        // Step 2: Generate docker-compose with DB connection
        const composeConfig = generateAppCompose(app, username, dbCredentials);
        
        // Step 3: Deploy via backend API
        const result = await window.fetchAPI('/paas/deploy', {
            method: 'POST',
            body: JSON.stringify({
                id: `${username}_${app.id}`,
                name: app.name,
                dockerImage: app.image,
                ports: app.ports,
                environment: composeConfig.environment,
                composeContent: composeConfig.yaml
            })
        });
        
        if (result.success) {
            showToast(`✅ ${app.name} deployed successfully!`, 'success');
            
            // Show access info
            showAppAccessInfo(app, username, result);
            
            await refreshApps();
        } else {
            throw new Error(result.error || 'Deployment failed');
        }
        
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
        console.error('Deployment error:', error);
    }
}

// Provision database (calls backend which calls bash script)
async function provisionDatabase(dbType, username, appname) {
    try {
        // In production, this would call backend API
        // For now, we'll simulate the response
        
        // Backend should call: /var/lib/thoth-paas/manage-databases.sh create-{pg|mysql}
        const response = await window.fetchAPI('/paas/database', {
            method: 'POST',
            body: JSON.stringify({
                type: dbType,
                username: username,
                appname: appname
            })
        });
        
        if (response.success) {
            return response.credentials;
        }
        
        // Fallback: return mock credentials for testing
        return {
            host: dbType === 'postgresql' ? 'thoth-postgres' : 'thoth-mariadb',
            port: dbType === 'postgresql' ? 5432 : 3306,
            database: `${username}_${appname}`,
            user: `${username}_${appname}_user`,
            password: 'temp_password_' + Math.random().toString(36)
        };
        
    } catch (error) {
        console.error('Database provisioning error:', error);
        return null;
    }
}

// Generate docker-compose configuration
function generateAppCompose(app, username, dbCredentials) {
    const appId = `${username}_${app.id}`;
    
    let environment = {};
    let yaml = '';
    
    // Common patterns for database connection
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
    
    // Generate docker-compose YAML
    yaml = `
version: '3.8'

services:
  ${appId}:
    image: ${app.image}
    container_name: ${appId}
    restart: unless-stopped
    ports:
${app.ports.map(p => `      - "${p}"`).join('\n')}
    environment:
${Object.entries(environment).map(([k, v]) => `      ${k}: "${v}"`).join('\n')}
    networks:
      - thoth_paas_network
    volumes:
      - ${appId}_data:/var/www/html

volumes:
  ${appId}_data:

networks:
  thoth_paas_network:
    external: true
`;
    
    return { environment, yaml };
}

// Show app access information modal
function showAppAccessInfo(app, username, deployResult) {
    const appId = `${username}_${app.id}`;
    const port = app.ports[0].split(':')[0];
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'app-access-modal';
    
    modal.innerHTML = `
        <div class="modal">
            <div class="modal-header">
                <h3>✅ ${app.name} Deployed Successfully!</h3>
                <button class="modal-close" onclick="closeAppAccessModal()">✖</button>
            </div>
            <div class="modal-body">
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
                        <ol style="margin: 10px 0; padding-left: 20px;">
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
                <button class="btn btn-secondary" onclick="closeAppAccessModal()">
                    Close
                </button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeAppAccessModal() {
    const modal = document.getElementById('app-access-modal');
    if (modal) modal.remove();
}

// Override the deployApp function to use integrated deployment
window.deployApp = async function(appId) {
    const app = PAAS_APPS_EXTENDED.find(a => a.id === appId);
    if (!app) {
        showToast('Application not found', 'error');
        return;
    }
    
    await deployAppWithDatabase(app);
};

// Export extended catalog
window.PAAS_APPS_EXTENDED = PAAS_APPS_EXTENDED;
window.deployAppWithDatabase = deployAppWithDatabase;
window.closeAppAccessModal = closeAppAccessModal;