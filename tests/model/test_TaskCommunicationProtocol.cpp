#include "../test_runner.h"

#include "model/TaskCommunication.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
                                                TaskProtocolStatus::Running,
                                                TaskProtocolStatus::Stopped, TaskProtocolStatus::Finished,
                                                TaskProtocolStatus::Failed, TaskProtocolStatus::Error})
            QCOMPARE(taskProtocolStatusFromName(taskProtocolStatusName(status)), status);
    }

    void commandRoundTrip()
    {
        QCOMPARE(taskCommandFromName(taskCommandName(TaskCommand::Stop)), TaskCommand::Stop);
        QCOMPARE(taskCommandFromName(taskCommandName(TaskCommand::Unknown)), TaskCommand::Unknown);
    }

    void validateSharedSamples()
    {
        const QString root = qEnvironmentVariable("DLT_RUNTIME_ROOT").trimmed();
        QString fixture_path;
        if (!root.isEmpty())
        {
            const QString candidate = QDir(root).filePath(QStringLiteral("tests/assets/task_protocol_samples.json"));
            if (QFile::exists(candidate))
                fixture_path = candidate;
        }
        if (fixture_path.isEmpty())
        {
            const QStringList fallbacks = {
                QDir::current().filePath(QStringLiteral("tests/assets/task_protocol_samples.json")),
                QDir::current().filePath(QStringLiteral("../tests/assets/task_protocol_samples.json")),
                QDir::current().filePath(QStringLiteral("../../tests/assets/task_protocol_samples.json")),
                QCoreApplication::applicationDirPath() + QStringLiteral("/../../tests/assets/task_protocol_samples.json")
            };
            for (const QString &path : fallbacks)
            {
                if (QFile::exists(path))
                {
                    fixture_path = QDir::cleanPath(path);
                    break;
                }
            }
        }
        QVERIFY2(!fixture_path.isEmpty(), "task_protocol_samples.json fixture not found");

        QFile file(fixture_path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QVERIFY(doc.isObject());
        const QJsonObject root_obj = doc.object();

        const QJsonArray valid_messages = root_obj.value(QStringLiteral("valid_messages")).toArray();
        QVERIFY(!valid_messages.isEmpty());
        for (const QJsonValue &val : valid_messages)
        {
            const QJsonObject sample = val.toObject();
            const QString name = sample.value(QStringLiteral("name")).toString();
            const QJsonObject payload = sample.value(QStringLiteral("payload")).toObject();

            TaskMessage message;
            QString error;
            const bool ok = validateTaskProtocolJson(payload, &message, &error);
            QVERIFY2(ok, qPrintable(QStringLiteral("[%1] Expected valid but got error: %2").arg(name, error)));

            const QString expected_type = sample.value(QStringLiteral("expected_type")).toString();
            QCOMPARE(taskMessageTypeName(message.type), expected_type);

            const QString expected_status = sample.value(QStringLiteral("expected_status")).toString();
            QCOMPARE(taskProtocolStatusName(message.status), expected_status);

            const int expected_progress = sample.value(QStringLiteral("expected_progress")).toInt();
            QCOMPARE(message.progress, expected_progress);

            const qint64 expected_eta = sample.value(QStringLiteral("expected_eta_seconds")).toInteger();
            QCOMPARE(message.eta_seconds, expected_eta);
        }

        const QJsonArray invalid_messages = root_obj.value(QStringLiteral("invalid_messages")).toArray();
        QVERIFY(!invalid_messages.isEmpty());
        for (const QJsonValue &val : invalid_messages)
        {
            const QJsonObject sample = val.toObject();
            const QString name = sample.value(QStringLiteral("name")).toString();
            const QJsonObject payload = sample.value(QStringLiteral("payload")).toObject();

            TaskMessage message;
            QString error;
            const bool ok = validateTaskProtocolJson(payload, &message, &error);
            QVERIFY2(!ok, qPrintable(QStringLiteral("[%1] Expected invalid but validation succeeded").arg(name)));
            QVERIFY2(!error.isEmpty(), qPrintable(QStringLiteral("[%1] Error message must not be empty").arg(name)));
        }
    }
};

REGISTER_TEST(TaskCommunicationProtocolTest)

#include "test_TaskCommunicationProtocol.moc"
