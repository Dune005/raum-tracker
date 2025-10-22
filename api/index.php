<?php
/**
 * API Haupteinstiegspunkt
 * Raum-Tracker API v1
 * 
 * Base URL: https://corner.klaus-klebband.ch/api/
 */

// Error Reporting (für Development)
error_reporting(E_ALL);
ini_set('display_errors', 0); // Für Production auf 0 setzen
ini_set('log_errors', 1);

// Timezone setzen
date_default_timezone_set('Europe/Zurich');

// CORS Headers
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, X-API-Key');

// Handle OPTIONS preflight
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

// Includes
require_once __DIR__ . '/config/Database.php';
require_once __DIR__ . '/includes/Auth.php';
require_once __DIR__ . '/includes/Response.php';

// Datenbank-Verbindung
try {
    $database = new Database();
    $db = $database->connect();
} catch (Exception $e) {
    Response::serverError('Database connection failed');
}

// Request-Methode
$method = $_SERVER['REQUEST_METHOD'];

// Path aus URL extrahieren
$path = $_GET['path'] ?? '';
$path = trim($path, '/');
$segments = explode('/', $path);

// Version prüfen (v1)
$version = $segments[0] ?? '';
if ($version !== 'v1') {
    Response::notFound('API version not found. Use /api/v1/');
}

// Endpoint ermitteln
$endpoint = $segments[1] ?? '';
$subEndpoint = $segments[2] ?? '';

// Router
switch ($endpoint) {
    case 'sensor':
        if ($subEndpoint === 'reading' && $method === 'POST') {
            require_once __DIR__ . '/v1/sensor/reading.php';
        } else {
            Response::notFound('Endpoint not found');
        }
        break;

    case 'gate':
        if ($subEndpoint === 'flow' && $method === 'POST') {
            require_once __DIR__ . '/v1/gate/flow.php';
        } else {
            Response::notFound('Endpoint not found');
        }
        break;

    case 'device':
        if ($subEndpoint === 'heartbeat' && $method === 'POST') {
            require_once __DIR__ . '/v1/device/heartbeat.php';
        } else {
            Response::notFound('Endpoint not found');
        }
        break;

    case 'display':
        if ($subEndpoint === 'current' && $method === 'GET') {
            require_once __DIR__ . '/v1/display/current.php';
        } elseif ($subEndpoint === 'ack' && $method === 'POST') {
            require_once __DIR__ . '/v1/display/ack.php';
        } else {
            Response::notFound('Endpoint not found');
        }
        break;

    case 'occupancy':
        if ($subEndpoint === 'current' && $method === 'GET') {
            require_once __DIR__ . '/v1/occupancy/current.php';
        } elseif ($subEndpoint === 'history' && $method === 'GET') {
            require_once __DIR__ . '/v1/occupancy/history.php';
        } else {
            Response::notFound('Endpoint not found');
        }
        break;

    case 'statistics':
        if ($subEndpoint === 'today' && $method === 'GET') {
            require_once __DIR__ . '/v1/statistics/today.php';
        } elseif ($subEndpoint === 'week' && $method === 'GET') {
            require_once __DIR__ . '/v1/statistics/week.php';
        } else {
            Response::notFound('Endpoint not found');
        }
        break;

    case 'flow':
        if ($subEndpoint === 'events' && $method === 'GET') {
            require_once __DIR__ . '/v1/flow/events.php';
        } else {
            Response::notFound('Endpoint not found');
        }
        break;

    case '':
        // API Info
        Response::success([
            'name' => 'Raum-Tracker API',
            'version' => 'v1',
            'status' => 'running',
            'documentation' => 'https://corner.klaus-klebband.ch/api/docs'
        ]);
        break;

    default:
        Response::notFound('Endpoint not found');
}
