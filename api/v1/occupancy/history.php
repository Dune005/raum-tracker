<?php
/**
 * GET /api/v1/occupancy/history
 * Liefert historische Auslastungsdaten (öffentlich)
 */

// Query-Parameter
$spaceId = $_GET['space_id'] ?? null;
$from = $_GET['from'] ?? date('Y-m-d H:i:s', strtotime('-30 days'));
$to = $_GET['to'] ?? date('Y-m-d H:i:s');
$interval = $_GET['interval'] ?? '15min';

if (!$spaceId) {
    Response::badRequest('Missing required parameter: space_id');
}

// Validiere Interval
$validIntervals = ['5min', '15min', '1hour', '1day'];
if (!in_array($interval, $validIntervals)) {
    Response::badRequest('Invalid interval. Allowed: ' . implode(', ', $validIntervals));
}

// Mapping Interval zu MySQL
$intervalMap = [
    '5min' => 5,
    '15min' => 15,
    '1hour' => 60,
    '1day' => 1440
];
$intervalMinutes = $intervalMap[$interval];

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
    
    // Hole aggregierte Daten
    $query = "SELECT 
                DATE_FORMAT(ts, '%Y-%m-%d %H:%i:00') as timestamp,
                AVG(people_estimate) as people_estimate_avg,
                AVG(noise_db) as noise_db_avg,
                AVG(motion_count) as motion_count_avg,
                MAX(CASE WHEN level = 'HIGH' THEN 3 WHEN level = 'MEDIUM' THEN 2 ELSE 1 END) as max_level
              FROM occupancy_snapshot 
              WHERE space_id = :space_id 
              AND ts BETWEEN :from AND :to
              GROUP BY FLOOR(UNIX_TIMESTAMP(ts) / (:interval * 60))
              ORDER BY ts ASC
              LIMIT 1000";
    
    $stmt = $db->prepare($query);
    $stmt->bindParam(':space_id', $spaceId);
    $stmt->bindParam(':from', $from);
    $stmt->bindParam(':to', $to);
    $stmt->bindParam(':interval', $intervalMinutes, PDO::PARAM_INT);
    $stmt->execute();
    
    $datapoints = [];
    while ($row = $stmt->fetch()) {
        // Konvertiere max_level zurück zu String
        $levelMap = [1 => 'LOW', 2 => 'MEDIUM', 3 => 'HIGH'];
        $level = $levelMap[$row['max_level']] ?? 'LOW';
        
        $datapoints[] = [
            'timestamp' => $row['timestamp'],
            'level' => $level,
            'people_estimate_avg' => $row['people_estimate_avg'] ? round($row['people_estimate_avg'], 1) : null,
            'noise_db_avg' => $row['noise_db_avg'] ? round($row['noise_db_avg'], 1) : null,
            'motion_count_avg' => $row['motion_count_avg'] ? round($row['motion_count_avg'], 1) : null
        ];
    }
    
    Response::success([
        'space_id' => $spaceId,
        'from' => $from,
        'to' => $to,
        'interval' => $interval,
        'datapoints' => $datapoints
    ]);
    
} catch (PDOException $e) {
    error_log("Occupancy-History Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}
