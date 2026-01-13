// PaaS Applications Catalog

const APPS_CATALOG = [
    {
        id: 'wordpress',
        name: 'WordPress',
        category: 'web',
        description: 'CMS for creating websites and blogs',
        icon: '📝',
        ports: ['80:80', '443:443'],
        requires: ['database'],
        price: 15
    },
    {
        id: 'odoo',
        name: 'Odoo',
        category: 'web',
        description: 'Open source ERP and CRM',
        icon: '📊',
        ports: ['8069:8069'],
        requires: ['database'],
        price: 25
    },
    {
        id: 'mysql',
        name: 'MySQL',
        category: 'database',
        description: 'Relational database management',
        icon: '🗄️',
        ports: ['3306:3306'],
        price: 20
    },
    {
        id: 'mongodb',
        name: 'MongoDB',
        category: 'database',
        description: 'NoSQL database',
        icon: '📊',
        ports: ['27017:27017'],
        price: 18
    },
    {
        id: 'grafana',
        name: 'Grafana',
        category: 'monitoring',
        description: 'Monitoring dashboards',
        icon: '📈',
        ports: ['3000:3000'],
        price: 12
    },
    {
        id: 'prometheus',
        name: 'Prometheus',
        category: 'monitoring',
        description: 'Monitoring and alerting system',
        icon: '⚠️',
        ports: ['9090:9090'],
        price: 10
    },
    {
        id: 'gitlab',
        name: 'GitLab',
        category: 'development',
        description: 'Complete DevOps platform',
        icon: '💻',
        ports: ['80:80', '443:443', '22:22'],
        price: 30
    },
    {
        id: 'nextcloud',
        name: 'NextCloud',
        category: 'collaboration',
        description: 'Private cloud and collaboration',
        icon: '☁️',
        ports: ['80:80', '443:443'],
        price: 25
    },
    {
        id: 'mattermost',
        name: 'Mattermost',
        category: 'collaboration',
        description: 'Team messaging platform',
        icon: '💬',
        ports: ['8065:8065'],
        price: 20
    },
    {
        id: 'owncloud',
        name: 'OwnCloud',
        category: 'collaboration',
        description: 'File sharing and collaboration',
        icon: '📁',
        ports: ['80:80', '443:443'],
        price: 22
    },
    {
        id: 'onlyoffice',
        name: 'OnlyOffice',
        category: 'collaboration',
        description: 'Office suite for collaboration',
        icon: '📄',
        ports: ['80:80', '443:443'],
        price: 28
    },
    {
        id: 'moodle',
        name: 'Moodle',
        category: 'web',
        description: 'Learning management system',
        icon: '🎓',
        ports: ['80:80', '443:443'],
        requires: ['database'],
        price: 24
    },
    {
        id: 'prestashop',
        name: 'PrestaShop',
        category: 'web',
        description: 'E-commerce platform',
        icon: '🛒',
        ports: ['80:80', '443:443'],
        requires: ['database'],
        price: 26
    },
    {
        id: 'openldap',
        name: 'OpenLDAP',
        category: 'development',
        description: 'Directory service',
        icon: '🔐',
        ports: ['389:389', '636:636'],
        price: 15
    }
];


export default APPS_CATALOG;