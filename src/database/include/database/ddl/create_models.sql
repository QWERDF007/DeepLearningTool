CREATE TABLE IF NOT EXISTS models (
    id INTEGER NOT NULL PRIMARY KEY,
    name TEXT,
    network_structure TEXT,
    training_result TEXT,
    test_result TEXT,
    ctime INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    extra_data BLOB
)
