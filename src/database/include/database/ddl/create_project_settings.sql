CREATE TABLE IF NOT EXISTS project_settings (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    max_recent_projects INTEGER NOT NULL DEFAULT 10,
    auto_save_enabled INTEGER NOT NULL DEFAULT 1,
    auto_save_interval INTEGER NOT NULL DEFAULT 300
)
