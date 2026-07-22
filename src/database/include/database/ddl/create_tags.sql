CREATE TABLE tags (
    id INTEGER NOT NULL PRIMARY KEY,
    image_id INTEGER REFERENCES images(id),
    label_id INTEGER REFERENCES labels(id),
    tag_ids BLOB NOT NULL,
    type INTEGER NOT NULL,
    extra_data BLOB,
    CHECK ((type = 0 AND image_id IS NOT NULL AND label_id IS NULL) OR
           (type = 1 AND image_id IS NULL AND label_id IS NOT NULL)),
    UNIQUE (image_id),
    UNIQUE (label_id)
)
