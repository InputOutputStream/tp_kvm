// User Profile Management
function showProfileModal() {
    const user = authService.currentUser;
    if (!user) return;
    
    const modal = document.createElement('div');
    modal.className = 'modal-overlay';
    modal.id = 'profile-modal';
    
    modal.innerHTML = `
        <div class="modal-content modal-medium">
            <div class="modal-header">
                <h3>👤 Mon profil</h3>
                <button class="btn-close" onclick="closeProfileModal()">✖</button>
            </div>
            <div class="modal-body">
                <div class="profile-header">
                    <div class="profile-avatar">
                        <span>${user.firstName?.charAt(0) || user.username?.charAt(0) || 'U'}</span>
                    </div>
                    <div class="profile-info">
                        <h4>${user.firstName || ''} ${user.lastName || ''}</h4>
                        <p>@${user.username}</p>
                        <span class="role-badge ${user.role}">${user.role}</span>
                    </div>
                </div>
                
                <div class="tabs">
                    <button class="tab active" onclick="switchProfileTab('info')">Informations</button>
                    <button class="tab" onclick="switchProfileTab('security')">Sécurité</button>
                    <button class="tab" onclick="switchProfileTab('preferences')">Préférences</button>
                </div>
                
                <div class="tab-content active" id="tab-info">
                    <form onsubmit="updateProfile(event)" id="profile-form">
                        <div class="form-row">
                            <div class="form-group">
                                <label>Prénom</label>
                                <input type="text" id="profile-firstName" value="${user.firstName || ''}">
                            </div>
                            <div class="form-group">
                                <label>Nom</label>
                                <input type="text" id="profile-lastName" value="${user.lastName || ''}">
                            </div>
                        </div>
                        
                        <div class="form-group">
                            <label>Email</label>
                            <input type="email" id="profile-email" value="${user.email || ''}">
                        </div>
                        
                        <div class="form-group">
                            <label>Nom d'utilisateur</label>
                            <input type="text" id="profile-username" value="${user.username}" disabled>
                            <small>Le nom d'utilisateur ne peut pas être modifié</small>
                        </div>
                        
                        <div class="form-group">
                            <label>Rôle</label>
                            <input type="text" value="${user.role}" disabled>
                        </div>
                        
                        <div class="form-actions">
                            <button type="submit" class="btn btn-primary">Enregistrer</button>
                        </div>
                    </form>
                </div>
                
                <div class="tab-content" id="tab-security">
                    <form onsubmit="changePassword(event)" id="password-form">
                        <div class="form-group">
                            <label>Mot de passe actuel</label>
                            <input type="password" id="current-password" required>
                        </div>
                        
                        <div class="form-group">
                            <label>Nouveau mot de passe</label>
                            <input type="password" id="new-password" required>
                            <div class="password-strength">
                                <div class="strength-bar"></div>
                                <span class="strength-text">Faible</span>
                            </div>
                        </div>
                        
                        <div class="form-group">
                            <label>Confirmer le nouveau mot de passe</label>
                            <input type="password" id="confirm-new-password" required>
                        </div>
                        
                        <div class="form-actions">
                            <button type="submit" class="btn btn-primary">Changer le mot de passe</button>
                        </div>
                    </form>
                </div>
                
                <div class="tab-content" id="tab-preferences">
                    <div class="form-group">
                        <label class="checkbox-label">
                            <input type="checkbox" id="notifications" ${user.notifications ? 'checked' : ''}>
                            <span>Recevoir les notifications par email</span>
                        </label>
                    </div>
                    
                    <div class="form-group">
                        <label>Thème</label>
                        <select id="theme-preference" onchange="updateThemePreference(this.value)">
                            <option value="light" ${themeManager.getCurrentTheme() === 'light' ? 'selected' : ''}>Clair</option>
                            <option value="dark" ${themeManager.getCurrentTheme() === 'dark' ? 'selected' : ''}>Sombre</option>
                            <option value="system" ${themeManager.getCurrentTheme() === 'system' ? 'selected' : ''}>Système</option>
                            <option value="auto" ${themeManager.getCurrentTheme() === 'auto' ? 'selected' : ''}>Automatique (jour/nuit)</option>
                        </select>
                    </div>
                    
                    <div class="form-group">
                        <label>Langue</label>
                        <select id="language">
                            <option value="fr" selected>Français</option>
                            <option value="en">English</option>
                        </select>
                    </div>
                </div>
            </div>
        </div>
    `;
    
    document.body.appendChild(modal);
    
    // Initialize password strength
    const newPasswordInput = document.getElementById('new-password');
    if (newPasswordInput) {
        newPasswordInput.addEventListener('input', (e) => {
            updatePasswordStrength(e.target.value);
        });
    }
}

function closeProfileModal() {
    const modal = document.getElementById('profile-modal');
    if (modal) {
        modal.remove();
    }
}

function switchProfileTab(tabName) {
    // Update tabs
    document.querySelectorAll('.tab').forEach(tab => {
        tab.classList.remove('active');
    });
    event.target.classList.add('active');
    
    // Update content
    document.querySelectorAll('.tab-content').forEach(content => {
        content.classList.remove('active');
    });
    document.getElementById(`tab-${tabName}`).classList.add('active');
}

async function updateProfile(event) {
    event.preventDefault();
    
    const profileData = {
        firstName: document.getElementById('profile-firstName').value,
        lastName: document.getElementById('profile-lastName').value,
        email: document.getElementById('profile-email').value
    };
    
    const result = await authService.updateProfile(profileData);
    
    if (result.success) {
        showToast('✅ Profil mis à jour avec succès', 'success');
        closeProfileModal();
    } else {
        showToast(`❌ ${result.error}`, 'error');
    }
}

async function changePassword(event) {
    event.preventDefault();
    
    const currentPassword = document.getElementById('current-password').value;
    const newPassword = document.getElementById('new-password').value;
    const confirmPassword = document.getElementById('confirm-new-password').value;
    
    if (newPassword !== confirmPassword) {
        showToast('❌ Les mots de passe ne correspondent pas', 'error');
        return;
    }
    
    if (!validatePasswordStrength(newPassword)) {
        showToast('❌ Le mot de passe est trop faible', 'error');
        return;
    }
    
    const result = await authService.changePassword(currentPassword, newPassword);
    
    if (result.success) {
        showToast('✅ Mot de passe changé avec succès', 'success');
        document.getElementById('password-form').reset();
    } else {
        showToast(`❌ ${result.error}`, 'error');
    }
}

function updateThemePreference(value) {
    localStorage.setItem('thoth_theme', value);
    themeManager.applyTheme(value);
    showToast('✅ Préférence de thème enregistrée', 'success');
}

// Export profile functions
window.showProfileModal = showProfileModal;
window.closeProfileModal = closeProfileModal;
window.switchProfileTab = switchProfileTab;
window.updateProfile = updateProfile;
window.changePassword = changePassword;
window.updateThemePreference = updateThemePreference;