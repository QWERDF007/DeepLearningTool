#include "PersistentProjectFixture.h"

#include "data/DataFormat.h"
#include "data/DataManager.h"
#include "database/DataBase.h"

#include <QDirIterator>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QDir>
#include <QTest>

#include <algorithm>
#include <vector>

using namespace dltool::model::integration;

namespace {

struct ExpectedAsset final
{
    QString image_path;
    QSize   image_size;
    QString mask_path;
    int     mask_nonzero_pixels;
    QRect   mask_bbox;
};

const QList<ExpectedAsset> kExpectedAssets = {
    {QStringLiteral("MT_Blowhole/exp1_num_262480.jpg"), QSize(196, 246),
     QStringLiteral("MT_Blowhole/exp1_num_262480.png"), 36, QRect(108, 160, 6, 7)},
    {QStringLiteral("MT_Blowhole/exp1_num_297464.jpg"), QSize(213, 270),
     QStringLiteral("MT_Blowhole/exp1_num_297464.png"), 42, QRect(201, 122, 6, 9)},
    {QStringLiteral("MT_Blowhole/exp1_num_308015.jpg"), QSize(221, 272),
     QStringLiteral("MT_Blowhole/exp1_num_308015.png"), 45, QRect(211, 189, 6, 9)},
    {QStringLiteral("MT_Blowhole/exp1_num_317483.jpg"), QSize(218, 269),
     QStringLiteral("MT_Blowhole/exp1_num_317483.png"), 110, QRect(96, 219, 14, 11)},
    {QStringLiteral("MT_Blowhole/exp1_num_322605.jpg"), QSize(204, 257),
     QStringLiteral("MT_Blowhole/exp1_num_322605.png"), 143, QRect(182, 190, 11, 21)},
    {QStringLiteral("MT_Crack/exp2_num_116527.jpg"), QSize(291, 353),
     QStringLiteral("MT_Crack/exp2_num_116527.png"), 428, QRect(71, 40, 36, 46)},
    {QStringLiteral("MT_Crack/exp2_num_249619.jpg"), QSize(221, 264),
     QStringLiteral("MT_Crack/exp2_num_249619.png"), 1626, QRect(93, 0, 16, 259)},
    {QStringLiteral("MT_Crack/exp2_num_265639.jpg"), QSize(260, 383),
     QStringLiteral("MT_Crack/exp2_num_265639.png"), 434, QRect(99, 0, 95, 383)},
    {QStringLiteral("MT_Crack/exp2_num_339841.jpg"), QSize(360, 380),
     QStringLiteral("MT_Crack/exp2_num_339841.png"), 331, QRect(54, 3, 9, 49)},
    {QStringLiteral("OK/exp0_num_743.jpg"), QSize(240, 289), QString(), 0, QRect()},
    {QStringLiteral("OK/exp1_num_126299.jpg"), QSize(298, 334), QString(), 0, QRect()},
    {QStringLiteral("OK/exp1_num_126795.jpg"), QSize(304, 342), QString(), 0, QRect()},
    {QStringLiteral("OK/exp1_num_1810.jpg"), QSize(237, 272), QString(), 0, QRect()},
    {QStringLiteral("OK/exp1_num_855.jpg"), QSize(251, 295), QString(), 0, QRect()},
};

QStringList assetFiles(const QString &root, const QStringList &filters)
{
    QStringList files;
    QDirIterator iterator(root, filters, QDir::Files, QDirIterator::Subdirectories);
    const QDir root_directory(root);
    while (iterator.hasNext())
        files.push_back(QDir::fromNativeSeparators(root_directory.relativeFilePath(iterator.next())));
    files.sort();
    return files;
}

bool validateAssetFixture(QString *error)
{
    const QString image_root  = PersistentProjectFixture::imageRoot();
    const QString mask_root   = PersistentProjectFixture::maskRoot();
    QStringList   expected_images;
    QStringList   expected_masks;
    for (const ExpectedAsset &asset : kExpectedAssets)
    {
        expected_images.push_back(asset.image_path);
        if (!asset.mask_path.isEmpty())
            expected_masks.push_back(asset.mask_path);
    }
    expected_images.sort();
    expected_masks.sort();

    if (assetFiles(image_root, {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                QStringLiteral("*.png"), QStringLiteral("*.bmp"), QStringLiteral("*.webp")})
        != expected_images)
    {
        if (error != nullptr)
            *error = QStringLiteral("测试图片文件清单与固定夹具不一致");
        return false;
    }
    if (assetFiles(mask_root, {QStringLiteral("*.png")}) != expected_masks)
    {
        if (error != nullptr)
            *error = QStringLiteral("测试 Mask 文件清单与固定夹具不一致");
        return false;
    }

    for (const ExpectedAsset &asset : kExpectedAssets)
    {
        const QString image_path = QDir(image_root).filePath(asset.image_path);
        const QImage  image(image_path);
        if (image.isNull() || image.size() != asset.image_size)
        {
            if (error != nullptr)
                *error = QStringLiteral("测试图片尺寸不符合固定基线: %1").arg(asset.image_path);
            return false;
        }

        if (asset.mask_path.isEmpty())
            continue;

        const QString mask_path = QDir(mask_root).filePath(asset.mask_path);
        const QImage  mask = QImage(mask_path).convertToFormat(QImage::Format_Grayscale8);
        if (mask.isNull() || mask.size() != asset.image_size)
        {
            if (error != nullptr)
                *error = QStringLiteral("测试 Mask 尺寸不符合对应图片: %1").arg(asset.mask_path);
            return false;
        }

        int nonzero_pixels = 0;
        int x_min = mask.width();
        int y_min = mask.height();
        int x_max = -1;
        int y_max = -1;
        for (int y = 0; y < mask.height(); ++y)
        {
            const uchar *row = mask.constScanLine(y);
            for (int x = 0; x < mask.width(); ++x)
            {
                if (row[x] == 0)
                    continue;
                ++nonzero_pixels;
                x_min = std::min(x_min, x);
                y_min = std::min(y_min, y);
                x_max = std::max(x_max, x);
                y_max = std::max(y_max, y);
            }
        }
        const QRect bbox = x_max < 0 ? QRect() : QRect(x_min, y_min, x_max - x_min + 1, y_max - y_min + 1);
        if (nonzero_pixels != asset.mask_nonzero_pixels || bbox != asset.mask_bbox)
        {
            if (error != nullptr)
                *error = QStringLiteral("测试 Mask 标注区域不符合固定基线: %1").arg(asset.mask_path);
            return false;
        }
    }
    return true;
}

bool datasetLabelClassCounts(const PersistentProjectFixture &fixture, const qint64 dataset_id,
                             QMap<QString, int> *counts, QString *error)
{
    dltool::database::ProjectDataBase database(PersistentProjectFixture::projectDatabasePath());
    std::vector<int64_t>      image_ids;
    std::vector<QString>      image_paths;
    QString                   database_error;
    if (!database.getImages(dataset_id, image_ids, image_paths, database_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("读取数据集图像失败: %1").arg(database_error);
        return false;
    }

    std::vector<int64_t>             label_ids;
    std::vector<int64_t>             label_image_ids;
    std::vector<int64_t>             label_class_ids;
    std::vector<int64_t>             label_types;
    std::vector<std::vector<uint8_t>> label_data;
    if (!database.getAllLabels(label_ids, label_image_ids, label_class_ids, label_types, label_data, database_error))
    {
        if (error != nullptr)
            *error = QStringLiteral("读取项目标注失败: %1").arg(database_error);
        return false;
    }

    QSet<qint64> dataset_images;
    for (const qint64 image_id : image_ids)
        dataset_images.insert(image_id);

    QMap<QString, int> result;
    for (std::size_t index = 0; index < label_ids.size(); ++index)
    {
        if (!dataset_images.contains(label_image_ids[index]))
            continue;
        const QString class_name = fixture.dataManager()->labelClassName(label_class_ids[index]);
        if (class_name.isEmpty())
        {
            if (error != nullptr)
                *error = QStringLiteral("标注引用了不存在的类别: %1").arg(label_class_ids[index]);
            return false;
        }
        ++result[class_name];
    }
    if (counts != nullptr)
        *counts = result;
    return true;
}

} // namespace

class DataImportIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void importsFolderAndSeparateMaskFixtures()
    {
        PersistentProjectFixture fixture;
        QVERIFY2(fixture.isValid(), qPrintable(fixture.error()));

        QString error;
        QVERIFY2(validateAssetFixture(&error), qPrintable(error));

        const qint64 dataset_id = fixture.dataManager()->getDatasetId(PersistentProjectFixture::datasetName());
        QVERIFY2(dataset_id >= 0,
                 qPrintable(QStringLiteral("数据集不存在，请先运行 data-creation: %1")
                                .arg(PersistentProjectFixture::datasetName())));

        int image_count = 0;
        int label_count = 0;
        QVERIFY2(fixture.datasetCounts(dataset_id, &image_count, &label_count, &error), qPrintable(error));

        if (image_count < 14)
        {
            const QVariantMap groups = {
                {QStringLiteral("OK"), QStringLiteral("good")},
                {QStringLiteral("MT_Blowhole"), QStringLiteral("anomaly")},
                {QStringLiteral("MT_Crack"), QStringLiteral("anomaly")},
            };
            QVERIFY2(fixture.importData(dataset_id, dltool::data::DataFormat::Folder,
                                       PersistentProjectFixture::imageRoot(), {}, groups, &error),
                     qPrintable(error));
        }

        QVERIFY2(fixture.datasetCounts(dataset_id, &image_count, &label_count, &error), qPrintable(error));
        QCOMPARE(image_count, 14);

        if (label_count < 10)
        {
            QVERIFY2(fixture.importData(dataset_id, dltool::data::DataFormat::Mask,
                                       PersistentProjectFixture::imageRoot(), PersistentProjectFixture::maskRoot(), {},
                                       &error),
                     qPrintable(error));
        }

        QVERIFY2(fixture.datasetCounts(dataset_id, &image_count, &label_count, &error), qPrintable(error));
        QCOMPARE(image_count, 14);
        QCOMPARE(label_count, 10);
        QMap<QString, int> class_counts;
        QVERIFY2(datasetLabelClassCounts(fixture, dataset_id, &class_counts, &error), qPrintable(error));
        QCOMPARE(class_counts.keys(), QStringList({QStringLiteral("MT_Blowhole"), QStringLiteral("MT_Crack")}));
        QCOMPARE(class_counts.value(QStringLiteral("MT_Blowhole")), 5);
        QCOMPARE(class_counts.value(QStringLiteral("MT_Crack")), 5);
        QVERIFY(QDir(PersistentProjectFixture::imageRoot()).exists());
        QVERIFY(QDir(PersistentProjectFixture::maskRoot()).exists());
    }
};

QTEST_GUILESS_MAIN(DataImportIntegrationTest)

#include "test_DataImport.moc"
