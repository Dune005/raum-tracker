<?php
/**
 * GET /api/v1/display/current
 * Liefert aktuelle Auslastung für das Display (öffentlich)
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
    
    // Hole neuesten Occupancy Snapshot
    $queryOcc = "SELECT ts, people_estimate, level, noise_db, method 
                 FROM occupancy_snapshot 
                 WHERE space_id = :space_id 
                 ORDER BY ts DESC 
                 LIMIT 1";
    
    $stmtOcc = $db->prepare($queryOcc);
    $stmtOcc->bindParam(':space_id', $spaceId);
    $stmtOcc->execute();
    $snapshot = $stmtOcc->fetch();
    
    // Wenn kein Snapshot vorhanden, erstelle Default
    if (!$snapshot) {
        $snapshot = [
            'ts' => date('Y-m-d H:i:s'),
            'people_estimate' => null,
            'level' => 'LOW',
            'noise_db' => null,
            'method' => 'FLOW_ONLY'
        ];
    }
    
    // Mapping Level zu Display-Daten
    $displayConfig = [
        'LOW' => [
            'text' => 'Wenig los',
            'icon' => 'low_occupancy',
            'color' => 'green'
        ],
        'MEDIUM' => [
            'text' => 'Mittlere Auslastung',
            'icon' => 'medium_occupancy',
            'color' => 'orange'
        ],
        'HIGH' => [
            'text' => 'Viel los',
            'icon' => 'high_occupancy',
            'color' => 'red'
        ]
    ];
    
    $config = $displayConfig[$snapshot['level']] ?? $displayConfig['LOW'];
    
    Response::success([
        'space_id' => $spaceId,
        'space_name' => $space['name'],
        'timestamp' => $snapshot['ts'],
        'level' => $snapshot['level'],
        'people_estimate' => $snapshot['people_estimate'],
        'noise_db' => $snapshot['noise_db'],
        'method' => $snapshot['method'],
        'display_text' => $config['text'],
        'icon' => $config['icon'],
        'color' => $config['color']
    ]);
    
} catch (PDOException $e) {
    error_log("Display-Current Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}
