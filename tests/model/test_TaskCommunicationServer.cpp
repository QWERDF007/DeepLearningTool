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
        QCOMPARE(taskProtocolFieldName(TaskProtocolField::TaskId), QStringLiteral("task_id"));
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

        const QByteArray first = R"({"task_id":7,"type":"progress","status":"running","progress":2)";
        socket.write(first.left(first.size() / 2));
        socket.flush();
        QTest::qWait(20);
        QVERIFY(received.isEmpty());
        socket.write(first.mid(first.size() / 2) + "}\n"
                     R"({"task_id":7,"type":"log","message":"hello","payload":{"x":3}})"
                     "\n");
        socket.flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 2, 2000);

        const TaskMessage first_message = qvariant_cast<TaskMessage>(received.at(0).at(0));
        QCOMPARE(first_message.task_id, 7);
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
        socket.write(R"({"task_id":9,"type":"event","status":"pending"})" "\n");
        socket.flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 1, 2000);

        QVERIFY(server.sendCommand(9, TaskCommand::Stop, {{QStringLiteral("reason"), QStringLiteral("test")}}));
        QVERIFY(socket.waitForReadyRead(2000));
        const QJsonObject command = QJsonDocument::fromJson(socket.readLine()).object();
        QCOMPARE(command.value(QStringLiteral("task_id")).toInt(), 9);
        QCOMPARE(command.value(QStringLiteral("type")).toString(), QStringLiteral("command"));
        QCOMPARE(command.value(QStringLiteral("command")).toString(), QStringLiteral("stop"));
        QCOMPARE(command.value(QStringLiteral("reason")).toString(), QStringLiteral("test"));

        QSignalSpy disconnected(&server, &TaskCommunicationServer::clientDisconnected);
        socket.disconnectFromHost();
        QVERIFY(socket.waitForDisconnected(2000) || socket.state() == QAbstractSocket::UnconnectedState);
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, 2000);
        QCOMPARE(disconnected.at(0).at(0).toInt(), 9);
        QVERIFY(!server.sendCommand(9, TaskCommand::Unknown));
    }
};

Q_DECLARE_METATYPE(dltool::model::TaskMessage)
REGISTER_TEST(TaskCommunicationServerTest)

#include "test_TaskCommunicationServer.moc"
