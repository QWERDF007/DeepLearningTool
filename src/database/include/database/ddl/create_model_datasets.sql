CREATE TABLE IF NOT EXISTS datasets (
    type       TEXT    NOT NULL,
    dataset_id INTEGER NOT NULL,
    class_ids  TEXT    NOT NULL,
    PRIMARY KEY (type, dataset_id)
)