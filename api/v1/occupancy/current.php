<?php
/**
 * GET /api/v1/occupancy/current
 * Liefert aktuelle Auslastung mit Details (öffentlich)
 */

// Query-Parameter
$spaceId = $_GET['space_id'] ?? null;

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
    
    // Hole neuesten Occupancy Snapshot (MIT DRIFT-KORREKTUR-DATEN!)
    $queryOcc = "SELECT ts, people_estimate, level, noise_db, motion_count, method, window_seconds,
                        counter_raw, display_count, drift_corrected, scale_applied
                 FROM occupancy_snapshot 
                 WHERE space_id = :space_id 
                 ORDER BY ts DESC 
                 LIMIT 1";
    
    $stmtOcc = $db->prepare($queryOcc);
    $stmtOcc->bindParam(':space_id', $spaceId);
    $stmtOcc->execute();
    $snapshot = $stmtOcc->fetch();
    
    // Hole Netto-Personenzahl heute
    $queryNet = "SELECT net_people FROM v_people_net_today WHERE space_id = :space_id";
    $stmtNet = $db->prepare($queryNet);
    $stmtNet->bindParam(':space_id', $spaceId);
    $stmtNet->execute();
    $netResult = $stmtNet->fetch();
    $netPeopleToday = $netResult ? (int)$netResult['net_people'] : 0;
    
    // Berechne Trend (vereinfacht: vergleiche mit Snapshot vor 15 Minuten)
    $queryTrend = "SELECT people_estimate 
                   FROM occupancy_snapshot 
                   WHERE space_id = :space_id 
                   AND ts <= DATE_SUB(NOW(), INTERVAL 15 MINUTE)
                   ORDER BY ts DESC 
                   LIMIT 1";
    $stmtTrend = $db->prepare($queryTrend);
    $stmtTrend->bindParam(':space_id', $spaceId);
    $stmtTrend->execute();
    $trendSnapshot = $stmtTrend->fetch();
    
    $trend = 'stable';
    if ($snapshot && $trendSnapshot) {
        if ($snapshot['people_estimate'] > $trendSnapshot['people_estimate']) {
            $trend = 'increasing';
        } elseif ($snapshot['people_estimate'] < $trendSnapshot['people_estimate']) {
            $trend = 'decreasing';
        }
    }
    
    if (!$snapshot) {
        Response::success([
            'space_id' => $spaceId,
            'space_name' => $space['name'],
            'timestamp' => date('Y-m-d H:i:s'),
            'level' => 'LOW',
            'people_estimate' => null,
            'display_count' => null,
            'counter_raw' => null,
            'drift_corrected' => false,
            'scale_applied' => false,
            'noise_db' => null,
            'motion_count' => null,
            'method' => 'NONE',
            'window_seconds' => 120,
            'net_people_today' => $netPeopleToday,
            'trend' => 'stable'
        ]);
    }
    
    Response::success([
        'space_id' => $spaceId,
        'space_name' => $space['name'],
        'timestamp' => $snapshot['ts'],
        'level' => $snapshot['level'],
        'people_estimate' => $snapshot['people_estimate'],  // Legacy-Support
        'display_count' => $snapshot['display_count'] ?? $snapshot['people_estimate'],  // Korrigierter Wert
        'counter_raw' => $snapshot['counter_raw'] ?? null,  // Debugging
        'drift_corrected' => (bool)($snapshot['drift_corrected'] ?? false),
        'scale_applied' => (bool)($snapshot['scale_applied'] ?? false),
        'noise_db' => $snapshot['noise_db'],
        'motion_count' => $snapshot['motion_count'],
        'method' => $snapshot['method'],
        'window_seconds' => $snapshot['window_seconds'],
        'net_people_today' => $netPeopleToday,
        'trend' => $trend
    ]);
    
} catch (PDOException $e) {
    error_log("Occupancy-Current Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}
