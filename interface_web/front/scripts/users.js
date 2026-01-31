// ==========================================
// USER MANAGEMENT - Complete CRUD Operations
// ==========================================

async function loadUsersTable() {
    const usersTableBody = document.getElementById('users-table-body');
    const quotasSummary = document.getElementById('quotas-summary-content');
    
    if (!usersTableBody) return;
    
    usersTableBody.innerHTML = '<tr><td colspan="6" class="loading-text">Loading users...</td></tr>';
    
    try {
        const data = await window.fetchAPI('/users');
        
        if (!data.success || !data.users) {
            usersTableBody.innerHTML = '<tr><td colspan="6" class="error-text">Failed to load users</td></tr>';
            return;
        }
        
        renderUsersTable(data.users);
        
        // Load usage stats for admin
        if (authService.isAdmin()) {
            await loadAllUsersUsage();
        }
        
    } catch (error) {
        usersTableBody.innerHTML = `<tr><td colspan="6" class="error-text">Error: ${error.message}</td></tr>`;
        showToast(`Error loading users: ${error.message}`, 'error');
    }
}

function renderUsersTable(users) {
    const tbody = document.getElementById('users-table-body');
    if (!tbody) return;
    
    tbody.innerHTML = '';
    
    if (users.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" class="empty-state">No users found</td></tr>';
        return;
    }
    
    users.forEach(user => {
        const row = document.createElement('tr');
        row.innerHTML = `
            <td>${user.username}</td>
            <td>${user.firstName || ''} ${user.lastName || ''}</td>
            <td>${user.email || 'N/A'}</td>
            <td><span class="role-badge role-${user.role}">${user.role}</span></td>
            <td>${user.quotas?.vms || 0} / ${user.quotas?.maxVMs || 10}</td>
            <td>
                <button class="btn btn-sm btn-secondary" onclick="showEditUserModal('${user.username}')">
                    ✏️ Edit
                </button>
                <button class="btn btn-sm btn-primary" onclick="showUserQuotasModal('${user.username}')">
                    📊 Quotas
                </button>
                <button class="btn btn-sm btn-danger" onclick="deleteUser('${user.username}')">
                    🗑️
                </button>
            </td>
        `;
        tbody.appendChild(row);
    });
}

async function loadAllUsersUsage() {
    try {
        const data = await window.fetchAPI('/users/usage');
        
        if (data.success && data.usage) {
            displayUsageSummary(data.usage);
        }
    } catch (error) {
        console.error('Error loading usage:', error);
    }
}

function displayUsageSummary(usage) {
    const summaryDiv = document.getElementById('quotas-summary-content');
    if (!summaryDiv) return;
    
    const totalVMs = usage.reduce((sum, u) => sum + (u.vms || 0), 0);
    const totalCPU = usage.reduce((sum, u) => sum + (u.cpu || 0), 0);
    const totalRAM = usage.reduce((sum, u) => sum + (u.memory || 0), 0);
    
    summaryDiv.innerHTML = `
        <div class="quota-items">
            <div class="quota-item">
                <span>Total Users:</span>
                <strong>${usage.length}</strong>
            </div>
            <div class="quota-item">
                <span>Total VMs:</span>
                <strong>${totalVMs}</strong>
            </div>
            <div class="quota-item">
                <span>Total vCPUs:</span>
                <strong>${totalCPU}</strong>
            </div>
            <div class="quota-item">
                <span>Total RAM:</span>
                <strong>${totalRAM} GB</strong>
            </div>
        </div>
    `;
}

function showCreateUserModal() {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'create-user-modal';
    
    modal.innerHTML = `
        <div class="modal modal-large">
            <div class="modal-header">
                <h3>➕ Create New User</h3>
                <button class="modal-close" onclick="closeCreateUserModal()">✖</button>
            </div>
            <form onsubmit="createUserSubmit(event)">
                <div class="modal-body">
                    <div class="user-form-section">
                        <h4>Basic Information</h4>
                        <div class="form-row">
                            <div class="form-group">
                                <label>First Name *</label>
                                <input type="text" id="user-firstname" required>
                            </div>
                            <div class="form-group">
                                <label>Last Name *</label>
                                <input type="text" id="user-lastname" required>
                            </div>
                        </div>
                        <div class="form-row">
                            <div class="form-group">
                                <label>Username *</label>
                                <input type="text" id="user-username" required 
                                       pattern="[a-z0-9_]+" title="Lowercase, numbers, underscores only">
                            </div>
                            <div class="form-group">
                                <label>Email *</label>
                                <input type="email" id="user-email" required>
                            </div>
                        </div>
                        <div class="form-row">
                            <div class="form-group">
                                <label>Password *</label>
                                <input type="password" id="user-password" required minlength="6">
                            </div>
                            <div class="form-group">
                                <label>Role *</label>
                                <select id="user-role" required>
                                    <option value="user">User</option>
                                    <option value="admin">Administrator</option>
                                </select>
                            </div>
                        </div>
                    </div>
                    
                    <div class="user-form-section">
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
                                <label>Max Disk (GB)</label>
                                <input type="number" id="user-max-disk" value="200" min="10" max="2000">
                            </div>
                        </div>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeCreateUserModal()">Cancel</button>
                    <button type="submit" class="btn btn-primary">Create User</button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeCreateUserModal() {
    const modal = document.getElementById('create-user-modal');
    if (modal) modal.remove();
}

async function createUserSubmit(event) {
    event.preventDefault();
    
    const userData = {
        username: document.getElementById('user-username').value,
        password: document.getElementById('user-password').value,
        firstName: document.getElementById('user-firstname').value,
        lastName: document.getElementById('user-lastname').value,
        email: document.getElementById('user-email').value,
        role: document.getElementById('user-role').value,
        quotas: {
            maxVMs: parseInt(document.getElementById('user-max-vms').value),
            maxVCPUs: parseInt(document.getElementById('user-max-vcpus').value),
            maxMemory: parseInt(document.getElementById('user-max-memory').value),
            maxDisk: parseInt(document.getElementById('user-max-disk').value)
        }
    };
    
    closeCreateUserModal();
    showToast('Creating user...', 'info');
    
    try {
        const result = await window.fetchAPI('/users', {
            method: 'POST',
            body: JSON.stringify(userData)
        });
        
        if (result.success) {
            showToast('✅ User created successfully', 'success');
            await loadUsersTable();
        } else {
            showToast(`❌ ${result.error || 'Failed to create user'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function showEditUserModal(username) {
    try {
        const data = await window.fetchAPI(`/users/${username}`);
        
        if (!data.success) {
            showToast('Failed to load user data', 'error');
            return;
        }
        
        const user = data.user;
        
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'edit-user-modal';
        
        modal.innerHTML = `
            <div class="modal modal-large">
                <div class="modal-header">
                    <h3>✏️ Edit User: ${username}</h3>
                    <button class="modal-close" onclick="closeEditUserModal()">✖</button>
                </div>
                <form onsubmit="updateUserSubmit(event, '${username}')">
                    <div class="modal-body">
                        <div class="form-row">
                            <div class="form-group">
                                <label>First Name</label>
                                <input type="text" id="edit-firstname" value="${user.firstName || ''}">
                            </div>
                            <div class="form-group">
                                <label>Last Name</label>
                                <input type="text" id="edit-lastname" value="${user.lastName || ''}">
                            </div>
                        </div>
                        <div class="form-group">
                            <label>Email</label>
                            <input type="email" id="edit-email" value="${user.email || ''}">
                        </div>
                        <div class="form-group">
                            <label>Role</label>
                            <select id="edit-role">
                                <option value="user" ${user.role === 'user' ? 'selected' : ''}>User</option>
                                <option value="admin" ${user.role === 'admin' ? 'selected' : ''}>Administrator</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label>New Password (leave empty to keep current)</label>
                            <input type="password" id="edit-password" minlength="6">
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" onclick="closeEditUserModal()">Cancel</button>
                        <button type="submit" class="btn btn-primary">Update User</button>
                    </div>
                </form>
            </div>
        `;
        
        document.body.appendChild(modal);
        
    } catch (error) {
        showToast(`Error: ${error.message}`, 'error');
    }
}

function closeEditUserModal() {
    const modal = document.getElementById('edit-user-modal');
    if (modal) modal.remove();
}

async function updateUserSubmit(event, username) {
    event.preventDefault();
    
    const updates = {
        firstName: document.getElementById('edit-firstname').value,
        lastName: document.getElementById('edit-lastname').value,
        email: document.getElementById('edit-email').value,
        role: document.getElementById('edit-role').value
    };
    
    const newPassword = document.getElementById('edit-password').value;
    if (newPassword) {
        updates.password = newPassword;
    }
    
    closeEditUserModal();
    showToast('Updating user...', 'info');
    
    try {
        const result = await window.fetchAPI(`/users/${username}`, {
            method: 'PATCH',
            body: JSON.stringify(updates)
        });
        
        if (result.success) {
            showToast('✅ User updated successfully', 'success');
            await loadUsersTable();
        } else {
            showToast(`❌ ${result.error || 'Failed to update user'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function showUserQuotasModal(username) {
    try {
        const userData = await window.fetchAPI(`/users/${username}`);
        const usageData = await window.fetchAPI(`/users/${username}/usage`);
        
        if (!userData.success || !usageData.success) {
            showToast('Failed to load user data', 'error');
            return;
        }
        
        const user = userData.user;
        const usage = usageData.usage;
        
        const modal = document.createElement('div');
        modal.className = 'modal-overlay active';
        modal.id = 'quotas-modal';
        
        modal.innerHTML = `
            <div class="modal modal-large">
                <div class="modal-header">
                    <h3>📊 Quotas & Usage: ${username}</h3>
                    <button class="modal-close" onclick="closeQuotasModal()">✖</button>
                </div>
                <form onsubmit="updateQuotasSubmit(event, '${username}')">
                    <div class="modal-body">
                        <div class="user-form-section">
                            <h4>Current Usage</h4>
                            <div class="quota-items">
                                <div class="quota-item">
                                    <span>VMs:</span>
                                    <strong>${usage.vms || 0} / ${user.quotas?.maxVMs || 10}</strong>
                                </div>
                                <div class="quota-item">
                                    <span>vCPUs:</span>
                                    <strong>${usage.cpu || 0} / ${user.quotas?.maxVCPUs || 20}</strong>
                                </div>
                                <div class="quota-item">
                                    <span>Memory:</span>
                                    <strong>${usage.memory || 0} GB / ${user.quotas?.maxMemory || 40} GB</strong>
                                </div>
                                <div class="quota-item">
                                    <span>Disk:</span>
                                    <strong>${usage.disk || 0} GB / ${user.quotas?.maxDisk || 200} GB</strong>
                                </div>
                            </div>
                        </div>
                        
                        <div class="user-form-section">
                            <h4>Update Quotas</h4>
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Max VMs</label>
                                    <input type="number" id="quota-max-vms" 
                                           value="${user.quotas?.maxVMs || 10}" min="1" max="100">
                                </div>
                                <div class="form-group">
                                    <label>Max vCPUs</label>
                                    <input type="number" id="quota-max-vcpus" 
                                           value="${user.quotas?.maxVCPUs || 20}" min="1" max="200">
                                </div>
                            </div>
                            <div class="form-row">
                                <div class="form-group">
                                    <label>Max Memory (GB)</label>
                                    <input type="number" id="quota-max-memory" 
                                           value="${user.quotas?.maxMemory || 40}" min="1" max="500">
                                </div>
                                <div class="form-group">
                                    <label>Max Disk (GB)</label>
                                    <input type="number" id="quota-max-disk" 
                                           value="${user.quotas?.maxDisk || 200}" min="10" max="2000">
                                </div>
                            </div>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" onclick="closeQuotasModal()">Cancel</button>
                        <button type="submit" class="btn btn-primary">Update Quotas</button>
                    </div>
                </form>
            </div>
        `;
        
        document.body.appendChild(modal);
        
    } catch (error) {
        showToast(`Error: ${error.message}`, 'error');
    }
}

function closeQuotasModal() {
    const modal = document.getElementById('quotas-modal');
    if (modal) modal.remove();
}

async function updateQuotasSubmit(event, username) {
    event.preventDefault();
    
    const quotas = {
        maxVMs: parseInt(document.getElementById('quota-max-vms').value),
        maxVCPUs: parseInt(document.getElementById('quota-max-vcpus').value),
        maxMemory: parseInt(document.getElementById('quota-max-memory').value),
        maxDisk: parseInt(document.getElementById('quota-max-disk').value)
    };
    
    closeQuotasModal();
    showToast('Updating quotas...', 'info');
    
    try {
        const result = await window.fetchAPI(`/users/${username}/quotas`, {
            method: 'PUT',
            body: JSON.stringify({ quotas })
        });
        
        if (result.success) {
            showToast('✅ Quotas updated successfully', 'success');
            await loadUsersTable();
        } else {
            showToast(`❌ ${result.error || 'Failed to update quotas'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function deleteUser(username) {
    if (!confirm(`Delete user "${username}"?\n\n⚠️ All their VMs and data will be removed!`)) {
        return;
    }
    
    showToast('Deleting user...', 'info');
    
    try {
        const result = await window.fetchAPI(`/users/${username}`, {
            method: 'DELETE'
        });
        
        if (result.success) {
            showToast('✅ User deleted successfully', 'success');
            await loadUsersTable();
        } else {
            showToast(`❌ ${result.error || 'Failed to delete user'}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

// Export functions
window.loadUsersTable = loadUsersTable;
window.showCreateUserModal = showCreateUserModal;
window.closeCreateUserModal = closeCreateUserModal;
window.createUserSubmit = createUserSubmit;
window.showEditUserModal = showEditUserModal;
window.closeEditUserModal = closeEditUserModal;
window.updateUserSubmit = updateUserSubmit;
window.showUserQuotasModal = showUserQuotasModal;
window.closeQuotasModal = closeQuotasModal;
window.updateQuotasSubmit = updateQuotasSubmit;
window.deleteUser = deleteUser;