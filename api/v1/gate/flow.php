<?php
/**
 * POST /api/v1/gate/flow
 * Speichert ein Flow-Event (IN/OUT)
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
if (!isset($input['gate_id']) || !isset($input['timestamp']) || !isset($input['direction'])) {
    Response::badRequest('Missing required fields: gate_id, timestamp, direction');
}

$gateId = $input['gate_id'];
$timestamp = $input['timestamp'];
$direction = strtoupper($input['direction']);
$confidence = $input['confidence'] ?? null;
$durationMs = $input['duration_ms'] ?? null;
$rawRefs = isset($input['raw_refs']) ? json_encode($input['raw_refs']) : null;

// Validiere Direction
if (!in_array($direction, ['IN', 'OUT'])) {
    Response::badRequest('Invalid direction. Allowed: IN, OUT');
}

// Prüfe ob Gate existiert und zum Space des Devices gehört
try {
    $query = "SELECT g.id, g.space_id, g.name, g.sensor_a_id, g.sensor_b_id
              FROM gate g
              WHERE g.id = :gate_id";
    
    $stmt = $db->prepare($query);
    $stmt->bindParam(':gate_id', $gateId);
    $stmt->execute();
    
    $gate = $stmt->fetch();
    
    if (!$gate) {
        Response::notFound('Gate not found');
    }
    
    // Prüfe ob Gate zum selben Space gehört wie das Device
    if ($gate['space_id'] !== $device['space_id']) {
        Response::unauthorized('Gate does not belong to device space');
    }
    
    $spaceId = $gate['space_id'];
    
} catch (PDOException $e) {
    error_log("Gate-Query Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}

// Speichere Flow-Event
try {
    $query = "INSERT INTO flow_event 
              (gate_id, space_id, ts, direction, confidence, duration_ms, raw_refs) 
              VALUES 
              (:gate_id, :space_id, :ts, :direction, :confidence, :duration_ms, :raw_refs)";
    
    $stmt = $db->prepare($query);
    $stmt->bindParam(':gate_id', $gateId);
    $stmt->bindParam(':space_id', $spaceId);
    $stmt->bindParam(':ts', $timestamp);
    $stmt->bindParam(':direction', $direction);
    $stmt->bindParam(':confidence', $confidence);
    $stmt->bindParam(':duration_ms', $durationMs, PDO::PARAM_INT);
    $stmt->bindParam(':raw_refs', $rawRefs);
    
    $stmt->execute();
    
    $flowEventId = $db->lastInsertId();
    
    // Berechne aktuellen Netto-Personensaldo für heute
    $queryNet = "SELECT net_people FROM v_people_net_today WHERE space_id = :space_id";
    $stmtNet = $db->prepare($queryNet);
    $stmtNet->bindParam(':space_id', $spaceId);
    $stmtNet->execute();
    $netResult = $stmtNet->fetch();
    $currentNetPeople = $netResult ? (int)$netResult['net_people'] : 0;
    
    Response::created([
        'flow_event_id' => (int)$flowEventId,
        'current_net_people' => $currentNetPeople
    ], 'Flow-Event erfolgreich gespeichert');
    
} catch (PDOException $e) {
    error_log("Flow-Event-Insert Fehler: " . $e->getMessage());
    Response::serverError('Failed to save flow event');
}
