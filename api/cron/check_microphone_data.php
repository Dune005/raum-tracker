<?php
/**
 * Test-Skript: Prüft Mikrofon-Daten in der reading-Tabelle
 */

require_once __DIR__ . '/../config/Database.php';

date_default_timezone_set('Europe/Zurich');

try {
    $database = new Database();
    $db = $database->getConnection();
    
    echo "=== MIKROFON-DATEN CHECK ===\n\n";
    
    // 1. Alle Mikrofon-Sensoren finden
    $querySensors = "SELECT s.id, s.name, s.type, d.name as device_name, d.space_id
                     FROM sensor s
                     JOIN device d ON s.device_id = d.id
                     WHERE s.type = 'MICROPHONE'";
    $stmtSensors = $db->query($querySensors);
    $sensors = $stmtSensors->fetchAll(PDO::FETCH_ASSOC);
    
    if (empty($sensors)) {
        echo "❌ Keine Mikrofon-Sensoren gefunden!\n";
        exit(1);
    }
    
    echo "✅ Gefundene Mikrofon-Sensoren:\n";
    foreach ($sensors as $sensor) {
        echo "  - {$sensor['name']} (ID: {$sensor['id']}) an Device: {$sensor['device_name']}\n";
    }
    echo "\n";
    
    // 2. Letzte Readings prüfen
    foreach ($sensors as $sensor) {
        $sensorId = $sensor['id'];
        
        echo "--- Sensor: {$sensor['name']} ---\n";
        
        // Gesamtanzahl Readings
        $queryCount = "SELECT COUNT(*) as total FROM reading WHERE sensor_id = :sensor_id";
        $stmtCount = $db->prepare($queryCount);
        $stmtCount->bindParam(':sensor_id', $sensorId);
        $stmtCount->execute();
        $countResult = $stmtCount->fetch(PDO::FETCH_ASSOC);
        echo "Total Readings: {$countResult['total']}\n";
        
        // Letzte 5 Readings
        $queryLast = "SELECT ts, value_num, value_str 
                      FROM reading 
                      WHERE sensor_id = :sensor_id 
                      ORDER BY ts DESC 
                      LIMIT 5";
        $stmtLast = $db->prepare($queryLast);
        $stmtLast->bindParam(':sensor_id', $sensorId);
        $stmtLast->execute();
        $lastReadings = $stmtLast->fetchAll(PDO::FETCH_ASSOC);
        
        if (empty($lastReadings)) {
            echo "❌ Keine Readings vorhanden!\n\n";
            continue;
        }
        
        echo "Letzte 5 Readings:\n";
        foreach ($lastReadings as $reading) {
            $value = $reading['value_num'] ?? $reading['value_str'];
            echo "  {$reading['ts']} → {$value} dB\n";
        }
        
        // Readings letzte 2 Minuten
        $queryRecent = "SELECT COUNT(*) as count, AVG(value_num) as avg_noise
                        FROM reading 
                        WHERE sensor_id = :sensor_id 
                        AND ts >= DATE_SUB(NOW(), INTERVAL 2 MINUTE)";
        $stmtRecent = $db->prepare($queryRecent);
        $stmtRecent->bindParam(':sensor_id', $sensorId);
        $stmtRecent->execute();
        $recentResult = $stmtRecent->fetch(PDO::FETCH_ASSOC);
        
        echo "\nLetzte 2 Minuten:\n";
        echo "  Anzahl: {$recentResult['count']}\n";
        if ($recentResult['avg_noise']) {
            echo "  Durchschnitt: " . round($recentResult['avg_noise'], 1) . " dB\n";
        } else {
            echo "  Durchschnitt: keine Daten\n";
        }
        echo "\n";
    }
    
    // 3. Query wie im Cron-Job testen
    echo "=== CRON-JOB QUERY TEST ===\n";
    $spaceId = '880e8400-e29b-41d4-a716-446655440001';
    
    $queryNoise = "SELECT AVG(r.value_num) as avg_noise, COUNT(*) as count
                   FROM reading r
                   JOIN sensor s ON r.sensor_id = s.id
                   JOIN device d ON s.device_id = d.id
                   WHERE d.space_id = :space_id
                   AND s.type = 'MICROPHONE'
                   AND r.ts >= DATE_SUB(NOW(), INTERVAL 2 MINUTE)";
    $stmtNoise = $db->prepare($queryNoise);
    $stmtNoise->bindParam(':space_id', $spaceId);
    $stmtNoise->execute();
    $noiseResult = $stmtNoise->fetch(PDO::FETCH_ASSOC);
    
    echo "Ergebnis (wie Cron-Job):\n";
    echo "  Anzahl Readings: {$noiseResult['count']}\n";
    echo "  Durchschnitt: " . ($noiseResult['avg_noise'] ? round($noiseResult['avg_noise'], 1) . " dB" : "keine Daten") . "\n";
    
} catch (Exception $e) {
    echo "❌ Fehler: " . $e->getMessage() . "\n";
    exit(1);
}
