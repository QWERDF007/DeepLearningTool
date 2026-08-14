CREATE TABLE IF NOT EXISTS {table_name} (
    id INTEGER PRIMARY KEY,
    name_en TEXT NOT NULL UNIQUE,
    name_cn TEXT,
    property_name TEXT,
    value TEXT NOT NULL,
    default_value TEXT,
    value_type TEXT NOT NULL,
    value_range TEXT,
    display_type TEXT,
    options TEXT,
    options_map TEXT,
    section TEXT,
    description TEXT,
    visible INTEGER NOT NULL DEFAULT 1,
    ordinal_index INTEGER NOT NULL DEFAULT 0,
    mtime INTEGER NOT NULL
)
