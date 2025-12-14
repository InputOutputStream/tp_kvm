const API_URL = 'http://localhost:3000/api';
let currentVM = null;
let statsInterval = null;
let monitoringInterval = null;
let charts = {};

// ========== Utility Functions ==========

function showToast(message, type = 'success') {
    const toast = document.getElementById('toast');
    toast.textContent = message;
    toast.className = `toast show ${type}`;
    
    setTimeout(() => {
        toast.classList.remove('show');
    }, 3000);
}

async function fetchAPI(endpoint, options = {}) {
    try {
        const response = await fetch(`${API_URL}${endpoint}`, options);
        const data = await response.json();
        
        if (!data.success && response.status >= 400) {
            throw new Error(data.error || 'Une erreur est survenue');
        }
        
        return data;
    } catch (error) {
        showToast(error.message, 'error');
        throw error;
    }
}

// ========== Tabs Management ==========

function switchTab(tabName) {
    document.querySelectorAll('.tab-content').forEach(tab => {
        tab.classList.remove('active');
    });
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.classList.remove('active');
    });
    
    document.getElementById(`tab-${tabName}`).classList.add('active');
    event.target.classList.add('active');
    
    if (tabName === 'vms') {
        loadVMs();
    } else if (tabName === 'system') {
        loadSystemInfo();
    }
}

// ========== VM Deployment ==========

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

function updateProgressStep(stepNum, status) {
    const step = document.getElementById(`step-${stepNum}`);
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

async function deployVM(event) {
    event.preventDefault();
    
    // Get form values
    const hostname = document.getElementById('vm-hostname').value;
    const memory = document.getElementById('vm-memory').value;
    const vcpus = document.getElementById('vm-vcpus').value;
    const disk = document.getElementById('vm-disk').value;
    const osVariant = document.getElementById('vm-os-variant').value;
    const isoPath = document.getElementById('vm-iso-path').value;
    const username = document.getElementById('vm-username').value;
    const authMethod = document.querySelector('input[name="auth-method"]:checked').value;
    const password = authMethod === 'password' ? document.getElementById('vm-password').value : null;
    const sshKey = authMethod === 'ssh-key' ? document.getElementById('vm-ssh-key').value : null;
    const network = document.getElementById('vm-network').value;
    const useDhcp = document.getElementById('vm-dhcp').checked;
    
    // Show progress
    document.getElementById('deploy-form').style.display = 'none';
    document.getElementById('deployment-progress').style.display = 'block';
    
    try {
        // Step 1: Generate cloud-init
        updateProgressStep(1, 'loading');
        await new Promise(resolve => setTimeout(resolve, 500));
        
        const deployData = {
            hostname,
            memory: parseInt(memory),
            vcpus: parseInt(vcpus),
            disk: parseInt(disk),
            osVariant,
            isoPath,
            username,
            authMethod,
            password,
            sshKey,
            network,
            useDhcp
        };
        
        // Call backend to deploy VM
        const result = await fetchAPI('/vms/deploy', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(deployData)
        });
        
        updateProgressStep(1, 'success');
        
        // Step 2: Creating VM
        updateProgressStep(2, 'loading');
        await new Promise(resolve => setTimeout(resolve, 1000));
        updateProgressStep(2, 'success');
        
        // Step 3: Starting VM
        updateProgressStep(3, 'loading');
        await new Promise(resolve => setTimeout(resolve, 1000));
        updateProgressStep(3, 'success');
        
        // Step 4: Wait for initialization
        updateProgressStep(4, 'loading');
        await new Promise(resolve => setTimeout(resolve, 2000));
        updateProgressStep(4, 'success');
        
        // Step 5: Get IP address
        updateProgressStep(5, 'loading');
        const ipResult = await waitForVMIP(hostname, 30); // Wait up to 30 seconds
        updateProgressStep(5, 'success');
        
        // Show result
        document.getElementById('result-hostname').textContent = hostname;
        document.getElementById('result-ip').textContent = ipResult.ip || 'IP non disponible';
        document.getElementById('result-username').textContent = username;
        document.getElementById('ssh-command').textContent = `ssh ${username}@${ipResult.ip}`;
        document.getElementById('deployment-result').style.display = 'block';
        
        showToast('✅ VM déployée avec succès!', 'success');
        
        // Reload VMs list
        await loadVMs();
        
    } catch (error) {
        showToast(`❌ Erreur lors du déploiement: ${error.message}`, 'error');
        
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

function copySshCommand() {
    const command = document.getElementById('ssh-command').textContent;
    navigator.clipboard.writeText(command);
    showToast('✅ Commande SSH copiée!', 'success');
}

// ========== VM Management ==========

async function loadVMs() {
    const vmsList = document.getElementById('vms-list');
    vmsList.innerHTML = '<p class="loading">Chargement des VMs...</p>';
    
    try {
        const data = await fetchAPI('/vms');
        
        if (data.vms.length === 0) {
            vmsList.innerHTML = '<p class="loading">Aucune VM trouvée</p>';
            return;
        }
        
        const runningCount = data.vms.filter(vm => vm.running).length;
        document.getElementById('running-count').textContent = runningCount;
        document.getElementById('total-count').textContent = data.vms.length;
        
        vmsList.innerHTML = '';
        data.vms.forEach(vm => {
            const vmCard = createVMCard(vm);
            vmsList.appendChild(vmCard);
        });
    } catch (error) {
        vmsList.innerHTML = '<p class="loading">Erreur lors du chargement</p>';
    }
}

function createVMCard(vm) {
    const div = document.createElement('div');
    div.className = 'vm-card';
    div.onclick = () => selectVM(vm.name);
    
    const statusClass = vm.running ? 'running' : 'shut-off';
    const statusText = vm.running ? '🟢 En cours' : '🔴 Arrêtée';
    
    let statsHTML = '';
    if (vm.stats) {
        statsHTML = `
            <div style="margin-top: 10px; font-size: 0.85rem; color: var(--text-dim);">
                CPU: ${vm.stats.cpu}% | Mem: ${vm.stats.memory.percent}%
            </div>
        `;
    }
    
    div.innerHTML = `
        <h3>🖥️ ${vm.name}</h3>
        <span class="vm-status ${statusClass}">${statusText}</span>
        <p style="margin-top: 10px; color: var(--text-dim);">État: ${vm.state}</p>
        ${statsHTML}
    `;
    
    return div;
}

async function selectVM(vmName) {
    currentVM = vmName;
    document.getElementById('vm-name-title').textContent = `🖥️ ${vmName}`;
    document.getElementById('vm-details').style.display = 'block';
    
    if (statsInterval) {
        clearInterval(statsInterval);
    }
    
    document.getElementById('vm-details').scrollIntoView({ behavior: 'smooth' });
    
    await loadVMInfo();
    await loadSnapshots();
    
    const statusData = await fetchAPI(`/vms/${currentVM}/status`);
    if (statusData.running) {
        document.getElementById('vm-stats').style.display = 'grid';
        startStatsUpdate();
    } else {
        document.getElementById('vm-stats').style.display = 'none';
    }
}

function closeDetails() {
    document.getElementById('vm-details').style.display = 'none';
    currentVM = null;
    if (statsInterval) {
        clearInterval(statsInterval);
        statsInterval = null;
    }
}

async function loadVMInfo() {
    const vmInfo = document.getElementById('vm-info');
    vmInfo.innerHTML = '<p class="loading">Chargement...</p>';
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}`);
        vmInfo.textContent = data.info;
    } catch (error) {
        vmInfo.innerHTML = '<p class="loading">Erreur lors du chargement</p>';
    }
}

// ========== Stats Update ==========

function startStatsUpdate() {
    updateStats();
    statsInterval = setInterval(updateStats, 3000);
}

async function updateStats() {
    if (!currentVM) return;
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}/stats`);
        const stats = data.stats;
        
        if (!stats) return;
        
        document.getElementById('cpu-usage').textContent = `${stats.cpu}%`;
        document.getElementById('memory-usage').textContent = 
            `${stats.memory.used} KB / ${stats.memory.max} KB (${stats.memory.percent}%)`;
        document.getElementById('disk-io').textContent = 
            `R: ${stats.disk.readMB} MB | W: ${stats.disk.writeMB} MB`;
        document.getElementById('network-io').textContent = 
            `RX: ${stats.network.rxMB} MB | TX: ${stats.network.txMB} MB`;
            
    } catch (error) {
        console.error('Error updating stats:', error);
    }
}

// ========== VM Actions ==========

async function startVM() {
    if (!currentVM) return;
    
    try {
        showToast('Démarrage de la VM...', 'success');
        await fetchAPI(`/vms/${currentVM}/start`, { method: 'POST' });
        showToast('✅ VM démarrée avec succès', 'success');
        await loadVMInfo();
        await loadVMs();
        document.getElementById('vm-stats').style.display = 'grid';
        startStatsUpdate();
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

async function shutdownVM() {
    if (!currentVM) return;
    
    if (!confirm(`Êtes-vous sûr de vouloir arrêter ${currentVM} ?`)) {
        return;
    }
    
    try {
        showToast('Arrêt de la VM...', 'success');
        await fetchAPI(`/vms/${currentVM}/shutdown`, { method: 'POST' });
        showToast('✅ VM arrêtée avec succès', 'success');
        await loadVMInfo();
        await loadVMs();
        document.getElementById('vm-stats').style.display = 'none';
        if (statsInterval) clearInterval(statsInterval);
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

async function rebootVM() {
    if (!currentVM) return;
    
    if (!confirm(`Redémarrer ${currentVM} ?`)) {
        return;
    }
    
    try {
        showToast('Redémarrage de la VM...', 'success');
        await fetchAPI(`/vms/${currentVM}/reboot`, { method: 'POST' });
        showToast('✅ VM redémarrée', 'success');
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

async function pauseVM() {
    if (!currentVM) return;
    
    try {
        showToast('Mise en pause...', 'success');
        await fetchAPI(`/vms/${currentVM}/pause`, { method: 'POST' });
        showToast('✅ VM en pause', 'success');
        await loadVMInfo();
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

async function resumeVM() {
    if (!currentVM) return;
    
    try {
        showToast('Reprise...', 'success');
        await fetchAPI(`/vms/${currentVM}/resume`, { method: 'POST' });
        showToast('✅ VM reprise', 'success');
        await loadVMInfo();
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

async function destroyVM() {
    if (!currentVM) return;
    
    if (!confirm(`⚠️ ATTENTION: Forcer l'arrêt de ${currentVM} ? Cela peut causer une perte de données.`)) {
        return;
    }
    
    try {
        showToast('Arrêt forcé de la VM...', 'success');
        await fetchAPI(`/vms/${currentVM}/destroy`, { method: 'POST' });
        showToast('✅ VM arrêtée de force', 'success');
        await loadVMInfo();
        await loadVMs();
        document.getElementById('vm-stats').style.display = 'none';
        if (statsInterval) clearInterval(statsInterval);
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

// ========== Snapshots ==========

async function loadSnapshots() {
    const snapshotsList = document.getElementById('snapshots-list');
    snapshotsList.innerHTML = '<p class="loading">Chargement des snapshots...</p>';
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}/snapshots`);
        
        if (data.snapshots.length === 0) {
            snapshotsList.innerHTML = '<p class="loading">Aucun snapshot trouvé</p>';
            return;
        }
        
        snapshotsList.innerHTML = '';
        data.snapshots.forEach(snapshot => {
            const snapshotCard = createSnapshotCard(snapshot);
            snapshotsList.appendChild(snapshotCard);
        });
    } catch (error) {
        snapshotsList.innerHTML = '<p class="loading">Erreur lors du chargement</p>';
    }
}

function createSnapshotCard(snapshot) {
    const div = document.createElement('div');
    div.className = 'snapshot-card';
    
    div.innerHTML = `
        <h4>📸 ${snapshot.name}</h4>
        <div class="time">🕒 ${snapshot.creationTime}</div>
        <div class="actions">
            <button class="btn btn-primary" onclick="revertSnapshot('${snapshot.name}')">
                ↩️ Restaurer
            </button>
            <button class="btn btn-danger" onclick="deleteSnapshot('${snapshot.name}')">
                🗑️
            </button>
        </div>
    `;
    
    return div;
}

function showSnapshotModal() {
    document.getElementById('snapshot-modal').classList.add('active');
}

function closeSnapshotModal() {
    document.getElementById('snapshot-modal').classList.remove('active');
    document.getElementById('snapshot-name').value = '';
    document.getElementById('snapshot-description').value = '';
}

async function createSnapshot(event) {
    event.preventDefault();
    
    const name = document.getElementById('snapshot-name').value;
    const description = document.getElementById('snapshot-description').value;
    
    try {
        showToast('Création du snapshot...', 'success');
        await fetchAPI(`/vms/${currentVM}/snapshots`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ snapshotName: name, description })
        });
        
        showToast('✅ Snapshot créé avec succès', 'success');
        closeSnapshotModal();
        await loadSnapshots();
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

async function revertSnapshot(snapshotName) {
    if (!confirm(`Restaurer le snapshot "${snapshotName}" ?\n\n⚠️ La VM sera arrêtée si elle est en cours d'exécution.`)) {
        return;
    }
    
    try {
        showToast('Restauration du snapshot...', 'success');
        await fetchAPI(`/vms/${currentVM}/snapshots/${snapshotName}/revert`, {
            method: 'POST'
        });
        
        showToast('✅ Snapshot restauré avec succès', 'success');
        await loadVMInfo();
        await loadVMs();
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

async function deleteSnapshot(snapshotName) {
    if (!confirm(`Supprimer définitivement le snapshot "${snapshotName}" ?`)) {
        return;
    }
    
    try {
        showToast('Suppression du snapshot...', 'success');
        await fetchAPI(`/vms/${currentVM}/snapshots/${snapshotName}`, {
            method: 'DELETE'
        });
        
        showToast('✅ Snapshot supprimé', 'success');
        await loadSnapshots();
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

// ========== Clone VM ==========

function showCloneModal() {
    document.getElementById('clone-modal').classList.add('active');
    document.getElementById('clone-name').value = `${currentVM}-clone`;
}

function closeCloneModal() {
    document.getElementById('clone-modal').classList.remove('active');
}

async function cloneVM(event) {
    event.preventDefault();
    
    const cloneName = document.getElementById('clone-name').value;
    
    try {
        showToast('Clonage en cours... (peut prendre du temps)', 'success');
        await fetchAPI(`/vms/${currentVM}/clone`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ cloneName })
        });
        
        showToast('✅ Clone créé avec succès!', 'success');
        closeCloneModal();
        await loadVMs();
    } catch (error) {
        showToast(`❌ Erreur: ${error.message}`, 'error');
    }
}

// ========== Console VNC ==========

function showConsoleModal() {
    document.getElementById('console-modal').classList.add('active');
    loadVNCInfo();
}

function closeConsoleModal() {
    document.getElementById('console-modal').classList.remove('active');
}

async function loadVNCInfo() {
    const vncInfo = document.getElementById('vnc-info');
    vncInfo.textContent = 'Chargement...';
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}/vnc`);
        
        if (!data.success) {
            vncInfo.innerHTML = `<span style="color: var(--danger);">❌ ${data.error}</span>`;
            return;
        }
        
        vncInfo.innerHTML = `
            <strong>Display:</strong> ${data.display}<br>
            <strong>Port:</strong> ${data.port}<br>
            <strong>Host:</strong> ${data.host}
        `;
        
        document.getElementById('vnc-command').textContent = 
            `vncviewer ${data.host}:${data.port}`;
    } catch (error) {
        vncInfo.innerHTML = `<span style="color: var(--danger);">❌ Erreur: ${error.message}</span>`;
    }
}

function copyVNCCommand() {
    const command = document.getElementById('vnc-command').textContent;
    navigator.clipboard.writeText(command);
    showToast('✅ Commande copiée!', 'success');
}

// ========== Monitoring with Charts ==========

function initCharts() {
    const chartConfig = {
        type: 'line',
        data: {
            labels: [],
            datasets: [{
                label: 'Value',
                data: [],
                borderColor: '#3b82f6',
                backgroundColor: 'rgba(59, 130, 246, 0.1)',
                tension: 0.4,
                fill: true
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: true,
            plugins: {
                legend: { display: false }
            },
            scales: {
                y: { beginAtZero: true }
            }
        }
    };
    
    charts.cpu = new Chart(document.getElementById('cpu-chart'), JSON.parse(JSON.stringify(chartConfig)));
    charts.memory = new Chart(document.getElementById('memory-chart'), JSON.parse(JSON.stringify(chartConfig)));
    charts.disk = new Chart(document.getElementById('disk-chart'), JSON.parse(JSON.stringify(chartConfig)));
    charts.network = new Chart(document.getElementById('network-chart'), JSON.parse(JSON.stringify(chartConfig)));
}

function toggleMonitoring() {
    if (monitoringInterval) {
        clearInterval(monitoringInterval);
        monitoringInterval = null;
        document.getElementById('monitoring-toggle').innerHTML = '▶️ Démarrer';
        showToast('Monitoring arrêté', 'success');
    } else {
        if (!currentVM) {
            showToast('⚠️ Sélectionnez une VM d\'abord', 'error');
            return;
        }
        monitoringInterval = setInterval(updateMonitoring, 3000);
        document.getElementById('monitoring-toggle').innerHTML = '⏸️ Arrêter';
        showToast('Monitoring démarré', 'success');
        updateMonitoring();
    }
}

async function updateMonitoring() {
    if (!currentVM) return;
    
    try {
        const data = await fetchAPI(`/vms/${currentVM}/stats`);
        const stats = data.stats;
        if (!stats) return;
        
        const now = new Date().toLocaleTimeString();
        
        updateChart(charts.cpu, now, stats.cpu);
        updateChart(charts.memory, now, parseFloat(stats.memory.percent));
        updateChart(charts.disk, now, parseFloat(stats.disk.writeMB));
        updateChart(charts.network, now, parseFloat(stats.network.txMB));
        
    } catch (error) {
        console.error('Error updating monitoring:', error);
    }
}

function updateChart(chart, label, value) {
    if (chart.data.labels.length > 20) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
    }
    
    chart.data.labels.push(label);
    chart.data.datasets[0].data.push(value);
    chart.update('none');
}

// ========== System Info ==========

async function loadSystemInfo() {
    const systemInfo = document.getElementById('system-info');
    systemInfo.innerHTML = '<p class="loading">Chargement...</p>';
    
    try {
        const data = await fetchAPI('/system/info');
        systemInfo.textContent = `${data.nodeInfo}\n\n${data.version}`;
    } catch (error) {
        systemInfo.innerHTML = '<p class="loading">Erreur lors du chargement</p>';
    }
}

// ========== Initialize ==========

window.addEventListener('DOMContentLoaded', () => {
    loadVMs();
    initCharts();
    
    document.getElementById('snapshot-modal').addEventListener('click', (e) => {
        if (e.target.id === 'snapshot-modal') closeSnapshotModal();
    });
    
    document.getElementById('clone-modal').addEventListener('click', (e) => {
        if (e.target.id === 'clone-modal') closeCloneModal();
    });
    
    document.getElementById('console-modal').addEventListener('click', (e) => {
        if (e.target.id === 'console-modal') closeConsoleModal();
    });
    
    setInterval(() => {
        if (document.getElementById('tab-vms').classList.contains('active')) {
            loadVMs();
        }
    }, 10000);
});