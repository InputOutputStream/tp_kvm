/* ==========================================
   THOTH CLOUD - VM Deployment
   ========================================== */

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

// Deploy VM
async function deployVM(event) {
    event.preventDefault();
    
    // Get flavor selection
    const selectedFlavor = document.querySelector('input[name="flavor"]:checked');
    if (!selectedFlavor) {
        showToast('Please select a flavor', 'error');
        return;
    }
    
    const flavorType = selectedFlavor.value;
    const flavorConfig = getFlavorConfig(flavorType);
    
    // Get form values
    const hostname = document.getElementById('vm-hostname').value;
    const network = document.getElementById('vm-network').value;
    const username = document.getElementById('vm-username').value;
    const authMethod = document.querySelector('input[name="auth-method"]:checked').value;
    const password = authMethod === 'password' ? document.getElementById('vm-password').value : "";
    const sshKey = authMethod === 'ssh-key' ? document.getElementById('vm-ssh-key').value : "";
    
    // Show progress
    document.getElementById('deploy-form').style.display = 'none';
    document.getElementById('deployment-progress').style.display = 'block';
    
    try {
        // Step 1: Generate cloud-init
        updateProgressStep(1, 'loading');
        
        const deployData = {
            hostname,
            memory: flavorConfig.memory,
            vcpus: flavorConfig.vcpus,
            disk: flavorConfig.disk,
            network,
            username,
            authMethod,
            password,
            sshKey,
            flavor: flavorType
        };
        
        // Call backend to deploy VM
        const result = await fetchAPI('/vms/deploy', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(deployData)
        });
        
        updateProgressStep(1, 'success');
        updateProgressStep(2, 'success');
        updateProgressStep(3, 'success');
        
        // Step 4: Wait for cloud-init
        updateProgressStep(4, 'loading');
        await new Promise(resolve => setTimeout(resolve, 60000));
        updateProgressStep(4, 'success');
        
        // Step 5: Get IP address
        updateProgressStep(5, 'loading');
        const ipResult = await waitForVMIP(hostname, 30);
        updateProgressStep(5, 'success');
        
        // Show result
        document.getElementById('result-hostname').textContent = hostname;
        document.getElementById('result-ip').textContent = ipResult.primaryIP || 'IP not available';
        document.getElementById('result-username').textContent = username;
        document.getElementById('ssh-command').textContent = `ssh ${username}@${ipResult.primaryIP}`;
        document.getElementById('deployment-result').style.display = 'block';
        
        showToast('✅ VM deployed successfully!', 'success');
        await loadVMs();
        
    } catch (error) {
        showToast(`❌ Deployment error: ${error.message}`, 'error');
        
        // Reset form
        setTimeout(() => {
            document.getElementById('deploy-form').style.display = 'block';
            document.getElementById('deployment-progress').style.display = 'none';
            document.getElementById('deployment-result').style.display = 'none';
            
            // Reset all steps
            for (let i = 1; i <= 5; i++) {
                const step = document.getElementById(`step-${i}`);
                step.className = 'progress-step';
                step.querySelector('.step-icon').textContent = '⏳';
            }
        }, 3000);
    }
}

// Get Flavor Configuration
function getFlavorConfig(flavorType) {
    const flavors = {
        small: {
            memory: 2048,  // 2GB
            vcpus: 1,
            disk: 15,
            price: 2500
        },
        medium: {
            memory: 4096,  // 4GB
            vcpus: 2,
            disk: 20,
            price: 3500
        },
        large: {
            memory: 8192,  // 8GB
            vcpus: 4,
            disk: 40,
            price: 6500
        }
    };
    
    return flavors[flavorType] || flavors.medium;
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