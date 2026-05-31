CREATE TABLE IF NOT EXISTS label_display_settings (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    border_width INTEGER NOT NULL DEFAULT 2,
    fill_opacity INTEGER NOT NULL DEFAULT 30
)
