// ==========================================
// MONITORING DASHBOARD
// ==========================================

class MonitoringDashboard {
    constructor() {
        this.api = window.apiService;
        this.charts = {};
        this.updateInterval = null;
        this.monitoredVMs = new Set();
        this.initialized = false;

    }

    async init() {
        if (this.initialized) return;
        await this.loadMonitoringData();
        this.initCharts();
        this.startAutoRefresh();

        // Connect to VM selection
        window.stateManager?.subscribe('currentVM', (vmName) => {
            if (vmName) {
                this.monitorVM(vmName);
            }
        });
        
        this.initialized = true;
    }

    async loadMonitoringData() {
        try {
            // Load overall infrastructure stats
            const [hosts, vms, paas] = await Promise.all([
                this.api.getHostStats(),
                this.api.getVMs(),
                this.api.getPaasApplications()
            ]);
            
            this.renderOverviewStats(hosts, vms, paas);
            this.updateResourceCharts(hosts);
            this.updateVMMetrics(vms);
            
        } catch (error) {
            console.error('Error loading monitoring data:', error);
        }
    }

    renderOverviewStats(hosts, vms, paas) {
        const statsContainer = document.getElementById('monitoring-stats');
        if (!statsContainer) return;

        const totalVMs = vms.vms?.length || 0;
        const runningVMs = vms.vms?.filter(vm => vm.running).length || 0;
        const totalApps = paas.applications?.length || 0;
        const runningApps = paas.applications?.filter(app => app.running).length || 0;

        statsContainer.innerHTML = `
            <div class="monitoring-stats-grid">
                <div class="monitoring-stat">
                    <div class="stat-icon">🖥️</div>
                    <div class="stat-content">
                        <h3>${hosts.hosts?.length || 0}</h3>
                        <p>Compute Hosts</p>
                    </div>
                </div>
                <div class="monitoring-stat">
                    <div class="stat-icon">💻</div>
                    <div class="stat-content">
                        <h3>${totalVMs}</h3>
                        <p>Total VMs</p>
                    </div>
                </div>
                <div class="monitoring-stat">
                    <div class="stat-icon">📦</div>
                    <div class="stat-content">
                        <h3>${totalApps}</h3>
                        <p>PaaS Apps</p>
                    </div>
                </div>
                <div class="monitoring-stat">
                    <div class="stat-icon">📈</div>
                    <div class="stat-content">
                        <h3>${this.calculateUtilization(hosts)}%</h3>
                        <p>Overall Utilization</p>
                    </div>
                </div>
            </div>
        `;
    }

    calculateUtilization(hosts) {
        if (!hosts.hosts || hosts.hosts.length === 0) return 0;
        
        let totalCPU = 0;
        let usedCPU = 0;
        let totalMem = 0;
        let usedMem = 0;
        
        hosts.hosts.forEach(host => {
            totalCPU += host.totalCPUs || 0;
            usedCPU += (host.totalCPUs - host.availableCPUs) || 0;
            totalMem += host.totalMemory || 0;
            usedMem += (host.totalMemory - host.availableMemory) || 0;
        });
        
        const cpuUtil = (usedCPU / totalCPU) * 100;
        const memUtil = (usedMem / totalMem) * 100;
        
        return ((cpuUtil + memUtil) / 2).toFixed(1);
    }

    initCharts() {
        if (typeof Chart === 'undefined') return;

        // CPU Usage Chart
        const cpuCtx = document.getElementById('cpu-chart')?.getContext('2d');
        if (cpuCtx) {
            this.charts.cpu = new Chart(cpuCtx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'CPU Usage %',
                        data: [],
                        borderColor: '#00843D',
                        backgroundColor: 'rgba(0, 132, 61, 0.1)',
                        tension: 0.4,
                        fill: true
                    }]
                },
                options: this.getChartOptions('CPU Usage (%)')
            });
        }

        // Memory Usage Chart
        const memCtx = document.getElementById('memory-chart')?.getContext('2d');
        if (memCtx) {
            this.charts.memory = new Chart(memCtx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Memory Usage %',
                        data: [],
                        borderColor: '#2196F3',
                        backgroundColor: 'rgba(33, 150, 243, 0.1)',
                        tension: 0.4,
                        fill: true
                    }]
                },
                options: this.getChartOptions('Memory Usage (%)')
            });
        }

        // Network I/O Chart
        const netCtx = document.getElementById('network-chart')?.getContext('2d');
        if (netCtx) {
            this.charts.network = new Chart(netCtx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [
                        {
                            label: 'Network RX (MB/s)',
                            data: [],
                            borderColor: '#4CAF50',
                            backgroundColor: 'rgba(76, 175, 80, 0.1)',
                            tension: 0.4,
                            fill: true
                        },
                        {
                            label: 'Network TX (MB/s)',
                            data: [],
                            borderColor: '#FF9800',
                            backgroundColor: 'rgba(255, 152, 0, 0.1)',
                            tension: 0.4,
                            fill: true
                        }
                    ]
                },
                options: this.getChartOptions('Network I/O (MB/s)')
            });
        }

        // Disk I/O Chart
        const diskCtx = document.getElementById('disk-chart')?.getContext('2d');
        if (diskCtx) {
            this.charts.disk = new Chart(diskCtx, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [
                        {
                            label: 'Disk Read (MB/s)',
                            data: [],
                            borderColor: '#9C27B0',
                            backgroundColor: 'rgba(156, 39, 176, 0.1)',
                            tension: 0.4,
                            fill: true
                        },
                        {
                            label: 'Disk Write (MB/s)',
                            data: [],
                            borderColor: '#F44336',
                            backgroundColor: 'rgba(244, 67, 54, 0.1)',
                            tension: 0.4,
                            fill: true
                        }
                    ]
                },
                options: this.getChartOptions('Disk I/O (MB/s)')
            });
        }
    }

    getChartOptions(title) {
        return {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: {
                    position: 'top',
                },
                title: {
                    display: true,
                    text: title
                }
            },
            scales: {
                y: {
                    beginAtZero: true,
                    title: {
                        display: true,
                        text: 'Value'
                    }
                },
                x: {
                    title: {
                        display: true,
                        text: 'Time'
                    }
                }
            },
            animation: {
                duration: 0
            }
        };
    }

    updateResourceCharts(hosts) {
        if (!hosts.hosts || hosts.hosts.length === 0) return;

        // Update resource distribution chart
        const resourceCtx = document.getElementById('resource-distribution-chart')?.getContext('2d');
        if (resourceCtx) {
            const hostNames = hosts.hosts.map(h => h.hostname);
            const cpuData = hosts.hosts.map(h => 
                ((h.totalCPUs - h.availableCPUs) / h.totalCPUs * 100).toFixed(1)
            );
            const memData = hosts.hosts.map(h => h.memoryUsage || 0);

            if (this.charts.resourceDistribution) {
                this.charts.resourceDistribution.destroy();
            }

            this.charts.resourceDistribution = new Chart(resourceCtx, {
                type: 'bar',
                data: {
                    labels: hostNames,
                    datasets: [
                        {
                            label: 'CPU Usage %',
                            data: cpuData,
                            backgroundColor: '#00843D'
                        },
                        {
                            label: 'Memory Usage %',
                            data: memData,
                            backgroundColor: '#2196F3'
                        }
                    ]
                },
                options: {
                    responsive: true,
                    plugins: {
                        title: {
                            display: true,
                            text: 'Resource Distribution Across Hosts'
                        }
                    },
                    scales: {
                        y: {
                            beginAtZero: true,
                            max: 100,
                            title: {
                                display: true,
                                text: 'Usage (%)'
                            }
                        }
                    }
                }
            });
        }
    }

    updateVMMetrics(vms) {
        const vmList = document.getElementById('vm-metrics-list');
        if (!vmList || !vms.vms) return;

        vmList.innerHTML = '';
        
        vms.vms.forEach(vm => {
            const vmCard = document.createElement('div');
            vmCard.className = 'vm-metric-card';
            vmCard.innerHTML = `
                <div class="vm-metric-header">
                    <h4>🖥️ ${vm.displayName || vm.name}</h4>
                    <span class="status-badge status-${vm.running ? 'active' : 'inactive'}">
                        ${vm.running ? '🟢 Running' : '🔴 Stopped'}
                    </span>
                </div>
                <div class="vm-metric-body">
                    <div class="vm-metric-row">
                        <span class="metric-label">CPU:</span>
                        <div class="metric-value">
                            <div class="progress-bar small">
                                <div class="progress-fill" style="width: ${vm.stats?.cpu || 0}%"></div>
                            </div>
                            <span>${vm.stats?.cpu || 0}%</span>
                        </div>
                    </div>
                    <div class="vm-metric-row">
                        <span class="metric-label">Memory:</span>
                        <div class="metric-value">
                            <div class="progress-bar small">
                                <div class="progress-fill" style="width: ${vm.stats?.memory?.percent || 0}%"></div>
                            </div>
                            <span>${vm.stats?.memory?.percent || 0}%</span>
                        </div>
                    </div>
                    <div class="vm-metric-row">
                        <span class="metric-label">Disk I/O:</span>
                        <span class="metric-value">${vm.stats?.disk?.readMB || 0}/${vm.stats?.disk?.writeMB || 0} MB</span>
                    </div>
                    <div class="vm-metric-row">
                        <span class="metric-label">Network:</span>
                        <span class="metric-value">${vm.stats?.network?.rxMB || 0}/${vm.stats?.network?.txMB || 0} MB</span>
                    </div>
                </div>
                <div class="vm-metric-actions">
                    <button class="btn btn-sm btn-secondary" onclick="monitoringDashboard.monitorVM('${vm.name}')">
                        ${this.monitoredVMs.has(vm.name) ? '📈 Monitoring' : '📊 Monitor'}
                    </button>
                </div>
            `;
            vmList.appendChild(vmCard);
        });
    }

    async monitorVM(vmName) {
        if (this.monitoredVMs.has(vmName)) {
            this.monitoredVMs.delete(vmName);
            showToast(`Stopped monitoring ${vmName}`, 'info');
        } else {
            this.monitoredVMs.add(vmName);
            showToast(`Started monitoring ${vmName}`, 'success');
            
            // Update charts with initial data
            await this.updateVMCharts(vmName);
        }
    }

    async updateVMCharts(vmName) {
        try {
            const stats = await this.api.getVMStats(vmName);
            
            if (!stats.stats) return;
            
            const timestamp = new Date().toLocaleTimeString();
            
            // Update CPU chart
            if (this.charts.cpu) {
                this.updateChartData(this.charts.cpu, timestamp, stats.stats.cpu);
            }
            
            // Update memory chart
            if (this.charts.memory) {
                this.updateChartData(this.charts.memory, timestamp, stats.stats.memory.percent);
            }
            
            // Update network chart
            if (this.charts.network) {
                this.updateChartData(this.charts.network, timestamp, [
                    stats.stats.network.rxMB,
                    stats.stats.network.txMB
                ]);
            }
            
            // Update disk chart
            if (this.charts.disk) {
                this.updateChartData(this.charts.disk, timestamp, [
                    stats.stats.disk.readMB,
                    stats.stats.disk.writeMB
                ]);
            }
            
        } catch (error) {
            console.error(`Error updating charts for ${vmName}:`, error);
        }
    }

    updateChartData(chart, label, data) {
        if (!chart) return;
        
        // Add new data point
        chart.data.labels.push(label);
        
        if (Array.isArray(data)) {
            data.forEach((value, index) => {
                if (chart.data.datasets[index]) {
                    chart.data.datasets[index].data.push(value);
                }
            });
        } else {
            if (chart.data.datasets[0]) {
                chart.data.datasets[0].data.push(data);
            }
        }
        
        // Keep only last 20 data points
        if (chart.data.labels.length > 20) {
            chart.data.labels.shift();
            chart.data.datasets.forEach(dataset => dataset.data.shift());
        }
        
        chart.update('none');
    }

    startAutoRefresh() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
        }
        
        this.updateInterval = setInterval(async () => {
            await this.refreshMonitoringData();
        }, 5000); // Update every 5 seconds
    }

    async refreshMonitoringData() {
        try {
            // Update monitored VMs
            for (const vmName of this.monitoredVMs) {
                await this.updateVMCharts(vmName);
            }
            
            // Refresh overall stats every 30 seconds
            if (this.refreshCounter % 6 === 0) {
                await this.loadMonitoringData();
            }
            
            this.refreshCounter = (this.refreshCounter || 0) + 1;
            
        } catch (error) {
            console.error('Error refreshing monitoring data:', error);
        }
    }

    stopMonitoring() {
        if (this.updateInterval) {
            clearInterval(this.updateInterval);
            this.updateInterval = null;
        }
        
        this.monitoredVMs.clear();
    }

    showAlerts() {
        const alertsContainer = document.getElementById('monitoring-alerts');
        if (!alertsContainer) return;

        // This would normally come from the backend
        const alerts = [
            { level: 'warning', message: 'Host "node-01" CPU usage at 85%', time: '2 minutes ago' },
            { level: 'info', message: 'VM "web-server-01" restarted', time: '5 minutes ago' },
            { level: 'critical', message: 'Storage pool "main" at 95% capacity', time: '10 minutes ago' }
        ];

        if (alerts.length === 0) {
            alertsContainer.innerHTML = `
                <div class="alert alert-success">
                    <span class="alert-icon">✅</span>
                    <div>
                        <strong>All systems operational</strong>
                        <p>No active alerts</p>
                    </div>
                </div>
            `;
            return;
        }

        alertsContainer.innerHTML = alerts.map(alert => `
            <div class="alert alert-${alert.level}">
                <span class="alert-icon">${this.getAlertIcon(alert.level)}</span>
                <div>
                    <strong>${alert.message}</strong>
                    <p>${alert.time}</p>
                </div>
                <button class="btn btn-sm btn-secondary">Acknowledge</button>
            </div>
        `).join('');
    }

    getAlertIcon(level) {
        switch(level) {
            case 'critical': return '🔥';
            case 'warning': return '⚠️';
            case 'info': return 'ℹ️';
            default: return '📌';
        }
    }
}

// Initialize
window.monitoringDashboard = new MonitoringDashboard();

// Update navigation.js to initialize monitoring
const originalSwitchView = window.switchView;
window.switchView = function(viewName) {
    originalSwitchView(viewName);
    
    if (viewName === 'monitoring') {
        setTimeout(() => {
            window.monitoringDashboard.init();
        }, 100);
    } else {
        window.monitoringDashboard.stopMonitoring();
    }
};