CREATE TABLE IF NOT EXISTS test_params (
    "group" TEXT NOT NULL,
    name_en TEXT NOT NULL,
    value   TEXT NOT NULL,
    type    TEXT NOT NULL,
    PRIMARY KEY ("group", name_en)
)