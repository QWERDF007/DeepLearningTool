#include "core/CoreDef.h"
#include "data/DataIO.h"
#include "data/ImportDatabaseWriter.h"
#include "data/LabelClasses.h"
#include "database/DataBase.h"

#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <vector>

class LabelClassesModelTest final : public QObject
{
    Q_OBJECT

private slots:
    void importWriterAssignsZeroBasedOrdinalIndices()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString project_path = QDir(dir.path()).filePath(QStringLiteral("test_project.dlpro"));
        dltool::database::ProjectDataBase database(project_path);
        QString err;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QVERIFY(database.initProject(QStringLiteral("ImportTest"),
                                     static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
                                     project_path, QStringLiteral(""), dir.path(), now, now, err));

        int64_t dataset_id = -1;
        QVERIFY(database.addDataset(QStringLiteral("test_dataset"), dataset_id, err));

        // 通过 ImportDatabaseWriter 写入 5 个类别的批次数据
        const std::map<QString, QString> label_class_info = {
            { QStringLiteral("good"),         QStringLiteral("#e62e2e") },
            { QStringLiteral("bent_lead"),    QStringLiteral("#2e63e6") },
            { QStringLiteral("cut_lead"),     QStringLiteral("#2ee6c7") },
            { QStringLiteral("damaged_case"), QStringLiteral("#99e62e") },
            { QStringLiteral("misplaced"),    QStringLiteral("#e6992e") },
        };

        const std::map<QString, QString> label_class_groups = {
            { QStringLiteral("good"),         QStringLiteral("good") },
            { QStringLiteral("bent_lead"),    QStringLiteral("anomaly") },
            { QStringLiteral("cut_lead"),     QStringLiteral("anomaly") },
            { QStringLiteral("damaged_case"), QStringLiteral("anomaly") },
            { QStringLiteral("misplaced"),    QStringLiteral("anomaly") },
        };

        auto *writer = new dltool::data::ImportDatabaseWriter(
            project_path, static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
            1, // Folder format
            dataset_id, label_class_groups, nullptr, this);

        writer->onDataBatchReady(dataset_id, {}, {}, {}, label_class_info, {}, 0, 0);
        writer->onImporterFinished(true, {}, {});
        delete writer;

        // 验证数据库中类别数量与 ordinal_index
        std::vector<int64_t>              class_ids, ordinal_indices;
        std::vector<QString>              names, colors, shortcuts;
        std::vector<std::vector<uint8_t>> extra_data;
        QVERIFY(database.getAllLabelClasses(class_ids, names, colors, shortcuts, ordinal_indices, extra_data, err));
        QCOMPARE(class_ids.size(), size_t{5});

        std::vector<int64_t> sorted_ordinals = ordinal_indices;
        std::sort(sorted_ordinals.begin(), sorted_ordinals.end());
        for (size_t i = 0; i < sorted_ordinals.size(); ++i)
        {
            QCOMPARE(sorted_ordinals[i], static_cast<int64_t>(i));
        }

        // 用 LabelClassesListModel 加载，检查每一行都是有效类别
        dltool::data::LabelClassesListModel model(&database);
        QCOMPARE(model.rowCount(), 5);
        for (int row = 0; row < 5; ++row)
        {
            const QModelIndex idx = model.index(row, 0);
            const qint64 id = model.data(idx, dltool::data::LabelClassesListModel::LabelClassIdRole).toLongLong();
            QVERIFY2(id >= 0, qPrintable(QString("row %1 should map to a valid class id").arg(row)));

            const QString name = model.data(idx, dltool::data::LabelClassesListModel::NameRole).toString();
            QVERIFY2(!name.isEmpty(), qPrintable(QString("row %1 should have a non-empty name").arg(row)));

            const QString color = model.data(idx, dltool::data::LabelClassesListModel::ColorRole).toString();
            QVERIFY2(!color.isEmpty(), qPrintable(QString("row %1 should have a non-empty color").arg(row)));
        }
    }

    void importWriterAvoidsColorCollisionsAcrossImports()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString project_path = QDir(dir.path()).filePath(QStringLiteral("color_collision.dlpro"));
        dltool::database::ProjectDataBase database(project_path);
        QString err;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QVERIFY(database.initProject(QStringLiteral("ColorTest"),
                                     static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
                                     project_path, QStringLiteral(""), dir.path(), now, now, err));

        int64_t ds1_id = -1;
        QVERIFY(database.addDataset(QStringLiteral("train"), ds1_id, err));

        // 第一次导入：导入 good 类别，颜色为 #e62e2e
        {
            auto *writer = new dltool::data::ImportDatabaseWriter(
                project_path, static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
                1, ds1_id, {{ QStringLiteral("good"), QStringLiteral("good") }}, nullptr, this);
            writer->onDataBatchReady(ds1_id, {}, {}, {}, {{ QStringLiteral("good"), QStringLiteral("#e62e2e") }}, {}, 0, 0);
            writer->onImporterFinished(true, {}, {});
            delete writer;
        }

        int64_t ds2_id = -1;
        QVERIFY(database.addDataset(QStringLiteral("test"), ds2_id, err));

        // 第二次导入：导入 bent_lead 和 cut_lead，其中 bent_lead 传入了冲突的颜色 #e62e2e
        {
            auto *writer = new dltool::data::ImportDatabaseWriter(
                project_path, static_cast<int>(dltool::core::DeepLearningMethod::AnomalyDetection),
                1, ds2_id,
                {{ QStringLiteral("bent_lead"), QStringLiteral("anomaly") },
                 { QStringLiteral("cut_lead"), QStringLiteral("anomaly") }},
                nullptr, this);
            writer->onDataBatchReady(ds2_id, {}, {}, {},
                                     {{ QStringLiteral("bent_lead"), QStringLiteral("#e62e2e") },
                                      { QStringLiteral("cut_lead"),  QStringLiteral("#2e63e6") }},
                                     {}, 0, 0);
            writer->onImporterFinished(true, {}, {});
            delete writer;
        }

        // 验证数据库中所有类别的颜色绝对互不重复
        std::vector<int64_t>              class_ids, ordinal_indices;
        std::vector<QString>              names, colors, shortcuts;
        std::vector<std::vector<uint8_t>> extra_data;
        QVERIFY(database.getAllLabelClasses(class_ids, names, colors, shortcuts, ordinal_indices, extra_data, err));
        QCOMPARE(class_ids.size(), size_t{3});

        std::set<QString> unique_colors;
        for (size_t i = 0; i < colors.size(); ++i)
        {
            const QString norm_color = QColor(colors[i]).name(QColor::HexRgb).toLower();
            QVERIFY2(!norm_color.isEmpty(), "Category color should be valid");
            QVERIFY2(unique_colors.insert(norm_color).second,
                     qPrintable(QString("Color collision detected: %1 used by class %2").arg(norm_color, names[i])));
        }
    }
};

QTEST_GUILESS_MAIN(LabelClassesModelTest)

#include "test_LabelClassesModel.moc"
