<?php
/**
 * POST /api/v1/display/ack
 * Display bestätigt Anzeige eines Frames (öffentlich)
 */

// Request-Body einlesen
$input = json_decode(file_get_contents('php://input'), true);

// Validierung
if (!isset($input['device_id']) || !isset($input['timestamp']) || !isset($input['status'])) {
    Response::badRequest('Missing required fields: device_id, timestamp, status');
}

$deviceId = $input['device_id'];
$timestamp = $input['timestamp'];
$frameId = $input['frame_id'] ?? null;
$status = strtoupper($input['status']);

// Validiere Status
if (!in_array($status, ['ACK', 'FAILED'])) {
    Response::badRequest('Invalid status. Allowed: ACK, FAILED');
}

try {
    // Prüfe ob Device existiert und ein Display ist
    $queryDevice = "SELECT id, type FROM device WHERE id = :device_id";
    $stmtDevice = $db->prepare($queryDevice);
    $stmtDevice->bindParam(':device_id', $deviceId);
    $stmtDevice->execute();
    $device = $stmtDevice->fetch();
    
    if (!$device) {
        Response::notFound('Device not found');
    }
    
    if ($device['type'] !== 'DISPLAY') {
        Response::badRequest('Device is not a display');
    }
    
    // Optional: Update Frame-Status falls frame_id angegeben
    if ($frameId) {
        $queryUpdate = "UPDATE display_frame 
                        SET status = :status 
                        WHERE id = :frame_id AND device_id = :device_id";
        $stmtUpdate = $db->prepare($queryUpdate);
        $stmtUpdate->bindParam(':status', $status);
        $stmtUpdate->bindParam(':frame_id', $frameId, PDO::PARAM_INT);
        $stmtUpdate->bindParam(':device_id', $deviceId);
        $stmtUpdate->execute();
    }
    
    // Update last_seen des Displays
    $queryLastSeen = "UPDATE device SET last_seen = CURRENT_TIMESTAMP WHERE id = :device_id";
    $stmtLastSeen = $db->prepare($queryLastSeen);
    $stmtLastSeen->bindParam(':device_id', $deviceId);
    $stmtLastSeen->execute();
    
    Response::success([], 'Bestätigung gespeichert');
    
} catch (PDOException $e) {
    error_log("Display-ACK Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}
