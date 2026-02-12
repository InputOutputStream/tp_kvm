
class UnifiedAPIClient {
    constructor() {
        this.baseURL = window.API_URL || 'http://localhost:3000/api';
        this.requestCache = new Map();
        this.cacheTimeout = 5000; // 5 seconds cache for GET requests
    }

    // ==========================================
    // Core Request Handler with Caching
    // ==========================================
    
    async request(endpoint, options = {}) {
        const url = `${this.baseURL}${endpoint}`;
        const method = options.method || 'GET';
        
        // Check cache for GET requests
        if (method === 'GET') {
            const cached = this.getCached(endpoint);
            if (cached) {
                console.log(`Cache hit: ${endpoint}`);
                return cached;
            }
        }
        
        const defaultHeaders = {
            'Content-Type': 'application/json',
        };

        // Add auth token if available
        if (window.authService?.token) {
            defaultHeaders['Authorization'] = `Bearer ${window.authService.token}`;
        }

        const config = {
            ...options,
            headers: {
                ...defaultHeaders,
                ...options.headers,
            },
        };

        try {
            const response = await fetch(url, config);
            
            // Handle non-JSON responses
            const contentType = response.headers.get('content-type');
            let data;
            
            if (contentType && contentType.includes('application/json')) {
                data = await response.json();
            } else {
                data = { success: response.ok, status: response.status };
            }
            
            if (!response.ok) {
                throw new Error(data.error || `HTTP ${response.status}: ${response.statusText}`);
            }
            
            // Cache successful GET requests
            if (method === 'GET') {
                this.setCached(endpoint, data);
            }
            
            // Invalidate related cache on mutations
            if (['POST', 'PUT', 'PATCH', 'DELETE'].includes(method)) {
                this.invalidateCache(endpoint);
            }
            
            return data;
        } catch (error) {
            console.error(`API Error (${method} ${endpoint}):`, error);
            
            // Provide user-friendly error messages
            if (error.message.includes('Failed to fetch')) {
                throw new Error('Network error: Unable to reach server');
            }
            
            throw error;
        }
    }

    // ==========================================
    // Cache Management
    // ==========================================
    
    getCached(key) {
        const cached = this.requestCache.get(key);
        if (!cached) return null;
        
        if (Date.now() - cached.timestamp > this.cacheTimeout) {
            this.requestCache.delete(key);
            return null;
        }
        
        return cached.data;
    }

    setCached(key, data) {
        this.requestCache.set(key, {
            data,
            timestamp: Date.now()
        });
    }

    invalidateCache(endpoint) {
        // Invalidate exact match
        this.requestCache.delete(endpoint);
        
        // Invalidate related endpoints
        const segments = endpoint.split('/').filter(Boolean);
        
        // Clear all cache entries that share the same resource type
        if (segments.length > 0) {
            const resourceType = segments[0];
            Array.from(this.requestCache.keys()).forEach(key => {
                if (key.includes(resourceType)) {
                    this.requestCache.delete(key);
                }
            });
        }
    }

    clearCache() {
        this.requestCache.clear();
    }

    // ==========================================
    // Convenience Methods
    // ==========================================
    
    get(endpoint) {
        return this.request(endpoint);
    }

    post(endpoint, data) {
        return this.request(endpoint, {
            method: 'POST',
            body: JSON.stringify(data)
        });
    }

    put(endpoint, data) {
        return this.request(endpoint, {
            method: 'PUT',
            body: JSON.stringify(data)
        });
    }

    patch(endpoint, data) {
        return this.request(endpoint, {
            method: 'PATCH',
            body: JSON.stringify(data)
        });
    }

    delete(endpoint, data) {
        return this.request(endpoint, {
            method: 'DELETE',
            body: data ? JSON.stringify(data) : undefined
        });
    }

    // ==========================================
    // VM MANAGEMENT
    // ==========================================
    
    async getVMs() {
        return this.get('/vms');
    }

    async getVMDetails(vmName) {
        return this.get(`/vms/${vmName}`);
    }

    async getVMStats(vmName) {
        // Don't cache stats - always fresh
        return this.request(`/vms/${vmName}/stats`, { method: 'GET' });
    }

    async startVM(vmName) {
        return this.post(`/vms/${vmName}/start`);
    }

    async shutdownVM(vmName) {
        return this.post(`/vms/${vmName}/shutdown`);
    }

    async rebootVM(vmName) {
        return this.post(`/vms/${vmName}/reboot`);
    }

    async pauseVM(vmName) {
        return this.post(`/vms/${vmName}/pause`);
    }

    async resumeVM(vmName) {
        return this.post(`/vms/${vmName}/resume`);
    }

    async deleteVM(vmName, removeDisks = true) {
        return this.delete(`/vms/${vmName}?removeDisks=${removeDisks}`);
    }

    async cloneVM(vmName, cloneName) {
        return this.post(`/vms/${vmName}/clone`, { cloneName });
    }

    async deployVM(deployData) {
        return this.post('/vms/deploy', deployData);
    }

    async getVMIP(vmName) {
        return this.get(`/vms/${vmName}/ip`);
    }

    async getVNCInfo(vmName) {
        return this.get(`/vms/${vmName}/vnc`);
    }

    // Snapshots
    async getSnapshots(vmName) {
        return this.get(`/vms/${vmName}/snapshots`);
    }

    async createSnapshot(vmName, snapshotName, description) {
        return this.post(`/vms/${vmName}/snapshots`, { snapshotName, description });
    }

    async revertSnapshot(vmName, snapshotName) {
        return this.post(`/vms/${vmName}/snapshots/${snapshotName}/revert`);
    }

    async deleteSnapshot(vmName, snapshotName) {
        return this.delete(`/vms/${vmName}/snapshots/${snapshotName}`);
    }

    // ==========================================
    // PAAS APPLICATIONS
    // ==========================================

    async getPaasApplications() {
        return this.get('/paas/apps');  
    }

    async deployPaasApplication(appConfig) {
        return this.post('/paas/deploy', appConfig);
    }

    async getPaasApplicationDetails(appId) {
        return this.get(`/paas/apps/${appId}`);
    }

    async getPaasApplicationStats(appId) {
        return this.get(`/paas/apps/${appId}/stats`);
    }

    async getPaasApplicationLogs(appId, lines = 100) {
        return this.get(`/paas/apps/${appId}/logs?lines=${lines}`);
    }

    async stopPaasApplication(appId) {
        return this.post(`/paas/apps/${appId}/stop`);
    }

    async startPaasApplication(appId) {
        return this.post(`/paas/apps/${appId}/start`);
    }

    async deletePaasApplication(appId) {
        return this.delete(`/paas/apps/${appId}`);
    }

    async getDatabaseCredentials(dbType, appName, username) {
        return this.get(`/paas/database/credentials?type=${dbType}&app=${appName}&user=${username}`);
    }

    // ==========================================
    // SWARM CLUSTERS
    // ==========================================

    async getSwarmClusters(owner) {
        return this.get(`/swarm/clusters?owner=${owner}`);
    }

    async createSwarmCluster(clusterData) {
        return this.post('/swarm/clusters', clusterData);
    }

    async getSwarmClusterDetails(clusterId) {
        return this.get(`/swarm/clusters/${clusterId}`);
    }

    async deleteSwarmCluster(clusterId) {
        return this.delete(`/swarm/clusters/${clusterId}`);
    }

    async getSwarmServices(clusterId) {
        return this.get(`/swarm/${clusterId}/services`);
    }

    async deploySwarmService(clusterId, serviceConfig) {
        return this.post(`/swarm/${clusterId}/services`, serviceConfig);
    }

    async deleteSwarmService(clusterId, serviceName) {
        return this.delete(`/swarm/${clusterId}/services/${serviceName}`);
    }

    // ==========================================
    // NETWORK MANAGEMENT
    // ==========================================

    async getNetworks() {
        return this.get('/networks');
    }

    async getUserNetworks() {
        return this.get('/networks/mine');
    }

    async createUserNetwork(username, networkName) {
        return this.post('/networks/user', { username, networkName });
    }

    async deleteUserNetwork(networkId, username) {
        return this.delete(`/networks/user/${networkId}`, { username });
    }

    async updateNetwork(networkId, updates) {
        return this.patch(`/networks/${networkId}`, updates);
    }

    // ==========================================
    // USER MANAGEMENT
    // ==========================================

    async getUsers() {
        return this.get('/users');
    }

    async createUser(userData) {
        return this.post('/users', userData);
    }

    async getUser(username) {
        return this.get(`/users/${username}`);
    }

    async updateUser(username, updates) {
        return this.patch(`/users/${username}`, updates);
    }

    async deleteUser(username) {
        return this.delete(`/users/${username}`);
    }

    async getUserUsage(username) {
        return this.get(`/users/${username}/usage`);
    }

    async updateUserQuotas(username, quotas) {
        return this.put(`/users/${username}/quotas`, { quotas });
    }

    // ==========================================
    // SYSTEM & MISC
    // ==========================================

    async getSystemInfo() {
        return this.get('/system/info');
    }

    async getFlavors() {
        return this.get('/flavors');
    }

    async getImages() {
        return this.get('/images');
    }

    async getHosts() {
        return this.get('/hosts');
    }

    async getHostStats() {
        return this.get('/hosts/stats');
    }

    async addHost(uri) {
        return this.post('/hosts', { uri });
    }

    async removeHost(hostId) {
        return this.delete(`/hosts/${hostId}`);
    }

    // ==========================================
    // BILLING
    // ==========================================
    
    async getSaaSBilling() {
        return this.get('/billing/saas');
    }
}

// Create global instance
window.api = new UnifiedAPIClient();

// Backward compatibility aliases
window.apiService = window.api;
window.fetchAPI = (endpoint, options) => window.api.request(endpoint, options);