CREATE TABLE project (
    id INTEGER NOT NULL PRIMARY KEY,
    name TEXT,
    method INTEGER NOT NULL,
    description TEXT,
    path TEXT,
    image_base_path TEXT,
    ctime INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    version TEXT,
    extra_data BLOB
)