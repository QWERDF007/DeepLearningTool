#include "database/DataBase.h"
#include "database/DatabaseSchema.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTest>

using namespace dltool::database;

namespace {

/// 创建已初始化的临时项目数据库并验证其可用。
class RepositoryFixture
{
public:
    explicit RepositoryFixture(const QString &tag)
    {
        if (!temp_dir_.isValid())
        {
            error_ = QStringLiteral("临时目录无效");
            return;
        }
        path_ = temp_dir_.filePath(QStringLiteral("repo_%1.dlpro").arg(tag));
        db_   = std::make_unique<ProjectDataBase>(path_);
        QString err;
        if (!db_->initProject(QStringLiteral("仓储测试"), 0, path_, QString(), path_, 1000, 1000, err))
        {
            error_ = QStringLiteral("初始化项目库失败: %1").arg(err);
            db_.reset();
        }
    }

    bool        isValid() const { return db_ != nullptr; }
    const QString &error() const { return error_; }
    ProjectDataBase *db() { return db_.get(); }
    const QString  &path() const { return path_; }

private:
    QTemporaryDir                     temp_dir_;
    QString                           path_;
    QString                           error_;
    std::unique_ptr<ProjectDataBase> db_;
};

} // namespace

class DatabaseRepositoriesTest : public QObject
{
    Q_OBJECT

private slots:
    /// 模型写入后重开数据库仍可读回（持久性与唯一约束）。
    void modelRoundtripPersistsAfterReopen();

    /// models.uuid 唯一约束触发失败时错误边界完整（不抛出到调用方之外）。
    void duplicateModelUuidReportsErrorWithoutThrowing();

    /// 删除 tag 类别时同步清除 tags 关系行中的对应 ID（跨行更新同事务）。
    void deleteTagClassRemovesRelationIds();

    /// tags 关系的添加与移除对图像与标注各自生效且互不影响。
    void tagRelationsScopeByTargetType();
};

void DatabaseRepositoriesTest::modelRoundtripPersistsAfterReopen()
{
    RepositoryFixture fixture(QStringLiteral("model_roundtrip"));
    QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

    int64_t model_id = -1;
    QString err;
    QVERIFY(fixture.db()->addModel(QStringLiteral("uuid-1"), QStringLiteral("模型A"), QStringLiteral("ultralytics"),
                                   QStringLiteral("yolov8"), 100, 200, model_id, err));
    QVERIFY(model_id > 0);

    // 重开数据库验证已落盘
    ProjectDataBase reopened(fixture.path());
    std::vector<int64_t>        ids;
    std::vector<QString>        uuids;
    std::vector<QString>        names;
    std::vector<QString>        frameworks;
    std::vector<QString>        archs;
    std::vector<qint64>         ctimes;
    std::vector<qint64>         mtimes;
    std::vector<std::vector<uint8_t>> extra;
    QVERIFY(reopened.getAllModels(ids, uuids, names, frameworks, archs, ctimes, mtimes, extra, err));
    QCOMPARE(ids.size(), size_t{1});
    QCOMPARE(uuids.front(), QStringLiteral("uuid-1"));
    QCOMPARE(names.front(), QStringLiteral("模型A"));
    QCOMPARE(ctimes.front(), qint64{100});
    QCOMPARE(mtimes.front(), qint64{200});
}

void DatabaseRepositoriesTest::duplicateModelUuidReportsErrorWithoutThrowing()
{
    RepositoryFixture fixture(QStringLiteral("model_dup"));
    QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

    int64_t first_id = -1;
    QString err;
    QVERIFY(fixture.db()->addModel(QStringLiteral("dup-uuid"), QStringLiteral("模型A"), QStringLiteral("fw"),
                                   QStringLiteral("arch"), 1, 1, first_id, err));

    // uuid 唯一约束：第二次插入必须失败且错误信息经统一错误边界返回
    int64_t second_id = -1;
    const bool ok = fixture.db()->addModel(QStringLiteral("dup-uuid"), QStringLiteral("模型B"), QStringLiteral("fw"),
                                           QStringLiteral("arch"), 2, 2, second_id, err);
    QVERIFY2(!ok, "重复 uuid 应触发唯一约束失败");
    QVERIFY2(!err.isEmpty(), "失败时应返回错误信息");
}

void DatabaseRepositoriesTest::deleteTagClassRemovesRelationIds()
{
    RepositoryFixture fixture(QStringLiteral("tag_delete"));
    QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

    int64_t tag_a = -1;
    int64_t tag_b = -1;
    QString err;
    QVERIFY(fixture.db()->addTagClass(QStringLiteral("缺陷"), {}, tag_a, err));
    QVERIFY(fixture.db()->addTagClass(QStringLiteral("划痕"), {}, tag_b, err));

    // 用标注关系验证（tags 表按值引用目标 ID，不依赖目标行存在）
    QVERIFY(fixture.db()->addTagsToLabels({201, 202}, tag_a, err));
    QVERIFY(fixture.db()->addTagsToLabels({201}, tag_b, err));

    std::vector<int64_t>              rel_label_ids;
    std::vector<std::vector<int64_t>> rel_label_tag_ids;
    std::vector<int64_t>              rel_image_ids;
    std::vector<std::vector<int64_t>> rel_image_tag_ids;
    QVERIFY(fixture.db()->getAllTags(rel_image_ids, rel_image_tag_ids, rel_label_ids, rel_label_tag_ids, err));
    QCOMPARE(rel_label_ids.size(), size_t{2});

    // 删除 tag_a 后：仅剩 tag_b 的行保留（202 的关系行因集合为空被删除）
    QVERIFY(fixture.db()->deleteTagClass(tag_a, err));
    rel_label_ids.clear();
    rel_label_tag_ids.clear();
    QVERIFY(fixture.db()->getAllTags(rel_image_ids, rel_image_tag_ids, rel_label_ids, rel_label_tag_ids, err));
    QCOMPARE(rel_label_ids.size(), size_t{1});
    QCOMPARE(rel_label_ids.front(), qint64{201});
    QVERIFY(std::find(rel_label_tag_ids.front().begin(), rel_label_tag_ids.front().end(), tag_a)
            == rel_label_tag_ids.front().end());
    QVERIFY(std::find(rel_label_tag_ids.front().begin(), rel_label_tag_ids.front().end(), tag_b)
            != rel_label_tag_ids.front().end());
}

void DatabaseRepositoriesTest::tagRelationsScopeByTargetType()
{
    RepositoryFixture fixture(QStringLiteral("tag_scope"));
    QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

    int64_t tag_id = -1;
    QString err;
    QVERIFY(fixture.db()->addTagClass(QStringLiteral("复核"), {}, tag_id, err));

    // 图像 301 与标注 401 各挂同一 tag 类别
    QVERIFY(fixture.db()->addTagsToImages({301}, tag_id, err));
    QVERIFY(fixture.db()->addTagsToLabels({401}, tag_id, err));

    // 移除图像侧不影响标注侧
    QVERIFY(fixture.db()->removeTagsForImages({301}, err));
    std::vector<int64_t>              image_ids;
    std::vector<std::vector<int64_t>> image_tag_ids;
    std::vector<int64_t>              label_ids;
    std::vector<std::vector<int64_t>> label_tag_ids;
    QVERIFY(fixture.db()->getAllTags(image_ids, image_tag_ids, label_ids, label_tag_ids, err));
    QCOMPARE(image_ids.size(), size_t{0});
    QCOMPARE(label_ids.size(), size_t{1});
    QCOMPARE(label_tag_ids.front().size(), size_t{1});
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    DatabaseRepositoriesTest test;

    // 本机 stdout 捕获失效：强制结果写入文件以便 ctest 之外诊断。
    QStringList args;
    for (int i = 0; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    const QString result_path = QCoreApplication::applicationDirPath()
                                + QStringLiteral("/repositories_test_result.txt");
    args << QStringLiteral("-o") << result_path + QStringLiteral(",txt");
    QList<QByteArray> arg_bytes;
    QVector<char *>   argv_ptrs;
    arg_bytes.reserve(args.size());
    for (const QString &a : args) arg_bytes.push_back(a.toLocal8Bit());
    for (QByteArray &a : arg_bytes) argv_ptrs.push_back(a.data());
    const int rc = QTest::qExec(&test, static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
    return rc;
}

#include "test_DatabaseRepositories.moc"
