let APPS_CATALOG = window.PAAS_APPS_EXTENDED

const style = document.createElement('style');
style.textContent = `
    .owner-badge {
        display: inline-block;
        padding: 2px 8px;
        background: rgba(0, 132, 61, 0.1);
        border-radius: 12px;
        font-size: 0.85em;
        color: var(--primary-color);
        margin-left: 8px;
    }
`;
document.head.appendChild(style);


// Monitoring Charts
let charts = {};
let monitoringInterval = null;

// ==========================================
// ROLE-BASED UI VISIBILITY
// ==========================================

function initializeRoleBasedUI() {
    const user = authService.currentUser;
    if (!user) return;
    
    const isAdmin = user.role === 'admin';
    
    // Hide admin-only sections for regular users
    const adminSections = document.querySelectorAll('[data-admin-only]');
    adminSections.forEach(section => {
        section.style.display = isAdmin ? '' : 'none';
    });
    
    // Hide admin nav items
    const adminNavItems = [
        'users',
        'monitoring',
        'system'
    ];
    
    adminNavItems.forEach(viewName => {
        const navItem = document.querySelector(`[data-view="${viewName}"]`);
        if (navItem && !isAdmin) {
            navItem.style.display = 'none';
        }
    });
}

// ==========================================
// PaaS Applications
// ==========================================

async function refreshApps() {
    const appsGrid = document.getElementById('apps-grid');
    if (!appsGrid) return;
    
    appsGrid.innerHTML = '<p class="loading-text">Loading applications...</p>';
    
    try {
        const data = await window.fetchAPI('/paas/apps');
        if (data.success && data.applications) {
            renderRunningApps(data.applications);
        } else {
            renderApps(APPS_CATALOG);
        }
    } catch (error) {
        console.log('Using catalog apps');
        renderApps(APPS_CATALOG);
    }
}

function switchToPaasCatalog() {
    // Hide all views
    document.querySelectorAll('.view').forEach(view => {
        view.classList.remove('active');
    });
    
    // Show catalog view
    const catalogView = document.getElementById('view-paas-catalog');
    if (catalogView) {
        catalogView.classList.add('active');
        renderCatalogApps();
    }
}

function renderApps(apps) {
    const appsGrid = document.getElementById('apps-grid');
    if (!appsGrid) return;
    
    appsGrid.innerHTML = '';
    
    apps.forEach(app => {
        const appCard = createAppCard(app);
        appsGrid.appendChild(appCard);
    });
}

function createAppCard(app) {
    const card = document.createElement('div');
    card.className = 'app-card';
    
    card.innerHTML = `
        <div class="app-card-icon">${app.icon}</div>
        <h4 class="app-card-title">${app.name}</h4>
        <p class="app-card-description">${app.description}</p>
        <div class="app-card-meta">
            <span class="app-category-badge">${app.category}</span>
            <span class="app-price">$${app.price}/mo</span>
        </div>
        <div class="app-card-actions">
            <button class="btn btn-primary btn-sm" onclick="deployApp('${app.id}')">
                Deploy
            </button>
        </div>
    `;
    
    return card;
}

function createRunningAppCard(app) {
    const card = document.createElement('div');
    card.className = 'app-card';
    
    const statusClass = app.running ? 'running' : 'stopped';
    const statusText = app.running ? '🟢 Running' : '🔴 Stopped';
    
    card.innerHTML = `
        <div class="app-card-icon">📦</div>
        <h4 class="app-card-title">${app.name}</h4>
        <p class="app-card-description">Status: ${app.status}</p>
        <div class="app-card-meta">
            <span class="status-badge status-${statusClass}">${statusText}</span>
        </div>
        <div class="app-card-actions">
            <button class="btn btn-danger btn-sm" onclick="deleteApp('${app.name}')">
                Delete
            </button>
        </div>
    `;
    
    return card;
}

function renderRunningApps(apps) {
    const appsGrid = document.getElementById('apps-grid');
    if (!appsGrid) return;
    
    appsGrid.innerHTML = '';
    
    if (!apps || apps.length === 0) {
        appsGrid.innerHTML = '<p class="loading-text">No applications deployed yet</p>';
        return;
    }
    
    apps.forEach(app => {
        const appCard = createRunningAppCard(app);
        appsGrid.appendChild(appCard);
    });
}

async function deployApp(appId) {
    const app = APPS_CATALOG.find(a => a.id === appId);
    if (!app) return;
    
    showToast(`Deploying ${app.name}...`, 'info');
    
    try {
        const result = await window.fetchAPI('/paas/deploy', {
            method: 'POST',
            body: JSON.stringify({
                id: appId,
                name: app.name,
                dockerImage: app.id,
                ports: app.ports || [],
                category: app.category
            })
        });
        
        if (result.success) {
            showToast(`✅ ${app.name} deployed successfully!`, 'success');
            await refreshApps();
        } else {
            showToast(`❌ Deployment failed: ${result.error}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function deleteApp(appName) {
    if (!confirm(`Delete application ${appName}?`)) return;
    
    try {
        showToast('Deleting application...', 'info');
        await window.fetchAPI(`/paas/apps/${appName}`, { method: 'DELETE' });
        showToast('✅ Application deleted', 'success');
        await refreshApps();
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

// App Search
const appSearchInput = document.getElementById('app-search');
if (appSearchInput) {
    appSearchInput.addEventListener('input', debounce((e) => {
        const searchTerm = e.target.value.toLowerCase();
        const filteredApps = APPS_CATALOG.filter(app =>
            app.name.toLowerCase().includes(searchTerm) ||
            app.description.toLowerCase().includes(searchTerm) ||
            app.category.includes(searchTerm)
        );
        renderApps(filteredApps);
    }, 300));
}

// App Category Filter
document.querySelectorAll('[data-category]').forEach(chip => {
    chip.addEventListener('click', () => {
        const category = chip.dataset.category;
        
        // Update active chip
        document.querySelectorAll('[data-category]').forEach(c => c.classList.remove('active'));
        chip.classList.add('active');
        
        if (category === 'all') {
            renderApps(APPS_CATALOG);
        } else {
            const filtered = APPS_CATALOG.filter(app => app.category === category);
            renderApps(filtered);
        }
    });
});
// ==========================================
// HOST MANAGEMENT
// ==========================================

async function loadHosts() {
    try {
        const data = await window.fetchAPI('/hosts');
        renderHosts(data.hosts);
    } catch (error) {
        console.error('Error loading hosts:', error);
        showToast(`Error loading hosts: ${error.message}`, 'error');
    }
}

function renderHosts(hosts) {
    // Update UI with hosts data
    console.log('Hosts:', hosts);
}

async function loadHostStats() {
    try {
        const data = await window.fetchAPI('/hosts/stats');
        // Update host stats display
        console.log('Host stats:', data);
    } catch (error) {
        console.error('Error loading host stats:', error);
    }
}


// ==========================================
// Docker Swarm Management
// ==========================================

function showCreateSwarmModal() {
    showToast('Create Swarm feature - UI ready, backend needed', 'info');
}

async function deploySwarmCluster() {
    const managerNodes = document.querySelector('input[placeholder="1, 3, 5..."]').value;
    const workerNodes = document.querySelector('input[placeholder="2"]').value;
    
    showToast(`Creating cluster: ${managerNodes} managers, ${workerNodes} workers...`, 'info');
    
    // Simulate cluster creation
    setTimeout(() => {
        showToast('✅ Swarm cluster created successfully!', 'success');
    }, 3000);
}

// ==========================================
// Users & Quotas Management
// ==========================================
async function loadUsersTable() {
    const tbody = document.getElementById('users-table-body');
    if (!tbody) return;
    
    tbody.innerHTML = '<tr><td colspan="6">Loading users...</td></tr>';
    
    try {
        const data = await window.fetchAPI('/users');
        
        if (!data.success || !data.users || data.users.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6">No users found</td></tr>';
            return;
        }
        
        tbody.innerHTML = '';
        data.users.forEach(user => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>
                    <strong>${user.username}</strong><br>
                    <small style="color: var(--text-secondary)">${user.email || ''}</small>
                </td>
                <td><span class="role-badge ${user.role}">${user.role}</span></td>
                <td>${user.usage?.vms || 0} / ${user.quotas?.maxVMs || 0}</td>
                <td>${user.usage?.cpu || 0} / ${user.quotas?.maxCPU || 0}</td>
                <td>${user.usage?.ram || 0} / ${user.quotas?.maxRAM || 0} GB</td>
                <td>
                    <button class="btn btn-sm btn-info" onclick="editUser('${user.username}')">✏️</button>
                    <button class="btn btn-sm btn-danger" onclick="deleteUser('${user.username}')">🗑️</button>
                </td>
            `;
            tbody.appendChild(row);
        });
    } catch (error) {
        tbody.innerHTML = `<tr><td colspan="6">Error: ${error.message}</td></tr>`;
        showToast(`Error loading users: ${error.message}`, 'error');
    }
}

function showAddUserModal() {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'add-user-modal';
    
    modal.innerHTML = `
        <div class="modal" style="max-width: 600px; max-height: 90vh; overflow-y: auto;">
            <div class="modal-header">
                <h3>👤 Add New User</h3>
                <button class="modal-close" onclick="closeAddUserModal()">✖</button>
            </div>
            <form onsubmit="createUser(event)">
                <div class="modal-body" style="max-height: calc(90vh - 150px); overflow-y: auto;">
                    <div class="form-row">
                        <div class="form-group">
                            <label>First Name *</label>
                            <input type="text" id="new-firstName" required>
                        </div>
                        <div class="form-group">
                            <label>Last Name *</label>
                            <input type="text" id="new-lastName" required>
                        </div>
                    </div>
                    
                    <div class="form-group">
                        <label>Username *</label>
                        <input type="text" id="new-username" required 
                               pattern="[a-z][a-z0-9_]*" 
                               title="Lowercase letters, numbers, underscore only">
                    </div>
                    
                    <div class="form-group">
                        <label>Email *</label>
                        <input type="email" id="new-email" required>
                    </div>
                    
                    <div class="form-group">
                        <label>Password *</label>
                        <input type="password" id="new-password" required minlength="8">
                    </div>
                    
                    <div class="form-group">
                        <label>Role *</label>
                        <select id="new-role" required>
                            <option value="user">User</option>
                            <option value="admin">Admin</option>
                        </select>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeAddUserModal()">Cancel</button>
                    <button type="submit" class="btn btn-primary">Create User</button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
}

async function createUser(event) {
    event.preventDefault();
    
    const userData = {
        username: document.getElementById('new-username').value,
        email: document.getElementById('new-email').value,
        password: document.getElementById('new-password').value,
        firstName: document.getElementById('new-firstName').value,
        lastName: document.getElementById('new-lastName').value,
        role: document.getElementById('new-role').value
    };
    
    try {
        showToast('Creating user...', 'info');
        const result = await window.fetchAPI('/users', {
            method: 'POST',
            body: JSON.stringify(userData)
        });
        
        if (result.success) {
            showToast('✅ User created successfully', 'success');
            closeAddUserModal();
            await loadUsersTable();
        } else {
            showToast(`❌ ${result.error}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

function closeAddUserModal() {
    const modal = document.getElementById('add-user-modal');
    if (modal) modal.remove();
}

function editUser(username) {
    showToast(`Edit user ${username} - TO DO :(`, 'info');
}

async function deleteUser(username) {
    if (!confirm(`Delete user ${username}?`)) return;
    
    try {
        showToast('Deleting user...', 'info');
        await window.fetchAPI(`/users/${username}`, { method: 'DELETE' });
        showToast('✅ User deleted', 'success');
        await loadUsersTable();
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

// ==========================================
// Billing Management
// ==========================================

function updateBilling() {
    const billingDetails = document.getElementById('billing-details');
    if (!billingDetails) return;
    
    billingDetails.innerHTML = `
        <tr>
            <td>Standard VMs</td>
            <td>7</td>
            <td>$15/VM</td>
            <td>$105.00</td>
        </tr>
        <tr>
            <td>PaaS Services</td>
            <td>3</td>
            <td>$20/service</td>
            <td>$60.00</td>
        </tr>
        <tr>
            <td>Storage</td>
            <td>250 GB</td>
            <td>$0.10/GB</td>
            <td>$25.00</td>
        </tr>
        <tr>
            <td><strong>Total</strong></td>
            <td colspan="2"></td>
            <td><strong>$190.00</strong></td>
        </tr>
    `;
    
    initConsumptionChart();
}


function initConsumptionChart() {
    if (typeof Chart === 'undefined') {
        console.warn('Chart.js not loaded');
        return;
    }
    
    const ctx = document.getElementById('consumption-chart');
    if (!ctx) return;
    
    // Destroy existing chart if it exists
    const existingChart = Chart.getChart(ctx);
    if (existingChart) {
        existingChart.destroy();
    }

    try {
        new Chart(ctx.getContext('2d'), {
            type: 'line',
            data: {
                labels: ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun'],
                datasets: [{
                    label: 'Monthly Cost ($)',
                    data: [150, 180, 200, 215, 230, 245],
                    borderColor: '#00843D',
                    backgroundColor: 'rgba(0, 132, 61, 0.1)',
                    tension: 0.4,
                    fill: true
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: true,
                plugins: {
                    legend: {
                        position: 'top',
                    }
                }
            }
        });
    } catch (error) {
        console.error('Error initializing consumption chart:', error);
    }
}

function generateInvoice() {
    const invoiceId = `INV-${Date.now().toString().slice(-6)}`;
    showToast(`Invoice ${invoiceId} generated!`, 'success');
}

// ==========================================
// Monitoring
// ==========================================

function initCharts() {
    if (typeof Chart === 'undefined') {
        setTimeout(initCharts, 1000);
        return;
    }
    
    const chartConfig = {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Value',
                data: [],
                borderColor: '#00843D',
                backgroundColor: 'rgba(0, 132, 61, 0.1)',
                tension: 0.4,
                fill: true
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: { display: false }
            },
            scales: {
                y: { beginAtZero: true }
            }
        }
    };
    
    const ids = ['cpu-chart', 'memory-chart', 'disk-chart', 'network-chart'];
    ids.forEach(id => {
        const el = document.getElementById(id);
        if (el) charts[id.replace('-chart', '')] = new Chart(el, JSON.parse(JSON.stringify(chartConfig)));
    });
}

function toggleMonitoring() {
    const toggleBtn = document.getElementById('monitoring-toggle');
    
    if (monitoringInterval) {
        clearInterval(monitoringInterval);
        monitoringInterval = null;
        toggleBtn.innerHTML = '<span>▶️</span> Start';
        showToast('Monitoring stopped', 'info');
    } else {
        if (!window.currentVM) {  
            showToast('⚠️ Select a VM first', 'warning');
            return;
        }
        monitoringInterval = setInterval(updateMonitoring, 3000);
        toggleBtn.innerHTML = '<span>⏸️</span> Stop';
        showToast('Monitoring started', 'success');
        updateMonitoring();
    }
}


async function updateMonitoring() {
    if (!currentVM) return;
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}/stats`);
        const stats = data.stats;
        if (!stats) return;
        
        const now = new Date().toLocaleTimeString();
        
        updateChart(charts.cpu, now, stats.cpu);
        updateChart(charts.memory, now, parseFloat(stats.memory.percent));
        updateChart(charts.disk, now, parseFloat(stats.disk.writeMB));
        updateChart(charts.network, now, parseFloat(stats.network.txMB));
    } catch (error) {
        console.error('Error updating monitoring:', error);
    }
}

function updateChart(chart, label, value) {
    if (!chart) return;
    
    if (chart.data.labels.length > 20) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
    }
    
    chart.data.labels.push(label);
    chart.data.datasets[0].data.push(value);
    chart.update('none');
}

// ==========================================
// System Information
// ==========================================

async function loadSystemInfo() {
    const systemInfo = document.getElementById('system-info');
    if (!systemInfo) return;
    
    systemInfo.textContent = 'Loading system information...';
    
    try {
        const data = await window.fetchAPI('/system/info');
        systemInfo.textContent = `${data.nodeInfo}\n\n${data.version}`;
    } catch (error) {
        systemInfo.textContent = `Error loading system info: ${error.message}`;
    }
}

// ==========================================
// Initialize Everything
// ==========================================

document.addEventListener('DOMContentLoaded', () => {
    if (window.authService?.isAuthenticated()) {
        initializeRoleBasedUI();
    }
});


function debounce(func, wait) {
    let timeout;
    return function(...args) {
        clearTimeout(timeout);
        timeout = setTimeout(() => func(...args), wait);
    };
}

// ==========================================
// EXPORT FUNCTIONS TO GLOBAL SCOPE
// ==========================================

window.refreshApps = refreshApps;
window.deployApp = deployApp;
window.deleteApp = deleteApp;
window.switchToPaasCatalog = switchToPaasCatalog;
window.loadUsersTable = loadUsersTable;
window.showAddUserModal = showAddUserModal;
window.closeAddUserModal = closeAddUserModal;
window.createUser = createUser;
window.editUser = editUser;
window.deleteUser = deleteUser;
window.updateBilling = updateBilling;
window.generateInvoice = generateInvoice;
window.initCharts = initCharts;
window.toggleMonitoring = toggleMonitoring;
window.loadSystemInfo = loadSystemInfo;
window.initializeRoleBasedUI = initializeRoleBasedUI;
