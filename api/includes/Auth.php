<?php
/**
 * Authentifizierungs-Middleware für API-Key-Validierung
 * Raum-Tracker API
 */

class Auth {
    private $db;

    public function __construct($db) {
        $this->db = $db;
    }

    /**
     * Validiert API-Key aus Request-Header
     * 
     * @return array|false Device-Daten bei Erfolg, false bei Fehler
     */
    public function validateApiKey() {
        // Hole API-Key aus Header
        $headers = getallheaders();
        $apiKey = $headers['X-Api-Key'] ?? $headers['X-API-Key'] ?? $headers['x-api-key'] ?? null;

        if (!$apiKey) {
            return false;
        }

        try {
            // Suche Device mit passendem API-Key-Hash
            $query = "SELECT id, space_id, type, model, is_active, api_key_hash 
                      FROM device 
                      WHERE is_active = 1";
            
            $stmt = $this->db->prepare($query);
            $stmt->execute();
            
            while ($device = $stmt->fetch()) {
                // Prüfe API-Key gegen gespeicherten Hash
                if (password_verify($apiKey, $device['api_key_hash'] ?? '')) {
                    // Update last_seen
                    $this->updateLastSeen($device['id']);
                    return $device;
                }
            }

            return false;
            
        } catch (PDOException $e) {
            error_log("Auth-Fehler: " . $e->getMessage());
            return false;
        }
    }

    /**
     * Aktualisiert last_seen des Geräts
     */
    private function updateLastSeen($deviceId) {
        try {
            $query = "UPDATE device SET last_seen = CURRENT_TIMESTAMP WHERE id = :id";
            $stmt = $this->db->prepare($query);
            $stmt->bindParam(':id', $deviceId);
            $stmt->execute();
        } catch (PDOException $e) {
            error_log("Fehler beim Update von last_seen: " . $e->getMessage());
        }
    }

    /**
     * Generiert einen neuen API-Key
     * 
     * @param int $length Länge des Keys (default: 32)
     * @return string
     */
    public static function generateApiKey($length = 32) {
        return bin2hex(random_bytes($length));
    }

    /**
     * Erstellt Hash für API-Key (zum Speichern in DB)
     * 
     * @param string $apiKey
     * @return string
     */
    public static function hashApiKey($apiKey) {
        return password_hash($apiKey, PASSWORD_BCRYPT);
    }
}
