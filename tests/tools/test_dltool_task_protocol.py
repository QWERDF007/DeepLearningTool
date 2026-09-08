import asyncio
import json
import sys
from argparse import Namespace
from pathlib import Path

import pytest


TASK_DIR = Path(__file__).resolve().parents[2] / "3rdparty" / "EasyTrain" / "src" / "python" / "task"
if str(TASK_DIR) not in sys.path:
    sys.path.insert(0, str(TASK_DIR))

from dltool_task_protocol import (  # noqa: E402
    AsyncTaskClient,
    MessageType,
    ProtocolField,
    TaskStatus,
    validate_task_message,
)
from dltool_task_reporting import create_task_client  # noqa: E402


def test_shared_protocol_samples() -> None:
    fixture_path = Path(__file__).resolve().parents[2] / "tests" / "assets" / "task_protocol_samples.json"
    with open(fixture_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    for item in data["valid_messages"]:
        ok, err = validate_task_message(item["payload"])
        assert ok, f"Expected valid for {item['name']}, but failed with: {err}"

    for item in data["invalid_messages"]:
        ok, err = validate_task_message(item["payload"])
        assert not ok, f"Expected invalid for {item['name']}, but validation succeeded"
        assert err, f"Error message should not be empty for {item['name']}"


def test_async_task_client_validates_out_of_range_inputs() -> None:
    client = AsyncTaskClient("127.0.0.1", 9999, 1, "run-1", "project-1")
    # Progress > 100
    with pytest.raises(ValueError):
        asyncio.run(client.send(1, MessageType.PROGRESS, None, 101, -1))
    # Progress < -1
    with pytest.raises(ValueError):
        asyncio.run(client.send(1, MessageType.PROGRESS, None, -2, -1))
    # Eta < -1
    with pytest.raises(ValueError):
        asyncio.run(client.send(1, MessageType.PROGRESS, None, 50, -2))


def test_task_messages_carry_project_and_run_identity_and_filter_stop_commands() -> None:
    asyncio.run(_exercise_task_protocol())


async def _exercise_task_protocol() -> None:
    received: list[dict] = []
    connection_ready = asyncio.Event()

    async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        line = await reader.readline()
        if line:
            received.append(json.loads(line.decode("utf-8")))
        connection_ready.set()
        commands = [
            {
                ProtocolField.TYPE.value: MessageType.COMMAND.value,
                ProtocolField.PROJECT_ID.value: "project-7",
                ProtocolField.TASK_ID.value: 7,
                ProtocolField.RUN_ID.value: "old-run",
                ProtocolField.COMMAND.value: "stop",
            },
            {
                ProtocolField.TYPE.value: MessageType.COMMAND.value,
                ProtocolField.PROJECT_ID.value: "project-7",
                ProtocolField.TASK_ID.value: 7,
                ProtocolField.RUN_ID.value: "current-run",
                ProtocolField.COMMAND.value: "stop",
            },
            {
                ProtocolField.TYPE.value: MessageType.COMMAND.value,
                ProtocolField.PROJECT_ID.value: "other-project",
                ProtocolField.TASK_ID.value: 7,
                ProtocolField.RUN_ID.value: "current-run",
                ProtocolField.COMMAND.value: "stop",
            },
        ]
        writer.write(("\n".join(json.dumps(command) for command in commands) + "\n").encode("utf-8"))
        await writer.drain()
        writer.close()
        await writer.wait_closed()

    server = await asyncio.start_server(handle_client, "127.0.0.1", 0)
    client = AsyncTaskClient("127.0.0.1", server.sockets[0].getsockname()[1], 7, "current-run", "project-7")
    try:
        await client.connect()
        await client.status(
            7,
            TaskStatus.RUNNING,
            12,
            -1,
            "running",
            project_id="other-project",
            run_id="old-run",
            type="log",
        )
        await asyncio.wait_for(connection_ready.wait(), timeout=1)
        await asyncio.sleep(0.05)
        assert await client.should_stop(7)
    finally:
        await client.close()
        server.close()
        await server.wait_closed()

    assert received == [
        {
            ProtocolField.PROJECT_ID.value: "project-7",
            ProtocolField.TASK_ID.value: 7,
            ProtocolField.RUN_ID.value: "current-run",
            ProtocolField.TYPE.value: MessageType.STATUS.value,
            ProtocolField.STATUS.value: TaskStatus.RUNNING.value,
            ProtocolField.PROGRESS.value: 12,
            ProtocolField.ETA_SECONDS.value: -1,
            ProtocolField.MESSAGE.value: "running",
        }
    ]


def test_task_client_requires_complete_identity_when_transport_is_enabled() -> None:
    args = Namespace(
        dltool_task_host="127.0.0.1",
        dltool_task_port=1,
        dltool_task_id=7,
        dltool_project_id="",
        dltool_run_id="",
    )

    with pytest.raises(ValueError, match="dltool_project_id"):
        create_task_client(args)

