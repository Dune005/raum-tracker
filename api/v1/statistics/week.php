<?php
/**
 * GET /api/v1/statistics/week
 * Liefert Wochenstatistiken (öffentlich)
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
    
    // Tägliche Statistiken der letzten 7 Tage
    $query = "SELECT 
                DATE(ts) as date,
                AVG(people_estimate) as avg_occupancy,
                MAX(people_estimate) as peak_occupancy,
                AVG(noise_db) as avg_noise_db
              FROM occupancy_snapshot 
              WHERE space_id = :space_id 
              AND ts >= DATE_SUB(CURDATE(), INTERVAL 7 DAY)
              GROUP BY DATE(ts)
              ORDER BY date DESC";
    
    $stmt = $db->prepare($query);
    $stmt->bindParam(':space_id', $spaceId);
    $stmt->execute();
    
    $dailyStats = [];
    while ($row = $stmt->fetch()) {
        $date = $row['date'];
        
        // Total Entries für diesen Tag
        $queryEntries = "SELECT COUNT(*) as total_entries 
                         FROM flow_event 
                         WHERE space_id = :space_id 
                         AND direction = 'IN'
                         AND DATE(ts) = :date";
        $stmtEntries = $db->prepare($queryEntries);
        $stmtEntries->bindParam(':space_id', $spaceId);
        $stmtEntries->bindParam(':date', $date);
        $stmtEntries->execute();
        $entriesResult = $stmtEntries->fetch();
        
        // Busieste Stunde für diesen Tag
        $queryHour = "SELECT HOUR(ts) as hour
                      FROM occupancy_snapshot 
                      WHERE space_id = :space_id 
                      AND DATE(ts) = :date
                      GROUP BY HOUR(ts)
                      ORDER BY AVG(people_estimate) DESC 
                      LIMIT 1";
        $stmtHour = $db->prepare($queryHour);
        $stmtHour->bindParam(':space_id', $spaceId);
        $stmtHour->bindParam(':date', $date);
        $stmtHour->execute();
        $hourResult = $stmtHour->fetch();
        
        $dailyStats[] = [
            'date' => $date,
            'avg_occupancy' => $row['avg_occupancy'] ? round($row['avg_occupancy'], 0) : 0,
            'peak_occupancy' => (int)($row['peak_occupancy'] ?? 0),
            'total_entries' => (int)($entriesResult['total_entries'] ?? 0),
            'avg_noise_db' => $row['avg_noise_db'] ? round($row['avg_noise_db'], 1) : null,
            'busiest_hour' => $hourResult ? (int)$hourResult['hour'] : null
        ];
    }
    
    // Wochen-Zusammenfassung
    $avgDailyEntries = 0;
    $mostBusyDay = null;
    $leastBusyDay = null;
    $maxOccupancy = 0;
    $minOccupancy = PHP_INT_MAX;
    $hourCount = [];
    
    foreach ($dailyStats as $stat) {
        $avgDailyEntries += $stat['total_entries'];
        
        if ($stat['peak_occupancy'] > $maxOccupancy) {
            $maxOccupancy = $stat['peak_occupancy'];
            $mostBusyDay = $stat['date'];
        }
        
        if ($stat['peak_occupancy'] < $minOccupancy && $stat['peak_occupancy'] > 0) {
            $minOccupancy = $stat['peak_occupancy'];
            $leastBusyDay = $stat['date'];
        }
        
        if ($stat['busiest_hour'] !== null) {
            $hourCount[$stat['busiest_hour']] = ($hourCount[$stat['busiest_hour']] ?? 0) + 1;
        }
    }
    
    $avgDailyEntries = count($dailyStats) > 0 ? round($avgDailyEntries / count($dailyStats), 0) : 0;
    $avgPeakHour = !empty($hourCount) ? array_search(max($hourCount), $hourCount) : null;
    
    Response::success([
        'space_id' => $spaceId,
        'from' => date('Y-m-d', strtotime('-7 days')),
        'to' => date('Y-m-d'),
        'daily_stats' => $dailyStats,
        'week_summary' => [
            'avg_daily_entries' => $avgDailyEntries,
            'most_busy_day' => $mostBusyDay,
            'least_busy_day' => $leastBusyDay,
            'avg_peak_hour' => $avgPeakHour
        ]
    ]);
    
} catch (PDOException $e) {
    error_log("Statistics-Week Fehler: " . $e->getMessage());
    Response::serverError('Database error');
}
