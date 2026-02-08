// ==========================================
// BILLING & MONITORING DASHBOARD MODULE
// ==========================================

const BillingDashboard = {
    billingData: null,
    refreshInterval: null,
    
    // Initialize billing dashboard
    async init() {
        await this.loadBillingData();
        this.startAutoRefresh();
    },
    
    // Load billing data
    async loadBillingData() {
        const dashboardContainer = document.getElementById('billing-dashboard');
        if (!dashboardContainer) return;
        
        dashboardContainer.innerHTML = '<p class="loading-text">Loading billing data...</p>';
        
        try {
            const response = await fetch('/api/billing/saas', {
                headers: {
                    'Authorization': `Bearer ${window.authService?.token}`
                }
            });
            
            const data = await response.json();
            
            if (data.success) {
                this.billingData = data;
                this.renderBillingDashboard(data);
            } else {
                dashboardContainer.innerHTML = '<p class="error-text">Failed to load billing data</p>';
            }
        } catch (error) {
            console.error('Billing data error:', error);
            dashboardContainer.innerHTML = `<p class="error-text">Error: ${error.message}</p>`;
        }
    },
    
    // Render billing dashboard
    renderBillingDashboard(data) {
        const dashboardContainer = document.getElementById('billing-dashboard');
        if (!dashboardContainer) return;
        
        const currentMonth = new Date().toLocaleDateString('en-US', { month: 'long', year: 'numeric' });
        
        dashboardContainer.innerHTML = `
            <div class="billing-header">
                <div class="billing-period">
                    <h2>📊 Billing Dashboard</h2>
                    <p class="billing-month">${currentMonth}</p>
                </div>
                <div class="billing-actions">
                    <button class="btn btn-secondary" onclick="BillingDashboard.exportBillingReport()">
                        📥 Export Report
                    </button>
                    <button class="btn btn-primary" onclick="BillingDashboard.loadBillingData()">
                        🔄 Refresh
                    </button>
                </div>
            </div>
            
            <div class="billing-summary">
                <div class="summary-card">
                    <div class="summary-icon">💰</div>
                    <div class="summary-content">
                        <h3>Total Revenue</h3>
                        <p class="summary-value">${data.totalRevenue?.toLocaleString() || 0} FCFA</p>
                        <small class="summary-change">+${data.revenueGrowth || 0}% from last month</small>
                    </div>
                </div>
                
                <div class="summary-card">
                    <div class="summary-icon">👥</div>
                    <div class="summary-content">
                        <h3>Active Users</h3>
                        <p class="summary-value">${data.activeUsers || 0}</p>
                        <small class="summary-change">Total accounts: ${data.totalUsers || 0}</small>
                    </div>
                </div>
                
                <div class="summary-card">
                    <div class="summary-icon">🖥️</div>
                    <div class="summary-content">
                        <h3>Running VMs</h3>
                        <p class="summary-value">${data.runningVMs || 0}</p>
                        <small class="summary-change">Total: ${data.totalVMs || 0} VMs</small>
                    </div>
                </div>
                
                <div class="summary-card">
                    <div class="summary-icon">📦</div>
                    <div class="summary-content">
                        <h3>PaaS Apps</h3>
                        <p class="summary-value">${data.paasApps || 0}</p>
                        <small class="summary-change">${data.paasRevenue || 0} FCFA revenue</small>
                    </div>
                </div>
            </div>
            
            <div class="billing-details">
                <div class="billing-section">
                    <h3>💳 Revenue Breakdown</h3>
                    <div class="revenue-breakdown">
                        ${this.renderRevenueBreakdown(data.revenueBreakdown)}
                    </div>
                </div>
                
                <div class="billing-section">
                    <h3>📈 Usage Trends</h3>
                    <div class="usage-trends">
                        <canvas id="billing-chart"></canvas>
                    </div>
                </div>
            </div>
            
            <div class="billing-section">
                <h3>👤 User Billing Details</h3>
                <div class="user-billing-table">
                    ${this.renderUserBillingTable(data.userBilling)}
                </div>
            </div>
        `;
        
        // Initialize chart
        this.initializeBillingChart(data.chartData);
    },
    
    // Render revenue breakdown
    renderRevenueBreakdown(breakdown) {
        if (!breakdown) {
            return '<p>No revenue data available</p>';
        }
        
        return `
            <div class="revenue-items">
                ${Object.entries(breakdown).map(([category, amount]) => `
                    <div class="revenue-item">
                        <div class="revenue-category">
                            <span class="category-icon">${this.getCategoryIcon(category)}</span>
                            <span class="category-name">${category}</span>
                        </div>
                        <div class="revenue-amount">${amount.toLocaleString()} FCFA</div>
                    </div>
                `).join('')}
            </div>
        `;
    },
    
    // Get category icon
    getCategoryIcon(category) {
        const icons = {
            'VM Services': '🖥️',
            'PaaS Applications': '📦',
            'Storage': '💾',
            'Network': '🌐',
            'Support': '🛟',
            'Other': '📊'
        };
        return icons[category] || '📊';
    },
    
    // Render user billing table
    renderUserBillingTable(userBilling) {
        if (!userBilling || userBilling.length === 0) {
            return '<p>No user billing data available</p>';
        }
        
        return `
            <table class="billing-table">
                <thead>
                    <tr>
                        <th>User</th>
                        <th>VMs</th>
                        <th>PaaS Apps</th>
                        <th>Total vCPUs</th>
                        <th>Total Memory</th>
                        <th>Monthly Cost</th>
                        <th>Status</th>
                    </tr>
                </thead>
                <tbody>
                    ${userBilling.map(user => `
                        <tr>
                            <td>
                                <div class="user-cell">
                                    <div class="user-avatar-small">${user.username.charAt(0).toUpperCase()}</div>
                                    <span>${user.username}</span>
                                </div>
                            </td>
                            <td>${user.vmCount || 0}</td>
                            <td>${user.paasCount || 0}</td>
                            <td>${user.totalVCPUs || 0}</td>
                            <td>${user.totalMemory || 0} GB</td>
                            <td><strong>${user.monthlyCost?.toLocaleString() || 0} FCFA</strong></td>
                            <td>
                                <span class="status-badge ${user.paymentStatus === 'paid' ? 'status-active' : 'status-warning'}">
                                    ${user.paymentStatus === 'paid' ? '✅ Paid' : '⚠️ Pending'}
                                </span>
                            </td>
                        </tr>
                    `).join('')}
                </tbody>
            </table>
        `;
    },
    
    // Initialize billing chart
    initializeBillingChart(chartData) {
        const canvas = document.getElementById('billing-chart');
        if (!canvas || !window.Chart) return;
        
        const ctx = canvas.getContext('2d');
        
        new Chart(ctx, {
            type: 'line',
            data: {
                labels: chartData?.labels || ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun'],
                datasets: [{
                    label: 'Revenue (FCFA)',
                    data: chartData?.revenue || [0, 0, 0, 0, 0, 0],
                    borderColor: '#00843d',
                    backgroundColor: 'rgba(0, 132, 61, 0.1)',
                    tension: 0.4
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    legend: {
                        display: true,
                        position: 'top'
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        ticks: {
                            callback: function(value) {
                                return value.toLocaleString() + ' FCFA';
                            }
                        }
                    }
                }
            }
        });
    },
    
    // Export billing report
    async exportBillingReport() {
        if (!this.billingData) {
            showToast('No billing data to export', 'warning');
            return;
        }
        
        showToast('Generating report...', 'info');
        
        try {
            // Create CSV content
            const csvContent = this.generateBillingCSV(this.billingData);
            
            // Create download link
            const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
            const link = document.createElement('a');
            const url = URL.createObjectURL(blob);
            
            const date = new Date().toISOString().split('T')[0];
            link.setAttribute('href', url);
            link.setAttribute('download', `billing_report_${date}.csv`);
            link.style.visibility = 'hidden';
            
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
            
            showToast('✅ Report exported successfully', 'success');
        } catch (error) {
            console.error('Export error:', error);
            showToast('❌ Failed to export report', 'error');
        }
    },
    
    // Generate billing CSV
    generateBillingCSV(data) {
        let csv = 'User,VMs,PaaS Apps,Total vCPUs,Total Memory (GB),Monthly Cost (FCFA),Payment Status\n';
        
        if (data.userBilling) {
            data.userBilling.forEach(user => {
                csv += `${user.username},${user.vmCount || 0},${user.paasCount || 0},${user.totalVCPUs || 0},${user.totalMemory || 0},${user.monthlyCost || 0},${user.paymentStatus || 'pending'}\n`;
            });
        }
        
        csv += '\n\nRevenue Breakdown\n';
        csv += 'Category,Amount (FCFA)\n';
        
        if (data.revenueBreakdown) {
            Object.entries(data.revenueBreakdown).forEach(([category, amount]) => {
                csv += `${category},${amount}\n`;
            });
        }
        
        csv += `\nTotal Revenue,${data.totalRevenue || 0}\n`;
        csv += `Active Users,${data.activeUsers || 0}\n`;
        csv += `Total VMs,${data.totalVMs || 0}\n`;
        
        return csv;
    },
    
    // Start auto-refresh
    startAutoRefresh(intervalMinutes = 5) {
        this.stopAutoRefresh();
        
        this.refreshInterval = setInterval(() => {
            this.loadBillingData();
        }, intervalMinutes * 60 * 1000);
    },
    
    // Stop auto-refresh
    stopAutoRefresh() {
        if (this.refreshInterval) {
            clearInterval(this.refreshInterval);
            this.refreshInterval = null;
        }
    },
    
    // Cleanup
    destroy() {
        this.stopAutoRefresh();
    }
};

// Add billing-specific CSS
const billingStyle = document.createElement('style');
billingStyle.textContent = `
    .billing-header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        margin-bottom: 30px;
        padding-bottom: 20px;
        border-bottom: 2px solid var(--border-color);
    }
    
    .billing-month {
        color: var(--text-secondary);
        margin-top: 5px;
    }
    
    .billing-actions {
        display: flex;
        gap: 10px;
    }
    
    .billing-summary {
        display: grid;
        grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
        gap: 20px;
        margin-bottom: 30px;
    }
    
    .summary-card {
        background: var(--card-bg);
        border-radius: 12px;
        padding: 20px;
        display: flex;
        gap: 15px;
        border: 1px solid var(--border-color);
        transition: transform 0.2s, box-shadow 0.2s;
    }
    
    .summary-card:hover {
        transform: translateY(-2px);
        box-shadow: 0 4px 12px rgba(0, 0, 0, 0.1);
    }
    
    .summary-icon {
        font-size: 2.5em;
        line-height: 1;
    }
    
    .summary-content h3 {
        margin: 0 0 8px 0;
        font-size: 0.9em;
        color: var(--text-secondary);
        font-weight: 500;
    }
    
    .summary-value {
        font-size: 1.8em;
        font-weight: 700;
        margin: 0 0 5px 0;
        color: var(--primary-color);
    }
    
    .summary-change {
        color: var(--text-secondary);
        font-size: 0.85em;
    }
    
    .billing-details {
        display: grid;
        grid-template-columns: 1fr 2fr;
        gap: 20px;
        margin-bottom: 30px;
    }
    
    .billing-section {
        background: var(--card-bg);
        border-radius: 12px;
        padding: 20px;
        border: 1px solid var(--border-color);
    }
    
    .billing-section h3 {
        margin-top: 0;
        margin-bottom: 20px;
    }
    
    .revenue-items {
        display: flex;
        flex-direction: column;
        gap: 12px;
    }
    
    .revenue-item {
        display: flex;
        justify-content: space-between;
        align-items: center;
        padding: 12px;
        background: var(--bg-color);
        border-radius: 8px;
    }
    
    .revenue-category {
        display: flex;
        align-items: center;
        gap: 10px;
    }
    
    .category-icon {
        font-size: 1.5em;
    }
    
    .revenue-amount {
        font-weight: 600;
        color: var(--primary-color);
    }
    
    .usage-trends {
        height: 300px;
    }
    
    .billing-table {
        width: 100%;
        border-collapse: collapse;
    }
    
    .billing-table thead {
        background: var(--bg-color);
    }
    
    .billing-table th,
    .billing-table td {
        padding: 12px;
        text-align: left;
        border-bottom: 1px solid var(--border-color);
    }
    
    .billing-table th {
        font-weight: 600;
        color: var(--text-secondary);
        font-size: 0.9em;
    }
    
    .billing-table tr:hover {
        background: var(--bg-color);
    }
    
    .user-cell {
        display: flex;
        align-items: center;
        gap: 10px;
    }
    
    .user-avatar-small {
        width: 32px;
        height: 32px;
        border-radius: 50%;
        background: var(--primary-color);
        color: white;
        display: flex;
        align-items: center;
        justify-content: center;
        font-weight: 600;
        font-size: 0.9em;
    }
    
    @media (max-width: 768px) {
        .billing-details {
            grid-template-columns: 1fr;
        }
        
        .billing-header {
            flex-direction: column;
            align-items: flex-start;
            gap: 15px;
        }
    }
`;
document.head.appendChild(billingStyle);

// Export to global scope
window.BillingDashboard = BillingDashboard;