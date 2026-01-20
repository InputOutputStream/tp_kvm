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
    
    const selectedFlavor = document.querySelector('input[name="flavor"]:checked');
    if (!selectedFlavor) {
        showToast('Please select a flavor', 'error');
        return;
    }
    
    const flavorType = selectedFlavor.value;
    const flavorConfig = getFlavorConfig(flavorType);
    
    const deployData = {
        hostname: document.getElementById('vm-hostname').value,  // User's chosen name
        memory: flavorConfig.memory,
        vcpus: flavorConfig.vcpus,
        disk: flavorConfig.disk,
        network: document.getElementById('vm-network').value,
        username: document.getElementById('vm-username').value,
        authMethod: document.querySelector('input[name="auth-method"]:checked').value,
        password: document.getElementById('vm-password').value,
        sshKey: document.getElementById('vm-ssh-key').value,
        flavor: flavorType
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
        await new Promise(resolve => setTimeout(resolve, 60000));
        updateProgressStep(4, 'success');
        
        // Get IP - use internal VM name from result
        updateProgressStep(5, 'loading');
        const internalName = result.vmName;
        const ipResult = await waitForVMIP(internalName, 30);
        updateProgressStep(5, 'success');
        
        // Show result with display name
        const displayName = result.displayName || deployData.hostname;
        document.getElementById('result-hostname').textContent = displayName;
        document.getElementById('result-ip').textContent = ipResult.primaryIP || 'IP not available';
        document.getElementById('result-username').textContent = deployData.username;
        document.getElementById('ssh-command').textContent = `ssh ${deployData.username}@${ipResult.primaryIP}`;
        document.getElementById('deployment-result').style.display = 'block';
        
        showToast('✅ VM deployed successfully!', 'success');
        await loadVMs();
        
    } catch (error) {
        showToast(`❌ Deployment error: ${error.message}`, 'error');
        
        // Show quota error details if available
        if (error.message.includes('quota exceeded')) {
            showToast('💡 Contact admin to increase your quota', 'info');
        }
        
        setTimeout(() => {
            document.getElementById('deploy-form').style.display = 'block';
            document.getElementById('deployment-progress').style.display = 'none';
            document.getElementById('deployment-result').style.display = 'none';
            
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
            memory: 1024,  // 2GB
            vcpus: 1,
            disk: 15,
            price: 2500
        },
        medium: {
            memory: 2048,  // 4GB
            vcpus: 1,
            disk: 20,
            price: 3500
        },
        large: {
            memory: 4096,  // 8GB
            vcpus: 2,
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