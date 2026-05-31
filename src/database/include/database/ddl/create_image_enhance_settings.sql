CREATE TABLE IF NOT EXISTS image_enhance_settings (
    id INTEGER PRIMARY KEY,
    key TEXT NOT NULL UNIQUE,
    value TEXT NOT NULL,
    mtime INTEGER NOT NULL
)
