import APPS_CATALOG from "./paas-catalog";


// Users Data (for demo)
let users = [
    { id: 1, username: 'admin', role: 'admin', vms: 5, cpu: 8, ram: 16 },
    { id: 2, username: 'dev1', role: 'user', vms: 2, cpu: 4, ram: 8 },
    { id: 3, username: 'dev2', role: 'user', vms: 1, cpu: 2, ram: 4 }
];

// Monitoring Charts
let charts = {};
let monitoringInterval = null;

// ==========================================
// PaaS Applications
// ==========================================

async function refreshApps() {
    const appsGrid = document.getElementById('apps-grid');
    if (!appsGrid) return;
    
    appsGrid.innerHTML = '<p class="loading-text">Loading applications...</p>';
    
    // Simulate API call
    setTimeout(() => {
        renderApps(APPS_CATALOG);
    }, 500);
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

function deployApp(appId) {
    const app = APPS_CATALOG.find(a => a.id === appId);
    if (!app) return;
    


    // TODO
    showToast(`Deploying ${app.name}...`, 'info');
    
    // Simulate deployment
    setTimeout(() => {
        showToast(`✅ ${app.name} deployed successfully!`, 'success');
        updateBilling();
    }, 2000);
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
// Docker Swarm Management
// ==========================================

function showCreateSwarmModal() {
    showToast('Create Swarm feature - UI ready, backend needed', 'info');
}

async function deploySwarmCluster() {
    const managerNodes = document.querySelector('input[placeholder="1, 3, 5..."]').value;
    const workerNodes = document.querySelector('input[placeholder="2"]').value;
    
    showToast(`Creating cluster: ${managerNodes} managers, ${workerNodes} workers...`, 'info');
    


    //TODO
    // Simulate cluster creation
    setTimeout(() => {
        showToast('✅ Swarm cluster created successfully!', 'success');
    }, 3000);
}

// ==========================================
// Users & Quotas Management
// ==========================================

function showAddUserModal() {
    showToast('Add User feature - UI ready, backend needed', 'info');
}

function loadUsersTable() {
    const tbody = document.getElementById('users-table-body');
    if (!tbody) return;
    
    tbody.innerHTML = '';
    
    users.forEach(user => {
        const row = document.createElement('tr');
        row.innerHTML = `
            <td>
                <strong>${user.username}</strong><br>
                <small style="color: var(--text-secondary)">${user.role}</small>
            </td>
            <td>${user.role}</td>
            <td>${user.vms}</td>
            <td>${user.cpu} vCPUs</td>
            <td>${user.ram} GB</td>
            <td>
                <button class="btn btn-sm btn-info" onclick="editUser(${user.id})">✏️</button>
                <button class="btn btn-sm btn-danger" onclick="deleteUser(${user.id})">🗑️</button>
            </td>
        `;
        tbody.appendChild(row);
    });
}

//TODO
function editUser(userId) {
    showToast(`Edit user ${userId} - UI ready, backend needed`, 'info');
}

function deleteUser(userId) {
    const user = users.find(u => u.id === userId);
    if (!user) return;
    
    if (!confirm(`Delete user ${user.username}?`)) return;
    
    users = users.filter(u => u.id !== userId);
    showToast(`✅ User ${user.username} deleted`, 'success');
    loadUsersTable();
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
            <td>Bandwidth</td>
            <td>500 GB</td>
            <td>$0.05/GB</td>
            <td>$25.00</td>
        </tr>
        <tr>
            <td><strong>Total</strong></td>
            <td colspan="2"></td>
            <td><strong>$215.00</strong></td>
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
// Monitoring with Charts
// ==========================================

function initCharts() {
    if (typeof Chart === 'undefined') {
        console.warn('Chart.js not loaded, retrying in 1 second...');
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
    
    try {
        const cpuChart = document.getElementById('cpu-chart');
        const memoryChart = document.getElementById('memory-chart');
        const diskChart = document.getElementById('disk-chart');
        const networkChart = document.getElementById('network-chart');
        
        if (cpuChart) charts.cpu = new Chart(cpuChart, JSON.parse(JSON.stringify(chartConfig)));
        if (memoryChart) charts.memory = new Chart(memoryChart, JSON.parse(JSON.stringify(chartConfig)));
        if (diskChart) charts.disk = new Chart(diskChart, JSON.parse(JSON.stringify(chartConfig)));
        if (networkChart) charts.network = new Chart(networkChart, JSON.parse(JSON.stringify(chartConfig)));
    } catch (error) {
        console.error('Error initializing charts:', error);
    }
}

function toggleMonitoring() {
    const toggleBtn = document.getElementById('monitoring-toggle');
    
    if (monitoringInterval) {
        clearInterval(monitoringInterval);
        monitoringInterval = null;
        toggleBtn.innerHTML = '<span>▶️</span> Start';
        showToast('Monitoring stopped', 'info');
    } else {
        if (!currentVM) {
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
        const data = await fetchAPI('/system/info');
        systemInfo.textContent = `${data.nodeInfo}\n\n${data.version}`;
    } catch (error) {
        // Fallback to static data
        const staticData = {
            nodeInfo: `🌍 THOTH CLOUD - Multi-Host Platform
=================================

🖥️ Main Host (node1)
• CPU: 32 vCPUs (8 physical)
• RAM: 64 GB DDR4
• Storage: 2 TB SSD NVMe
• Network: 10 Gbps
• Hypervisor: KVM/libvirt

🖥️ Secondary Host (node2)
• CPU: 16 vCPUs (4 physical)
• RAM: 32 GB DDR4
• Storage: 1 TB SSD
• Network: 10 Gbps
• Status: 🟢 Connected

🖥️ Tertiary Host (node3)
• CPU: 8 vCPUs (2 physical)
• RAM: 16 GB DDR4
• Storage: 500 GB SSD
• Network: 1 Gbps
• Status: 🟡 Maintenance

📊 Cluster Statistics:
• Total VMs: 12
• Active VMs: 7
• CPU Usage: 65%
• RAM Usage: 78%
• Storage Used: 42%`,
            version: 'THOTH CLOUD v2.0 - PaaS Edition'
        };
        
        systemInfo.textContent = `${staticData.nodeInfo}\n\n${staticData.version}`;
    }
}

//TODO
// ==========================================
// Modals Management TODO
// ==========================================

function showSnapshotModal() {
    showToast('Create Snapshot - UI ready, backend needed', 'info');
}

function closeSnapshotModal() {
    // Modal close logic
}

function showCloneModal() {
    showToast('Clone VM - UI ready, backend needed', 'info');
}

function closeCloneModal() {
    // Modal close logic
}

function showConsoleModal() {
    showToast('VM Console - UI ready, backend needed', 'info');
}

function closeConsoleModal() {
    // Modal close logic
}

function showDeleteVMModal() {
    showToast('Delete VM - UI ready, backend needed', 'info');
}

async function createSnapshot(event) {
    event.preventDefault();
    showToast('Creating snapshot...', 'info');
}

async function cloneVM(event) {
    event.preventDefault();
    showToast('Cloning VM...', 'info');
}

// ==========================================
// Initialize Everything
// ==========================================

window.addEventListener('DOMContentLoaded', () => {
    // Initialize charts
    initCharts();
    
    // Load initial data
    loadVMs();
    refreshApps();
    loadUsersTable();
    updateBilling();
    
    // Auto-refresh VMs every 10 seconds
    setInterval(() => {
        if (document.getElementById('view-vms')?.classList.contains('active') ||
            document.getElementById('view-dashboard')?.classList.contains('active')) {
            loadVMs();
        }
    }, 10000);
});