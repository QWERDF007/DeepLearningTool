#include "feature/RegionSearchController.h"
#include "feature/SearchControllerUtils.h"
#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/GlobalFilter.h"
#include "database/DataBase.h"
#include "settings/GlobalSettings.h"
#include "dltool/settings/SettingsKeys.hpp"

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
            f_idx << "manifest:\n"
                  << "  schema_version: \"1.0\"\n"
                  << "  feature_version: \"v4_hadamard_sparse_fine\"\n"
                  << "  model_name: dinov2_vits14_reg4\n"
                  << "  weights_id: dinov2_vits14_reg4.wts\n"
                  << "  encoder_edge: 518\n"
                  << "  gallery_tile_edges:\n"
                  << "    - 512\n"
                  << "    - 1024\n"
                  << "    - 2048\n"
                  << "  view_overlap: 0.25\n"
                  << "  region_window_ratios:\n"
                  << "    - 1.0\n"
                  << "    - 0.5\n"
                  << "  region_window_stride_ratio: 0.5\n"
                  << "  region_min_valid_fraction: 0.5\n"
                  << "  coarse_dimension: 96\n"
                  << "  local_representatives: 64\n"
                  << "  merge_enabled: true\n"
                  << "  merge_epsilon: 0.1\n"
                  << "  max_leaf_side_patches: 4\n"
                  << "  quantize_int8: true\n"
                  << "  patch_size: 14\n"
                  << "  token_dimension: 384\n"
                  << "  preprocess_description: \"bgr8->rgb32f;scale=0.003922;mean0=0.485000;mean1=0.456000;mean2=0.406000;std0=0.229000;std1=0.224000;std2=0.225000;resize=linear;letterbox=topleft;pad=mean\"\n"
                  << "total_images: 2\n";
        }
        for (const auto &bin_name : {"views.npy", "offsets.npy", "region_meta.npy", "local_meta.npy",
                                     "region_scales.f32", "local_scales.f32", "region_vectors.i8", "local_vectors.i8"})
        {
            std::ofstream f_bin(QDir(index_dir).filePath(QString::fromLatin1(bin_name)).toStdString(), std::ios::binary);
            f_bin.put(0);
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

    void testApplyProfileToSettings()
    {
        TestFixture fixture;
        QVERIFY(fixture.init());

        dltool::feature::RegionSearchController controller(fixture.data_manager.get());
        auto *gs = dltool::settings::GlobalSettings::getInstance();
        QVERIFY(gs != nullptr);

        namespace gen_field = dltool::settings::generated::field;
        const int acc_key = static_cast<int>(dltool::settings::generated::AccessorKey::RegionSearch);

        QTemporaryDir temp_dir;
        QVERIFY(temp_dir.isValid());
        const QString profile_path = QDir(temp_dir.path()).filePath(QStringLiteral("custom_profile.yaml"));
        {
            std::ofstream f(profile_path.toStdString());
            f << "preset_id: test_preset\n"
              << "model:\n"
              << "  model_name: dinov3_vits16\n"
              << "  weights_file: custom_models/dinov3.wts\n"
              << "  encoder_edge: 512\n"
              << "gallery_views:\n"
              << "  gallery_tile_edges:\n"
              << "    - 512\n"
              << "    - 1024\n"
              << "  view_overlap: 0.30\n"
              << "descriptors:\n"
              << "  quantize_int8: false\n"
              << "coarse_scan:\n"
              << "  coarse_k: 128\n"
              << "  coarse_dedup_iou: 0.70\n"
              << "  final_k: 30\n"
              << "fine_match:\n"
              << "  fine_verify_k: 96\n"
              << "  fine_match_cosine_threshold: 0.65\n"
              << "  fine_nms_iou: 0.45\n"
              << "  consistency_mode: instance\n"
              << "  score_weight_template: 0.50\n"
              << "  score_weight_coverage: 0.30\n"
              << "  score_weight_consistency: 0.20\n"
              << "decision:\n"
              << "  enable_decision_threshold: true\n"
              << "  decision_threshold: 0.75\n"
              << "runtime:\n"
              << "  model_precision: fp16\n"
              << "  model_batch_size: 16\n"
              << "  query_deadline_ms: 45000\n"
              << "  scan_backend: cpu\n";
        }

        QVERIFY(controller.applyProfileToSettings(profile_path));

        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ModelName)).toString(), QStringLiteral("dinov3_vits16"));
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::EncoderEdge)).toInt(), 512);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ViewOverlap)).toDouble(), 0.30);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::QuantizeInt8)).toBool(), false);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ModelPrecision)).toInt(), 1);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ModelBatchSize)).toInt(), 16);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ScanBackend)).toInt(), 0);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::QueryDeadlineMs)).toInt(), 45000);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::CoarseK)).toInt(), 128);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::CoarseDedupIou)).toDouble(), 0.70);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::TopK)).toInt(), 30);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::FineVerifyK)).toInt(), 96);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::FineMatchCosineThreshold)).toDouble(), 0.65);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::FineNmsIou)).toDouble(), 0.45);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ConsistencyMode)).toInt(), 1);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ScoreWeightTemplate)).toDouble(), 0.50);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ScoreWeightCoverage)).toDouble(), 0.30);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ScoreWeightConsistency)).toDouble(), 0.20);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::EnableDecisionThreshold)).toBool(), true);
        QCOMPARE(gs->valueForField(acc_key, static_cast<int>(gen_field::RegionSearch::Key::DecisionThreshold)).toDouble(), 0.75);
    }
};

QTEST_MAIN(RegionSearchTest)
#include "test_RegionSearchController.moc"
