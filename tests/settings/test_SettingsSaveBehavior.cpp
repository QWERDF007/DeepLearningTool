#include "settings/GlobalSettings.h"
#include "settings/SettingsKeys.h"
#include "settings/SettingsSchema.h"
#include "settings/SettingsValue.h"
#include "database/DataBase.h"
#include <sqlite3.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>

namespace dltool::settings {

class SettingsSaveBehaviorTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void crossGroupTransactionAtomicityOnFailure();
    void authoritativeFieldChangeInvalidatesCache();
    void storageFailureRetainsDirtyAndRetryReloadsValues();

private:
    QString original_db_path_;
};

void SettingsSaveBehaviorTest::initTestCase()
{
    auto *gs = GlobalSettings::getInstance();
    QVERIFY(gs != nullptr);
    original_db_path_ = gs->databasePath();
}

void SettingsSaveBehaviorTest::cleanupTestCase()
{
    auto *gs = GlobalSettings::getInstance();
    if (gs != nullptr && !original_db_path_.isEmpty())
    {
        gs->setDatabasePath(original_db_path_);
    }
}

void SettingsSaveBehaviorTest::crossGroupTransactionAtomicityOnFailure()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString db_path = temp_dir.filePath(QStringLiteral("test_tx.db"));

    database::SettingsDataBase db(db_path);

    QString err_msg;
    QVERIFY(db.ensureSettingsTable(QStringLiteral("ui_settings"), err_msg));
    QVERIFY(db.ensureSettingsTable(QStringLiteral("software_setting"), err_msg));

    // 1. 写入初始数据
    QMap<QString, QVariantMap> initial_data;
    initial_data[QStringLiteral("ui_settings")][QStringLiteral("theme")] = QStringLiteral("dark");
    initial_data[QStringLiteral("software_setting")][QStringLiteral("python_env_path")] = QStringLiteral("/opt/python_initial");

    QVERIFY(db.saveAllSettings(initial_data, err_msg));

    // 验证初始写入成功
    QVariantMap ui_loaded = db.loadSettings(QStringLiteral("ui_settings"), err_msg);
    QCOMPARE(ui_loaded.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));

    // 2. 跨组原子事务测试：Group 1 尝试更新为 light，但 Group 2 注入非法表名触发 SQL 失败
    QMap<QString, QVariantMap> failing_batch;
    failing_batch[QStringLiteral("ui_settings")][QStringLiteral("theme")] = QStringLiteral("light");
    // 故意使用带非法字符的表名导致 SQL 异常
    failing_batch[QStringLiteral("invalid--table!!name")][QStringLiteral("foo")] = QStringLiteral("bar");

    const bool save_result = db.saveAllSettings(failing_batch, err_msg);
    QVERIFY(!save_result);
    QVERIFY(!err_msg.isEmpty());

    // 事务必须完整回滚：ui_settings 的 theme 必须仍然保持 "dark"，绝不能部分提交为 "light"！
    ui_loaded = db.loadSettings(QStringLiteral("ui_settings"), err_msg);
    QCOMPARE(ui_loaded.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));

    // 3. 跨组原子事务成功用例：两组同时成功提交
    QMap<QString, QVariantMap> valid_batch;
    valid_batch[QStringLiteral("ui_settings")][QStringLiteral("theme")] = QStringLiteral("light");
    valid_batch[QStringLiteral("software_setting")][QStringLiteral("python_env_path")] = QStringLiteral("/opt/python_updated");

    QVERIFY(db.saveAllSettings(valid_batch, err_msg));

    ui_loaded = db.loadSettings(QStringLiteral("ui_settings"), err_msg);
    QCOMPARE(ui_loaded.value(QStringLiteral("theme")).toString(), QStringLiteral("light"));
    QVariantMap software_loaded = db.loadSettings(QStringLiteral("software_setting"), err_msg);
    QCOMPARE(software_loaded.value(QStringLiteral("python_env_path")).toString(), QStringLiteral("/opt/python_updated"));
}

void SettingsSaveBehaviorTest::authoritativeFieldChangeInvalidatesCache()
{
    auto *gs = GlobalSettings::getInstance();
    QVERIFY(gs != nullptr);

    int cache_clear_count = 0;

    // 模拟权威字段驱动缓存失效的监听（直接监听 GlobalSettings 领域单例信号，不依赖 QML 属性映射投影）
    auto connection = connect(gs, &GlobalSettings::fieldValueChanged, this,
        [&cache_clear_count](generated::AccessorKey accessor_key, const QString &field_name, const QVariant &) {
            if (accessor_key == generated::AccessorKey::SmartAnnotation)
            {
                if (field_name == QStringLiteral("model")
                    || field_name == QStringLiteral("model_path")
                    || field_name == QStringLiteral("model_runtime")
                    || field_name == QStringLiteral("model_precision")
                    || field_name == QStringLiteral("device")
                    || field_name == QStringLiteral("enabled"))
                {
                    ++cache_clear_count;
                }
            }
        });

    namespace gen_field = generated::field;

    // 1. 修改非模型缓存字段（例如 mask_threshold），不应使模型预测器缓存失效
    cache_clear_count = 0;
    gs->setFieldValue(gen_field::SmartAnnotation::MaskThreshold, 0.42);
    QCOMPARE(cache_clear_count, 0);

    // 2. 修改权威模型字段（例如 model），必须准确使缓存失效
    gs->setFieldValue(gen_field::SmartAnnotation::Model, QStringLiteral("sam2_base"));
    QCOMPARE(cache_clear_count, 1);

    // 3. 修改权威运行环境字段（例如 model_runtime），必须准确使缓存失效
    gs->setFieldValue(gen_field::SmartAnnotation::ModelRuntime, QStringLiteral("TensorRT"));
    QCOMPARE(cache_clear_count, 2);

    // 4. 验证 loadValues 对已改变字段同样发射 valueChanged
    SettingsFieldModel *smart_model = gs->catalog() ? gs->catalog()->groupForAccessor(QStringLiteral("advanced.smartAnnotation")) : nullptr;
    QVERIFY(smart_model != nullptr);

    QVariantMap new_values;
    new_values[QStringLiteral("model")] = QStringLiteral("sam2_large");
    smart_model->loadValues(new_values);

    // 经过 loadValues 后，权威字段 model 变化必须触发信号
    QCOMPARE(cache_clear_count, 3);

    disconnect(connection);
}

void SettingsSaveBehaviorTest::storageFailureRetainsDirtyAndRetryReloadsValues()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString db_path = temp_dir.filePath(QStringLiteral("isolated_settings.db"));

    auto *gs = GlobalSettings::getInstance();
    QVERIFY(gs != nullptr);
    gs->setDatabasePath(db_path);

    // 初始状态下不应 dirty
    QVERIFY(!gs->isDirty());
    QVERIFY(gs->lastSaveError().isEmpty());

    // 禁用自动定时器，以便精确控制手动保存与重试
    gs->setAutoSaveEnabled(false);

    namespace gen_field = generated::field;
    const QString test_env_path = QStringLiteral("C:/test_python_env/python.exe");

    QSignalSpy dirtySpy(gs, &GlobalSettings::isDirtyChanged);
    QSignalSpy saveFailedSpy(gs, &GlobalSettings::saveFailed);
    QSignalSpy savedSpy(gs, &GlobalSettings::saved);

    // 修改设置
    QVERIFY(gs->setFieldValue(gen_field::Software::PythonEnvPath, test_env_path));
    QVERIFY(gs->isDirty());
    QVERIFY(dirtySpy.count() >= 1);

    // 注入真实存储失败：持有独占写锁模拟数据库并发锁定或存储不可写
    sqlite3 *lock_db = nullptr;
    QCOMPARE(sqlite3_open(db_path.toUtf8().constData(), &lock_db), SQLITE_OK);
    QCOMPARE(sqlite3_exec(lock_db, "BEGIN EXCLUSIVE;", nullptr, nullptr, nullptr), SQLITE_OK);

    // 执行保存：由于独占锁引发真实存储写入失败
    QString err;
    const bool save_ok = gs->save(err);
    QVERIFY(!save_ok);
    QVERIFY(!err.isEmpty());

    // 验收：保存失败必须返回明确 false，并且保留 dirty 状态！
    QVERIFY(gs->isDirty());
    QVERIFY(!gs->lastSaveError().isEmpty());
    QCOMPARE(saveFailedSpy.count(), 1);
    QCOMPARE(savedSpy.count(), 0);

    // 修复真实存储：释放排他锁
    QCOMPARE(sqlite3_exec(lock_db, "ROLLBACK;", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(lock_db);
    lock_db = nullptr;

    // 重试保存
    const bool retry_ok = gs->save(err);
    QVERIFY(retry_ok);
    QVERIFY(err.isEmpty());

    // 验收：重试成功后清除 dirty 状态与错误信息
    QVERIFY(!gs->isDirty());
    QVERIFY(gs->lastSaveError().isEmpty());
    QCOMPARE(savedSpy.count(), 1);

    // 重新加载并验证值准确落盘
    gs->load();
    QCOMPARE(gs->valueForField(gen_field::Software::PythonEnvPath).toString(), test_env_path);
}

} // namespace dltool::settings

QTEST_MAIN(dltool::settings::SettingsSaveBehaviorTest)
#include "test_SettingsSaveBehavior.moc"
