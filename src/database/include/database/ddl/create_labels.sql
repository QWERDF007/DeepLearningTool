CREATE TABLE labels (
    id INTEGER NOT NULL PRIMARY KEY, 
    image_id INTEGER NOT NULL REFERENCES images(id), 
    label_class_id INTEGER NOT NULL REFERENCES label_classes, 
    region_type INTEGER NOT NULL, 
    region BLOB, 
    ordinal_index INTEGER,
    extra_data BLOB
)
