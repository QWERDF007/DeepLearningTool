#include "../test_runner.h"

#include "model/TaskCommunication.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QSignalSpy>
#include <QTest>

using namespace dltool::model;

class TaskCommunicationServerTest : public QObject
{
    Q_OBJECT

private slots:
    void protocolMappingsNormalizeInput()
    {
        QCOMPARE(taskMessageTypeFromName(QStringLiteral(" STATUS ")), TaskMessageType::Status);
        QCOMPARE(taskProtocolStatusFromName(QStringLiteral(" RUNNING ")), TaskProtocolStatus::Running);
        QCOMPARE(taskCommandFromName(QStringLiteral(" STOP ")), TaskCommand::Stop);
        QCOMPARE(taskMessageTypeFromName(QStringLiteral("unknown")), TaskMessageType::Unknown);
        QCOMPARE(taskProtocolFieldName(TaskProtocolField::ProjectId), QStringLiteral("project_id"));
        QCOMPARE(taskProtocolFieldName(TaskProtocolField::TaskId), QStringLiteral("task_id"));
        QCOMPARE(taskProtocolFieldName(TaskProtocolField::RunId), QStringLiteral("run_id"));
        QCOMPARE(taskCommandName(TaskCommand::Stop), QStringLiteral("stop"));
    }

    void receivesSplitAndMultipleJsonLines()
    {
        TaskCommunicationServer server;
        QString error;
        QVERIFY2(server.start(&error), qPrintable(error));
        QTcpSocket socket;
        socket.connectToHost(server.host(), server.port());
        QVERIFY(socket.waitForConnected(2000));
        QSignalSpy received(&server, &TaskCommunicationServer::messageReceived);

        const QByteArray first
            = R"({"project_id":"project-7","task_id":7,"run_id":"run-7","type":"progress","status":"running","progress":2)";
        socket.write(first.left(first.size() / 2));
        socket.flush();
        QTest::qWait(20);
        QVERIFY(received.isEmpty());
        socket.write(first.mid(first.size() / 2) + "}\n"
                     R"({"project_id":"project-7","task_id":7,"run_id":"run-7","type":"log","message":"hello","payload":{"x":3}})"
                     "\n");
        socket.flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 2, 2000);

        const TaskMessage first_message = qvariant_cast<TaskMessage>(received.at(0).at(0));
        QCOMPARE(first_message.identity.task_id, 7);
        QCOMPARE(first_message.identity.run_id, QStringLiteral("run-7"));
        QCOMPARE(first_message.identity.project_id, QStringLiteral("project-7"));
        QCOMPARE(first_message.type, TaskMessageType::Progress);
        QCOMPARE(first_message.status, TaskProtocolStatus::Running);
        QCOMPARE(first_message.progress, 2);
        const TaskMessage second_message = qvariant_cast<TaskMessage>(received.at(1).at(0));
        QCOMPARE(second_message.type, TaskMessageType::Log);
        QCOMPARE(second_message.payload.value(QStringLiteral("payload")).toMap().value(QStringLiteral("x")).toInt(), 3);
    }

    void invalidJsonIsIgnoredAndCommandTargetsClient()
    {
        TaskCommunicationServer server;
        QVERIFY(server.start());
        QTcpSocket socket;
        socket.connectToHost(server.host(), server.port());
        QVERIFY(socket.waitForConnected(2000));
        QSignalSpy received(&server, &TaskCommunicationServer::messageReceived);
        socket.write("not-json\n");
        socket.write(R"({"project_id":"project-9","task_id":9,"run_id":"run-9","type":"event","status":"pending"})" "\n");
        socket.flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 1, 2000);

        const TaskIdentity identity{9, QStringLiteral("run-9"), QStringLiteral("project-9")};
        QVERIFY(server.sendCommand(identity, TaskCommand::Stop, {{QStringLiteral("reason"), QStringLiteral("test")}}));
        QVERIFY(socket.waitForReadyRead(2000));
        const QJsonObject command = QJsonDocument::fromJson(socket.readLine()).object();
        QCOMPARE(command.value(QStringLiteral("project_id")).toString(), QStringLiteral("project-9"));
        QCOMPARE(command.value(QStringLiteral("task_id")).toInt(), 9);
        QCOMPARE(command.value(QStringLiteral("run_id")).toString(), QStringLiteral("run-9"));
        QCOMPARE(command.value(QStringLiteral("type")).toString(), QStringLiteral("command"));
        QCOMPARE(command.value(QStringLiteral("command")).toString(), QStringLiteral("stop"));
        QCOMPARE(command.value(QStringLiteral("reason")).toString(), QStringLiteral("test"));

        QSignalSpy disconnected(&server, &TaskCommunicationServer::clientDisconnected);
        socket.disconnectFromHost();
        QVERIFY(socket.waitForDisconnected(2000) || socket.state() == QAbstractSocket::UnconnectedState);
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, 2000);
        const TaskIdentity disconnected_identity = qvariant_cast<TaskIdentity>(disconnected.at(0).at(0));
        QCOMPARE(disconnected_identity, identity);
        QVERIFY(!server.sendCommand(identity, TaskCommand::Unknown));
    }

    void rejectsMissingAndConflictingRunIdentity()
    {
        TaskCommunicationServer server;
        QVERIFY(server.start());
        QSignalSpy received(&server, &TaskCommunicationServer::messageReceived);

        QTcpSocket socket;
        socket.connectToHost(server.host(), server.port());
        QVERIFY(socket.waitForConnected(2000));

        socket.write(R"({"task_id":7,"run_id":"run-a","type":"progress","progress":1})" "\n");
        socket.flush();
        QTest::qWait(100);
        QCOMPARE(received.count(), 0);

        socket.write(R"({"project_id":"project-7","task_id":7,"run_id":"run-a","type":"progress","progress":2})" "\n");
        socket.flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 1, 2000);

        // The server binds the connection to the first valid complete identity.
        // It does not need to know the task manager's expected project; the
        // task manager performs that project-scope check at its own seam.
        socket.write(R"({"project_id":"wrong-project","task_id":7,"run_id":"run-a","type":"progress","progress":3})" "\n");
        socket.write(R"({"project_id":"project-7","task_id":8,"run_id":"run-b","type":"progress","progress":4})" "\n");
        socket.write(R"({"project_id":"project-7","task_id":7,"run_id":"run-a","type":"progress","progress":5})" "\n");
        socket.flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 2, 2000);

        const TaskIdentity wrong_task{8, QStringLiteral("run-a"), QStringLiteral("project-7")};
        QVERIFY(!server.sendCommand(wrong_task, TaskCommand::Stop));
        const TaskIdentity identity{7, QStringLiteral("run-a"), QStringLiteral("project-7")};
        QVERIFY(server.sendCommand(identity, TaskCommand::Stop));

        QTcpSocket duplicate;
        duplicate.connectToHost(server.host(), server.port());
        QVERIFY(duplicate.waitForConnected(2000));
        duplicate.write(R"({"project_id":"project-7","task_id":7,"run_id":"run-a","type":"progress","progress":5})" "\n");
        duplicate.flush();
        QTest::qWait(100);
        QCOMPARE(received.count(), 2);
    }

    void unboundClientInvalidMessageDoesNotDamageOtherTasks()
    {
        TaskCommunicationServer server;
        QVERIFY(server.start());
        QSignalSpy received(&server, &TaskCommunicationServer::messageReceived);

        QTcpSocket socket;
        socket.connectToHost(server.host(), server.port());
        QVERIFY(socket.waitForConnected(2000));

        // Invalid JSON on unbound socket
        socket.write("malformed-not-json\n");
        // Invalid progress on unbound socket
        socket.write(R"({"project_id":"proj","task_id":1,"run_id":"run-1","type":"progress","progress":999})" "\n");
        // Unknown type on unbound socket
        socket.write(R"({"project_id":"proj","task_id":1,"run_id":"run-1","type":"unknown_type"})" "\n");
        // Unknown status on unbound socket
        socket.write(R"({"project_id":"proj","task_id":1,"run_id":"run-1","type":"status","status":"paused"})" "\n");
        socket.flush();

        QTest::qWait(100);
        QCOMPARE(received.count(), 0);
    }

    void boundClientIllegalMessageExplicitlyFailsTask()
    {
        TaskCommunicationServer server;
        QVERIFY(server.start());
        QSignalSpy received(&server, &TaskCommunicationServer::messageReceived);

        QTcpSocket socket;
        socket.connectToHost(server.host(), server.port());
        QVERIFY(socket.waitForConnected(2000));

        // First message binds the connection
        socket.write(R"({"project_id":"project-42","task_id":42,"run_id":"run-42","type":"status","status":"running","progress":10})" "\n");
        socket.flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 1, 2000);

        const TaskMessage first_msg = qvariant_cast<TaskMessage>(received.at(0).at(0));
        QCOMPARE(first_msg.status, TaskProtocolStatus::Running);
        QCOMPARE(first_msg.identity.task_id, 42);

        // Now, bound socket sends illegal progress (> 100)
        socket.write(R"({"project_id":"project-42","task_id":42,"run_id":"run-42","type":"progress","progress":200})" "\n");
        socket.flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 2, 2000);

        const TaskMessage second_msg = qvariant_cast<TaskMessage>(received.at(1).at(0));
        QCOMPARE(second_msg.identity.task_id, 42);
        QCOMPARE(second_msg.identity.run_id, QStringLiteral("run-42"));
        QCOMPARE(second_msg.identity.project_id, QStringLiteral("project-42"));
        QCOMPARE(second_msg.status, TaskProtocolStatus::Failed);
        QVERIFY2(!second_msg.message.isEmpty(), "Failure message should describe the validation failure");
    }
};

REGISTER_TEST(TaskCommunicationServerTest)

#include "test_TaskCommunicationServer.moc"
