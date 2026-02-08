const HostAdminService = {
    // API Call: Update Global Scheduling Strategy
    async updateStrategy(strategyIndex) {
        const loadingToast = showToast('Updating scheduling strategy...', 'info');
        try {
            const result = await window.fetchAPI('/hosts/strategy', {
                method: 'PUT',
                body: JSON.stringify({ HostSelectionStrategy: parseInt(strategyIndex) })
            });
            if (result.success) showToast('✅ Scheduling strategy updated', 'success');
        } catch (e) {
            showToast('❌ Failed to update strategy', 'error');
        }
    }
};

// UI: Host Strategy Modal Content
function showHostStrategyModal() {
    const strategies = [
        { id: 0, name: "Least Used", desc: "Select host with most free CPU/RAM" },
        { id: 1, name: "Round Robin", desc: "Cycle through hosts sequentially" },
        { id: 2, name: "Best Fit", desc: "Select host that matches specs exactly to minimize waste" }
    ];

    const content = `
        <div class="strategy-selector">
            ${strategies.map(s => `
                <label class="strategy-card">
                    <input type="radio" name="host-strategy" value="${s.id}" 
                        ${window.currentStrategy == s.id ? 'checked' : ''}>
                    <div class="strategy-details">
                        <strong>${s.name}</strong>
                        <p>${s.desc}</p>
                    </div>
                </label>
            `).join('')}
        </div>
    `;
    // Pass 'content' to your generic modal function...
}