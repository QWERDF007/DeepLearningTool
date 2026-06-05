CREATE TABLE IF NOT EXISTS smart_annotation_settings (
    id INTEGER PRIMARY KEY,
    key TEXT NOT NULL UNIQUE,
    value TEXT NOT NULL,
    mtime INTEGER NOT NULL
)
