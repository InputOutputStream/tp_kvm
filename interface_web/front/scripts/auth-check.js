// Authentication Check Script

(function() {
    'use strict';
    
    // Check if user is authenticated
    function checkAuthentication() {
        const token = localStorage.getItem('thoth_token') || sessionStorage.getItem('thoth_token');
        const user = localStorage.getItem('thoth_user') || sessionStorage.getItem('thoth_user');
        
        // If no authentication found, redirect to login
        if (!token || !user) {
            console.log('No authentication found, redirecting to login...');
            window.location.href = 'login.html';
            return false;
        }
        
        return true;
    }
    
    // Only check if we're not already on the login page
    if (!window.location.pathname.includes('login.html') && 
        !window.location.pathname.includes('register.html')) {
        checkAuthentication();
    }
})();