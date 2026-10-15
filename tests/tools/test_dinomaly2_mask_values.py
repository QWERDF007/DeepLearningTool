from argparse import Namespace
from pathlib import Path
import sys
import pytest

DINOMALY2_DIR = Path(__file__).resolve().parents[2] / "3rdparty" / "EasyTrain" / "src" / "python" / "guojiajeremy" / "Dinomaly2"
TASK_DIR = Path(__file__).resolve().parents[2] / "3rdparty" / "EasyTrain" / "src" / "python" / "task"
for path in (DINOMALY2_DIR, TASK_DIR):
    if str(path) not in sys.path:
        sys.path.insert(0, str(path))

from dltool_common import mask_value_lists  # noqa: E402
from mask_constraint_dataset import _validate_value_lists  # noqa: E402


def test_validate_value_lists_allows_empty_good_values() -> None:
    good, anomaly, ignore = _validate_value_lists(
        good_values=[],
        anomaly_values=[1, 2, 3, 4, 5, 6, 7],
        ignore_values=[],
    )
    assert good == []
    assert anomaly == [1, 2, 3, 4, 5, 6, 7]
    assert ignore == []


def test_mask_value_lists_under_dltool_preserves_empty_good_values() -> None:
    config = {
        "train_params": {
            "mask": {
                "good_value": "1",
                "anomaly_value": "255",
                "ignore_value": "254",
            }
        }
    }
    # DLTool task with only anomaly classes (1..7) and no good classes
    args = Namespace(
        dltool_task_id=1,
        dltool_run_id="run-1",
        good_values="",
        anomaly_values="1,2,3,4,5,6,7",
        ignore_values="",
    )
    good, anomaly, ignore = mask_value_lists(args, config)
    assert good == [], "good_values should remain empty when dltool has no good classes"
    assert anomaly == [1, 2, 3, 4, 5, 6, 7]
    assert ignore == []


def test_mask_value_lists_manual_run_without_args_uses_config_fallback() -> None:
    config = {
        "train_params": {
            "mask": {
                "good_value": "1",
                "anomaly_value": "255",
                "ignore_value": "254",
            }
        }
    }
    # Standalone manual run without CLI arguments or dltool task ID
    args = Namespace(
        dltool_task_id=-1,
        dltool_run_id="",
        good_values=None,
        anomaly_values=None,
        ignore_values=None,
    )
    good, anomaly, ignore = mask_value_lists(args, config)
    assert good == [1]
    assert anomaly == [255]
    assert ignore == [254]


def test_dltool_mask_constraint_dataset_allows_empty_good_values() -> None:
    from mask_constraint_dataset import DltoolMaskConstraintTrainDataset

    dataset = DltoolMaskConstraintTrainDataset(
        samples=[{"id": 1, "path": "dummy.jpg"}],
        image_transform=lambda x: x,
        image_size=448,
        crop_size=392,
        good_values=[],
        anomaly_values=[1, 2, 3, 4, 5, 6, 7],
        ignore_values=[],
    )
    assert dataset.good_values == []
    assert dataset.anomaly_values == [1, 2, 3, 4, 5, 6, 7]
    assert dataset.ignore_values == []

