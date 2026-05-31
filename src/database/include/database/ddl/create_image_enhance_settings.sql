CREATE TABLE IF NOT EXISTS image_enhance_settings (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    brightness REAL NOT NULL DEFAULT 0.0,
    brightness_from REAL NOT NULL DEFAULT -1.0,
    brightness_to REAL NOT NULL DEFAULT 1.0,
    brightness_step REAL NOT NULL DEFAULT 0.1,
    contrast REAL NOT NULL DEFAULT 0.0,
    contrast_from REAL NOT NULL DEFAULT -1.0,
    contrast_to REAL NOT NULL DEFAULT 1.0,
    contrast_step REAL NOT NULL DEFAULT 0.1
)
