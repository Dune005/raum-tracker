<?php
/**
 * GET /api/v1/statistics/today
 * Liefert Statistiken für heute (öffentlich)
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
    
    // Netto-Personen heute
    $queryNet = "SELECT net_people FROM v_people_net_today WHERE space_id = :space_id";
    $stmtNet = $db->prepare($queryNet);
    $stmtNet->bindParam(':space_id', $spaceId);
    $stmtNet->execute();
    $netResult = $stmtNet->fetch();
    $netPeopleToday = $netResult ? (int)$netResult['net_people'] : 0;
    
    // Total Entries/Exits heute
    $queryFlow = "SELECT 
                    SUM(CASE WHEN direction = 'IN' THEN 1 ELSE 0 END) as total_entries,
                    SUM(CASE WHEN direction = 'OUT' THEN 1 ELSE 0 END) as total_exits
                  FROM flow_event 
                  WHERE space_id = :space_id 
                  AND DATE(ts) = CURDATE()";
    $stmtFlow = $db->prepare($queryFlow);
    $stmtFlow->bindParam(':space_id', $spaceId);
    $stmtFlow->execute();
    $flowResult = $stmtFlow->fetch();
    
    // Peak Occupancy heute - berechnet aus Flow-Events
    // Hole alle Flow-Events von heute
    $queryFlowEvents = "SELECT ts, direction 
                        FROM flow_event 
                        WHERE space_id = :space_id 
                        AND DATE(ts) = CURDATE()
                        ORDER BY ts ASC";
    $stmtFlowEvents = $db->prepare($queryFlowEvents);
    $stmtFlowEvents->bindParam(':space_id', $spaceId);
    $stmtFlowEvents->execute();
    $flowEvents = $stmtFlowEvents->fetchAll();
    
    // Berechne Peak-Zeit aus Flow-Events
    $peakResult = null;
    if (!empty($flowEvents)) {
        $maxPeople = 0;
        $maxTimestamp = null;
        $currentPeople = 0;
        
        foreach ($flowEvents as $event) {
            if ($event['direction'] === 'IN') {
                $currentPeople++;
            } else if ($event['direction'] === 'OUT') {
                $currentPeople--;
            }
            
            // Negative Werte vermeiden
            $currentPeople = max(0, $currentPeople);
            
            // Neuer Peak?
            if ($currentPeople > $maxPeople) {
                $maxPeople = $currentPeople;
                $maxTimestamp = $event['ts'];
            }
        }
        
        // Wenn ein Peak gefunden wurde
        if ($maxPeople > 0 && $maxTimestamp) {
            // Level basierend auf Personenanzahl bestimmen
            $peakLevel = 'LOW';
            if ($maxPeople >= 20) {
                $peakLevel = 'HIGH';
            } elseif ($maxPeople >= 5) {
                $peakLevel = 'MEDIUM';
            }
            
            $peakResult = [
                'ts' => $maxTimestamp,
                'people_estimate' => $maxPeople,
                'level' => $peakLevel
            ];
        }
    }
    
    // Durchschnittliche Lautstärke heute
    $queryNoise = "SELECT AVG(noise_db) as avg_noise 
                   FROM occupancy_snapshot 
                   WHERE space_id = :space_id 
                   AND DATE(ts) = CURDATE()";
    $stmtNoise = $db->prepare($queryNoise);
    $stmtNoise->bindParam(':space_id', $spaceId);
    $stmtNoise->execute();
    $noiseResult = $stmtNoise->fetch();
    
    // Busieste Stunde heute
    $queryBusiest = "SELECT 
                        HOUR(ts) as hour,
                        AVG(people_estimate) as avg_people
                     FROM occupancy_snapshot 
                     WHERE space_id = :space_id 
                     AND DATE(ts) = CURDATE()
                     GROUP BY HOUR(ts)
                     ORDER BY avg_people DESC 
                     LIMIT 1";
    $stmtBusiest = $db->prepare($queryBusiest);
    $stmtBusiest->bindParam(':space_id', $spaceId);
    $stmtBusiest->execute();
    $busiestResult = $stmtBusiest->fetch();
    
    // Aktuelles Level
    $queryCurrent = "SELECT level FROM occupancy_snapshot 
                     WHERE space_id = :space_id 
                     ORDER BY ts DESC LIMIT 1";
    $stmtCurrent = $db->prepare($queryCurrent);
    $stmtCurrent->bindParam(':space_id', $spaceId);
    $stmtCurrent->execute();
    $currentResult = $stmtCurrent->fetch();
    
    Response::success([
        'space_id' => $spaceId,
        'date' => date('Y-m-d'),
        'net_people_today' => $netPeopleToday,
        'total_entries' => (int)($flowResult['total_entries'] ?? 0),
        'total_exits' => (int)($flowResult['total_exits'] ?? 0),
        'peak_occupancy' => $peakResult ? [
            'time' => date('H:i', strtotime($peakResult['ts'])),
            'people_estimate' => (int)$peakResult['people_estimate'],
            'level' => $peakResult['level']
        ] : null,
        'avg_noise_db' => $noiseResult['avg_noise'] ? round($noiseResult['avg_noise'], 1) : null,
        'busiest_hour' => $busiestResult ? [
            'hour' => (int)$busiestResult['hour'],
            'avg_people' => round($busiestResult['avg_people'], 0)
        ] : null,
        'current_level' => $currentResult['level'] ?? 'LOW'
    ]);
    
} catch (PDOException $e) {
    error_log("Statistics-Today Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}
