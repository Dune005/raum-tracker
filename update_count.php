<?php
// update_count.php - Erweitert für 2 separate ESP32-Boards (mit File-Locking)
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

$dataFile = 'counter_data.json';

// POST-Daten vom ESP32 empfangen
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    // File-Handle mit LOCK öffnen
    $lockFile = fopen($dataFile . '.lock', 'w');
    if (!$lockFile || !flock($lockFile, LOCK_EX)) {
        http_response_code(503);
        echo json_encode(['status' => 'error', 'message' => 'Could not acquire lock']);
        exit;
    }
    
    // Bestehende Daten laden (falls vorhanden)
    $existingData = [];
    if (file_exists($dataFile)) {
        $existingData = json_decode(file_get_contents($dataFile), true) ?: [];
    }
    
    // Neue Daten initialisieren
    $data = $existingData;
    $timestamp = date('Y-m-d H:i:s');
    $updated = false;
    
    // ===== BOARD 1: Lichtschranken (sendet count + direction) =====
    if (isset($_POST['count'])) {
        $data['count'] = intval($_POST['count']);
        $data['direction'] = isset($_POST['direction']) ? $_POST['direction'] : 'IDLE';
        $data['last_count_update'] = time();
        $data['timestamp'] = $timestamp;
        $updated = true;
    }
    
    // ===== BOARD 2: Mikrofon (sendet nur sound_level) =====
    if (isset($_POST['sound_level'])) {
        $data['sound_level'] = intval($_POST['sound_level']);
        $data['last_sound_update'] = time();
        if (!isset($data['timestamp'])) {
            $data['timestamp'] = $timestamp;
        }
        $updated = true;
    }
    
    // Globales last_update setzen
    if ($updated) {
        $data['last_update'] = time();
        
        // Daten in Datei speichern
        file_put_contents($dataFile, json_encode($data));
        
        // Lock freigeben
        flock($lockFile, LOCK_UN);
        fclose($lockFile);
        
        echo json_encode([
            'status' => 'success',
            'count' => isset($data['count']) ? $data['count'] : null,
            'sound_level' => isset($data['sound_level']) ? $data['sound_level'] : null,
            'message' => 'Data merged successfully'
        ]);
        exit;
    }
    
    // Lock freigeben bei Fehler
    flock($lockFile, LOCK_UN);
    fclose($lockFile);
    
    // Falls keine relevanten POST-Daten
    http_response_code(400);
    echo json_encode([
        'status' => 'error', 
        'message' => 'No valid data received (expected: count or sound_level)'
    ]);
    exit;
}

// GET-Anfrage: Aktuelle Daten zurückgeben
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    if (file_exists($dataFile)) {
        $data = json_decode(file_get_contents($dataFile), true);
        
        // Berechne Sekunden seit letztem Update (global)
        $data['seconds_since_update'] = isset($data['last_update']) ? time() - $data['last_update'] : 999999;
        
        // Separate Timestamps für Count und Sound
        $data['seconds_since_count_update'] = isset($data['last_count_update']) ? time() - $data['last_count_update'] : 999999;
        $data['seconds_since_sound_update'] = isset($data['last_sound_update']) ? time() - $data['last_sound_update'] : 999999;
        
        $data['has_data'] = true;
        
        // Sound Level in dB umrechnen (30-60 dB Range - realistischer für Aufenthaltsraum)
        if (isset($data['sound_level']) && $data['sound_level'] !== null) {
            $data['sound_db'] = round(30 + ($data['sound_level'] / 100 * 30), 1);
        } else {
            $data['sound_db'] = null;
        }
        
        // Defaults setzen falls Daten fehlen
        if (!isset($data['count'])) $data['count'] = 0;
        if (!isset($data['sound_level'])) $data['sound_level'] = null;
        if (!isset($data['direction'])) $data['direction'] = 'IDLE';
        
        echo json_encode($data);
    } else {
        // Keine Daten vorhanden - Arduino war noch nie verbunden
        echo json_encode([
            'count' => 0,
            'sound_level' => null,
            'sound_db' => null,
            'direction' => 'IDLE',
            'timestamp' => '',
            'last_update' => 0,
            'seconds_since_update' => 999999,
            'seconds_since_count_update' => 999999,
            'seconds_since_sound_update' => 999999,
            'has_data' => false
        ]);
    }
    exit;
}

http_response_code(400);
echo json_encode(['status' => 'error', 'message' => 'Invalid request']);
?>
