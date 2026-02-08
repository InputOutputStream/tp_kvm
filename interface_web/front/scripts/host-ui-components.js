// ==========================================
// HOST MANAGEMENT UI COMPONENTS
// ==========================================

class HostManagementUI {
    constructor() {
        this.api = window.apiService;
    }

    // Render host statistics with charts
    renderHostStatistics(hosts) {
        const statsContainer = document.getElementById('hosts-list');
        if (!statsContainer) return;

        if (hosts.length === 0) {
            statsContainer.innerHTML = `
                <div class="empty-state">
                    <div class="empty-icon">🖥️</div>
                    <h3>No hosts configured</h3>
                    <p>Add your first compute host to get started</p>
                    <button class="btn btn-primary" onclick="showAddHostModal()">
                        ➕ Add First Host
                    </button>
                </div>
            `;
            return;
        }

        // Create overall statistics
        const overallStats = this.calculateOverallStats(hosts);
        
        statsContainer.innerHTML = `
            <div class="overall-stats-section">
                <h3>📊 Overall Infrastructure Health</h3>
                <div class="overall-stats-grid">
                    <div class="overall-stat">
                        <span class="stat-icon">🖥️</span>
                        <div>
                            <h4>${overallStats.totalHosts}</h4>
                            <p>Total Hosts</p>
                        </div>
                    </div>
                    <div class="overall-stat">
                        <span class="stat-icon">💻</span>
                        <div>
                            <h4>${overallStats.activeVMs}</h4>
                            <p>Active VMs</p>
                        </div>
                    </div>
                    <div class="overall-stat">
                        <span class="stat-icon">📈</span>
                        <div>
                            <h4>${overallStats.cpuUsage}%</h4>
                            <p>Avg CPU Usage</p>
                        </div>
                    </div>
                    <div class="overall-stat">
                        <span class="stat-icon">💾</span>
                        <div>
                            <h4>${overallStats.memoryUsage}%</h4>
                            <p>Avg Memory Usage</p>
                        </div>
                    </div>
                </div>
            </div>
            
            <div class="hosts-grid">
                ${hosts.map(host => this.createHostCard(host)).join('')}
            </div>
        `;

        // Initialize host-specific charts
        this.initHostCharts(hosts);
    }

    calculateOverallStats(hosts) {
        const totalHosts = hosts.length;
        const activeHosts = hosts.filter(h => h.active).length;
        const totalVMs = hosts.reduce((sum, h) => sum + (h.activeVMs || 0), 0);
        const cpuUsage = hosts.reduce((sum, h) => {
            const cpuPercent = ((h.totalCPUs - h.availableCPUs) / h.totalCPUs * 100) || 0;
            return sum + cpuPercent;
        }, 0) / totalHosts;
        const memoryUsage = hosts.reduce((sum, h) => sum + (h.memoryUsage || 0), 0) / totalHosts;

        return {
            totalHosts,
            activeHosts,
            totalVMs,
            cpuUsage: cpuUsage.toFixed(1),
            memoryUsage: memoryUsage.toFixed(1),
            activeVMs: totalVMs
        };
    }

    createHostCard(host) {
        const cpuPercent = ((host.totalCPUs - host.availableCPUs) / host.totalCPUs * 100).toFixed(1);
        const memPercent = host.memoryUsage || 0;
        const diskPercent = ((host.totalDisk - host.availableDisk) / host.totalDisk * 100).toFixed(1);

        return `
            <div class="host-card ${host.active ? 'active' : 'inactive'}">
                <div class="host-card-header">
                    <div class="host-info">
                        <h3>🖥️ ${host.hostname}</h3>
                        <p class="host-uri">${host.uri}</p>
                        <div class="host-tags">
                            <span class="host-tag ${host.active ? 'active' : 'inactive'}">
                                ${host.active ? '🟢 Active' : '🔴 Inactive'}
                            </span>
                            ${host.isPrimary ? '<span class="host-tag primary">⭐ Primary</span>' : ''}
                        </div>
                    </div>
                    <div class="host-actions">
                        <button class="btn btn-sm btn-icon" onclick="testHostConnection('${host.id}')" title="Test Connection">
                            🔗
                        </button>
                        <button class="btn btn-sm btn-icon" onclick="showHostDetails('${host.id}')" title="View Details">
                            👁️
                        </button>
                        <button class="btn btn-sm btn-icon btn-danger" onclick="removeHost('${host.id}')" title="Remove Host">
                            🗑️
                        </button>
                    </div>
                </div>

                <div class="host-resources">
                    <div class="resource-meter">
                        <div class="resource-label">
                            <span>💻 CPU</span>
                            <span>${cpuPercent}%</span>
                        </div>
                        <div class="progress-bar">
                            <div class="progress-fill cpu" style="width: ${cpuPercent}%"></div>
                        </div>
                        <small>${host.totalCPUs - host.availableCPUs}/${host.totalCPUs} cores</small>
                    </div>

                    <div class="resource-meter">
                        <div class="resource-label">
                            <span>💾 Memory</span>
                            <span>${memPercent}%</span>
                        </div>
                        <div class="progress-bar">
                            <div class="progress-fill memory" style="width: ${memPercent}%"></div>
                        </div>
                        <small>${Math.round((host.totalMemory - host.availableMemory) / 1024)}/${Math.round(host.totalMemory / 1024)} GB</small>
                    </div>

                    <div class="resource-meter">
                        <div class="resource-label">
                            <span>💿 Storage</span>
                            <span>${diskPercent}%</span>
                        </div>
                        <div class="progress-bar">
                            <div class="progress-fill disk" style="width: ${diskPercent}%"></div>
                        </div>
                        <small>${host.availableDisk} GB free</small>
                    </div>
                </div>

                <div class="host-vms">
                    <h4>🖥️ Virtual Machines</h4>
                    <div class="vm-stats">
                        <span class="vm-stat">${host.activeVMs || 0} Active</span>
                        <span class="vm-stat">${host.totalVMs || 0} Total</span>
                        <span class="vm-stat">${host.pausedVMs || 0} Paused</span>
                    </div>
                </div>

                <div class="host-health">
                    <div class="health-indicator ${this.getHealthStatus(host)}">
                        <span class="health-dot"></span>
                        <span>${this.getHealthStatus(host).toUpperCase()} Health</span>
                    </div>
                    <small>Last checked: ${this.formatTimestamp(host.lastCheck)}</small>
                </div>
            </div>
        `;
    }

    getHealthStatus(host) {
        const cpuUsage = ((host.totalCPUs - host.availableCPUs) / host.totalCPUs * 100);
        const memUsage = host.memoryUsage || 0;
        
        if (!host.active) return 'critical';
        if (cpuUsage > 90 || memUsage > 90) return 'warning';
        if (cpuUsage > 75 || memUsage > 75) return 'degraded';
        return 'healthy';
    }

    formatTimestamp(timestamp) {
        if (!timestamp) return 'Never';
        const date = new Date(timestamp * 1000);
        return date.toLocaleTimeString();
    }

    async testHostConnection(hostId) {
        try {
            showToast('Testing host connection...', 'info');
            const result = await this.api.request(`/hosts/${hostId}/test`, {
                method: 'POST'
            });
            
            if (result.success) {
                showToast('✅ Host connection successful', 'success');
                await loadHostsManagement();
            } else {
                showToast('❌ Host connection failed', 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    }

    async showHostDetails(hostId) {
        try {
            const host = await this.api.getHostDetails(hostId);
            this.renderHostDetailsModal(host);
        } catch (error) {
            showToast(`Error loading host details: ${error.message}`, 'error');
        }
    }

    renderHostDetailsModal(host) {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'host-details-modal';
        
        modal.innerHTML = `
            <div class="modal modal-large">
                <div class="modal-header">
                    <h3>🖥️ ${host.hostname} - Details</h3>
                    <button class="modal-close" onclick="closeModal('host-details-modal')">✖</button>
                </div>
                <div class="modal-body">
                    <div class="host-details-tabs">
                        <div class="tabs">
                            <button class="tab active" onclick="switchHostTab('overview')">Overview</button>
                            <button class="tab" onclick="switchHostTab('vms')">VMs</button>
                            <button class="tab" onclick="switchHostTab('networks')">Networks</button>
                            <button class="tab" onclick="switchHostTab('storage')">Storage</button>
                        </div>
                        
                        <div id="host-tab-overview" class="tab-content active">
                            ${this.renderHostOverview(host)}
                        </div>
                        
                        <div id="host-tab-vms" class="tab-content">
                            <div id="host-vms-list">Loading VMs...</div>
                        </div>
                        
                        <div id="host-tab-networks" class="tab-content">
                            <div id="host-networks-list">Loading networks...</div>
                        </div>
                        
                        <div id="host-tab-storage" class="tab-content">
                            <div id="host-storage-list">Loading storage...</div>
                        </div>
                    </div>
                </div>
                <div class="modal-footer">
                    <button class="btn btn-secondary" onclick="closeModal('host-details-modal')">Close</button>
                </div>
            </div>
        `;
        
        document.body.appendChild(modal);
        
        // Load additional data
        this.loadHostVMs(host.id);
        this.loadHostNetworks(host.id);
        this.loadHostStorage(host.id);
    }

    renderHostOverview(host) {
        return `
            <div class="host-details-grid">
                <div class="detail-item">
                    <strong>Hostname:</strong>
                    <span>${host.hostname}</span>
                </div>
                <div class="detail-item">
                    <strong>URI:</strong>
                    <code>${host.uri}</code>
                </div>
                <div class="detail-item">
                    <strong>Status:</strong>
                    <span class="status-badge status-${host.active ? 'active' : 'inactive'}">
                        ${host.active ? '🟢 Active' : '🔴 Inactive'}
                    </span>
                </div>
                <div class="detail-item">
                    <strong>CPU Model:</strong>
                    <span>${host.cpuModel || 'Unknown'}</span>
                </div>
                <div class="detail-item">
                    <strong>Architecture:</strong>
                    <span>${host.architecture || 'Unknown'}</span>
                </div>
                <div class="detail-item">
                    <strong>Libvirt Version:</strong>
                    <span>${host.libvirtVersion || 'Unknown'}</span>
                </div>
                <div class="detail-item">
                    <strong>Hypervisor:</strong>
                    <span>${host.hypervisor || 'Unknown'}</span>
                </div>
            </div>
            
            <div class="resource-charts" style="margin-top: 20px;">
                <h4>Resource Usage</h4>
                <div class="charts-grid">
                    <canvas id="host-cpu-chart" width="200" height="100"></canvas>
                    <canvas id="host-memory-chart" width="200" height="100"></canvas>
                    <canvas id="host-disk-chart" width="200" height="100"></canvas>
                </div>
            </div>
        `;
    }

    async loadHostVMs(hostId) {
        try {
            const vms = await this.api.request(`/hosts/${hostId}/vms`);
            const container = document.getElementById('host-vms-list');
            
            if (vms.length === 0) {
                container.innerHTML = '<p>No VMs on this host</p>';
                return;
            }
            
            container.innerHTML = `
                <table class="data-table">
                    <thead>
                        <tr>
                            <th>VM Name</th>
                            <th>Status</th>
                            <th>vCPUs</th>
                            <th>Memory</th>
                            <th>Actions</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${vms.map(vm => `
                            <tr>
                                <td>${vm.name}</td>
                                <td>
                                    <span class="status-badge status-${vm.running ? 'active' : 'inactive'}">
                                        ${vm.running ? '🟢 Running' : '🔴 Stopped'}
                                    </span>
                                </td>
                                <td>${vm.vcpus || 1}</td>
                                <td>${Math.round((vm.memory || 0) / 1024)} GB</td>
                                <td>
                                    <button class="btn btn-sm btn-secondary" onclick="selectVM('${vm.name}')">
                                        Select
                                    </button>
                                </td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            `;
        } catch (error) {
            document.getElementById('host-vms-list').innerHTML = 
                `<p class="error-text">Error loading VMs: ${error.message}</p>`;
        }
    }

    initHostCharts(hosts) {
        if (typeof Chart === 'undefined') return;
        
        hosts.forEach((host, index) => {
            const canvasId = `host-chart-${host.id}`;
            setTimeout(() => {
                const ctx = document.getElementById(canvasId)?.getContext('2d');
                if (ctx) {
                    new Chart(ctx, {
                        type: 'doughnut',
                        data: {
                            labels: ['Used CPU', 'Free CPU'],
                            datasets: [{
                                data: [
                                    host.totalCPUs - host.availableCPUs,
                                    host.availableCPUs
                                ],
                                backgroundColor: ['#00843D', '#e0e0e0']
                            }]
                        },
                        options: {
                            responsive: true,
                            plugins: {
                                legend: { display: false }
                            }
                        }
                    });
                }
            }, index * 100);
        });
    }
}

// Initialize
window.hostManagementUI = new HostManagementUI();