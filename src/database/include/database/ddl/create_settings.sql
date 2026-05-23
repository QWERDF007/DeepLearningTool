CREATE TABLE IF NOT EXISTS settings (
    id INTEGER NOT NULL PRIMARY KEY,
    group_name TEXT NOT NULL,
    setting_key TEXT NOT NULL,
    setting_value TEXT NOT NULL,
    value_type TEXT NOT NULL,
    mtime INTEGER NOT NULL,
    extra_data BLOB,
    UNIQUE (group_name, setting_key)
)
