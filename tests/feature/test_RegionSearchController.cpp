#include "feature/RegionSearchController.h"
#include "feature/SearchControllerUtils.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/GlobalFilter.h"
#include "database/DataBase.h"

#include <inferrt/features/DinoRegionSearch.hpp>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class RegionSearchTest final : public QObject
{
    Q_OBJECT

private:
    struct TestFixture
    {
        QTemporaryDir dir;
        QString project_path;
        std::unique_ptr<dltool::database::ProjectDataBase> database;
        std::unique_ptr<dltool::data::DataManager> data_manager;
        int64_t dataset_id{-1};
        int64_t class_car{-1};
        int64_t class_person{-1};
        std::vector<int64_t> image_ids;
        std::vector<QString> image_paths;
        int64_t bbox_label_id{-1};
        int64_t poly_label_id{-1};

        ~TestFixture()
        {
            if (data_manager)
                data_manager->shutdown();
            data_manager.reset();
            database.reset();
        }

        bool init()
        {
            if (!dir.isValid())
                return false;
            project_path = QDir(dir.path()).filePath(QStringLiteral("region_search_test.dlpro"));
            database = std::make_unique<dltool::database::ProjectDataBase>(project_path);
            QString err;
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            if (!database->initProject(QStringLiteral("RegionSearchTest"),
                                       static_cast<int>(dltool::core::DeepLearningMethod::Segmentation),
                                       project_path, QStringLiteral("desc"), dir.path(), now, now, err))
                return false;

            if (!database->addDataset(QStringLiteral("dataset1"), dataset_id, err))
                return false;

            if (!database->addLabelClass(QStringLiteral("car"), QStringLiteral("#ff0000"), QStringLiteral("1"), 0, {}, class_car, err))
                return false;
            if (!database->addLabelClass(QStringLiteral("person"), QStringLiteral("#00ff00"), QStringLiteral("2"), 1, {}, class_person, err))
                return false;

            for (int i = 0; i < 2; ++i)
            {
                const QString p = QDir(dir.path()).filePath(QString("img_%1.png").arg(i));
                QImage img(200, 200, QImage::Format_RGB888);
                img.fill(i == 0 ? Qt::red : Qt::blue);
                img.save(p);
                image_paths.push_back(p);
            }

            if (!database->addImages(dataset_id, image_paths, image_ids, err))
                return false;

            data_manager = std::make_unique<dltool::data::DataManager>(
                static_cast<int>(dltool::core::DeepLearningMethod::Segmentation), database.get(), dir.path());
            data_manager->waitForOperations();

            // Add bbox label on image 0
            QVariantMap bbox_data;
            bbox_data[QStringLiteral("x")] = 10.0;
            bbox_data[QStringLiteral("y")] = 10.0;
            bbox_data[QStringLiteral("width")] = 40.0;
            bbox_data[QStringLiteral("height")] = 40.0;

            std::vector<int64_t> added_ids;
            if (!data_manager->addLabelsWithIds({image_ids[0]}, {class_car}, {bbox_data}, &added_ids, &err) || added_ids.empty())
                return false;
            bbox_label_id = added_ids[0];

            // Add polygon label on image 0
            QVariantMap poly_data;
            QVariantList pts;
            pts.append(QVariantMap{{QStringLiteral("x"), 100.0}, {QStringLiteral("y"), 100.0}});
            pts.append(QVariantMap{{QStringLiteral("x"), 180.0}, {QStringLiteral("y"), 100.0}});
            pts.append(QVariantMap{{QStringLiteral("x"), 180.0}, {QStringLiteral("y"), 180.0}});
            pts.append(QVariantMap{{QStringLiteral("x"), 100.0}, {QStringLiteral("y"), 180.0}});
            poly_data[QStringLiteral("points")] = pts;

            added_ids.clear();
            if (!data_manager->addLabelsWithIds({image_ids[0]}, {class_person}, {poly_data}, &added_ids, &err) || added_ids.empty())
                return false;
            poly_label_id = added_ids[0];

            return true;
        }
    };

private slots:
    void captureQueryValidatesBboxAndPolygon()
    {
        TestFixture fixture;
        QVERIFY(fixture.init());

        dltool::feature::RegionSearchController controller(fixture.data_manager.get());

        // Invalid IDs
        QVERIFY(!controller.captureQuery(-1));
        QVERIFY(!controller.queryValid());
        QVERIFY(!controller.captureQuery(99999));
        QVERIFY(!controller.queryValid());

        // Valid bbox label
        QVERIFY(controller.captureQuery(fixture.bbox_label_id));
        QVERIFY(controller.queryValid());
        QCOMPARE(controller.queryLabelId(), fixture.bbox_label_id);
        QCOMPARE(controller.queryImageId(), fixture.image_ids[0]);
        QCOMPARE(controller.queryClassId(), fixture.class_car);
        QCOMPARE(controller.queryRect(), QRectF(10, 10, 40, 40));

        // Valid polygon label
        QVERIFY(controller.captureQuery(fixture.poly_label_id));
        QVERIFY(controller.queryValid());
        QCOMPARE(controller.queryLabelId(), fixture.poly_label_id);
        QCOMPARE(controller.queryImageId(), fixture.image_ids[0]);
        QCOMPARE(controller.queryClassId(), fixture.class_person);
        QCOMPARE(controller.queryRect(), QRectF(100, 100, 80, 80));
    }

    void checkNeedsBuildTracksScope()
    {
        TestFixture fixture;
        QVERIFY(fixture.init());

        dltool::feature::RegionSearchController controller(fixture.data_manager.get());

        // Without index files, needsBuild is true
        QVERIFY(controller.checkNeedsBuild({fixture.dataset_id}));
        QVERIFY(controller.needsBuild());
        QCOMPARE(controller.indexedImageCount(), 0);

        // Create index files
        const QString index_dir = QDir(fixture.dir.path()).filePath(QStringLiteral("region_search"));
        QDir().mkpath(index_dir);

        {
            std::ofstream f_idx(QDir(index_dir).filePath(QStringLiteral("index.yaml")).toStdString());
            f_idx << "total_images: 2\n";
        }
        {
            std::ofstream f_scope(QDir(index_dir).filePath(QStringLiteral("index_scope.yaml")).toStdString());
            f_scope << "dataset_ids:\n  - " << fixture.dataset_id << "\nfailed_paths: []\n";
        }

        // Now scope covers dataset1
        QVERIFY(!controller.checkNeedsBuild({fixture.dataset_id}));
        QVERIFY(!controller.needsBuild());
        QCOMPARE(controller.indexedImageCount(), 2);

        // Asking for an un-indexed dataset triggers needsBuild
        QVERIFY(controller.checkNeedsBuild({fixture.dataset_id, 9999}));
        QVERIFY(controller.needsBuild());
    }

    void commitResultsDeduplicationAndReuse()
    {
        TestFixture fixture;
        QVERIFY(fixture.init());

        dltool::feature::RegionSearchController controller(fixture.data_manager.get());
        QVERIFY(controller.captureQuery(fixture.bbox_label_id));

        // Prepare test job with target class car
        controller.prepareTestJob(fixture.class_car);

        // Construct search results
        auto response = std::make_shared<irt::features::DinoSearchResponse>();

        // 1. Self candidate on img_0 (IoU with query [10,10,50,50] is 1.0) -> must be self_skipped
        irt::features::DinoSearchResult hit1;
        hit1.image_id = fixture.image_ids[0];
        hit1.bbox = {10.0f, 10.0f, 50.0f, 50.0f};
        hit1.score = 0.99f;
        response->results.push_back(hit1);

        // 2. New candidate on img_1 -> accepted
        irt::features::DinoSearchResult hit2;
        hit2.image_id = fixture.image_ids[1];
        hit2.bbox = {20.0f, 20.0f, 80.0f, 80.0f};
        hit2.score = 0.95f;
        response->results.push_back(hit2);

        // 3. Duplicate candidate in same batch on img_1 (IoU >= 0.90 with hit2) -> duplicate_skipped
        irt::features::DinoSearchResult hit3;
        hit3.image_id = fixture.image_ids[1];
        hit3.bbox = {21.0f, 21.0f, 80.0f, 80.0f};
        hit3.score = 0.94f;
        response->results.push_back(hit3);

        // 4. Another distinct candidate on img_1 -> accepted
        irt::features::DinoSearchResult hit4;
        hit4.image_id = fixture.image_ids[1];
        hit4.bbox = {120.0f, 120.0f, 180.0f, 180.0f};
        hit4.score = 0.90f;
        response->results.push_back(hit4);

        controller.commitResults(response);

        QCOMPARE(controller.returnedCount(), 4);
        QCOMPARE(controller.createdCount(), 2);
        QCOMPARE(controller.reusedCount(), 0);
        QCOMPARE(controller.skippedCount(), 2); // hit1 self_skipped + hit3 duplicate_skipped

        // Verify "区域检索生成" tag was created and applied
        const int64_t tag_id = fixture.data_manager->findTagClassId(QString("区域检索生成"));
        QVERIFY(tag_id >= 0);

        const auto filter_results = fixture.data_manager->globalFilter()->regionSearchResultIds();
        QCOMPARE(filter_results.size(), size_t{2});
        for (const int64_t lid : filter_results)
        {
            const auto tags = fixture.data_manager->labelTagIds(lid);
            QVERIFY(tags.count(tag_id) > 0);
        }

        // Test Reuse on subsequent search
        controller.prepareTestJob(fixture.class_car);
        auto reuse_response = std::make_shared<irt::features::DinoSearchResponse>();
        reuse_response->results.push_back(hit4); // same bbox as hit4 previously committed

        controller.commitResults(reuse_response);
        QCOMPARE(controller.createdCount(), 0);
        QCOMPARE(controller.reusedCount(), 1);
        QCOMPARE(controller.skippedCount(), 0);

        // Test manual label collision (conflict / manual label with same IoU is skipped, not overwritten)
        // Add manual label of class_person on img_1
        QVariantMap person_data;
        person_data[QStringLiteral("x")] = 50.0;
        person_data[QStringLiteral("y")] = 50.0;
        person_data[QStringLiteral("width")] = 40.0;
        person_data[QStringLiteral("height")] = 40.0;
        std::vector<int64_t> manual_ids;
        QString err;
        QVERIFY(fixture.data_manager->addLabelsWithIds({fixture.image_ids[1]}, {fixture.class_person}, {person_data}, &manual_ids, &err));

        // Attempt region search candidate of class_car overlapping with this manual label
        controller.prepareTestJob(fixture.class_car);
        auto conflict_response = std::make_shared<irt::features::DinoSearchResponse>();
        irt::features::DinoSearchResult hit_conflict;
        hit_conflict.image_id = fixture.image_ids[1];
        hit_conflict.bbox = {50.0f, 50.0f, 90.0f, 90.0f};
        hit_conflict.score = 0.88f;
        conflict_response->results.push_back(hit_conflict);

        controller.commitResults(conflict_response);
        QCOMPARE(controller.createdCount(), 0);
        QCOMPARE(controller.reusedCount(), 0);
        QCOMPARE(controller.skippedCount(), 1); // duplicate_skipped due to existing manual label
    }

    void showLatestResultsConfiguresFilter()
    {
        TestFixture fixture;
        QVERIFY(fixture.init());

        dltool::feature::RegionSearchController controller(fixture.data_manager.get());
        QSignalSpy spy(&controller, &dltool::feature::RegionSearchController::requestNavigateToReview);

        // First set region search results so condition is ready
        fixture.data_manager->setRegionSearchResults({fixture.bbox_label_id});

        controller.showLatestResults();

        QCOMPARE(spy.count(), 1);
        auto *filter = fixture.data_manager->globalFilter();
        QVERIFY(filter != nullptr);
        QVERIFY(filter->isFilterEnabled(dltool::data::GlobalFilter::FilterType::Custom));

        const auto custom_conditions = filter->getActiveIds(dltool::data::GlobalFilter::FilterType::Custom);
        const auto it = std::find(custom_conditions.begin(), custom_conditions.end(),
                                  static_cast<int64_t>(dltool::data::GlobalFilter::CustomCondition::RegionSearchResult));
        QVERIFY(it != custom_conditions.end());
    }

    void shutdownGating()
    {
        TestFixture fixture;
        QVERIFY(fixture.init());

        dltool::feature::RegionSearchController controller(fixture.data_manager.get());

        controller.requestShutdown();
        QCOMPARE(controller.validationError(), QString("区域检索控制器正在关闭"));
        QVERIFY(!controller.start({}));
        QCOMPARE(controller.errorText(), QString("区域检索控制器正在关闭"));

        controller.shutdown();
        controller.shutdown(); // Idempotent
        QVERIFY(!controller.isBusy());
    }

    void searchScopeAndPropertiesValidation()
    {
        TestFixture fixture;
        QVERIFY(fixture.init());

        dltool::feature::RegionSearchController controller(fixture.data_manager.get());
        QCOMPARE(controller.isRunning(), false);
        QCOMPARE(controller.lastError(), QString());

        // Empty ids should fail validation
        QVERIFY(!controller.search({}, {}));
        QCOMPARE(controller.lastError(), QString("请选择要检索的标注"));

        // Invalid query label id
        QVERIFY(!controller.search({999999}, {}));
        QCOMPARE(controller.lastError(), QString("无效的查询标注"));

        // Empty search scope
        QVERIFY(!controller.search({fixture.bbox_label_id}, {}));
        QCOMPARE(controller.lastError(), QString("请至少选择一个搜索数据集"));
    }

    void chinesePathGalleryAndQueryHandling()
    {
        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());

        const QString sub_dir = QDir(temp_dir.path()).filePath(QString("异常检测数据集/测试类别"));
        QDir().mkpath(sub_dir);

        const QString project_path = QDir(sub_dir).filePath(QString("项目.dlpro"));
        auto database = std::make_unique<dltool::database::ProjectDataBase>(project_path);
        QString err;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        QVERIFY(database->initProject(QString("中文测试项目"),
                                      static_cast<int>(dltool::core::DeepLearningMethod::Detection),
                                      project_path, QString("desc"), sub_dir, now, now, err));

        int64_t ds_id = -1;
        QVERIFY(database->addDataset(QString("测试数据集"), ds_id, err));

        int64_t class_id = -1;
        QVERIFY(database->addLabelClass(QString("缺陷"), QString("#ff0000"), QString("1"), 0, {}, class_id, err));

        const QString img_p = QDir(sub_dir).filePath(QString("样本图像_01.png"));
        QImage img(200, 200, QImage::Format_RGB888);
        img.fill(Qt::yellow);
        img.save(img_p);

        std::vector<int64_t> img_ids;
        QVERIFY(database->addImages(ds_id, {img_p}, img_ids, err));

        auto data_manager = std::make_unique<dltool::data::DataManager>(
            static_cast<int>(dltool::core::DeepLearningMethod::Detection), database.get(), sub_dir);
        data_manager->waitForOperations();

        QVariantMap bbox_data;
        bbox_data[QStringLiteral("x")] = 20.0;
        bbox_data[QStringLiteral("y")] = 20.0;
        bbox_data[QStringLiteral("width")] = 50.0;
        bbox_data[QStringLiteral("height")] = 50.0;

        std::vector<int64_t> added_ids;
        QVERIFY(data_manager->addLabelsWithIds({img_ids[0]}, {class_id}, {bbox_data}, &added_ids, &err));
        const int64_t query_lbl = added_ids[0];

        dltool::feature::RegionSearchController controller(data_manager.get());
        QVERIFY(controller.captureQuery(query_lbl));
        QVERIFY(controller.queryValid());
        QCOMPARE(controller.queryImagePath(), img_p);

        controller.prepareTestJob(class_id);

        auto response = std::make_shared<irt::features::DinoSearchResponse>();
        irt::features::DinoSearchResult hit;
        hit.image_id = img_ids[0];
        hit.bbox = {80.0f, 80.0f, 140.0f, 140.0f};
        hit.score = 0.92f;
        response->results.push_back(hit);

        controller.commitResults(response);
        QCOMPARE(controller.returnedCount(), 1);
        QCOMPARE(controller.createdCount(), 1);
        QCOMPARE(controller.skippedCount(), 0);

        data_manager->shutdown();
    }

    void batchProgressFormattingAndResolution()
    {
        using namespace dltool::feature;

        // 1. DinoBuildProgress batch formatting
        irt::features::DinoBuildProgress bp1;
        bp1.stage = irt::features::DinoBuildStage::ExtractingViews;
        bp1.batch_index = 0;
        bp1.batch_begin = 0;
        bp1.batch_count = 8;
        bp1.processed_count = 8;
        bp1.total_count = 30;

        size_t processed = 0;
        size_t total = 0;
        QVERIFY(resolveProgressCount(bp1, 30, processed, total));
        QCOMPARE(processed, size_t{8});
        QCOMPARE(total, size_t{30});
        QCOMPARE(progressPercent(bp1, 30), 26);
        QCOMPARE(formatBuildProgressMessage(bp1, 30), QString("图库特征建库 [提取特征]: 批次 1 [1-8] / 30"));

        irt::features::DinoBuildProgress bp2;
        bp2.stage = irt::features::DinoBuildStage::ExtractingViews;
        bp2.batch_index = 1;
        bp2.batch_begin = 8;
        bp2.batch_count = 8;
        bp2.processed_count = 16;
        bp2.total_count = 30;
        QCOMPARE(formatBuildProgressMessage(bp2, 30), QString("图库特征建库 [提取特征]: 批次 2 [9-16] / 30"));

        irt::features::DinoBuildProgress bp_single;
        bp_single.stage = irt::features::DinoBuildStage::ExtractingViews;
        bp_single.batch_index = 0;
        bp_single.batch_begin = 0;
        bp_single.batch_count = 1;
        bp_single.processed_count = 1;
        bp_single.total_count = 30;
        QCOMPARE(formatBuildProgressMessage(bp_single, 30), QString("图库特征建库 [提取特征]: 1 / 30"));

        irt::features::DinoBuildProgress bp_loading;
        bp_loading.stage = irt::features::DinoBuildStage::LoadingModel;
        QCOMPARE(formatBuildProgressMessage(bp_loading, 30), QString("图库特征建库 [加载模型]"));

        // 2. DinoSearchProgress batch and stage formatting
        irt::features::DinoSearchProgress sp_batch;
        sp_batch.stage = irt::features::DinoSearchStage::RegionScan;
        sp_batch.batch_index = 0;
        sp_batch.batch_begin = 0;
        sp_batch.batch_count = 8;
        sp_batch.processed_count = 8;
        sp_batch.total_count = 30;
        QCOMPARE(formatSearchProgressMessage(sp_batch), QString("区域检索 [全图扫描]: 批次 1 (8 / 30)"));

        irt::features::DinoSearchProgress sp_single;
        sp_single.stage = irt::features::DinoSearchStage::FineExtract;
        sp_single.batch_index = 0;
        sp_single.batch_begin = 0;
        sp_single.batch_count = 1;
        sp_single.processed_count = 5;
        sp_single.total_count = 10;
        QCOMPARE(formatSearchProgressMessage(sp_single), QString("区域检索 [候选特征提取]: 5 / 10"));

        irt::features::DinoSearchProgress sp_msg;
        sp_msg.stage = irt::features::DinoSearchStage::QueryExtract;
        sp_msg.message = "提取完成";
        QCOMPARE(formatSearchProgressMessage(sp_msg), QString("区域检索 [提取查询特征]: 提取完成"));
    }
};

QTEST_MAIN(RegionSearchTest)
#include "test_RegionSearchController.moc"
