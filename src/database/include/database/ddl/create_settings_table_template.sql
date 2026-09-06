CREATE TABLE IF NOT EXISTS {table_name} (
    name_en TEXT NOT NULL UNIQUE,
    value TEXT NOT NULL,
    mtime INTEGER NOT NULL
)
