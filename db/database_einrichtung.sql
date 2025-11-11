-- Empfohlen vorab:
-- SET NAMES utf8mb4;
-- SET time_zone = '+00:00';

-- --------------------------------------------------------------------
-- Tabellen werden mit ENGINE=InnoDB, UTF8MB4 erstellt
-- --------------------------------------------------------------------

-- ============ SPACE (ein Raum) ============
CREATE TABLE IF NOT EXISTS space (
  id           CHAR(36)     NOT NULL,            -- UUID (z.B. via UUID())
  name         VARCHAR(120) NOT NULL,
  description  TEXT         NULL,
  created_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at   TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ DEVICE ============
CREATE TABLE IF NOT EXISTS device (
  id               CHAR(36)     NOT NULL,
  space_id         CHAR(36)     NOT NULL,
  type             ENUM('MICROCONTROLLER','DISPLAY') NOT NULL,
  model            VARCHAR(120) NULL,
  firmware_version VARCHAR(50)  NULL,
  api_key_hash     VARCHAR(255) NULL,     -- Hash (z.B. Argon2/bcrypt) – im Backend prüfen
  is_active        TINYINT(1)   NOT NULL DEFAULT 1,
  last_seen        TIMESTAMP    NULL,
  created_at       TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at       TIMESTAMP    NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_device_space (space_id),
  CONSTRAINT fk_device_space FOREIGN KEY (space_id) REFERENCES space(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ SENSOR ============
CREATE TABLE IF NOT EXISTS sensor (
  id           CHAR(36)   NOT NULL,
  device_id    CHAR(36)   NOT NULL,
  type         ENUM('LIGHT_BARRIER','MICROPHONE','DISTANCE') NOT NULL,
  unit         VARCHAR(20) NULL,          -- "mm", "dB", "bool" …
  config       JSON        NULL,          -- Kalibrier-/Schwellwerte
  is_active    TINYINT(1)  NOT NULL DEFAULT 1,
  created_at   TIMESTAMP   NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at   TIMESTAMP   NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_sensor_device (device_id),
  KEY idx_sensor_type (type),
  CONSTRAINT fk_sensor_device FOREIGN KEY (device_id) REFERENCES device(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ GATE (Paar A/B) ============
CREATE TABLE IF NOT EXISTS gate (
  id            CHAR(36) NOT NULL,
  space_id      CHAR(36) NOT NULL,
  name          VARCHAR(120) NOT NULL,
  sensor_a_id   CHAR(36) NOT NULL,
  sensor_b_id   CHAR(36) NOT NULL,
  invert_dir    TINYINT(1) NOT NULL DEFAULT 0,
  created_at    TIMESTAMP  NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at    TIMESTAMP  NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_gate_space (space_id),
  CONSTRAINT chk_gate_distinct_sensors CHECK (sensor_a_id <> sensor_b_id),
  CONSTRAINT fk_gate_space     FOREIGN KEY (space_id)    REFERENCES space(id)  ON DELETE CASCADE,
  CONSTRAINT fk_gate_sensor_a  FOREIGN KEY (sensor_a_id) REFERENCES sensor(id) ON DELETE RESTRICT,
  CONSTRAINT fk_gate_sensor_b  FOREIGN KEY (sensor_b_id) REFERENCES sensor(id) ON DELETE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ READING (Rohmessungen; optional) ============
CREATE TABLE IF NOT EXISTS reading (
  id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  sensor_id    CHAR(36)        NOT NULL,
  ts           TIMESTAMP       NOT NULL,
  value_num    DOUBLE          NULL,
  value_text   VARCHAR(255)    NULL,
  value_json   JSON            NULL,
  quality      ENUM('OK','NOISE','DROPPED') DEFAULT 'OK',
  created_at   TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_reading_sensor_ts (sensor_id, ts),
  CONSTRAINT fk_reading_sensor FOREIGN KEY (sensor_id) REFERENCES sensor(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ FLOW_EVENT (IN/OUT) ============
CREATE TABLE IF NOT EXISTS flow_event (
  id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  gate_id      CHAR(36)        NOT NULL,
  space_id     CHAR(36)        NOT NULL,
  ts           TIMESTAMP       NOT NULL,
  direction    ENUM('IN','OUT') NOT NULL,
  confidence   DOUBLE          NULL,          -- 0..1
  duration_ms  INT             NULL,
  raw_refs     JSON            NULL,
  created_at   TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_flow_ts_space (space_id, ts),
  KEY idx_flow_gate_ts (gate_id, ts),
  CONSTRAINT fk_flow_gate  FOREIGN KEY (gate_id)  REFERENCES gate(id)   ON DELETE CASCADE,
  CONSTRAINT fk_flow_space FOREIGN KEY (space_id) REFERENCES space(id)  ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ OCCUPANCY_SNAPSHOT (abgeleitet) ============
CREATE TABLE IF NOT EXISTS occupancy_snapshot (
  id                 BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  space_id           CHAR(36)        NOT NULL,
  ts                 TIMESTAMP       NOT NULL,
  people_estimate    INT             NULL,          -- kann NULL sein
  level              ENUM('LOW','MEDIUM','HIGH') NULL,
  method             ENUM('FLOW_ONLY','NOISE_ONLY','FUSION') NOT NULL,
  noise_db           DOUBLE          NULL,
  motion_count       INT             NULL,
  window_seconds     INT             NOT NULL DEFAULT 120,
  created_at         TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_occ_space_ts (space_id, ts),
  CONSTRAINT fk_occ_space FOREIGN KEY (space_id) REFERENCES space(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ THRESHOLD_PROFILE ============
CREATE TABLE IF NOT EXISTS threshold_profile (
  id             CHAR(36) NOT NULL,
  space_id       CHAR(36) NOT NULL,
  name           VARCHAR(120) NOT NULL,
  noise_levels   JSON    NULL,   -- {"low":45,"medium":60,"high":75}
  motion_levels  JSON    NULL,   -- {"low":2,"medium":5,"high":10}
  fusion_rules   JSON    NULL,   -- {"weights":{"flow":0.6,"noise":0.3,"motion":0.1}}
  is_default     TINYINT(1) NOT NULL DEFAULT 0,
  created_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at     TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_thr_space (space_id),
  KEY idx_thr_space_default (space_id, is_default),
  CONSTRAINT fk_thr_space FOREIGN KEY (space_id) REFERENCES space(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ DISPLAY_FRAME (Waveshare Anzeige) ============
CREATE TABLE IF NOT EXISTS display_frame (
  id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  device_id    CHAR(36)        NOT NULL,
  ts           TIMESTAMP       NOT NULL,
  payload      JSON            NOT NULL,   -- kompaktes JSON (Level, Text, Icon-ID)
  status       ENUM('SENT','ACK','FAILED') NOT NULL DEFAULT 'SENT',
  created_at   TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_display_device_ts (device_id, ts),
  CONSTRAINT fk_display_device FOREIGN KEY (device_id) REFERENCES device(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ HEARTBEAT (Gerätestatus) ============
CREATE TABLE IF NOT EXISTS heartbeat (
  id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  device_id    CHAR(36)        NOT NULL,
  ts           TIMESTAMP       NOT NULL,
  ip           VARCHAR(45)     NULL,   -- IPv4/IPv6
  rssi         INT             NULL,
  metrics      JSON            NULL,
  created_at   TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_hb_device_ts (device_id, ts),
  CONSTRAINT fk_hb_device FOREIGN KEY (device_id) REFERENCES device(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ CALIBRATION ============
CREATE TABLE IF NOT EXISTS calibration (
  id           BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  sensor_id    CHAR(36)        NOT NULL,
  ts           TIMESTAMP       NOT NULL,
  params       JSON            NULL,
  note         TEXT            NULL,
  created_at   TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_cal_sensor_ts (sensor_id, ts),
  CONSTRAINT fk_cal_sensor FOREIGN KEY (sensor_id) REFERENCES sensor(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ============ View: Nettosaldo seit Mitternacht ============
CREATE OR REPLACE VIEW v_people_net_today AS
SELECT
  fe.space_id,
  DATE(CURRENT_TIMESTAMP) AS day_start,
  COALESCE(SUM(CASE WHEN fe.direction='IN' THEN 1 ELSE -1 END), 0) AS net_people
FROM flow_event fe
WHERE fe.ts >= DATE(CURRENT_TIMESTAMP)
GROUP BY fe.space_id;