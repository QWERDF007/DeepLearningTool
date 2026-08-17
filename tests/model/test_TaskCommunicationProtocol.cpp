#include "../test_runner.h"

#include "model/TaskCommunication.h"

#include <QTest>

using namespace dltool::model;

class TaskCommunicationProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void messageTypeRoundTrip()
    {
        for (const TaskMessageType type : {TaskMessageType::Unknown, TaskMessageType::Event, TaskMessageType::Status,
                                           TaskMessageType::Progress, TaskMessageType::Log, TaskMessageType::Command})
            QCOMPARE(taskMessageTypeFromName(taskMessageTypeName(type)), type);
    }

    void statusRoundTrip()
    {
        for (const TaskProtocolStatus status : {TaskProtocolStatus::Unknown, TaskProtocolStatus::Pending,
                                                TaskProtocolStatus::Running, TaskProtocolStatus::Paused,
                                                TaskProtocolStatus::Stopped, TaskProtocolStatus::Finished,
                                                TaskProtocolStatus::Failed, TaskProtocolStatus::Error})
            QCOMPARE(taskProtocolStatusFromName(taskProtocolStatusName(status)), status);
    }

    void commandRoundTrip()
    {
        QCOMPARE(taskCommandFromName(taskCommandName(TaskCommand::Stop)), TaskCommand::Stop);
        QCOMPARE(taskCommandFromName(taskCommandName(TaskCommand::Unknown)), TaskCommand::Unknown);
    }
};

REGISTER_TEST(TaskCommunicationProtocolTest)

#include "test_TaskCommunicationProtocol.moc"
