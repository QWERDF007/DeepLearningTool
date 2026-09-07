import asyncio
import json
import sys
from argparse import Namespace
from pathlib import Path

import pytest


TASK_DIR = Path(__file__).resolve().parents[2] / "3rdparty" / "EasyTrain" / "src" / "python" / "task"
if str(TASK_DIR) not in sys.path:
    sys.path.insert(0, str(TASK_DIR))

from dltool_task_protocol import AsyncTaskClient, MessageType, ProtocolField, TaskStatus  # noqa: E402
from dltool_task_reporting import create_task_client  # noqa: E402


def test_task_messages_carry_run_identity_and_filter_stop_commands() -> None:
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
                ProtocolField.TASK_ID.value: 7,
                ProtocolField.RUN_ID.value: "old-run",
                ProtocolField.COMMAND.value: "stop",
            },
            {
                ProtocolField.TYPE.value: MessageType.COMMAND.value,
                ProtocolField.TASK_ID.value: 7,
                ProtocolField.RUN_ID.value: "current-run",
                ProtocolField.COMMAND.value: "stop",
            },
        ]
        writer.write(("\n".join(json.dumps(command) for command in commands) + "\n").encode("utf-8"))
        await writer.drain()

    server = await asyncio.start_server(handle_client, "127.0.0.1", 0)
    client = AsyncTaskClient("127.0.0.1", server.sockets[0].getsockname()[1], 7, "current-run")
    try:
        await client.connect()
        await client.status(
            7,
            TaskStatus.RUNNING,
            12,
            -1,
            "running",
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
            ProtocolField.TASK_ID.value: 7,
            ProtocolField.RUN_ID.value: "current-run",
            ProtocolField.TYPE.value: MessageType.STATUS.value,
            ProtocolField.STATUS.value: TaskStatus.RUNNING.value,
            ProtocolField.PROGRESS.value: 12,
            ProtocolField.ETA_SECONDS.value: -1,
            ProtocolField.MESSAGE.value: "running",
        }
    ]


def test_task_client_requires_run_identity_when_transport_is_enabled() -> None:
    args = Namespace(
        dltool_task_host="127.0.0.1",
        dltool_task_port=1,
        dltool_task_id=7,
        dltool_run_id="",
    )

    with pytest.raises(ValueError, match="dltool_run_id"):
        create_task_client(args)
