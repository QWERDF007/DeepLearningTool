CREATE TABLE label_classes (
    id INTEGER NOT NULL PRIMARY KEY, 
    name TEXT, 
    color TEXT, 
    shortcut TEXT, 
    ordinal_index INTEGER, 
    extra_data BLOB
)
