<?php
/**
 * POST /api/v1/device/heartbeat
 * Speichert ein Heartbeat (Lebenszeichen) vom Device
 * 
 * Authentifizierung: API-Key erforderlich
 */

// Authentifizierung
$auth = new Auth($db);
$device = $auth->validateApiKey();

if (!$device) {
    Response::unauthorized();
}

// Request-Body einlesen
$input = json_decode(file_get_contents('php://input'), true);

// Validierung
if (!isset($input['device_id']) || !isset($input['timestamp'])) {
    Response::badRequest('Missing required fields: device_id, timestamp');
}

$deviceId = $input['device_id'];
$timestamp = $input['timestamp'];
$ip = $input['ip'] ?? null;
$rssi = $input['rssi'] ?? null;
$metrics = isset($input['metrics']) ? json_encode($input['metrics']) : null;

// Prüfe ob device_id zum authentifizierten Device passt
if ($deviceId !== $device['id']) {
    Response::unauthorized('Device ID mismatch');
}

// Speichere Heartbeat
try {
    $query = "INSERT INTO heartbeat 
              (device_id, ts, ip, rssi, metrics) 
              VALUES 
              (:device_id, :ts, :ip, :rssi, :metrics)";
    
    $stmt = $db->prepare($query);
    $stmt->bindParam(':device_id', $deviceId);
    $stmt->bindParam(':ts', $timestamp);
    $stmt->bindParam(':ip', $ip);
    $stmt->bindParam(':rssi', $rssi, PDO::PARAM_INT);
    $stmt->bindParam(':metrics', $metrics);
    
    $stmt->execute();
    
    $heartbeatId = $db->lastInsertId();
    
    // Hole Device-Status
    $deviceStatus = $device['is_active'] ? 'active' : 'inactive';
    
    Response::success([
        'heartbeat_id' => (int)$heartbeatId,
        'device_status' => $deviceStatus
    ], 'Heartbeat empfangen');
    
} catch (PDOException $e) {
    error_log("Heartbeat-Insert Fehler: " . $e->getMessage());
    Response::serverError('Failed to save heartbeat');
}
