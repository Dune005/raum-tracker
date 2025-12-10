<?php
/**
 * DriftCorrector.php
 * 
 * Drift-Erkennung und Korrektur für Lichtschranken-basierte Personenzählung
 * 
 * Funktionen:
 * 1. Drift-Erkennung: Erkennt "Geister-Personen" im Counter
 * 2. Drift-Korrektur: Setzt counter_raw auf 0 wenn Raum leer
 * 3. Skalierung: Berechnet display_count mit Faktor für hohe Werte
 * 
 * @author Raum-Tracker Team IM5
 * @version 1.0
 * @date 6. Dezember 2025
 */

class DriftCorrector {
    private $db;
    
    /**
     * Constructor
     * @param PDO $database PDO-Datenbankverbindung
     */
    public function __construct($database) {
        $this->db = $database;
    }
    
    /**
     * Hole Drift-Konfiguration für einen Space
     * 
     * @param string $spaceId UUID des Space
     * @return array Drift-Config als assoziatives Array
     */
    public function getDriftConfig($spaceId) {
        $query = "SELECT drift_config FROM threshold_profile 
                  WHERE space_id = :space_id AND is_default = 1 LIMIT 1";
        $stmt = $this->db->prepare($query);
        $stmt->bindParam(':space_id', $spaceId);
        $stmt->execute();
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if ($result && $result['drift_config']) {
            return json_decode($result['drift_config'], true);
        }
        
        // Fallback: Standard-Werte
        return [
            'drift_max' => 7,          // Maximaler counter_raw Wert für Drift-Erkennung
            'drift_window_minutes' => 30,   // Zeitfenster für Event-Analyse
            'min_out_events_for_reset' => 2,    // Minimale OUT-Events für Drift-Reset
            'scale_threshold' => 15,    // Ab wann Skalierung aktiviert wird
            'scale_factor' => 2,      // Faktor für Skalierung
            'out_event_multiplier' => 1.3,  // OUT-Events werden stärker gewichtet (z.B. 1.3x)
            'max_capacity' => 60,
            'min_correction_interval_minutes' => 5
        ];
    }
    
    /**
     * Hole Event-Statistik der letzten X Minuten
     * Nutzt flow_event Tabelle (präziser als counter_state)
     * 
     * @param string $spaceId UUID des Space
     * @param int $windowMinutes Zeitfenster in Minuten
     * @return array Event-Statistik
     */
    private function getRecentEventStats($spaceId, $windowMinutes) {
        $query = "SELECT 
                    SUM(CASE WHEN direction = 'IN' THEN 1 ELSE 0 END) as in_events,
                    SUM(CASE WHEN direction = 'OUT' THEN 1 ELSE 0 END) as out_events,
                    COUNT(*) as total_events,
                    MAX(ts) as last_event
                  FROM flow_event
                  WHERE space_id = :space_id
                  AND ts >= DATE_SUB(NOW(), INTERVAL :window MINUTE)";
        
        $stmt = $this->db->prepare($query);
        $stmt->bindParam(':space_id', $spaceId);
        $stmt->bindParam(':window', $windowMinutes, PDO::PARAM_INT);
        $stmt->execute();
        
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        
        return [
            'in_events' => (int)($result['in_events'] ?? 0),
            'out_events' => (int)($result['out_events'] ?? 0),
            'total_events' => (int)($result['total_events'] ?? 0),
            'last_event' => $result['last_event']
        ];
    }
    
    /**
     * Prüfe ob letzte Drift-Korrektur zu kürzlich war
     * Verhindert zu häufige Resets
     * 
     * @param string $spaceId UUID des Space
     * @param int $minIntervalMinutes Mindestabstand in Minuten
     * @return bool True wenn Intervall eingehalten wurde
     */
    private function isMinIntervalRespected($spaceId, $minIntervalMinutes) {
        $query = "SELECT last_drift_correction FROM counter_state WHERE space_id = :space_id";
        $stmt = $this->db->prepare($query);
        $stmt->bindParam(':space_id', $spaceId);
        $stmt->execute();
        $result = $stmt->fetch(PDO::FETCH_ASSOC);
        
        if (!$result || !$result['last_drift_correction']) {
            return true; // Noch nie korrigiert
        }
        
        $lastCorrection = strtotime($result['last_drift_correction']);
        $now = time();
        $minutesSince = ($now - $lastCorrection) / 60;
        
        return $minutesSince >= $minIntervalMinutes;
    }
    
    /**
     * Hauptfunktion: Prüfe ob Drift-Korrektur notwendig ist
     * 
     * Kriterien für Drift:
     * 1. counter_raw ist klein aber > 0 (typisch 1-7)
     * 2. Keine IN-Events in den letzten X Minuten
     * 3. Mehrere OUT-Events im gleichen Zeitraum
     * 4. Mindestabstand zur letzten Korrektur eingehalten
     * 
     * @param string $spaceId UUID des Space
     * @param int $counterRaw Aktueller Rohwert vom Arduino
     * @param array $driftConfig Drift-Konfiguration
     * @return bool True wenn Drift-Korrektur notwendig ist
     */
    public function shouldCorrectDrift($spaceId, $counterRaw, $driftConfig) {
        // Keine Korrektur bei 0 oder sehr hohen Werten
        if ($counterRaw <= 0 || $counterRaw > $driftConfig['drift_max']) {
            return false;
        }
        
        // Prüfe Mindestabstand zur letzten Korrektur
        if (!$this->isMinIntervalRespected($spaceId, $driftConfig['min_correction_interval_minutes'])) {
            return false;
        }
        
        // Hole Event-Statistik
        $stats = $this->getRecentEventStats($spaceId, $driftConfig['drift_window_minutes']);
        
        // Drift-Kriterien:
        $noRecentInEvents = ($stats['in_events'] == 0);
        $hasOutEvents = ($stats['out_events'] >= $driftConfig['min_out_events_for_reset']);
        
        return ($noRecentInEvents && $hasOutEvents);
    }
    
    /**
     * Berechne display_count (korrigierter Wert mit Skalierung)
     * 
     * Logik:
     * - Kleine Werte (< scale_threshold): Keine Skalierung
     * - Hohe Werte: Multiplikation mit scale_factor
     * - Begrenzt auf max_capacity
     * 
     * @param int $counterRaw Roher Zählerstand
     * @param array $driftConfig Drift-Konfiguration
     * @return int Korrigierter display_count
     */
    public function computeDisplayCount($counterRaw, $driftConfig) {
        // Negative Werte auf 0 setzen
        if ($counterRaw < 0) {
            return 0;
        }
        
        // Kleine Werte: Keine Skalierung (oft akkurat genug)
        if ($counterRaw < $driftConfig['scale_threshold']) {
            return $counterRaw;
        }
        
        // Hohe Werte: Skalierung anwenden
        $scaled = round($counterRaw * $driftConfig['scale_factor']);
        
        // Auf Maximum begrenzen
        return min($scaled, $driftConfig['max_capacity']);
    }
    
    /**
     * Wende Drift-Korrektur an und aktualisiere counter_state
     * Setzt counter_raw und display_count auf 0
     * 
     * @param string $spaceId UUID des Space
     * @return bool True bei Erfolg
     */
    public function applyDriftCorrection($spaceId) {
        $query = "UPDATE counter_state 
                  SET counter_raw = 0,
                      display_count = 0,
                      drift_corrections_today = drift_corrections_today + 1,
                      last_drift_correction = NOW(),
                      last_update = NOW()
                  WHERE space_id = :space_id";
        
        $stmt = $this->db->prepare($query);
        $stmt->bindParam(':space_id', $spaceId);
        return $stmt->execute();
    }
    
    /**
     * Inkrementiere Counter (IN-Event)
     * 
     * @param string $spaceId UUID des Space
     * @param int $amount Anzahl Personen (Standard: 1)
     * @return bool True bei Erfolg
     */
    public function incrementCounter($spaceId, $amount = 1) {
        $query = "UPDATE counter_state 
                  SET counter_raw = counter_raw + :amount,
                      in_events_today = in_events_today + 1,
                      last_in_event = NOW(),
                      last_update = NOW()
                  WHERE space_id = :space_id";
        
        $stmt = $this->db->prepare($query);
        $stmt->bindParam(':space_id', $spaceId);
        $stmt->bindParam(':amount', $amount, PDO::PARAM_INT);
        
        return $stmt->execute();
    }
    
    /**
     * Dekrementiere Counter (OUT-Event) mit Multiplikator
     * 
     * @param string $spaceId UUID des Space
     * @param float $outMultiplier Gewichtungsfaktor für OUT-Events
     * @return bool True bei Erfolg
     */
    public function decrementCounter($spaceId, $outMultiplier = 1.0) {
        // Berechne gewichteten Abzug (z.B. 1.3 statt 1)
        $amount = round($outMultiplier);
        
        $query = "UPDATE counter_state 
                  SET counter_raw = GREATEST(0, counter_raw - :amount),
                      out_events_today = out_events_today + 1,
                      last_out_event = NOW(),
                      last_update = NOW()
                  WHERE space_id = :space_id";
        
        $stmt = $this->db->prepare($query);
        $stmt->bindParam(':space_id', $spaceId);
        $stmt->bindParam(':amount', $amount, PDO::PARAM_INT);
        
        return $stmt->execute();
    }
    
    /**
     * Aktualisiere counter_state mit neuen Werten
     * 
     * @param string $spaceId UUID des Space
     * @param int $counterRaw Roher Zählerstand
     * @param int $displayCount Korrigierter Wert
     * @param bool $driftCorrected Wurde Drift korrigiert?
     * @return bool True bei Erfolg
     */
    public function updateCounterState($spaceId, $counterRaw, $displayCount, $driftCorrected = false) {
        $query = "UPDATE counter_state 
                  SET counter_raw = :counter_raw,
                      display_count = :display_count,
                      last_update = NOW()
                  WHERE space_id = :space_id";
        
        $stmt = $this->db->prepare($query);
        $stmt->bindParam(':space_id', $spaceId);
        $stmt->bindParam(':counter_raw', $counterRaw, PDO::PARAM_INT);
        $stmt->bindParam(':display_count', $displayCount, PDO::PARAM_INT);
        
        return $stmt->execute();
    }
    
    /**
     * Hole aktuellen State für einen Space
     * 
     * @param string $spaceId UUID des Space
     * @return array|false Counter-State als assoziatives Array
     */
    public function getCounterState($spaceId) {
        $query = "SELECT * FROM counter_state WHERE space_id = :space_id";
        $stmt = $this->db->prepare($query);
        $stmt->bindParam(':space_id', $spaceId);
        $stmt->execute();
        
        return $stmt->fetch(PDO::FETCH_ASSOC);
    }
}
