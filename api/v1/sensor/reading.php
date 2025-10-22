<?php
/**
 * POST /api/v1/sensor/reading
 * Speichert eine Sensor-Messung
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
if (!isset($input['sensor_id']) || !isset($input['timestamp'])) {
    Response::badRequest('Missing required fields: sensor_id, timestamp');
}

$sensorId = $input['sensor_id'];
$timestamp = $input['timestamp'];
$valueNum = $input['value_num'] ?? null;
$valueText = $input['value_text'] ?? null;
$valueJson = isset($input['value_json']) ? json_encode($input['value_json']) : null;
$quality = $input['quality'] ?? 'OK';

// Validiere Quality
$allowedQualities = ['OK', 'NOISE', 'DROPPED'];
if (!in_array($quality, $allowedQualities)) {
    Response::badRequest('Invalid quality. Allowed: ' . implode(', ', $allowedQualities));
}

// Prüfe ob Sensor existiert und zum Device gehört
try {
    $query = "SELECT s.id, s.device_id, s.type, s.is_active 
              FROM sensor s
              WHERE s.id = :sensor_id AND s.is_active = 1";
    
    $stmt = $db->prepare($query);
    $stmt->bindParam(':sensor_id', $sensorId);
    $stmt->execute();
    
    $sensor = $stmt->fetch();
    
    if (!$sensor) {
        Response::notFound('Sensor not found or inactive');
    }
    
    // Prüfe ob Sensor zum authentifizierten Device gehört
    if ($sensor['device_id'] !== $device['id']) {
        Response::unauthorized('Sensor does not belong to this device');
    }
    
} catch (PDOException $e) {
    error_log("Sensor-Query Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}

// Speichere Reading
try {
    $query = "INSERT INTO reading 
              (sensor_id, ts, value_num, value_text, value_json, quality) 
              VALUES 
              (:sensor_id, :ts, :value_num, :value_text, :value_json, :quality)";
    
    $stmt = $db->prepare($query);
    $stmt->bindParam(':sensor_id', $sensorId);
    $stmt->bindParam(':ts', $timestamp);
    $stmt->bindParam(':value_num', $valueNum);
    $stmt->bindParam(':value_text', $valueText);
    $stmt->bindParam(':value_json', $valueJson);
    $stmt->bindParam(':quality', $quality);
    
    $stmt->execute();
    
    $readingId = $db->lastInsertId();
    
    Response::created([
        'reading_id' => (int)$readingId,
        'sensor_type' => $sensor['type']
    ], 'Messung erfolgreich gespeichert');
    
} catch (PDOException $e) {
    error_log("Reading-Insert Fehler: " . $e->getMessage());
    Response::serverError('Failed to save reading');
}
