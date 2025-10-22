<?php
/**
 * GET /api/v1/flow/events
 * Liefert Flow-Events (IN/OUT) für einen Zeitraum (öffentlich)
 */

// Query-Parameter
$spaceId = $_GET['space_id'] ?? null;
$from = $_GET['from'] ?? date('Y-m-d 00:00:00');
$to = $_GET['to'] ?? date('Y-m-d H:i:s');
$limit = isset($_GET['limit']) ? min((int)$_GET['limit'], 1000) : 100;

if (!$spaceId) {
    Response::badRequest('Missing required parameter: space_id');
}

try {
    // Hole Space-Info
    $querySpace = "SELECT id, name FROM space WHERE id = :space_id";
    $stmtSpace = $db->prepare($querySpace);
    $stmtSpace->bindParam(':space_id', $spaceId);
    $stmtSpace->execute();
    $space = $stmtSpace->fetch();
    
    if (!$space) {
        Response::notFound('Space not found');
    }
    
    // Count total events
    $queryCount = "SELECT COUNT(*) as total 
                   FROM flow_event 
                   WHERE space_id = :space_id 
                   AND ts BETWEEN :from AND :to";
    $stmtCount = $db->prepare($queryCount);
    $stmtCount->bindParam(':space_id', $spaceId);
    $stmtCount->bindParam(':from', $from);
    $stmtCount->bindParam(':to', $to);
    $stmtCount->execute();
    $countResult = $stmtCount->fetch();
    
    // Hole Events
    $query = "SELECT 
                fe.id,
                fe.ts as timestamp,
                fe.direction,
                fe.confidence,
                fe.duration_ms,
                g.name as gate_name
              FROM flow_event fe
              LEFT JOIN gate g ON fe.gate_id = g.id
              WHERE fe.space_id = :space_id 
              AND fe.ts BETWEEN :from AND :to
              ORDER BY fe.ts DESC
              LIMIT :limit";
    
    $stmt = $db->prepare($query);
    $stmt->bindParam(':space_id', $spaceId);
    $stmt->bindParam(':from', $from);
    $stmt->bindParam(':to', $to);
    $stmt->bindParam(':limit', $limit, PDO::PARAM_INT);
    $stmt->execute();
    
    $events = [];
    while ($row = $stmt->fetch()) {
        $events[] = [
            'id' => (int)$row['id'],
            'timestamp' => $row['timestamp'],
            'direction' => $row['direction'],
            'gate_name' => $row['gate_name'],
            'confidence' => $row['confidence'] ? (float)$row['confidence'] : null,
            'duration_ms' => $row['duration_ms'] ? (int)$row['duration_ms'] : null
        ];
    }
    
    Response::success([
        'space_id' => $spaceId,
        'from' => $from,
        'to' => $to,
        'total_events' => (int)$countResult['total'],
        'events' => $events
    ]);
    
} catch (PDOException $e) {
    error_log("Flow-Events Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}
