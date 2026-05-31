CREATE TABLE IF NOT EXISTS feature_search_settings (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    enabled INTEGER NOT NULL DEFAULT 1,
    model TEXT NOT NULL DEFAULT 'resnet18',
    model_path TEXT NOT NULL DEFAULT 'F:/models/resnet18.wts',
    feature_name TEXT NOT NULL DEFAULT 'layer4',
    rebuild_index INTEGER NOT NULL DEFAULT 0,
    top_k INTEGER NOT NULL DEFAULT 5,
    norm TEXT NOT NULL DEFAULT 'l2',
    preprocess_backend TEXT NOT NULL DEFAULT 'cpu',
    faiss_backend TEXT NOT NULL DEFAULT 'cpu',
    index_storage TEXT NOT NULL DEFAULT 'ram',
    disk_build_batch_size INTEGER NOT NULL DEFAULT 256,
    model_backend TEXT NOT NULL DEFAULT 'tensorrt',
    model_device TEXT NOT NULL DEFAULT 'gpu',
    index_directory TEXT NOT NULL DEFAULT '',
    custom_feature_names TEXT NOT NULL DEFAULT '{}'
)
