CREATE TABLE IF NOT EXISTS models (
    id INTEGER NOT NULL PRIMARY KEY,
    uuid TEXT NOT NULL UNIQUE,
    name TEXT,
    framework_name TEXT,
    model_architecture TEXT,
    training_result TEXT,
    test_result TEXT,
    ctime INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    extra_data BLOB
)
