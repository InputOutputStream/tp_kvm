// ==========================================
// USER MANAGEMENT MODULE - Table Based
// ==========================================

const UserManagement = {
    users: [],
    
    // Load all users (Admin only)
    async loadUsers() {
        const tableBody = document.getElementById('users-table-body');
        if (!tableBody) return;
        
        tableBody.innerHTML = '<tr><td colspan="6" class="loading-text">Loading users...</td></tr>';
        
        try {
            const data = await window.fetchAPI('/users');
            
            if (data.success && data.users) {
                this.users = data.users;
                this.renderUsersTable(data.users);
            } else {
                tableBody.innerHTML = '<tr><td colspan="6" class="error-text">Failed to load users</td></tr>';
            }
        } catch (error) {
            tableBody.innerHTML = `<tr><td colspan="6" class="error-text">Error: ${error.message}</td></tr>`;
            showToast(`Error loading users: ${error.message}`, 'error');
        }
    },
    
    // Render users in table format
    renderUsersTable(users) {
        const tableBody = document.getElementById('users-table-body');
        if (!tableBody) return;
        
        tableBody.innerHTML = '';
        
        if (users.length === 0) {
            tableBody.innerHTML = '<tr><td colspan="6" class="empty-state">No users found</td></tr>';
            return;
        }
        
        users.forEach(user => {
            const row = this.createUserRow(user);
            tableBody.appendChild(row);
        });
    },
    
    // Create table row for user
    createUserRow(user) {
        const tr = document.createElement('tr');
        tr.dataset.username = user.username;
        
        const usage = user.usage || { vms: 0, cpu: 0, ram: 0, storage: 0 };
        const quotas = user.quotas || { maxVMs: 10, maxVCPUs: 20, maxMemory: 40, maxStorage: 200 };
        const isActive = user.active !== false;
        
        // Format values
        const ramUsedGB = ((usage.ram || 0) / 1024).toFixed(1);
        const cpuPercent = quotas.maxVCPUs > 0 ? ((usage.cpu || 0) / quotas.maxVCPUs * 100).toFixed(0) : 0;
        const ramPercent = quotas.maxMemory > 0 ? ((usage.ram || 0) / 1024 / quotas.maxMemory * 100).toFixed(0) : 0;
        
        tr.innerHTML = `
            <td>
                <div class="user-cell">
                    <div class="user-avatar-small">${user.username.charAt(0).toUpperCase()}</div>
                    <div class="user-info-cell">
                        <strong>${user.username}</strong>
                        <small>${user.email || 'No email'}</small>
                    </div>
                </div>
            </td>
            <td>
                <span class="role-badge ${user.role === 'admin' ? 'role-admin' : 'role-user'}">
                    ${user.role === 'admin' ? '👑 Admin' : '👤 User'}
                </span>
                ${!isActive ? '<br><span class="status-badge status-inactive">🔴 Inactive</span>' : ''}
            </td>
            <td>
                <div class="usage-cell">
                    <strong>${usage.vms || 0}</strong> / ${quotas.maxVMs || 0}
                    <div class="mini-progress">
                        <div class="mini-progress-bar" style="width: ${Math.min((usage.vms || 0) / (quotas.maxVMs || 10) * 100, 100)}%"></div>
                    </div>
                </div>
            </td>
            <td>
                <div class="usage-cell">
                    <strong>${usage.cpu || 0}</strong> / ${quotas.maxVCPUs || 0}
                    <div class="mini-progress">
                        <div class="mini-progress-bar ${cpuPercent > 90 ? 'danger' : cpuPercent > 70 ? 'warning' : ''}" 
                             style="width: ${Math.min(cpuPercent, 100)}%"></div>
                    </div>
                </div>
            </td>
            <td>
                <div class="usage-cell">
                    <strong>${ramUsedGB}</strong> / ${quotas.maxMemory || 0} GB
                    <div class="mini-progress">
                        <div class="mini-progress-bar ${ramPercent > 90 ? 'danger' : ramPercent > 70 ? 'warning' : ''}" 
                             style="width: ${Math.min(ramPercent, 100)}%"></div>
                    </div>
                </div>
            </td>
            <td>
                <div class="table-actions">
                    <button class="btn btn-sm btn-secondary" onclick="UserManagement.showEditModal('${user.username}')" title="Edit User">
                        ✏️
                    </button>
                    <button class="btn btn-sm btn-info" onclick="UserManagement.showQuotasModal('${user.username}')" title="Manage Quotas">
                        ⚙️
                    </button>
                    <button class="btn btn-sm btn-primary" onclick="UserManagement.refreshUsage('${user.username}')" title="Refresh Usage">
                        🔄
                    </button>
                    ${user.role !== 'admin' ? `
                        <button class="btn btn-sm btn-danger" onclick="UserManagement.deleteUser('${user.username}')" title="Delete User">
                            🗑️
                        </button>
                    ` : ''}
                </div>
            </td>
        `;
        
        return tr;
    },
    
    // Show create user modal
    showCreateUserModal() {
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'create-user-modal';
        
        modal.innerHTML = `
            <div class="modal modal-large">
                <div class="modal-header">
                    <h3>➕ Create New User</h3>
                    <button class="modal-close" onclick="UserManagement.closeModal()">✖</button>
                </div>
                <form onsubmit="UserManagement.createUser(event)">
                    <div class="modal-body">
                        <div class="form-section">
                            <h4>Basic Information</h4>
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Username *</label>
                                    <input type="text" id="user-username" required 
                                           pattern="[a-z0-9_-]+" 
                                           title="Lowercase letters, numbers, underscore, and hyphen only"
                                           placeholder="john_doe">
                                    <small>Lowercase letters, numbers, underscore, and hyphen only</small>
                                </div>
                                <div class="form-group">
                                    <label>Password *</label>
                                    <input type="password" id="user-password" required 
                                           minlength="6" placeholder="Min 6 characters">
                                </div>
                            </div>
                            
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Email</label>
                                    <input type="email" id="user-email" placeholder="user@example.com">
                                </div>
                                <div class="form-group">
                                    <label>Role *</label>
                                    <select id="user-role" required>
                                        <option value="user">User</option>
                                        <option value="admin">Administrator</option>
                                    </select>
                                </div>
                            </div>
                            
                            <div class="form-row">
                                <div class="form-group">
                                    <label>First Name</label>
                                    <input type="text" id="user-firstname" placeholder="John">
                                </div>
                                <div class="form-group">
                                    <label>Last Name</label>
                                    <input type="text" id="user-lastname" placeholder="Doe">
                                </div>
                            </div>
                        </div>
                        
                        <div class="form-section">
                            <h4>Resource Quotas</h4>
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Max VMs</label>
                                    <input type="number" id="user-max-vms" value="10" min="1" max="100">
                                </div>
                                <div class="form-group">
                                    <label>Max vCPUs</label>
                                    <input type="number" id="user-max-vcpus" value="20" min="1" max="200">
                                </div>
                            </div>
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Max Memory (GB)</label>
                                    <input type="number" id="user-max-memory" value="40" min="1" max="500">
                                </div>
                                <div class="form-group">
                                    <label>Max Storage (GB)</label>
                                    <input type="number" id="user-max-storage" value="200" min="10" max="2000">
                                </div>
                            </div>
                        </div>
                        
                        <div class="info-banner">
                            <span class="info-icon">ℹ️</span>
                            <p>A default network will be automatically created for this user.</p>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" onclick="UserManagement.closeModal()">Cancel</button>
                        <button type="submit" class="btn btn-primary">Create User</button>
                    </div>
                </form>
            </div>
        `;
        
        document.body.appendChild(modal);
    },
    
    // Create user
    async createUser(event) {
        event.preventDefault();
        
        const userData = {
            username: document.getElementById('user-username').value.trim(),
            password: document.getElementById('user-password').value,
            email: document.getElementById('user-email').value.trim(),
            role: document.getElementById('user-role').value,
            firstName: document.getElementById('user-firstname').value.trim(),
            lastName: document.getElementById('user-lastname').value.trim(),
            quotas: {
                maxVMs: parseInt(document.getElementById('user-max-vms').value),
                maxVCPUs: parseInt(document.getElementById('user-max-vcpus').value),
                maxMemory: parseInt(document.getElementById('user-max-memory').value),
                maxStorage: parseInt(document.getElementById('user-max-storage').value)
            }
        };
        
        this.closeModal();
        showToast('Creating user...', 'info');
        
        try {
            const result = await window.fetchAPI('/users', {
                method: 'POST',
                body: JSON.stringify(userData)
            });
            
            if (result.success) {
                showToast('✅ User created successfully', 'success');
                if (result.user && result.user.defaultNetwork) {
                    showToast(`🌐 Default network created`, 'info');
                }
                await this.loadUsers();
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    // Show edit modal
    showEditModal(username) {
        const user = this.users.find(u => u.username === username);
        if (!user) return;
        
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'edit-user-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>✏️ Edit User: ${username}</h3>
                    <button class="modal-close" onclick="UserManagement.closeModal()">✖</button>
                </div>
                <form onsubmit="UserManagement.updateUser(event, '${username}')">
                    <div class="modal-body">
                        <div class="form-group">
                            <label>Email</label>
                            <input type="email" id="edit-user-email" value="${user.email || ''}">
                        </div>
                        
                        <div class="form-row">
                            <div class="form-group">
                                <label>First Name</label>
                                <input type="text" id="edit-user-firstname" value="${user.firstName || ''}">
                            </div>
                            <div class="form-group">
                                <label>Last Name</label>
                                <input type="text" id="edit-user-lastname" value="${user.lastName || ''}">
                            </div>
                        </div>
                        
                        <div class="form-row">
                            <div class="form-group">
                                <label>Role</label>
                                <select id="edit-user-role">
                                    <option value="user" ${user.role === 'user' ? 'selected' : ''}>User</option>
                                    <option value="admin" ${user.role === 'admin' ? 'selected' : ''}>Administrator</option>
                                </select>
                            </div>
                            <div class="form-group">
                                <label>Status</label>
                                <select id="edit-user-status">
                                    <option value="true" ${user.active !== false ? 'selected' : ''}>Active</option>
                                    <option value="false" ${user.active === false ? 'selected' : ''}>Inactive</option>
                                </select>
                            </div>
                        </div>
                        
                        <div class="form-group">
                            <label>New Password (leave empty to keep current)</label>
                            <input type="password" id="edit-user-password" minlength="6" 
                                   placeholder="Enter new password or leave empty">
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" onclick="UserManagement.closeModal()">Cancel</button>
                        <button type="submit" class="btn btn-primary">Update User</button>
                    </div>
                </form>
            </div>
        `;
        
        document.body.appendChild(modal);
    },
    
    // Update user
    async updateUser(event, username) {
        event.preventDefault();
        
        const updates = {
            email: document.getElementById('edit-user-email').value.trim(),
            firstName: document.getElementById('edit-user-firstname').value.trim(),
            lastName: document.getElementById('edit-user-lastname').value.trim(),
            role: document.getElementById('edit-user-role').value,
            active: document.getElementById('edit-user-status').value === 'true'
        };
        
        const newPassword = document.getElementById('edit-user-password').value;
        if (newPassword) {
            updates.password = newPassword;
        }
        
        this.closeModal();
        showToast('Updating user...', 'info');
        
        try {
            const result = await window.fetchAPI(`/users/${username}`, {
                method: 'PUT',
                body: JSON.stringify(updates)
            });
            
            if (result.success) {
                showToast('✅ User updated successfully', 'success');
                await this.loadUsers();
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    // Show quotas modal
    showQuotasModal(username) {
        const user = this.users.find(u => u.username === username);
        if (!user) return;
        
        const usage = user.usage || { vms: 0, cpu: 0, ram: 0, storage: 0 };
        const quotas = user.quotas || { maxVMs: 10, maxVCPUs: 20, maxMemory: 40, maxStorage: 200 };
        
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'quotas-modal';
        
        modal.innerHTML = `
            <div class="modal">
                <div class="modal-header">
                    <h3>⚙️ Manage Quotas: ${username}</h3>
                    <button class="modal-close" onclick="UserManagement.closeModal()">✖</button>
                </div>
                <form onsubmit="UserManagement.updateQuotas(event, '${username}')">
                    <div class="modal-body">
                        <div class="quota-display">
                            <h4>Current Usage</h4>
                            <div class="quota-grid">
                                <div class="quota-box">
                                    <span class="quota-label">💻 VMs</span>
                                    <strong>${usage.vms || 0}</strong> / ${quotas.maxVMs || 0}
                                </div>
                                <div class="quota-box">
                                    <span class="quota-label">🔹 vCPUs</span>
                                    <strong>${usage.cpu || 0}</strong> / ${quotas.maxVCPUs || 0}
                                </div>
                                <div class="quota-box">
                                    <span class="quota-label">💾 RAM</span>
                                    <strong>${((usage.ram || 0) / 1024).toFixed(1)}</strong> / ${quotas.maxMemory || 0} GB
                                </div>
                                <div class="quota-box">
                                    <span class="quota-label">💿 Storage</span>
                                    <strong>${((usage.storage || 0) / 1024 / 1024 / 1024).toFixed(1)}</strong> / ${quotas.maxStorage || 0} GB
                                </div>
                            </div>
                        </div>
                        
                        <div class="form-section">
                            <h4>Update Quotas</h4>
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Max VMs</label>
                                    <input type="number" id="quota-max-vms" 
                                           value="${quotas.maxVMs || 10}" min="${usage.vms || 0}">
                                </div>
                                <div class="form-group">
                                    <label>Max vCPUs</label>
                                    <input type="number" id="quota-max-vcpus" 
                                           value="${quotas.maxVCPUs || 20}" min="${usage.cpu || 0}">
                                </div>
                            </div>
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Max Memory (GB)</label>
                                    <input type="number" id="quota-max-memory" 
                                           value="${quotas.maxMemory || 40}" 
                                           min="${Math.ceil((usage.ram || 0) / 1024)}">
                                </div>
                                <div class="form-group">
                                    <label>Max Storage (GB)</label>
                                    <input type="number" id="quota-max-storage" 
                                           value="${quotas.maxStorage || 200}" 
                                           min="${Math.ceil((usage.storage || 0) / 1024 / 1024 / 1024)}">
                                </div>
                            </div>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" onclick="UserManagement.closeModal()">Cancel</button>
                        <button type="submit" class="btn btn-primary">Update Quotas</button>
                    </div>
                </form>
            </div>
        `;
        
        document.body.appendChild(modal);
    },
    
    // Update quotas
    async updateQuotas(event, username) {
        event.preventDefault();
        
        const quotas = {
            maxVMs: parseInt(document.getElementById('quota-max-vms').value),
            maxVCPUs: parseInt(document.getElementById('quota-max-vcpus').value),
            maxMemory: parseInt(document.getElementById('quota-max-memory').value),
            maxStorage: parseInt(document.getElementById('quota-max-storage').value)
        };
        
        this.closeModal();
        showToast('Updating quotas...', 'info');
        
        try {
            const result = await window.fetchAPI(`/users/${username}`, {
                method: 'PUT',
                body: JSON.stringify({ quotas })
            });
            
            if (result.success) {
                showToast('✅ Quotas updated successfully', 'success');
                await this.loadUsers();
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    // Refresh user usage
    async refreshUsage(username) {
        try {
            showToast('Refreshing usage data...', 'info');
            
            const result = await window.fetchAPI(`/users/${username}/usage`);
            
            if (result.success) {
                showToast('✅ Usage refreshed', 'success');
                await this.loadUsers();
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },

    // Filter users by role (admin / user / all)
    filterByRole(role) {
        if (!this.users || this.users.length === 0) return;

        if (role === 'all' || !role) {
            this.renderUsersTable(this.users);
            return;
        }

        const filtered = this.users.filter(u => u.role === role);
        this.renderUsersTable(filtered);
    },

    // Filter users by status (active / inactive / all)
    filterByStatus(status) {
        if (!this.users || this.users.length === 0) return;

        if (status === 'all' || !status) {
            this.renderUsersTable(this.users);
            return;
        }

        const isActive = status === 'active';

        const filtered = this.users.filter(u => {
            const active = u.active !== false; // par défaut: actif
            return active === isActive;
        });

        this.renderUsersTable(filtered);
    },

    
    // Delete user
    async deleteUser(username) {
        if (!confirm(`Delete user "${username}"?\n\n⚠️ This will also delete all VMs owned by this user.`)) {
            return;
        }
        
        const confirmText = prompt(`Type "${username}" to confirm deletion:`);
        if (confirmText !== username) {
            showToast('Deletion cancelled', 'info');
            return;
        }
        
        try {
            showToast('Deleting user...', 'info');
            
            const result = await window.fetchAPI(`/users/${username}`, {
                method: 'DELETE'
            });
            
            if (result.success) {
                showToast('✅ User deleted successfully', 'success');
                await this.loadUsers();
            } else {
                showToast(`❌ ${result.error}`, 'error');
            }
        } catch (error) {
            showToast(`❌ ${error.message}`, 'error');
        }
    },
    
    // Close modal
    closeModal() {
        const modals = document.querySelectorAll('.modal-overlay');
        modals.forEach(modal => modal.remove());
    }
};

// Initialize when view becomes active
document.addEventListener('DOMContentLoaded', () => {
    // Load users when the users view is shown
    const usersNavItem = document.querySelector('[data-view="users"]');
    if (usersNavItem) {
        usersNavItem.addEventListener('click', () => {
            setTimeout(() => {
                if (window.UserManagement && typeof window.UserManagement.loadUsers === 'function') {
                    window.UserManagement.loadUsers();
                }
            }, 100);
        });
    }
});

// Export to global scope
window.UserManagement = UserManagement;

// Compatibility with existing code
window.showAddUserModal = () => UserManagement.showCreateUserModal();
window.loadUsersTable = () => UserManagement.loadUsers();