CREATE TABLE tags (
    id INTEGER NOT NULL PRIMARY KEY, 
    image_id INTEGER NOT NULL REFERENCES images(id),
    tag_id INTEGER NOT NULL REFERENCES tag_classes(id),
    extra_data BLOB
)