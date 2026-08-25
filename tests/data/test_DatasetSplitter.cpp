#include "data/DatasetSplitter.h"

#include "core/CoreDef.h"

#include <QCoreApplication>
#include <QTest>

#include <algorithm>
#include <map>
#include <set>
#include <vector>

using namespace dltool::data;

namespace {

std::vector<DatasetSplitItem> makeClassificationItems(const int class_count, const int items_per_class)
{
    std::vector<DatasetSplitItem> items;
    for (int class_id = 0; class_id < class_count; ++class_id)
    {
        for (int index = 0; index < items_per_class; ++index)
        {
            DatasetSplitItem item;
            item.image_id              = class_id * 100 + index;
            item.image_label_class_id = class_id;
            items.push_back(item);
        }
    }
    return items;
}

std::set<int64_t> asSet(const std::vector<int64_t> &ids)
{
    return {ids.cbegin(), ids.cend()};
}

void verifyPartition(const DatasetSplitResult &result, const std::vector<DatasetSplitItem> &items)
{
    QVERIFY(result.success);
    const std::set<int64_t> input_ids = [&items]
    {
        std::set<int64_t> ids;
        for (const DatasetSplitItem &item : items)
            ids.insert(item.image_id);
        return ids;
    }();

    const std::set<int64_t> train_ids = asSet(result.train_image_ids);
    const std::set<int64_t> validation_ids = asSet(result.validation_image_ids);
    const std::set<int64_t> test_ids = asSet(result.test_image_ids);
    QCOMPARE(train_ids.size(), result.train_image_ids.size());
    QCOMPARE(validation_ids.size(), result.validation_image_ids.size());
    QCOMPARE(test_ids.size(), result.test_image_ids.size());

    std::set<int64_t> union_ids = train_ids;
    union_ids.insert(validation_ids.cbegin(), validation_ids.cend());
    union_ids.insert(test_ids.cbegin(), test_ids.cend());
    QCOMPARE(union_ids, input_ids);

    for (const int64_t id : train_ids)
    {
        QVERIFY(!validation_ids.contains(id));
        QVERIFY(!test_ids.contains(id));
    }
    for (const int64_t id : validation_ids)
        QVERIFY(!test_ids.contains(id));
}

} // namespace

class DatasetSplitterTest final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidRatios()
    {
        DatasetSplitRatios ratios;
        ratios.train = 0.7;
        ratios.test  = 0.2;
        QString            error;
        QVERIFY(!DatasetSplitter::validateRatios(ratios, &error));
        QVERIFY(!error.isEmpty());

        ratios.train = -0.1;
        ratios.test  = 1.1;
        error.clear();
        QVERIFY(!DatasetSplitter::validateRatios(ratios, &error));
        QVERIFY(!error.isEmpty());

        ratios.train          = 0.8;
        ratios.validation     = 0.1;
        ratios.test           = 0.1;
        ratios.use_validation = false;
        error.clear();
        QVERIFY(!DatasetSplitter::validateRatios(ratios, &error));
        QVERIFY(!error.isEmpty());
    }

    void rejectsUnsupportedMethod()
    {
        DatasetSplitRatios ratios;
        const DatasetSplitResult result
            = DatasetSplitter::split(makeClassificationItems(2, 2), dltool::core::DeepLearningMethod::Pose, ratios);
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
    }

    void preservesStratifiedClassificationPartitions()
    {
        const std::vector<DatasetSplitItem> items = makeClassificationItems(2, 6);
        DatasetSplitRatios                   ratios;
        ratios.train          = 0.5;
        ratios.validation     = 0.25;
        ratios.test           = 0.25;
        ratios.use_validation = true;

        const DatasetSplitResult result
            = DatasetSplitter::split(items, dltool::core::DeepLearningMethod::Classification, ratios, 42);
        verifyPartition(result, items);
        QCOMPARE(result.train_image_ids.size(), size_t(6));
        QCOMPARE(result.validation_image_ids.size(), size_t(4));
        QCOMPARE(result.test_image_ids.size(), size_t(2));

        for (const std::vector<int64_t> *partition : {&result.train_image_ids, &result.validation_image_ids,
                                                       &result.test_image_ids})
        {
            std::map<int64_t, int> counts;
            for (const int64_t image_id : *partition)
                ++counts[image_id / 100];
            QCOMPARE(counts.size(), size_t(2));
        }
    }

    void anomalyDetectionUsesImageLevelClass()
    {
        std::vector<DatasetSplitItem> items;
        for (int index = 0; index < 12; ++index)
        {
            DatasetSplitItem item;
            item.image_id              = index;
            item.image_label_class_id = index < 6 ? 10 : 20;
            item.label_class_ids       = {999};
            items.push_back(item);
        }

        DatasetSplitRatios ratios;
        ratios.train = 0.5;
        ratios.test  = 0.5;
        const DatasetSplitResult result
            = DatasetSplitter::split(items, dltool::core::DeepLearningMethod::AnomalyDetection, ratios, 7);
        verifyPartition(result, items);
        QCOMPARE(result.train_image_ids.size(), size_t(6));
        QCOMPARE(result.test_image_ids.size(), size_t(6));

        for (const std::vector<int64_t> *partition : {&result.train_image_ids, &result.test_image_ids})
        {
            std::set<int64_t> classes;
            for (const int64_t image_id : *partition)
                classes.insert(image_id < 6 ? 10 : 20);
            QCOMPARE(classes.size(), size_t(2));
        }
    }

    void detectionAndSegmentationKeepImageTogether()
    {
        std::vector<DatasetSplitItem> items;
        for (int index = 0; index < 12; ++index)
        {
            DatasetSplitItem item;
            item.image_id = index;
            item.label_class_ids = index % 2 == 0 ? std::vector<int64_t>{1, 2} : std::vector<int64_t>{2};
            items.push_back(item);
        }

        DatasetSplitRatios ratios;
        ratios.train = 0.75;
        ratios.test  = 0.25;
        for (const int method : {dltool::core::DeepLearningMethod::Detection,
                                 dltool::core::DeepLearningMethod::Segmentation})
        {
            const DatasetSplitResult result = DatasetSplitter::split(items, method, ratios, 123);
            verifyPartition(result, items);
            QCOMPARE(result.train_image_ids.size(), size_t(10));
            QCOMPARE(result.test_image_ids.size(), size_t(2));
        }
    }

    void fixedSeedIsReproducible()
    {
        const std::vector<DatasetSplitItem> items = makeClassificationItems(3, 9);
        DatasetSplitRatios                   ratios;
        ratios.train = 0.7;
        ratios.test  = 0.3;

        const DatasetSplitResult first
            = DatasetSplitter::split(items, dltool::core::DeepLearningMethod::Classification, ratios, 2024);
        const DatasetSplitResult second
            = DatasetSplitter::split(items, dltool::core::DeepLearningMethod::Classification, ratios, 2024);
        QVERIFY(first.success);
        QVERIFY(second.success);
        QCOMPARE(first.train_image_ids, second.train_image_ids);
        QCOMPARE(first.validation_image_ids, second.validation_image_ids);
        QCOMPARE(first.test_image_ids, second.test_image_ids);
    }

    void rejectsEmptyAndDuplicateItems()
    {
        DatasetSplitRatios ratios;
        QVERIFY(!DatasetSplitter::split({}, dltool::core::DeepLearningMethod::Classification, ratios).success);

        const std::vector<DatasetSplitItem> duplicate_items = {{1, 0, {}}, {1, 1, {}}};
        const DatasetSplitResult result
            = DatasetSplitter::split(duplicate_items, dltool::core::DeepLearningMethod::Classification, ratios);
        QVERIFY(!result.success);
        QVERIFY(!result.error.isEmpty());
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    DatasetSplitterTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_DatasetSplitter.moc"
