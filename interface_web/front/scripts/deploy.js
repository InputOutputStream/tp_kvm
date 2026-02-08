let availableFlavors = [];
let availableImages = [];

async function initDeployForm() {
    await Promise.all([
        loadFlavors(),
        loadBaseImages()
    ]);
}

// Toggle Auth Method
function toggleAuthMethod() {
    const authMethod = document.querySelector('input[name="auth-method"]:checked').value;
    const passwordSection = document.getElementById('password-section');
    const sshKeySection = document.getElementById('ssh-key-section');
    
    if (authMethod === 'password') {
        passwordSection.style.display = 'block';
        sshKeySection.style.display = 'none';
        document.getElementById('vm-password').required = true;
        document.getElementById('vm-ssh-key').required = false;
    } else {
        passwordSection.style.display = 'none';
        sshKeySection.style.display = 'block';
        document.getElementById('vm-password').required = false;
        document.getElementById('vm-ssh-key').required = true;
    }
}

async function loadFlavors() {
    try {
        const data = await window.fetchAPI('/flavors');
        
        if (data.success && data.flavors) {
            availableFlavors = data.flavors;
            renderFlavorCards(data.flavors);
        }
    } catch (error) {
        console.error('Error loading flavors:', error);
        showToast('Using default flavors', 'warning');
    }
}

async function loadBaseImages() {
    try {
        const data = await window.fetchAPI('/images');
        
        if (data.success && data.images) {
            availableImages = data.images;
            renderImageSelector(data.images);
        }
    } catch (error) {
        console.error('Error loading images:', error);
    }
}


// Update Progress Step
function updateProgressStep(stepNum, status) {
    const step = document.getElementById(`step-${stepNum}`);
    if (!step) return;
    
    const icon = step.querySelector('.step-icon');
    
    if (status === 'loading') {
        icon.textContent = '⏳';
        step.classList.add('active');
    } else if (status === 'success') {
        icon.textContent = '✅';
        step.classList.add('complete');
        step.classList.remove('active');
    } else if (status === 'error') {
        icon.textContent = '❌';
        step.classList.add('error');
        step.classList.remove('active');
    }
}



function renderFlavorCards(flavors) {
    const flavorContainer = document.querySelector('.flavor-cards');
    if (!flavorContainer) return;
    
    flavorContainer.innerHTML = '';
    
    flavors.forEach((flavor, index) => {
        const card = document.createElement('label');
        card.className = 'flavor-card';
        
        card.innerHTML = `
            <input type="radio" name="flavor" value="${flavor.id}" ${index === 0 ? 'checked' : ''}>
            <div class="flavor-content">
                <h4>${flavor.name}</h4>
                <p class="flavor-specs">
                    ${flavor.specs.vcpus} vCPU • 
                    ${flavor.specs.ram} MB RAM • 
                    ${flavor.specs.disk} GB Disk
                </p>
                <p class="flavor-price">${flavor.priceFormatted}</p>
            </div>
        `;
        
        flavorContainer.appendChild(card);
    });
}

function renderImageSelector(images) {
    const deployForm = document.getElementById('deploy-form');
    if (!deployForm) return;
    
    // Find the network section and insert image selector before it
    const networkGroup = document.querySelector('#vm-network')?.closest('.form-group');
    if (!networkGroup) return;
    
    const imageSection = document.createElement('div');
    imageSection.className = 'form-group';
    imageSection.innerHTML = `
        <label>Base Image *</label>
        <select id="vm-base-image" required>
            ${images.map(img => `
                <option value="${img.id}">
                    ${img.displayName} - ${img.sizeFormatted}
                </option>
            `).join('')}
        </select>
    `;
    
    networkGroup.parentNode.insertBefore(imageSection, networkGroup);
}

// Enhanced deployVM with flavor and image selection
async function deployVMEnhanced(event) {
    event.preventDefault();
    
    const selectedFlavor = document.querySelector('input[name="flavor"]:checked');
    if (!selectedFlavor) {
        showToast('Please select a flavor', 'error');
        return;
    }
    
    const flavorId = selectedFlavor.value;
    const flavor = availableFlavors.find(f => f.id === flavorId);
    
    if (!flavor) {
        showToast('Invalid flavor selected', 'error');
        return;
    }
    
    const baseImageId = document.getElementById('vm-base-image')?.value;
    
    const deployData = {
        hostname: document.getElementById('vm-hostname').value,
        memory: flavor.specs.ram,
        vcpus: flavor.specs.vcpus,
        disk: flavor.specs.disk,
        baseImage: baseImageId,
        network: document.getElementById('vm-network').value,
        username: document.getElementById('vm-username').value,
        authMethod: document.querySelector('input[name="auth-method"]:checked').value,
        password: document.getElementById('vm-password').value,
        sshKey: document.getElementById('vm-ssh-key').value,
        flavor: flavorId
    };
    
    document.getElementById('deploy-form').style.display = 'none';
    document.getElementById('deployment-progress').style.display = 'block';
    
    try {
        updateProgressStep(1, 'loading');
        
        const result = await fetchAPI('/vms/deploy', {
            method: 'POST',
            body: JSON.stringify(deployData)
        });
        
        updateProgressStep(1, 'success');
        updateProgressStep(2, 'success');
        updateProgressStep(3, 'success');
        
        // Wait for cloud-init
        updateProgressStep(4, 'loading');
        // await new Promise(resolve => setTimeout(resolve, 60000));
        updateProgressStep(4, 'success');
        
        // Get IP
        updateProgressStep(5, 'loading');
        const internalName = result.vmName;
        const ipResult = await waitForVMIP(internalName, 30);
        updateProgressStep(5, 'success');
        
        // Show result
        const displayName = result.displayName || deployData.hostname;
        document.getElementById('result-hostname').textContent = displayName;
        document.getElementById('result-ip').textContent = ipResult.primaryIP || 'IP not available';
        document.getElementById('result-username').textContent = deployData.username;
        document.getElementById('ssh-command').textContent = `ssh ${deployData.username}@${ipResult.primaryIP}`;
        
        // Show VM information panel
        const vmInfoPanel = document.getElementById('vm-info-panel');
        if (vmInfoPanel && ipResult.primaryIP) {
            vmInfoPanel.innerHTML = `
                <h4>📊 VM Information</h4>
                <div class="vm-info-grid">
                    <div class="vm-info-item">
                        <strong>Hostname</strong>
                        <span>${displayName}</span>
                    </div>
                    <div class="vm-info-item">
                        <strong>IP Address</strong>
                        <code>${ipResult.primaryIP}</code>
                    </div>
                    <div class="vm-info-item">
                        <strong>Internal Name</strong>
                        <code>${internalName}</code>
                    </div>
                    <div class="vm-info-item">
                        <strong>Network</strong>
                        <span>${deployData.network}</span>
                    </div>
                    <div class="vm-info-item">
                        <strong>Flavor</strong>
                        <span>${flavor.name}</span>
                    </div>
                    <div class="vm-info-item">
                        <strong>Resources</strong>
                        <span>${flavor.specs.vcpus} vCPU • ${Math.floor(flavor.specs.ram / 1024)} GB RAM</span>
                    </div>
                    <div class="vm-info-item">
                        <strong>Base Image</strong>
                        <span>${baseImageId}</span>
                    </div>
                </div>
            `;
            vmInfoPanel.style.display = 'block';
        }
        
        document.getElementById('deployment-result').style.display = 'block';
        
        showToast('✅ VM deployed successfully!', 'success');
        await loadVMs();
        
    } catch (error) {
        showToast(`❌ ${error.message}`, 'error');
        
        setTimeout(() => {
            document.getElementById('deploy-form').style.display = 'block';
            document.getElementById('deployment-progress').style.display = 'none';
            document.getElementById('deployment-result').style.display = 'none';
            
            for (let i = 1; i <= 5; i++) {
                const step = document.getElementById(`step-${i}`);
                if (step) {
                    step.className = 'progress-step';
                    step.querySelector('.step-icon').textContent = '⏳';
                }
            }
        }, 3000);
    }
}


// Wait for VM IP
async function waitForVMIP(vmName, maxWaitSeconds) {
    const startTime = Date.now();
    const maxWaitMs = maxWaitSeconds * 1000;
    
    while ((Date.now() - startTime) < maxWaitMs) {
        try {
            const result = await fetchAPI(`/vms/${vmName}/ip`);
            if (result.success && result.ip) {
                return result;
            }
        } catch (error) {
            // Continue waiting
        }
        
        await new Promise(resolve => setTimeout(resolve, 2000)); // Check every 2 seconds
    }
    
    return { success: false, ip: null };
}

// Copy SSH Command
function copySshCommand() {
    const command = document.getElementById('ssh-command').textContent;
    navigator.clipboard.writeText(command).then(() => {
        showToast('✅ SSH command copied!', 'success');
    }).catch(() => {
        showToast('❌ Failed to copy', 'error');
    });
}

// Reset Deployment Form
document.getElementById('deploy-form')?.addEventListener('reset', () => {
    document.getElementById('deployment-progress').style.display = 'none';
    document.getElementById('deployment-result').style.display = 'none';
    
    // Reset all steps
    for (let i = 1; i <= 5; i++) {
        const step = document.getElementById(`step-${i}`);
        if (step) {
            step.className = 'progress-step';
            step.querySelector('.step-icon').textContent = '⏳';
        }
    }
});

document.addEventListener('DOMContentLoaded', () => {
    if (document.getElementById('deploy-form')) {
        initDeployForm();
    }
});



window.initDeployForm = initDeployForm;
window.deployVMEnhanced = deployVMEnhanced;
window.deployVM = deployVMEnhanced;