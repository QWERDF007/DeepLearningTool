#include "database/ModelTaskDataBase.h"

#include "DatabaseSqliteUtils.h"
#include "DatabaseValueUtils.h"
#include "database/ddl/PredictionTable.h"
#include "database/ddl/TaskInfoTable.h"
#include "database/ddl/TestParamsTable.h"

#include <sqlpp11/insert.h>
#include <sqlpp11/remove.h>
#include <sqlpp11/select.h>
#include <sqlpp11/order_by.h>
#include <sqlpp11/limit.h>
#include <sqlpp11/sqlpp11.h>
#include <sqlpp11/sqlite3/insert_or.h>
#include <sqlpp11/transaction.h>

#include <exception>
#include <sqlite3.h>
#include <tuple>

namespace dltool::database {

namespace {

bool setError(QString *err_msg, const QString &message)
{
    if (err_msg != nullptr)
        *err_msg = message;
    return false;
}

bool failFromException(QString *err_msg, const std::exception &error, const QString &prefix)
{
    return setError(err_msg, prefix + QStringLiteral(": ") + QString::fromUtf8(error.what()));
}

} // namespace

ModelTaskDataBase::ModelTaskDataBase(const QString &path, QObject *parent)
    : DataBase(path, parent)
{
    ensureSchema(&schema_error_);
}

ModelTaskDataBase::~ModelTaskDataBase() = default;

bool ModelTaskDataBase::ensureSchema(QString *err_msg) const
{
    if (!schema_error_.isEmpty())
        return setError(err_msg, schema_error_);
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    return detail::ensureTaskSchema(db, err_msg);
}

bool ModelTaskDataBase::readTaskInfo(TaskInfoRecord &info, QString *err_msg) const
{
    info = {};
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TaskInfo{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto         rows = db(sqlpp::select(table.taskId, table.ctime, table.mtime)
                                 .from(table)
                                 .unconditionally()
                                 .limit(1U));
        if (rows.empty())
            return setError(err_msg, QString("task_info 为空"));
        const auto &row = rows.front();
        info.task_id    = QString::fromStdString(row.taskId).trimmed();
        info.ctime      = static_cast<qint64>(row.ctime);
        info.mtime      = static_cast<qint64>(row.mtime);
        if (info.task_id.isEmpty() || info.ctime < 0 || info.mtime < 0)
            return setError(err_msg, QString("task_info 字段无效"));
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("读取 task_info 失败"));
    }
}

bool ModelTaskDataBase::upsertTaskInfo(const TaskInfoRecord &info, QString *err_msg) const
{
    if (info.task_id.trimmed().isEmpty() || info.ctime < 0 || info.mtime < 0)
        return setError(err_msg, QString("task_info 字段无效"));
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TaskInfo{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto tx = sqlpp::start_transaction(db);
        db(sqlpp::remove_from(table).unconditionally());
        db(sqlpp::insert_into(table).set(table.taskId = info.task_id.trimmed().toStdString(),
                                         table.ctime  = static_cast<int64_t>(info.ctime),
                                         table.mtime  = static_cast<int64_t>(info.mtime)));
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("写入 task_info 失败"));
    }
}

bool ModelTaskDataBase::readTestParams(QVariantMap &params, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureTaskSchema(db, err_msg))
        return false;
    return detail::readParams(db, QStringLiteral("test_params"), params, err_msg);
}

bool ModelTaskDataBase::replaceTestParams(const QVariantMap &params, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureTaskSchema(db, err_msg))
        return false;

    QVariantMap merged = params;
    if (!merged.contains(QStringLiteral("execution")))
    {
        QVariantMap existing_exec;
        if (readExecutionState(existing_exec) && !existing_exec.isEmpty())
            merged.insert(QStringLiteral("execution"), existing_exec);
    }
    if (!merged.contains(QStringLiteral("preprocessing")))
    {
        QVariantMap existing_prep;
        if (readPreprocessingConfig(existing_prep) && !existing_prep.isEmpty())
            merged.insert(QStringLiteral("preprocessing"), existing_prep);
    }
    const QVariantMap eval_map = merged.value(QStringLiteral("evaluation")).toMap();
    if (!eval_map.contains(QStringLiteral("adaptive_threshold_applied")))
    {
        bool applied = false;
        if (readAdaptiveThresholdApplied(applied) && applied)
        {
            QVariantMap updated_eval = eval_map;
            updated_eval.insert(QStringLiteral("adaptive_threshold_applied"), true);
            merged.insert(QStringLiteral("evaluation"), updated_eval);
        }
    }

    return detail::replaceParams(db, QStringLiteral("test_params"), merged, err_msg);
}

bool ModelTaskDataBase::readDatasets(QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureTaskSchema(db, err_msg))
        return false;
    return detail::readDatasets(db, selections, err_msg);
}

bool ModelTaskDataBase::replaceDatasets(const QList<DatasetSelectionRecord> &selections, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    auto db = connectionPool()->get();
    if (!detail::ensureTaskSchema(db, err_msg))
        return false;
    return detail::replaceDatasets(db, selections, err_msg);
}

bool ModelTaskDataBase::clearPredictions(QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = Prediction{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        db(sqlpp::remove_from(table).unconditionally());
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("清理预测结果失败"));
    }
}

bool ModelTaskDataBase::readPredictions(QHash<qint64, QVariant> &predictions, QString *err_msg) const
{
    predictions.clear();
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = Prediction{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto         rows = db(sqlpp::select(table.imageId, table.data).from(table).unconditionally());
        for (const auto &row : rows)
        {
            const qint64 image_id = static_cast<qint64>(row.imageId);
            const std::string &encoded = row.data;
            QString value_error;
            const QVariant value = detail::jsonToVariant(
                QByteArray(encoded.data(), static_cast<int>(encoded.size())), &value_error);
            if (!value_error.isEmpty())
                return setError(err_msg, QString("读取图像 %1 的预测失败: %2").arg(image_id).arg(value_error));
            if (image_id < 0)
                return setError(err_msg, QString("prediction.image_id 无效"));
            predictions.insert(image_id, value);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("读取预测结果失败"));
    }
}

bool ModelTaskDataBase::upsertPrediction(const PredictionRecord &prediction, QString *err_msg) const
{
    if (prediction.image_id < 0)
        return setError(err_msg, QString("prediction.image_id 无效"));
    QString value_error;
    const QByteArray encoded = detail::variantToJson(prediction.data, &value_error);
    if (!value_error.isEmpty())
        return setError(err_msg, value_error);
    if (connectionPool() == nullptr)
        return setError(err_msg, QString("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = Prediction{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        db(sqlpp::sqlite3::insert_or_replace_into(table)
               .set(table.imageId = static_cast<int64_t>(prediction.image_id),
                    table.data    = encoded.toStdString()));
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QString("写入 prediction 失败"));
    }
}

bool ModelTaskDataBase::replacePredictions(const QHash<qint64, QVariant> &predictions, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QStringLiteral("数据库连接池为空"));
    try
    {
        auto db = connectionPool()->get();
        const auto table = Prediction{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto tx = sqlpp::start_transaction(db);
        db(sqlpp::remove_from(table).unconditionally());
        for (auto it = predictions.cbegin(); it != predictions.cend(); ++it)
        {
            const qint64 image_id = it.key();
            if (image_id < 0)
                return setError(err_msg, QStringLiteral("prediction.image_id 无效"));
            QString value_error;
            const QByteArray encoded = detail::variantToJson(it.value(), &value_error);
            if (!value_error.isEmpty())
                return setError(err_msg, value_error);
            db(sqlpp::insert_into(table).set(
                table.imageId = static_cast<int64_t>(image_id),
                table.data    = encoded.toStdString()));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QStringLiteral("批量替换 prediction 失败"));
    }
}

bool ModelTaskDataBase::readAdaptiveThresholdApplied(bool &applied, QString *err_msg) const
{
    applied = false;
    if (connectionPool() == nullptr)
        return setError(err_msg, QStringLiteral("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestParams{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto rows = db(sqlpp::select(table.value, table.type)
                          .from(table)
                          .where(table.group == "evaluation" and table.nameEn == "adaptive_threshold_applied")
                          .limit(1U));
        if (!rows.empty())
        {
            const auto &row = rows.front();
            const std::string val = row.value;
            applied = (val == "true" || val == "1");
        }
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QStringLiteral("读取自适应阈值标记失败"));
    }
}

bool ModelTaskDataBase::writeAdaptiveThresholdApplied(const bool applied, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QStringLiteral("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestParams{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        db(sqlpp::sqlite3::insert_or_replace_into(table)
               .set(table.group  = std::string("evaluation"),
                    table.nameEn = std::string("adaptive_threshold_applied"),
                    table.value  = applied ? std::string("true") : std::string("false"),
                    table.type   = std::string("bool")));
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QStringLiteral("写入自适应阈值标记失败"));
    }
}

bool ModelTaskDataBase::readExecutionState(QVariantMap &state, QString *err_msg) const
{
    state.clear();
    if (connectionPool() == nullptr)
        return setError(err_msg, QStringLiteral("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestParams{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto rows = db(sqlpp::select(table.nameEn, table.value, table.type)
                          .from(table)
                          .where(table.group == "execution"));
        for (const auto &row : rows)
        {
            const QString key = QString::fromStdString(row.nameEn);
            const QString val = QString::fromStdString(row.value);
            const QString type = QString::fromStdString(row.type);
            QString parse_error;
            state.insert(key, detail::paramValueFromText(type, val, &parse_error));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QStringLiteral("读取执行状态失败"));
    }
}

bool ModelTaskDataBase::writeExecutionState(const QVariantMap &state, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QStringLiteral("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestParams{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto tx = sqlpp::start_transaction(db);
        db(sqlpp::remove_from(table).where(table.group == "execution"));
        for (auto it = state.cbegin(); it != state.cend(); ++it)
        {
            const QString key = it.key().trimmed();
            if (key.isEmpty())
                continue;
            db(sqlpp::insert_into(table).set(
                table.group  = std::string("execution"),
                table.nameEn = key.toStdString(),
                table.value  = detail::paramValueText(it.value()).toStdString(),
                table.type   = detail::paramValueType(it.value()).toStdString()));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QStringLiteral("写入执行状态失败"));
    }
}

bool ModelTaskDataBase::readPreprocessingConfig(QVariantMap &config, QString *err_msg) const
{
    config.clear();
    if (connectionPool() == nullptr)
        return setError(err_msg, QStringLiteral("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestParams{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto rows = db(sqlpp::select(table.nameEn, table.value, table.type)
                          .from(table)
                          .where(table.group == "preprocessing"));
        for (const auto &row : rows)
        {
            const QString key = QString::fromStdString(row.nameEn);
            const QString val = QString::fromStdString(row.value);
            const QString type = QString::fromStdString(row.type);
            QString parse_error;
            config.insert(key, detail::paramValueFromText(type, val, &parse_error));
        }
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QStringLiteral("读取预处理配置失败"));
    }
}

bool ModelTaskDataBase::writePreprocessingConfig(const QVariantMap &config, QString *err_msg) const
{
    if (connectionPool() == nullptr)
        return setError(err_msg, QStringLiteral("数据库连接池为空"));
    try
    {
        auto       db    = connectionPool()->get();
        const auto table = TestParams{};
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;
        auto tx = sqlpp::start_transaction(db);
        db(sqlpp::remove_from(table).where(table.group == "preprocessing"));
        for (auto it = config.cbegin(); it != config.cend(); ++it)
        {
            const QString key = it.key().trimmed();
            if (key.isEmpty())
                continue;
            db(sqlpp::insert_into(table).set(
                table.group  = std::string("preprocessing"),
                table.nameEn = key.toStdString(),
                table.value  = detail::paramValueText(it.value()).toStdString(),
                table.type   = detail::paramValueType(it.value()).toStdString()));
        }
        tx.commit();
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QStringLiteral("写入预处理配置失败"));
    }
}

bool ModelTaskDataBase::getPredictionFingerprint(QString &fingerprint, QString *err_msg) const
{
    fingerprint.clear();
    if (connectionPool() == nullptr)
        return setError(err_msg, QStringLiteral("数据库连接池为空"));

    try
    {
        auto db = connectionPool()->get();
        if (!detail::ensureTaskSchema(db, err_msg))
            return false;

        sqlite3 *handle = db.native_handle();
        if (handle == nullptr)
            return setError(err_msg, QStringLiteral("数据库句柄为空"));

        const char *sql = "SELECT "
                          "(SELECT count(*) FROM prediction), "
                          "(SELECT coalesce(max(image_id), 0) FROM prediction), "
                          "(SELECT coalesce(total(length(data)), 0) FROM prediction), "
                          "(SELECT count(*) FROM datasets), "
                          "(SELECT coalesce(max(dataset_id), 0) FROM datasets), "
                          "(SELECT coalesce(total(length(class_ids)), 0) FROM datasets)";
        sqlite3_stmt *stmt = nullptr;
        int rc = sqlite3_prepare_v2(handle, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK)
            return setError(err_msg, QString::fromUtf8(sqlite3_errmsg(handle)));

        rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW)
        {
            const qint64 pred_count  = sqlite3_column_int64(stmt, 0);
            const qint64 pred_max_id = sqlite3_column_int64(stmt, 1);
            const double pred_len    = sqlite3_column_double(stmt, 2);
            const qint64 ds_count    = sqlite3_column_int64(stmt, 3);
            const qint64 ds_max_id   = sqlite3_column_int64(stmt, 4);
            const double ds_len      = sqlite3_column_double(stmt, 5);
            fingerprint = QStringLiteral("p:%1|%2|%3;d:%4|%5|%6")
                              .arg(pred_count).arg(pred_max_id).arg(pred_len, 0, 'f', 1)
                              .arg(ds_count).arg(ds_max_id).arg(ds_len, 0, 'f', 1);
        }
        sqlite3_finalize(stmt);
        return true;
    }
    catch (const std::exception &e)
    {
        return failFromException(err_msg, e, QStringLiteral("获取预测指纹失败"));
    }
}

} // namespace dltool::database

