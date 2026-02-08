// ==========================================
// PAAS CATALOG VIEW HANDLER 
// ==========================================

function initPaaSCatalog() {
    console.log('Initializing PaaS catalog...');
    
    // Render immediately if catalog view is active
    if (document.getElementById('view-paas-catalog').classList.contains('active')) {
        renderCatalogApps();
    }
    
    // Also set up observer for future switches
    const observer = new MutationObserver((mutations) => {
        mutations.forEach((mutation) => {
            if (mutation.target.id === 'view-paas-catalog' && 
                mutation.target.classList.contains('active')) {
                console.log('Catalog view activated');
                renderCatalogApps();
            }
        });
    });

    const catalogView = document.getElementById('view-paas-catalog');
    if (catalogView) {
        observer.observe(catalogView, { 
            attributes: true, 
            attributeFilter: ['class'] 
        });
    }
}

function renderCatalogApps() {
    console.log('Rendering catalog apps...');
    
    const catalogGrid = document.getElementById('paas-catalog-grid');
    if (!catalogGrid) {
        console.error('paas-catalog-grid element not found');
        return;
    }

    catalogGrid.innerHTML = '';

    // Use the extended catalog
    const apps = window.PAAS_APPS_EXTENDED || [];
    
    if (apps.length === 0) {
        catalogGrid.innerHTML = `
            <div style="grid-column: 1 / -1; text-align: center; padding: 40px;">
                <p>No applications available in catalog</p>
            </div>
        `;
        return;
    }

    apps.forEach(app => {
        const card = createCatalogAppCard(app);
        catalogGrid.appendChild(card);
    });
}

function createCatalogAppCard(app) {
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
        <div class="catalog-card-actions">
            <button class="btn btn-primary" onclick="deployApp('${app.id}')" style="flex: 1;">
                🚀 Deploy Now
            </button>
            <button class="btn btn-secondary" onclick="showAppDetailsModal('${app.id}')">
                ℹ️ Info
            </button>
        </div>
    `;
    
    return card;
}

// Simple modal for app details
function showAppDetailsModal(appId) {
    const app = window.PAAS_APPS_EXTENDED.find(a => a.id === appId);
    if (!app) return;
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'app-details-modal';
    
    modal.innerHTML = `
        <div class="modal">
            <div class="modal-header">
                <h3>${app.icon || '📦'} ${app.name}</h3>
                <button class="modal-close" onclick="this.closest('.modal-overlay').remove()">✖</button>
            </div>
            <div class="modal-body">
                <p><strong>Description:</strong> ${app.description}</p>
                <p><strong>Category:</strong> ${app.category}</p>
                <p><strong>Image:</strong> ${app.image || app.id}</p>
                <p><strong>Ports:</strong> ${app.ports?.join(', ') || 'Default'}</p>
                <p><strong>Database:</strong> ${app.database || 'None'}</p>
                <p><strong>Price:</strong> ${app.price || 0} FCFA/month</p>
            </div>
            <div class="modal-footer">
                <button class="btn btn-secondary" onclick="this.closest('.modal-overlay').remove()">Close</button>
                <button class="btn btn-primary" onclick="deployApp('${app.id}')">Deploy</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
}

async function showPaasAppDetails(appName, dbType) {
    const username = authService.currentUser.username;
    // Multi-call state management
    const [stats, creds, logs] = await Promise.all([
        window.fetchAPI(`/paas/apps/${appName}/stats`),
        window.fetchAPI(`/paas/database/credentials?type=${dbType}&app=${appName}&user=${username}`),
        window.fetchAPI(`/paas/apps/${appName}/logs?lines=50`)
    ]);

    const modalBody = `
        <div class="paas-details-tabs">
            <div class="tab-header">
                <button class="active" onclick="switchTab('access')">Access & DB</button>
                <button onclick="switchTab('monitoring')">Performance</button>
                <button onclick="switchTab('logs')">Logs</button>
            </div>
            <div id="tab-access" class="tab-pane active">
                <div class="credential-box">
                    <h4>🗄️ Database Credentials</h4>
                    <p><strong>Host:</strong> ${creds.host}</p>
                    <p><strong>DB Name:</strong> ${creds.database}</p>
                    <p><strong>User:</strong> ${creds.user}</p>
                    <div class="password-field">
                        <strong>Password:</strong> 
                        <span id="db-pass">••••••••</span>
                        <button onclick="document.getElementById('db-pass').innerText='${creds.password}'">Show</button>
                    </div>
                </div>
            </div>
            <div id="tab-monitoring" class="tab-pane">
                <p>CPU: ${stats.cpu}</p>
                <p>Memory: ${stats.memory}</p>
            </div>
            <div id="tab-logs" class="tab-pane">
                <pre class="log-viewer">${logs.logs}</pre>
            </div>
        </div>
    `;
    // Render to modal...
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    initPaaSCatalog();
    
    // Update the back button in catalog view
    const backButton = document.querySelector('#view-paas-catalog .btn-secondary');
    if (backButton) {
        backButton.onclick = () => switchView('paas');
    }
});

// Export functions
window.renderCatalogApps = renderCatalogApps;
window.initPaaSCatalog = initPaaSCatalog;
window.showAppDetailsModal = showAppDetailsModal;
window.showPaasAppDetails = showPaasAppDetails