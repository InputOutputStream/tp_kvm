// Navigation State
let currentView = 'dashboard';
let sidebarCollapsed = false;

// Initialize Navigation
document.addEventListener('DOMContentLoaded', () => {
    initializeNavigation();
    initializeSidebar();
    loadInitialData();
});

// Initialize Navigation System
function initializeNavigation() {
    const navItems = document.querySelectorAll('.nav-item');
    
    navItems.forEach(item => {
        item.addEventListener('click', (e) => {
            e.preventDefault();
            const view = item.dataset.view;
            if (view) {
                switchView(view);
                updateActiveNavItem(item);
            }
        });
    });
}

// Switch Between Views
function switchView(viewName) {
    // Hide all views
    document.querySelectorAll('.view').forEach(view => {
        view.classList.remove('active');
    });
    
    // Show selected view
    const targetView = document.getElementById(`view-${viewName}`);
    if (targetView) {
        targetView.classList.add('active');
        currentView = viewName;
        
        // Load view-specific data
        loadViewData(viewName);
    }
}

// Update Active Navigation Item
function updateActiveNavItem(activeItem) {
    document.querySelectorAll('.nav-item').forEach(item => {
        item.classList.remove('active');
    });
    activeItem.classList.add('active');
}

// Load View-Specific Data
function loadViewData(viewName) {
    switch(viewName) {
        case 'dashboard':
            loadDashboardData();
            break;
        case 'vms':
            loadVMs();
            break;
        case 'paas':
            refreshApps();
            break;
        case 'users':
            loadUsersTable();
            break;
        case 'billing':
            updateBilling();
            break;
        case 'system':
            loadSystemInfo();
            break;
    }
}

// Initialize Sidebar Controls
function initializeSidebar() {
    const sidebarToggle = document.getElementById('sidebar-toggle');
    const sidebar = document.getElementById('sidebar');
    
    if (sidebarToggle) {
        sidebarToggle.addEventListener('click', () => {
            sidebar.classList.toggle('collapsed');
            sidebarCollapsed = !sidebarCollapsed;
        });
    }
    
    // Mobile sidebar toggle
    if (window.innerWidth <= 768) {
        sidebar.classList.add('collapsed');
    }
}

// Load Initial Dashboard Data
function loadInitialData() {
    loadDashboardData();
}

// Load Dashboard Data
async function loadDashboardData() {
    try {
        await loadVMs();
        // Load recent VMs in dashboard
        const dashboardVMsList = document.getElementById('dashboard-vms-list');
        if (dashboardVMsList) {
            const vmsList = document.getElementById('vms-list');
            if (vmsList && vmsList.children.length > 0) {
                // Clone first 4 VMs to dashboard
                dashboardVMsList.innerHTML = '';
                const vms = Array.from(vmsList.children).slice(0, 4);
                vms.forEach(vm => {
                    dashboardVMsList.appendChild(vm.cloneNode(true));
                });
            }
        }
    } catch (error) {
        console.error('Error loading dashboard data:', error);
    }
}

// Close Details Panel
function closeDetailsPanel() {
    const panel = document.getElementById('vm-details-panel');
    if (panel) {
        panel.classList.remove('open');
    }
    
    // Clear stats interval if exists
    if (window.statsInterval) {
        clearInterval(window.statsInterval);
        window.statsInterval = null;
    }
}

// Show Toast Notification
function showToast(message, type = 'info') {
    const toast = document.getElementById('toast');
    if (!toast) return;
    
    toast.textContent = message;
    toast.className = `toast ${type} show`;
    
    setTimeout(() => {
        toast.classList.remove('show');
    }, 3000);
}

// Handle Window Resize
window.addEventListener('resize', () => {
    const sidebar = document.getElementById('sidebar');
    if (window.innerWidth <= 1024 && !sidebar.classList.contains('collapsed')) {
        sidebar.classList.add('collapsed');
        sidebarCollapsed = true;
    }
});

// Mobile Menu Toggle
function toggleMobileMenu() {
    const sidebar = document.getElementById('sidebar');
    sidebar.classList.toggle('mobile-open');
}

// Utility: Format Bytes
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

// Utility: Format Date
function formatDate(dateString) {
    const date = new Date(dateString);
    return date.toLocaleDateString('fr-FR', {
        year: 'numeric',
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit'
    });
}

// Utility: Debounce Function
function debounce(func, wait) {
    let timeout;
    return function executedFunction(...args) {
        const later = () => {
            clearTimeout(timeout);
            func(...args);
        };
        clearTimeout(timeout);
        timeout = setTimeout(later, wait);
    };
}

