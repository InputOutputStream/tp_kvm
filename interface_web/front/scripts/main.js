// Global state and utility functions that need to be accessible everywhere
window.GLOBAL_STATE = {
    currentUser: null,
    pricing: null,
    systemInfo: null
};

// Global utility functions (keep minimal)
window.formatBytes = function(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
};

window.formatDate = function(dateString) {
    const date = new Date(dateString);
    return date.toLocaleDateString('fr-FR', {
        year: 'numeric',
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit'
    });
};

window.debounce = function(func, wait) {
    let timeout;
    return function(...args) {
        clearTimeout(timeout);
        timeout = setTimeout(() => func(...args), wait);
    };
};

// Global toast function used by all modules
window.showToast = function(message, type = 'info') {
    const toast = document.getElementById('toast');
    if (!toast) {
        // Create toast if it doesn't exist
        const toastEl = document.createElement('div');
        toastEl.id = 'toast';
        toastEl.className = 'toast';
        document.body.appendChild(toastEl);
    }
    
    const toastElement = document.getElementById('toast');
    toastElement.textContent = message;
    toastElement.className = `toast ${type} show`;
    
    setTimeout(() => {
        toastElement.classList.remove('show');
    }, 3000);
};

// Wait for DOM and initialize everything
document.addEventListener('DOMContentLoaded', async () => {
    console.log('🚀 THoTH Cloud Platform - Initializing...');
    
    // Wait for auth to be ready
    await waitForAuthService();
    
    // Initialize the app
    if (window.appInitializer) {
        await window.appInitializer.init();
    } else {
        console.error('AppInitializer not found!');
        // Fallback: Initialize essential modules directly
        await initializeFallback();
    }
    
    // Update user display
    updateUserDisplay();
});

async function waitForAuthService() {
    return new Promise((resolve) => {
        const checkAuth = () => {
            if (window.authService) {
                // Check if user is already logged in
                if (window.authService.currentUser) {
                    resolve();
                } else {
                    // Wait for user to be loaded
                    setTimeout(checkAuth, 100);
                }
            } else {
                setTimeout(checkAuth, 100);
            }
        };
        checkAuth();
    });
}

async function initializeFallback() {
    console.warn('Using fallback initialization');
    
    // Initialize essential modules directly
    if (window.themeManager) {
        window.themeManager.init();
    }
    
    if (window.vmController) {
        await window.vmController.loadVMs();
    }
    
    if (window.paasController) {
        window.paasController.setupEventListeners();
    }
    
    // Initialize navigation
    if (window.initializeNavigation) {
        window.initializeNavigation();
    }
}

function updateUserDisplay() {
    if (window.authService?.currentUser) {
        const user = window.authService.currentUser;
        
        const nameElement = document.getElementById('current-user-name');
        const roleElement = document.getElementById('current-user-role');
        
        if (nameElement) {
            const displayName = user.firstName && user.lastName 
                ? `${user.firstName} ${user.lastName}`
                : user.username;
            nameElement.textContent = displayName;
        }
        
        if (roleElement) {
            roleElement.textContent = user.role === 'admin' ? 'Administrator' : 'User';
        }
    }
}

// Export for testing/console access
console.log('THoTH Cloud Platform - Main module loaded');