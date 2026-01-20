// Authentication System
const API_URL = window.API_URL || 'http://localhost:3000/api';
window.API_URL = API_URL

// User session management
class AuthService {
    constructor() {
        this.currentUser = null;
        this.token = null;
        this.init();
    }

    init() {
        // Check for existing session
        const savedToken = localStorage.getItem('thoth_token');
        const savedUser = localStorage.getItem('thoth_user');
        
        if (savedToken && savedUser) {
            this.token = savedToken;
            this.currentUser = JSON.parse(savedUser);
            this.setAuthHeader();
        }
    }

    async login(username, password, rememberMe = false) {
        try {
            const response = await fetch(`${API_URL}/auth/login`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ username, password })
            });

            const data = await response.json();
            
            if (data.success && data.token) {
                this.token = data.token;
                this.currentUser = data.user;
                
                // Store session
                const storage = rememberMe ? localStorage : sessionStorage;
                storage.setItem('thoth_token', data.token);
                storage.setItem('thoth_user', JSON.stringify(data.user));
            
                
                // Set auth header for future requests
                this.setAuthHeader();
                
                return { success: true, user: data.user };
            } else {
                return { success: false, error: data.error || 'Authentication failed' };
            }
        } catch (error) {
            console.error('Login error:', error);
            return { success: false, error: 'Server error' };
        }
    }

    async register(userData) {
        try {
            const response = await fetch(`${API_URL}/auth/register`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(userData)
            });

            const data = await response.json();
            
            if (data.success) {
                // Auto-login after registration
                return await this.login(userData.username, userData.password);
            } else {
                return { success: false, error: data.error || 'Registration failed' };
            }
        } catch (error) {
            console.error('Register error:', error);
            return { success: false, error: 'Server error' };
        }
    }

    logout() {
        this.token = null;
        this.currentUser = null;
        
        // Clear all storage
        localStorage.removeItem('thoth_token');
        localStorage.removeItem('thoth_user');
        sessionStorage.removeItem('thoth_token');
        sessionStorage.removeItem('thoth_user');
        
        // Remove auth header
        delete window.fetchAPI;
        
        // Redirect to login
        window.location.href = 'login.html';
    }

    isAuthenticated() {
        return !!this.token && !!this.currentUser;
    }

    hasRole(role) {
        return this.currentUser && this.currentUser.role === role;
    }

    isAdmin() {
        return this.hasRole('admin');
    }

    setAuthHeader() {
        const authService = this;
        
        window.fetchAPI = async function(endpoint, options = {}) {
            if (!authService.token || !authService.currentUser) {
                throw new Error('Not authenticated');
            }

            options.headers = {
                ...options.headers,
                'Authorization': `Bearer ${authService.token}`,
                'X-User-ID': authService.currentUser.username,
                'X-User-Role': authService.currentUser.role,
                'Content-Type': 'application/json'
            };
            
            try {
                const response = await fetch(`${API_URL}${endpoint}`, options);
                
                const contentType = response.headers.get('content-type');
                if (contentType && contentType.includes('application/json')) {
                    const data = await response.json();
                    
                    if (response.status === 403) {
                        showToast('⛔ Access denied', 'error');
                        throw new Error(data.error || 'Access denied');
                    }
                    
                    if (!data.success && response.status >= 400) {
                        throw new Error(data.error || 'An error occurred');
                    }
                    
                    return data;
                } else {
                    throw new Error('Non-JSON response from server');
                }
            } catch (error) {
                if (error.message.includes('Failed to fetch')) {
                    throw new Error('Backend server unavailable. Please check connection.');
                }
                throw error;
            }
        };
    }

    // To IMPL
    async changePassword(oldPassword, newPassword) {
        try {
            const response = await fetch(`${API_URL}/auth/change-password`, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                    'Authorization': `Bearer ${this.token}`
                },
                body: JSON.stringify({ oldPassword, newPassword })
            });

            const data = await response.json();
            return data;
        } catch (error) {
            console.error('Change password error:', error);
            return { success: false, error: 'Server error' };
        }
    }

    async updateProfile(profileData) {
        try {
            const response = await fetch(`${API_URL}/auth/profile`, {
                method: 'PUT',
                headers: {
                    'Content-Type': 'application/json',
                    'Authorization': `Bearer ${this.token}`
                },
                body: JSON.stringify(profileData)
            });

            const data = await response.json();
            
            if (data.success) {
                this.currentUser = { ...this.currentUser, ...data.user };
                
                // Update stored user data
                const storage = localStorage.getItem('thoth_token') ? localStorage : sessionStorage;
                storage.setItem('thoth_user', JSON.stringify(this.currentUser));
            }
            
            return data;
        } catch (error) {
            console.error('Update profile error:', error);
            return { success: false, error: 'Server error' };
        }
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

// Initialize auth service
const authService = new AuthService();

// Login Form Handler
async function handleLogin(event) {
    event.preventDefault();
    
    const username = document.getElementById('username').value;
    const password = document.getElementById('password').value;
    const rememberMe = document.getElementById('remember-me')?.checked || false;
    
    const loginBtn = event.target.querySelector('button[type="submit"]');
    const originalText = loginBtn.innerHTML;
    loginBtn.innerHTML = '<span>⏳</span> Connecting...';
    loginBtn.disabled = true;
    
    const result = await authService.login(username, password, rememberMe);
    
    if (result.success) {
        showToast('✅ Login successful! Redirecting...', 'success');
        setTimeout(() => {
            window.location.href = 'index.html';
        }, 1000);
    } else {
        showToast(`❌ ${result.error}`, 'error');
        loginBtn.innerHTML = originalText;
        loginBtn.disabled = false;
    }
}

// Register Form Handler
async function handleRegister(event) {
    event.preventDefault();
    
    const userData = {
        firstName: document.getElementById('first-name').value,
        lastName: document.getElementById('last-name').value,
        email: document.getElementById('email').value,
        username: document.getElementById('username').value,
        password: document.getElementById('password').value,
        role: document.getElementById('role').value,
        maxVMs: 5,
        maxCPU: 8,
        maxRAM: 16,
        maxStorage: 100
    };
    
    const confirmPassword = document.getElementById('confirm-password').value;
    
    if (userData.password !== confirmPassword) {
        showToast('❌ Passwords do not match', 'error');
        return;
    }
    
    if (!validatePasswordStrength(userData.password)) {
        showToast('❌ Password is too weak', 'error');
        return;
    }
    
    const registerBtn = event.target.querySelector('button[type="submit"]');
    const originalText = registerBtn.innerHTML;
    registerBtn.innerHTML = '<span>⏳</span> Creating account...';
    registerBtn.disabled = true;
    
    const result = await authService.register(userData);
    
    if (result.success) {
        showToast('✅ Account created successfully!', 'success');
        setTimeout(() => {
            window.location.href = 'index.html';
        }, 1000);
    } else {
        showToast(`❌ ${result.error}`, 'error');
        registerBtn.innerHTML = originalText;
        registerBtn.disabled = false;
    }
}

function validatePasswordStrength(password) {
    return password.length >= 8;
}

// Password Strength Validator
function validatePasswordStrength(password) {
    const hasMinLength = password.length >= 8;
    const hasUpperCase = /[A-Z]/.test(password);
    const hasLowerCase = /[a-z]/.test(password);
    const hasNumbers = /\d/.test(password);
    const hasSpecialChar = /[!@#$%^&*(),.?":{}|<>]/.test(password);
    
    return hasMinLength && hasUpperCase && hasLowerCase && hasNumbers && hasSpecialChar;
}

// Password Strength Indicator
function updatePasswordStrength(password) {
    const strengthBar = document.querySelector('.strength-bar');
    const strengthText = document.querySelector('.strength-text');
    const container = document.querySelector('.password-strength');
    
    if (!strengthBar || !strengthText) return;
    
    let score = 0;
    
    // Length check
    if (password.length >= 8) score++;
    if (password.length >= 12) score++;
    
    // Complexity checks
    if (/[A-Z]/.test(password)) score++;
    if (/[a-z]/.test(password)) score++;
    if (/\d/.test(password)) score++;
    if (/[!@#$%^&*(),.?":{}|<>]/.test(password)) score++;
    
    // Update display
    let width = 0;
    let text = '';
    let className = '';
    
    if (score <= 2) {
        width = 33;
        text = 'Faible';
        className = 'weak';
    } else if (score <= 4) {
        width = 66;
        text = 'Moyen';
        className = 'medium';
    } else {
        width = 100;
        text = 'Fort';
        className = 'strong';
    }
    
    strengthBar.style.width = `${width}%`;
    strengthText.textContent = text;
    container.className = `password-strength ${className}`;
}

// Toggle password visibility
function togglePasswordVisibility() {
    const passwordInput = document.getElementById('password');
    const toggleBtn = document.querySelector('.password-toggle');
    
    if (!passwordInput) return;
    
    if (passwordInput.type === 'password') {
        passwordInput.type = 'text';
        toggleBtn.textContent = '🙈';
    } else {
        passwordInput.type = 'password';
        toggleBtn.textContent = '👁️';
    }
}

// Forgot Password Modal, 
// TO DO Add password Reset
function showForgotPassword() {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    modal.id = 'forgot-password-modal';
    
    modal.innerHTML = `
        <div class="modal-content">
            <div class="modal-header">
                <h3>🔐 Réinitialiser le mot de passe</h3>
                <button class="btn-close" onclick="closeForgotPassword()">✖</button>
            </div>
            <div class="modal-body">
                <div class="form-group">
                    <label>Email associé à votre compte</label>
                    <input type="email" id="reset-email" placeholder="email@example.com">
                </div>
                <p class="text-muted" style="font-size: 13px;">
                    Vous recevrez un lien pour réinitialiser votre mot de passe.
                </p>
            </div>
            <div class="modal-footer">
                <button type="button" class="btn btn-secondary" onclick="closeForgotPassword()">Annuler</button>
                <button type="button" class="btn btn-primary" onclick="sendResetLink()">Envoyer le lien</button>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeForgotPassword() {
    const modal = document.getElementById('forgot-password-modal');
    if (modal) {
        modal.remove();
    }
}

// TO DO
async function sendResetLink() {
    const email = document.getElementById('reset-email').value;
    
    if (!email) {
        showToast('❌ Veuillez entrer votre email', 'error');
        return;
    }
    
    // Simulate API call
    showToast('📧 Envoi du lien de réinitialisation...', 'info');
    
    setTimeout(() => {
        closeForgotPassword();
        showToast('✅ Lien envoyé ! Vérifiez votre email.', 'success');
    }, 2000);
}

// Initialize auth forms
document.addEventListener('DOMContentLoaded', () => {
    // Add password strength listener
    const passwordInput = document.getElementById('password');
    if (passwordInput) {
        passwordInput.addEventListener('input', (e) => {
            updatePasswordStrength(e.target.value);
        });
    }
    
    // Check if user is already logged in
    if (authService.isAuthenticated() && window.location.pathname.includes('login.html')) {
        window.location.href = 'index.html';
    }
});

// Export auth service
window.authService = authService;
window.handleLogin = handleLogin;
window.handleRegister = handleRegister;
window.togglePasswordVisibility = togglePasswordVisibility;
window.showForgotPassword = showForgotPassword;

window.closeForgotPassword = closeForgotPassword;
window.sendResetLink = sendResetLink;