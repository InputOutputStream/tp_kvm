// Theme Management
class ThemeManager {
    constructor() {
        this.theme = localStorage.getItem('thoth_theme') || 'light';
        this.init();
    }

    init() {
        // Apply saved theme
        this.applyTheme(this.theme);
        
        // Listen for system theme changes
        const prefersDark = window.matchMedia('(prefers-color-scheme: dark)');
        prefersDark.addEventListener('change', (e) => {
            if (localStorage.getItem('thoth_theme') === 'system') {
                this.applyTheme(e.matches ? 'dark' : 'light');
            }
        });
    }

    applyTheme(theme) {
        const html = document.documentElement;
        
        if (theme === 'dark' || (theme === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches)) {
            html.classList.add('dark-theme');
            this.updateThemeIcon('dark');
        } else {
            html.classList.remove('dark-theme');
            this.updateThemeIcon('light');
        }
    }

    updateThemeIcon(theme) {
        const toggleBtn = document.getElementById('theme-toggle');
        if (!toggleBtn) return;
        
        if (theme === 'dark') {
            toggleBtn.innerHTML = '<span>☀️</span>';
            toggleBtn.title = 'Passer au mode clair';
        } else {
            toggleBtn.innerHTML = '<span>🌙</span>';
            toggleBtn.title = 'Passer au mode sombre';
        }
    }

    toggleTheme() {
        const themes = ['light', 'dark', 'system'];
        let currentIndex = themes.indexOf(this.theme);
        
        // Cycle through themes
        this.theme = themes[(currentIndex + 1) % themes.length];
        
        // Save preference
        localStorage.setItem('thoth_theme', this.theme);
        
        // Apply new theme
        this.applyTheme(this.theme);
        
        // Show notification
        const themeNames = {
            'light': 'clair',
            'dark': 'sombre',
            'system': 'système'
        };
        showToast(`🌓 Thème ${themeNames[this.theme]} activé`, 'info');
    }

    getCurrentTheme() {
        return this.theme;
    }

    // Add theme-specific styles dynamically
    addDynamicStyles() {
        const style = document.createElement('style');
        style.id = 'dynamic-theme-styles';
        
        // Add any dynamic style rules here
        style.textContent = `
            .dark-theme .logo-icon {
                filter: brightness(1.2);
            }
            
            .dark-theme .stat-card {
                background: linear-gradient(135deg, #2d2d2d 0%, #1a1a1a 100%);
            }
            
            .dark-theme .progress-step.active .step-indicator {
                background: linear-gradient(135deg, #00c853, #00b248);
                color: white;
            }
        `;
        
        document.head.appendChild(style);
    }
}

// Initialize theme manager
const themeManager = new ThemeManager();

// Toggle theme function
function toggleTheme() {
    themeManager.toggleTheme();
}

// Apply theme based on time of day
function applyTimeBasedTheme() {
    const hour = new Date().getHours();
    
    if (localStorage.getItem('thoth_theme') === 'auto') {
        if (hour >= 18 || hour < 6) {
            themeManager.applyTheme('dark');
        } else {
            themeManager.applyTheme('light');
        }
    }
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    // Apply saved theme
    themeManager.applyTheme(themeManager.getCurrentTheme());
    
    // Check for time-based theme
    applyTimeBasedTheme();
    
    // Update every hour
    setInterval(applyTimeBasedTheme, 3600000);
});

// Export theme functions
window.toggleTheme = toggleTheme;
window.themeManager = themeManager;