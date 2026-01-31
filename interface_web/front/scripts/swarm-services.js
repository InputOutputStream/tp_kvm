// ==========================================
// SWARM SERVICE MANAGEMENT
// ==========================================

async function loadClusterServices(clusterId) {
    try {
        const data = await window.fetchAPI(`/swarm/${clusterId}/services`);
        
        if (data.success && data.services) {
            renderClusterServices(data.services, clusterId);
        }
    } catch (error) {
        console.error('Error loading services:', error);
    }
}

function renderClusterServices(services, clusterId) {
    const servicesDiv = document.getElementById('cluster-services');
    if (!servicesDiv) return;
    
    if (services.length === 0) {
        servicesDiv.innerHTML = `
            <p class="empty-state">No services deployed</p>
            <button class="btn btn-primary" onclick="showDeployServiceModal('${clusterId}')">
                Deploy Service
            </button>
        `;
        return;
    }
    
    let html = `<button class="btn btn-primary" onclick="showDeployServiceModal('${clusterId}')">
        ➕ Deploy Service
    </button><div class="services-grid">`;
    
    services.forEach(service => {
        html += `
            <div class="service-card">
                <h4>${service.Name || service.ID}</h4>
                <p>Replicas: ${service.Replicas}</p>
                <p>Image: ${service.Image}</p>
                <button class="btn btn-sm btn-danger" onclick="deleteService('${clusterId}', '${service.Name || service.ID}')">
                    Delete
                </button>
            </div>
        `;
    });
    
    html += '</div>';
    servicesDiv.innerHTML = html;
}

function showDeployServiceModal(clusterId) {
    const modal = document.createElement('div');
    modal.className = 'modal-overlay active';
    modal.id = 'deploy-service-modal';
    
    modal.innerHTML = `
        <div class="modal">
            <div class="modal-header">
                <h3>🚀 Deploy Service to Swarm</h3>
                <button class="modal-close" onclick="closeDeployServiceModal()">✖</button>
            </div>
            <form onsubmit="deployServiceSubmit(event, '${clusterId}')">
                <div class="modal-body">
                    <div class="form-group">
                        <label>Service Name *</label>
                        <input type="text" id="service-name" required pattern="[a-z0-9-]+">
                    </div>
                    <div class="form-group">
                        <label>Docker Image *</label>
                        <input type="text" id="service-image" required placeholder="nginx:latest">
                    </div>
                    <div class="form-group">
                        <label>Replicas</label>
                        <input type="number" id="service-replicas" value="3" min="1" max="10">
                    </div>
                    <div class="form-group">
                        <label>Ports (format: 80:80)</label>
                        <input type="text" id="service-ports" placeholder="8080:80">
                    </div>
                    <div class="form-group">
                        <label>Environment Variables (JSON)</label>
                        <textarea id="service-env" rows="3" placeholder='{"KEY": "value"}'></textarea>
                    </div>
                </div>
                <div class="modal-footer">
                    <button type="button" class="btn btn-secondary" onclick="closeDeployServiceModal()">Cancel</button>
                    <button type="submit" class="btn btn-primary">Deploy</button>
                </div>
            </form>
        </div>
    `;
    
    document.body.appendChild(modal);
}

function closeDeployServiceModal() {
    const modal = document.getElementById('deploy-service-modal');
    if (modal) modal.remove();
}

async function deployServiceSubmit(event, clusterId) {
    event.preventDefault();
    
    const serviceConfig = {
        name: document.getElementById('service-name').value,
        image: document.getElementById('service-image').value,
        replicas: parseInt(document.getElementById('service-replicas').value)
    };
    
    const ports = document.getElementById('service-ports').value;
    if (ports) {
        serviceConfig.ports = [ports];
    }
    
    const envText = document.getElementById('service-env').value;
    if (envText) {
        try {
            serviceConfig.env = JSON.parse(envText);
        } catch (e) {
            showToast('Invalid JSON for environment variables', 'error');
            return;
        }
    }
    
    closeDeployServiceModal();
    showToast('Deploying service...', 'info');
    
    try {
        const result = await window.fetchAPI(`/swarm/${clusterId}/services`, {
            method: 'POST',
            body: JSON.stringify(serviceConfig)
        });
        
        if (result.success) {
            showToast('✅ Service deployed', 'success');
            await loadClusterServices(clusterId);
        } else {
            showToast(`❌ ${result.error}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

async function deleteService(clusterId, serviceName) {
    if (!confirm(`Delete service ${serviceName}?`)) return;
    
    showToast('Deleting service...', 'info');
    
    try {
        const result = await window.fetchAPI(`/swarm/${clusterId}/services/${serviceName}`, {
            method: 'DELETE'
        });
        
        if (result.success) {
            showToast('✅ Service deleted', 'success');
            await loadClusterServices(clusterId);
        } else {
            showToast(`❌ ${result.error}`, 'error');
        }
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
    }
}

// Enhance cluster details modal to show services
const originalShowClusterDetails = window.showClusterDetails;
window.showClusterDetails = async function(clusterId) {
    await originalShowClusterDetails(clusterId);
    
    // Add services tab
    const modal = document.getElementById('cluster-details-modal');
    if (!modal) return;
    
    const modalBody = modal.querySelector('.modal-body');
    modalBody.innerHTML += `
        <h4 style="margin-top: 20px;">Services:</h4>
        <div id="cluster-services"></div>
    `;
    
    await loadClusterServices(clusterId);
};

// Export
window.loadClusterServices = loadClusterServices;
window.showDeployServiceModal = showDeployServiceModal;
window.closeDeployServiceModal = closeDeployServiceModal;
window.deployServiceSubmit = deployServiceSubmit;
window.deleteService = deleteService;