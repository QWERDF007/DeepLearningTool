#include "feature/RegionSearchController.h"
#include "SearchControllerUtils.h"

#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/GlobalFilter.h"
#include "settings/GlobalSettings.h"
#include "dltool/settings/SettingsKeys.hpp"
#include "ui/ProgressManager.h"
#include "ui/SignalHelper.h"

#include <inferrt/features/DinoRegionSearch.hpp>
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QThread>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace dltool::feature {

namespace {

namespace fs = std::filesystem;

struct GalleryImage
{
    int64_t  image_id{-1};
    int64_t  dataset_id{-1};
    fs::path source_path{};
};

struct QuerySnapshot
{
    int64_t                     query_label_id{-1};
    int64_t                     query_image_id{-1};
    int64_t                     query_dataset_id{-1};
    int64_t                     query_class_id{-1};
    QString                     query_image_path{};
    QRectF                      query_rect{};
    irt::features::DinoSearchRoi query_roi{};
    bool                        valid{false};
};

struct RegionJob
{
    QuerySnapshot                                  query{};
    std::vector<GalleryImage>                      gallery{};
    std::vector<irt::features::DinoImageItem>      build_items{};
    std::unordered_map<int64_t, fs::path>          image_paths{};
    std::set<int64_t>                              build_dataset_ids{};
    std::set<int64_t>                              target_dataset_ids{};
    int64_t                                        target_class_id{-1};
    int                                            top_k{50};
    bool                                           include_self{true};
    bool                                           build_required{false};
    irt::features::DinoRegionSearchConfig          config{};
    fs::path                                       index_root{};
    fs::path                                       scope_file{};
    std::shared_ptr<std::atomic_bool>              stop{std::make_shared<std::atomic_bool>(false)};
};

struct TaskOutcome
{
    enum class Kind
    {
        Completed,
        Partial,
        Cancelled,
        Failed,
    } kind{Kind::Failed};

    std::shared_ptr<irt::features::DinoSearchResponse> response{nullptr};
    QString                                            error{};
};

struct GenerationSummary
{
    int                  returned{0};
    int                  created{0};
    int                  reused{0};
    int                  self_skipped{0};
    int                  duplicate_skipped{0};
    int                  unmapped{0};
    int                  invalid{0};
    int                  limit_skipped{0};
    int64_t              tag_id{-1};
    std::vector<int64_t> created_label_ids{};
    std::vector<int64_t> result_ids{};
};

struct AcceptedItem
{
    bool    is_existing{false};
    int64_t id{-1};
    int64_t image_id{-1};
    QRectF  rect{};
};

double computeIoU(const QRectF &a, const QRectF &b)
{
    const double inter_x1 = std::max(a.left(), b.left());
    const double inter_y1 = std::max(a.top(), b.top());
    const double inter_x2 = std::min(a.right(), b.right());
    const double inter_y2 = std::min(a.bottom(), b.bottom());

    const double inter_w = std::max(0.0, inter_x2 - inter_x1);
    const double inter_h = std::max(0.0, inter_y2 - inter_y1);
    const double inter_area = inter_w * inter_h;

    const double area_a = a.width() * a.height();
    const double area_b = b.width() * b.height();
    const double union_area = area_a + area_b - inter_area;

    return union_area > 0.0 ? inter_area / union_area : 0.0;
}

QVariantMap createLabelDataMap(int method, const QRectF &rect)
{
    QVariantMap map;
    map[QStringLiteral("x")]      = rect.x();
    map[QStringLiteral("y")]      = rect.y();
    map[QStringLiteral("width")]  = rect.width();
    map[QStringLiteral("height")] = rect.height();

    if (method == core::DeepLearningMethod::Segmentation
        || method == core::DeepLearningMethod::AnomalyDetection)
    {
        QVariantList pts;
        pts.append(QVariantMap{{QStringLiteral("x"), rect.left()}, {QStringLiteral("y"), rect.top()}});
        pts.append(QVariantMap{{QStringLiteral("x"), rect.right()}, {QStringLiteral("y"), rect.top()}});
        pts.append(QVariantMap{{QStringLiteral("x"), rect.right()}, {QStringLiteral("y"), rect.bottom()}});
        pts.append(QVariantMap{{QStringLiteral("x"), rect.left()}, {QStringLiteral("y"), rect.bottom()}});
        map[QStringLiteral("points")] = pts;
    }
    return map;
}

bool readIndexScope(const fs::path &scope_file, std::set<int64_t> &dataset_ids,
                    std::vector<std::string> &failed_paths)
{
    dataset_ids.clear();
    failed_paths.clear();
    if (!fs::exists(scope_file))
    {
        return false;
    }

    try
    {
        std::ifstream fin(scope_file, std::ios::binary);
        if (!fin.is_open())
        {
            return false;
        }
        YAML::Node node = YAML::Load(fin);
        if (node["dataset_ids"] && node["dataset_ids"].IsSequence())
        {
            for (const auto &item : node["dataset_ids"])
            {
                dataset_ids.insert(item.as<int64_t>());
            }
        }
        if (node["failed_paths"] && node["failed_paths"].IsSequence())
        {
            for (const auto &item : node["failed_paths"])
            {
                failed_paths.push_back(item.as<std::string>());
            }
        }
        return true;
    }
    catch (const std::exception &e)
    {
        spdlog::warn("读取 index_scope.yaml 失败: {}", e.what());
        return false;
    }
}

void writeIndexScope(const fs::path &scope_file, const std::set<int64_t> &dataset_ids,
                     const std::vector<std::string> &failed_paths)
{
    try
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "dataset_ids" << YAML::Value << YAML::BeginSeq;
        for (const int64_t id : dataset_ids)
        {
            out << id;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "failed_paths" << YAML::Value << YAML::BeginSeq;
        for (const auto &p : failed_paths)
        {
            out << p;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;

        std::ofstream fout(scope_file, std::ios::binary);
        if (fout.is_open())
        {
            fout << out.c_str() << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        spdlog::error("写入 index_scope.yaml 失败: {}", e.what());
    }
}

} // namespace

// ============================================================================
// runRegionJob 后台检索执行函数
// ============================================================================

TaskOutcome runRegionJob(RegionJob job, const std::function<void(double, const QString &)> &progress_fn)
{
    TaskOutcome outcome;
    try
    {
        auto is_stopped = [&job]() {
            return job.stop != nullptr && job.stop->load(std::memory_order_relaxed);
        };

        if (is_stopped())
        {
            outcome.kind = TaskOutcome::Kind::Cancelled;
            return outcome;
        }

        auto   last_progress_time  = std::chrono::steady_clock::time_point{};
        double max_progress_so_far = 0.0;
        auto throttle_progress = [&](double pct, const QString &msg) {
            max_progress_so_far = std::max(max_progress_so_far, pct);
            auto now = std::chrono::steady_clock::now();
            if (last_progress_time == std::chrono::steady_clock::time_point{}
                || std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress_time).count() >= 100)
            {
                last_progress_time = now;
                if (progress_fn)
                {
                    progress_fn(max_progress_so_far, msg);
                }
            }
        };

        if (job.build_required)
        {
            throttle_progress(0.01, QString("正在准备建立图库特征..."));
            irt::features::DinoRegionSearch::releaseRuntime(false);

            irt::features::DinoBuildProgressCallback build_cb = [&](const irt::features::DinoBuildProgress &p) {
                size_t processed = 0;
                size_t total     = 0;
                const double pct = resolveProgressCount(p, job.build_items.size(), processed, total) && total > 0
                                       ? static_cast<double>(processed) / static_cast<double>(total)
                                       : 0.0;
                double stage_base = 0.0;
                double stage_span = 0.0;
                switch (p.stage)
                {
                case irt::features::DinoBuildStage::ScanningImages:
                    stage_base = 0.01;
                    stage_span = 0.02;
                    break;
                case irt::features::DinoBuildStage::LoadingModel:
                    stage_base = 0.03;
                    stage_span = 0.04;
                    break;
                case irt::features::DinoBuildStage::ExtractingViews:
                    stage_base = 0.07;
                    stage_span = 0.48;
                    break;
                case irt::features::DinoBuildStage::WritingIndex:
                    stage_base = 0.55;
                    stage_span = 0.02;
                    break;
                case irt::features::DinoBuildStage::Quantizing:
                    stage_base = 0.57;
                    stage_span = 0.02;
                    break;
                case irt::features::DinoBuildStage::Finalizing:
                    stage_base = 0.59;
                    stage_span = 0.01;
                    break;
                default:
                    stage_base = 0.0;
                    stage_span = 0.60;
                    break;
                }
                const QString msg = formatBuildProgressMessage(p, job.build_items.size());
                const double overall_pct = stage_base + pct * stage_span;
                throttle_progress(overall_pct, msg);
            };

            irt::features::DinoOperationControl control{is_stopped};
            auto report = irt::features::DinoRegionSearch::build(
                job.build_items, job.config, job.index_root, build_cb, control);

            writeIndexScope(job.scope_file, job.build_dataset_ids, report.failed_files);

            if (is_stopped())
            {
                outcome.kind = TaskOutcome::Kind::Cancelled;
                return outcome;
            }
            if (report.image_count == 0 || report.image_count == report.failed_image_count)
            {
                outcome.kind = TaskOutcome::Kind::Failed;
                QString detail;
                if (!report.messages.empty())
                {
                    detail = QString::fromUtf8(report.messages.front().c_str());
                }
                outcome.error = detail.isEmpty()
                                     ? QString("没有可用图库图像")
                                     : QString("建库失败: %1").arg(detail);
                return outcome;
            }
        }

        throttle_progress(job.build_required ? 0.60 : 0.0, QString("正在检索相似区域..."));

        std::vector<int64_t> allowed_image_ids;
        allowed_image_ids.reserve(job.gallery.size());
        for (const auto &item : job.gallery)
        {
            allowed_image_ids.push_back(item.image_id);
        }

        if (allowed_image_ids.empty())
        {
            outcome.kind  = TaskOutcome::Kind::Failed;
            outcome.error = QString("所选范围没有可用的已建库图像");
            return outcome;
        }

        irt::features::DinoSearchRequest req;
        req.query_path        = toFsPath(job.query.query_image_path);
        req.query_image_id    = job.query.query_image_id;
        req.roi               = job.query.query_roi;
        req.top_k             = job.top_k;
        req.include_self      = job.include_self;
        req.deadline_ms       = job.config.runtime.query_deadline_ms;
        req.allowed_image_ids = allowed_image_ids;
        req.image_resolver    = [&paths = job.image_paths](int64_t image_id) -> fs::path {
            const auto it = paths.find(image_id);
            return it != paths.end() ? it->second : fs::path{};
        };

        irt::features::DinoSearchProgressCallback search_cb = [&](const irt::features::DinoSearchProgress &p) {
            const double pct = p.total_count > 0
                                   ? static_cast<double>(p.processed_count) / static_cast<double>(p.total_count)
                                   : 0.0;
            double stage_base = 0.0;
            double stage_span = 0.0;
            switch (p.stage)
            {
            case irt::features::DinoSearchStage::Decode:
                stage_base = 0.0;
                stage_span = 0.05;
                break;
            case irt::features::DinoSearchStage::QueryExtract:
                stage_base = 0.05;
                stage_span = 0.10;
                break;
            case irt::features::DinoSearchStage::RegionScan:
                stage_base = 0.15;
                stage_span = 0.35;
                break;
            case irt::features::DinoSearchStage::LocalScan:
                stage_base = 0.50;
                stage_span = 0.15;
                break;
            case irt::features::DinoSearchStage::LocalWindowRescore:
                stage_base = 0.65;
                stage_span = 0.05;
                break;
            case irt::features::DinoSearchStage::Fusion:
                stage_base = 0.70;
                stage_span = 0.05;
                break;
            case irt::features::DinoSearchStage::FineMatch:
                stage_base = 0.75;
                stage_span = 0.10;
                break;
            case irt::features::DinoSearchStage::FineExtract:
                stage_base = 0.85;
                stage_span = 0.10;
                break;
            case irt::features::DinoSearchStage::Output:
                stage_base = 0.95;
                stage_span = 0.05;
                break;
            default:
                stage_base = 0.0;
                stage_span = 1.0;
                break;
            }
            const QString msg = formatSearchProgressMessage(p);
            const double search_progress = stage_base + pct * stage_span;
            const double overall_pct = job.build_required
                                           ? (0.60 + search_progress * 0.35)
                                           : (search_progress * 0.95);
            throttle_progress(overall_pct, msg);
        };

        irt::features::DinoOperationControl control{is_stopped};
        auto response = irt::features::DinoRegionSearch::search(
            job.index_root, req, job.config, search_cb, control);

        if (is_stopped())
        {
            outcome.kind = TaskOutcome::Kind::Cancelled;
            return outcome;
        }

        outcome.response = std::make_shared<irt::features::DinoSearchResponse>(std::move(response));
        if (outcome.response->status == irt::features::DinoSearchStatus::Completed)
        {
            outcome.kind = TaskOutcome::Kind::Completed;
        }
        else if (outcome.response->status == irt::features::DinoSearchStatus::Incomplete)
        {
            outcome.kind = TaskOutcome::Kind::Partial;
        }
        else
        {
            outcome.kind  = TaskOutcome::Kind::Failed;
            outcome.error = outcome.response->message.empty()
                                 ? QString("检索失败")
                                 : QString::fromStdString(outcome.response->message);
        }
    }
    catch (const irt::Exception &e)
    {
        if (e.code() == irt::Status::INVALID_OPERATION && std::string(e.what()).find("Cancelled") != std::string::npos)
        {
            outcome.kind = TaskOutcome::Kind::Cancelled;
        }
        else
        {
            outcome.kind  = TaskOutcome::Kind::Failed;
            outcome.error = QString::fromStdString(e.what());
        }
    }
    catch (const std::exception &e)
    {
        outcome.kind  = TaskOutcome::Kind::Failed;
        outcome.error = QString::fromStdString(e.what());
    }
    return outcome;
}

// ============================================================================
// RegionSearchController::Impl 内部结构体
// ============================================================================

struct RegionSearchController::Impl
{
    dltool::data::DataManager *data_manager{nullptr};
    QPointer<QThread>          worker_thread{nullptr};
    std::shared_ptr<std::atomic_bool> stop_flag{std::make_shared<std::atomic_bool>(false)};

    State   state{State::Idle};
    bool    busy{false};
    bool    computing{false};
    bool    closing{false};
    bool    commit_started{false};
    bool    applying_profile{false};
    QString status_text{};
    double  progress_value{0.0};
    QString progress_text{};
    QString error_text{};
    QString current_task_id{};

    QuerySnapshot query{};
    RegionJob     active_job{};

    int profile_final_k{50};
    bool needs_build{true};
    int  indexed_image_count{0};
    QString build_reason{};

    int returned_count{0};
    int created_count{0};
    int reused_count{0};
    int skipped_count{0};

    std::shared_ptr<irt::features::DinoSearchResponse> partial_response{nullptr};
    int partial_count{0};

    void handleTaskOutcome(RegionSearchController *q, const TaskOutcome &outcome);

    fs::path indexDir() const
    {
        if (data_manager == nullptr)
            return {};
        const QString proj_dir = data_manager->projectDir();
        if (proj_dir.isEmpty())
            return {};
        return toFsPath(proj_dir) / "region_search";
    }

    fs::path scopeFile() const
    {
        const auto idx = indexDir();
        return idx.empty() ? fs::path{} : idx / "index_scope.yaml";
    }

    irt::features::DinoRegionSearchConfig loadConfig() const
    {
        namespace generated_field = dltool::settings::generated::field;
        auto *gs = dltool::settings::GlobalSettings::getInstance();

        QString profile_path;
        QString weights_path;
        QString model_runtime;

        if (gs != nullptr)
        {
            const int acc_key = static_cast<int>(dltool::settings::generated::AccessorKey::RegionSearch);
            profile_path = gs->valueForField(acc_key,
                                             static_cast<int>(generated_field::RegionSearch::Key::ProfilePath),
                                             QString{})
                               .toString();
            weights_path = gs->valueForField(acc_key,
                                             static_cast<int>(generated_field::RegionSearch::Key::WeightsPath),
                                             QString{})
                               .toString();
            model_runtime = gs->valueForField(acc_key,
                                              static_cast<int>(generated_field::RegionSearch::Key::ModelRuntime),
                                              QString{})
                                .toString();
        }

        if (profile_path.isEmpty() || !QFileInfo::exists(profile_path))
        {
            // 默认搜索候选路径（优先已支持的 dinov2_vits14_reg4）
            const QStringList candidates = {
                QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/profiles/dinov2_vits14_reg4.yaml")),
                QStringLiteral("config/profiles/dinov2_vits14_reg4.yaml"),
                QStringLiteral("F:/Projects/DeepLearningTool/config/profiles/dinov2_vits14_reg4.yaml"),
                QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/profiles/dinov3_vits16.yaml")),
                QStringLiteral("config/profiles/dinov3_vits16.yaml"),
                QStringLiteral("F:/Projects/DeepLearningTool/config/profiles/dinov3_vits16.yaml"),
            };
            for (const auto &cand : candidates)
            {
                if (QFileInfo::exists(cand))
                {
                    profile_path = cand;
                    break;
                }
            }
        }

        std::string yaml_text;
        if (!profile_path.isEmpty() && QFileInfo::exists(profile_path))
        {
            QFile f(profile_path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                yaml_text = QString::fromUtf8(f.readAll()).toStdString();
            }
        }

        irt::features::DinoRegionSearchConfig config;
        if (!yaml_text.empty())
        {
            config = irt::features::dinoConfigFromYaml(yaml_text);
        }

        // 解析权重文件路径
        QString effective_weights = weights_path;
        if (effective_weights.isEmpty() || !QFileInfo::exists(effective_weights))
        {
            const QString profile_weights = fromFsPath(config.model.weights_file);
            const QString profile_dir = QFileInfo(profile_path).dir().path();
            const QString app_dir = QCoreApplication::applicationDirPath();

            const QStringList candidates = {
                weights_path,
                profile_weights,
                QDir(profile_dir).filePath(profile_weights),
                QDir(app_dir).filePath(profile_weights),
                QDir(app_dir).filePath(QStringLiteral("models/dinov2_vits14_reg4.wts")),
                QDir(app_dir).filePath(QStringLiteral("models/dinov2_vits14.wts")),
                QDir(app_dir).filePath(QStringLiteral("models/dinov2_vits14.engine")),
                QStringLiteral("F:/models/dinov2/dinov2_vits14_reg4_pretrain/dinov2_vits14_reg4.wts"),
                QStringLiteral("F:/Projects/InferRT/samples/model/classification/dinov2_vits14.wts"),
                QStringLiteral("F:/Projects/InferRT/samples/model/classification/dinov2_vits14.engine"),
            };
            for (const auto &cand : candidates)
            {
                if (!cand.isEmpty() && QFileInfo::exists(cand))
                {
                    effective_weights = cand;
                    break;
                }
            }
        }

        if (!effective_weights.isEmpty() && QFileInfo::exists(effective_weights))
        {
            config.model.weights_file = toFsPath(effective_weights);
        }

        if (!model_runtime.isEmpty())
        {
            try
            {
                config.runtime.model_runtime = irt::model::ModelRuntime::parse(model_runtime.toStdString());
            }
            catch (...)
            {
            }
        }

        if (gs != nullptr)
        {
            const int acc_key = static_cast<int>(dltool::settings::generated::AccessorKey::RegionSearch);

            const QString model_name = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ModelName), QString{}).toString();
            if (!model_name.isEmpty())
            {
                config.model.model_name = model_name.toStdString();
            }

            const int encoder_edge = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::EncoderEdge), 0).toInt();
            if (encoder_edge > 0)
            {
                config.model.encoder_edge = encoder_edge;
            }

            // 图库与特征
            config.gallery_views.view_overlap = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ViewOverlap), config.gallery_views.view_overlap).toDouble();
            config.descriptors.quantize_int8  = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::QuantizeInt8), config.descriptors.quantize_int8).toBool();

            // 运行时
            const int precision_val = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ModelPrecision), 1).toInt();
            config.runtime.model_precision = (precision_val == 1) ? irt::model::ModelPrecision::FP16 : irt::model::ModelPrecision::FP32;

            const int batch_size = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ModelBatchSize), static_cast<int>(config.runtime.model_batch_size)).toInt();
            if (batch_size > 0)
            {
                config.runtime.model_batch_size = static_cast<size_t>(batch_size);
            }

            const int backend_val = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ScanBackend), 1).toInt();
            config.runtime.scan_backend = (backend_val == 0) ? irt::features::DinoScanBackend::Cpu : irt::features::DinoScanBackend::Cuda;

            const int deadline = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::QueryDeadlineMs), static_cast<int>(config.runtime.query_deadline_ms)).toInt();
            if (deadline > 0)
            {
                config.runtime.query_deadline_ms = deadline;
            }

            // 粗排与检索
            const int coarse_k = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::CoarseK), static_cast<int>(config.coarse_scan.coarse_k)).toInt();
            if (coarse_k > 0)
            {
                config.coarse_scan.coarse_k = static_cast<size_t>(coarse_k);
            }
            config.coarse_scan.coarse_dedup_iou = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::CoarseDedupIou), config.coarse_scan.coarse_dedup_iou).toDouble();

            const int top_k = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::TopK), static_cast<int>(config.coarse_scan.final_k)).toInt();
            if (top_k > 0)
            {
                config.coarse_scan.final_k = static_cast<size_t>(top_k);
            }

            // 精排与匹配
            const int fine_k = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::FineVerifyK), static_cast<int>(config.fine_match.fine_verify_k)).toInt();
            config.fine_match.fine_verify_k = static_cast<size_t>(std::max(0, fine_k));

            config.fine_match.fine_match_cosine_threshold = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::FineMatchCosineThreshold), config.fine_match.fine_match_cosine_threshold).toDouble();
            config.fine_match.fine_nms_iou                 = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::FineNmsIou), config.fine_match.fine_nms_iou).toDouble();

            const int consistency_val = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ConsistencyMode), 0).toInt();
            config.fine_match.consistency_mode = (consistency_val == 1) ? irt::features::DinoConsistencyMode::Instance : irt::features::DinoConsistencyMode::Appearance;

            // 打分权重
            config.fine_match.score_weight_template    = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ScoreWeightTemplate), config.fine_match.score_weight_template).toDouble();
            config.fine_match.score_weight_coverage    = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ScoreWeightCoverage), config.fine_match.score_weight_coverage).toDouble();
            config.fine_match.score_weight_consistency = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::ScoreWeightConsistency), config.fine_match.score_weight_consistency).toDouble();

            // 判定阈值
            config.decision.enable_decision_threshold = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::EnableDecisionThreshold), config.decision.enable_decision_threshold).toBool();
            config.decision.decision_threshold        = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::DecisionThreshold), config.decision.decision_threshold).toDouble();
        }

        return config;
    }
};

// ============================================================================
// RegionSearchController 实现
// ============================================================================

RegionSearchController::RegionSearchController(dltool::data::DataManager *data_manager, QObject *parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>())
{
    impl_->data_manager = data_manager;

    const auto config = impl_->loadConfig();
    impl_->profile_final_k = config.coarse_scan.final_k > 0 ? config.coarse_scan.final_k : 50;

    if (auto *gs = dltool::settings::GlobalSettings::getInstance())
    {
        connect(gs, &dltool::settings::GlobalSettings::fieldValueChanged, this,
                [this](dltool::settings::generated::AccessorKey accessor_key,
                       const QString &field_name,
                       const QVariant &value) {
                    if (accessor_key == dltool::settings::generated::AccessorKey::RegionSearch)
                    {
                        if (field_name == QStringLiteral("enabled"))
                        {
                            emit enabledChanged();
                        }
                        else if (field_name == QStringLiteral("profile_path"))
                        {
                            applyProfileToSettings(value.toString());
                            const auto cfg = impl_->loadConfig();
                            impl_->profile_final_k = cfg.coarse_scan.final_k > 0 ? static_cast<int>(cfg.coarse_scan.final_k) : 50;
                            emit profileChanged();
                        }
                        else if (field_name == QStringLiteral("top_k")
                                 || field_name == QStringLiteral("weights_path")
                                 || field_name == QStringLiteral("model_runtime")
                                 || field_name == QStringLiteral("model_name"))
                        {
                            const auto cfg = impl_->loadConfig();
                            impl_->profile_final_k = cfg.coarse_scan.final_k > 0 ? static_cast<int>(cfg.coarse_scan.final_k) : 50;
                            emit profileChanged();
                        }
                    }
                });
    }
}

RegionSearchController::~RegionSearchController()
{
    shutdown();
}

bool RegionSearchController::isEnabled() const
{
    namespace generated_field = dltool::settings::generated::field;
    auto *gs = dltool::settings::GlobalSettings::getInstance();
    if (gs == nullptr)
        return true;
    return gs->valueForField(static_cast<int>(dltool::settings::generated::AccessorKey::RegionSearch),
                            static_cast<int>(generated_field::RegionSearch::Key::Enabled),
                            true)
        .toBool();
}

bool RegionSearchController::isBusy() const
{
    return impl_->busy;
}

bool RegionSearchController::isRunning() const
{
    return isBusy();
}

bool RegionSearchController::isComputing() const
{
    return impl_->computing;
}

RegionSearchController::State RegionSearchController::state() const
{
    return impl_->state;
}

QString RegionSearchController::statusText() const
{
    return impl_->status_text;
}

double RegionSearchController::progressValue() const
{
    return impl_->progress_value;
}

QString RegionSearchController::progressText() const
{
    return impl_->progress_text;
}

QString RegionSearchController::errorText() const
{
    return impl_->error_text;
}

QString RegionSearchController::lastError() const
{
    return impl_->error_text;
}

void RegionSearchController::setLastError(const QString &last_error)
{
    if (impl_->error_text == last_error)
        return;
    impl_->error_text = last_error;
    if (!impl_->error_text.isEmpty())
    {
        spdlog::error("区域检索失败: {}", impl_->error_text.toUtf8().constData());
    }
    emit errorTextChanged();
}

QString RegionSearchController::validationError() const
{
    if (impl_->closing)
    {
        return QString("区域检索控制器正在关闭");
    }
    if (!isEnabled())
    {
        return QString("区域检索未启用");
    }
    if (!canRepresentResultRect())
    {
        return QString("当前项目类型不支持区域检索");
    }
    if (impl_->query.query_label_id >= 0 && !impl_->query.valid)
    {
        return QString("查询标注无效");
    }
    return {};
}

int64_t RegionSearchController::queryLabelId() const
{
    return impl_->query.query_label_id;
}

int64_t RegionSearchController::queryImageId() const
{
    return impl_->query.query_image_id;
}

int64_t RegionSearchController::queryDatasetId() const
{
    return impl_->query.query_dataset_id;
}

int64_t RegionSearchController::queryClassId() const
{
    return impl_->query.query_class_id;
}

QString RegionSearchController::queryClassName() const
{
    if (impl_->data_manager == nullptr || impl_->query.query_class_id < 0)
        return {};
    return impl_->data_manager->labelClassName(impl_->query.query_class_id);
}

QString RegionSearchController::queryImagePath() const
{
    return impl_->query.query_image_path;
}

QRectF RegionSearchController::queryRect() const
{
    return impl_->query.query_rect;
}

bool RegionSearchController::queryValid() const
{
    return impl_->query.valid;
}

bool RegionSearchController::canRepresentResultRect() const
{
    if (impl_->data_manager == nullptr)
        return false;
    const int m = impl_->data_manager->method();
    return m == core::DeepLearningMethod::Detection
        || m == core::DeepLearningMethod::Segmentation
        || m == core::DeepLearningMethod::AnomalyDetection;
}

int RegionSearchController::profileFinalK() const
{
    return impl_->profile_final_k;
}

bool RegionSearchController::needsBuild() const
{
    return impl_->needs_build;
}

int RegionSearchController::indexedImageCount() const
{
    return impl_->indexed_image_count;
}

QString RegionSearchController::buildReason() const
{
    return impl_->build_reason;
}

int RegionSearchController::returnedCount() const
{
    return impl_->returned_count;
}

int RegionSearchController::createdCount() const
{
    return impl_->created_count;
}

int RegionSearchController::reusedCount() const
{
    return impl_->reused_count;
}

int RegionSearchController::skippedCount() const
{
    return impl_->skipped_count;
}

bool RegionSearchController::hasPartialResults() const
{
    return impl_->partial_count > 0;
}

int RegionSearchController::partialCount() const
{
    return impl_->partial_count;
}

bool RegionSearchController::captureQuery(const int64_t label_id)
{
    if (impl_->data_manager == nullptr || !canRepresentResultRect() || label_id < 0)
    {
        return false;
    }

    const int64_t image_id = impl_->data_manager->labelImageId(label_id);
    if (image_id < 0)
    {
        return false;
    }

    const QString image_path = impl_->data_manager->imagePath(image_id);
    if (!QFileInfo::exists(image_path))
    {
        return false;
    }

    const int64_t dataset_id = impl_->data_manager->imageDatasetId(image_id);
    const int64_t class_id   = impl_->data_manager->labelClassId(label_id);
    const QVariantMap data   = impl_->data_manager->labelData(label_id);

    irt::features::DinoSearchRoi roi;
    QRectF                       rect;

    const auto pts_val = data.value(QStringLiteral("points"));
    if (pts_val.isValid() && pts_val.canConvert<QVariantList>())
    {
        const QVariantList pts_list = pts_val.toList();
        if (pts_list.size() >= 3)
        {
            double min_x = 1e9, min_y = 1e9, max_x = -1e9, max_y = -1e9;
            for (const auto &p_var : pts_list)
            {
                const QVariantMap p_map = p_var.toMap();
                const double x = p_map.value(QStringLiteral("x")).toDouble();
                const double y = p_map.value(QStringLiteral("y")).toDouble();
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
                roi.polygon.push_back({static_cast<float>(x), static_cast<float>(y)});
            }
            roi.has_polygon = true;
            roi.has_bbox    = false;
            rect            = QRectF(min_x, min_y, std::max(0.0, max_x - min_x), std::max(0.0, max_y - min_y));
        }
    }

    if (!roi.has_polygon)
    {
        bool ok_x = false, ok_y = false, ok_w = false, ok_h = false;
        const double x = data.value(QStringLiteral("x")).toDouble(&ok_x);
        const double y = data.value(QStringLiteral("y")).toDouble(&ok_y);
        const double w = data.value(QStringLiteral("width")).toDouble(&ok_w);
        const double h = data.value(QStringLiteral("height")).toDouble(&ok_h);

        if (!ok_x || !ok_y || !ok_w || !ok_h || w <= 1.0 || h <= 1.0)
        {
            return false;
        }

        roi.has_bbox    = true;
        roi.has_polygon = false;
        roi.bbox        = {
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(x + w),
            static_cast<float>(y + h),
        };
        rect = QRectF(x, y, w, h);
    }

    impl_->query.query_label_id   = label_id;
    impl_->query.query_image_id   = image_id;
    impl_->query.query_dataset_id = dataset_id;
    impl_->query.query_class_id   = class_id;
    impl_->query.query_image_path = image_path;
    impl_->query.query_rect       = rect;
    impl_->query.query_roi        = roi;
    impl_->query.valid            = true;

    // 默认检查自身所在数据集
    checkNeedsBuild({dataset_id});

    emit queryChanged();
    return true;
}

bool RegionSearchController::checkNeedsBuild(const QList<int64_t> &dataset_ids)
{
    const auto idx_root   = impl_->indexDir();
    const auto scope_path = impl_->scopeFile();
    const auto idx_yaml   = idx_root / "index.yaml";

    if (!fs::exists(idx_yaml))
    {
        impl_->build_reason         = QStringLiteral("索引元数据 index.yaml 不存在 (尚未建库或上次构建被中断)");
        impl_->needs_build          = true;
        impl_->indexed_image_count  = 0;
        spdlog::info("区域检索检查索引: {}", impl_->build_reason.toUtf8().constData());
        emit scopeChanged();
        return true;
    }

    if (!fs::exists(scope_path))
    {
        impl_->build_reason         = QStringLiteral("索引范围文件 index_scope.yaml 不存在");
        impl_->needs_build          = true;
        impl_->indexed_image_count  = 0;
        spdlog::info("区域检索检查索引: {}", impl_->build_reason.toUtf8().constData());
        emit scopeChanged();
        return true;
    }

    try
    {
        const auto config = impl_->loadConfig();
        if (irt::features::DinoRegionSearch::needsRebuild(idx_root, config))
        {
            QStringList diffs;
            try
            {
                std::ifstream fin(idx_yaml, std::ios::binary);
                if (fin.is_open())
                {
                    YAML::Node node = YAML::Load(fin);
                    if (node["manifest"])
                    {
                        const auto m = node["manifest"];
                        auto check_field = [&](const char *name, const std::string &actual, const std::string &expected) {
                            if (actual != expected)
                            {
                                diffs.append(QString("%1 (索引: '%2', 当前设置: '%3')")
                                                 .arg(QString::fromUtf8(name))
                                                 .arg(QString::fromUtf8(actual.c_str()))
                                                 .arg(QString::fromUtf8(expected.c_str())));
                            }
                        };
                        if (m["model_name"])
                            check_field("model_name", m["model_name"].as<std::string>(), config.model.model_name);
                        if (m["weights_id"])
                            check_field("weights_id", m["weights_id"].as<std::string>(), config.model.weights_id);
                        if (m["encoder_edge"])
                            check_field("encoder_edge", m["encoder_edge"].as<std::string>(), std::to_string(config.model.encoder_edge));
                        if (m["view_overlap"])
                            check_field("view_overlap", m["view_overlap"].as<std::string>(), QString::number(config.gallery_views.view_overlap, 'f', 2).toStdString());
                        if (m["quantize_int8"])
                            check_field("quantize_int8", m["quantize_int8"].as<std::string>(), config.descriptors.quantize_int8 ? "true" : "false");
                    }
                }
            }
            catch (...)
            {
            }

            if (!diffs.isEmpty())
            {
                impl_->build_reason = QString("索引契约参数不一致 [%1]").arg(diffs.join(QStringLiteral("; ")));
            }
            else
            {
                impl_->build_reason = QStringLiteral("索引契约校验不通过或特征文件不匹配");
            }

            impl_->needs_build         = true;
            impl_->indexed_image_count = 0;
            spdlog::info("区域检索检查索引: {}，需要重新建立特征索引", impl_->build_reason.toUtf8().constData());
            emit scopeChanged();
            return true;
        }
    }
    catch (const std::exception &e)
    {
        impl_->build_reason        = QString("检查索引一致性异常: %1").arg(e.what());
        spdlog::warn("{}", impl_->build_reason.toUtf8().constData());
        impl_->needs_build         = true;
        impl_->indexed_image_count = 0;
        emit scopeChanged();
        return true;
    }

    std::set<int64_t>        indexed_datasets;
    std::vector<std::string> failed_paths;
    if (!readIndexScope(scope_path, indexed_datasets, failed_paths))
    {
        impl_->build_reason        = QStringLiteral("读取 index_scope.yaml 失败");
        spdlog::info("区域检索检查索引: {}，需要重新建立特征索引", impl_->build_reason.toUtf8().constData());
        impl_->needs_build         = true;
        impl_->indexed_image_count = 0;
        emit scopeChanged();
        return true;
    }

    try
    {
        std::ifstream fin(idx_yaml, std::ios::binary);
        if (fin.is_open())
        {
            YAML::Node node = YAML::Load(fin);
            if (node["total_images"])
            {
                impl_->indexed_image_count = node["total_images"].as<int>();
            }
            else if (node["images"] && node["images"].IsSequence())
            {
                impl_->indexed_image_count = static_cast<int>(node["images"].size());
            }
        }
    }
    catch (...)
    {
    }

    QStringList missing_ds;
    for (const int64_t ds_id : dataset_ids)
    {
        if (indexed_datasets.count(ds_id) == 0)
        {
            missing_ds.append(QString::number(ds_id));
        }
    }

    if (!missing_ds.isEmpty())
    {
        QStringList existing_ds;
        for (const int64_t id : indexed_datasets)
        {
            existing_ds.append(QString::number(id));
        }
        impl_->build_reason = QString("目标数据集 [%1] 尚未建立索引 (已有范围: [%2])")
                                  .arg(missing_ds.join(QStringLiteral(", ")))
                                  .arg(existing_ds.join(QStringLiteral(", ")));
        spdlog::info("区域检索检查索引: {}，需要建立特征索引", impl_->build_reason.toUtf8().constData());
        impl_->needs_build = true;
        emit scopeChanged();
        return true;
    }

    impl_->build_reason.clear();
    impl_->needs_build = false;
    spdlog::info("区域检索检查索引: 已有索引完全有效，直接复用 (共 {} 张图像)", impl_->indexed_image_count);
    emit scopeChanged();
    return false;
}

bool RegionSearchController::start(const QVariantMap &options)
{
    if (impl_->closing)
    {
        setLastError(QString("区域检索控制器正在关闭"));
        return false;
    }
    if (impl_->busy || impl_->computing)
    {
        setLastError(QString("区域检索正在运行"));
        return false;
    }
    if (!impl_->query.valid || impl_->data_manager == nullptr)
    {
        setLastError(QString("查询标注无效"));
        return false;
    }

    const QVariantList ds_list = options.value(QStringLiteral("datasetIds")).toList();
    if (ds_list.isEmpty())
    {
        setLastError(QString("请至少选择一个搜索数据集"));
        return false;
    }

    std::set<int64_t> target_ds;
    for (const auto &v : ds_list)
    {
        target_ds.insert(v.toLongLong());
    }

    const int64_t target_class_id = options.value(QStringLiteral("targetClassId"), impl_->query.query_class_id).toLongLong();
    if (impl_->data_manager->labelClassName(target_class_id).isEmpty())
    {
        setLastError(QString("所选生成类别不存在"));
        return false;
    }

    irt::features::DinoRegionSearchConfig config;
    try
    {
        config = impl_->loadConfig();
    }
    catch (const std::exception &e)
    {
        setLastError(QString("加载区域检索配置失败: %1").arg(e.what()));
        return false;
    }

    if (config.model.weights_file.empty() || !fs::exists(config.model.weights_file))
    {
        const QString err = config.model.weights_file.empty()
                                ? QString("未配置区域检索骨干模型权重")
                                : QString("区域检索骨干权重文件不存在: %1").arg(fromFsPath(config.model.weights_file));
        setLastError(err);
        return false;
    }

    const int  top_k_req = options.value(QStringLiteral("topK"), static_cast<int>(config.coarse_scan.final_k)).toInt();
    const int  top_k = std::max(1, top_k_req);
    config.coarse_scan.final_k = static_cast<size_t>(top_k);
    const bool include_self = options.value(QStringLiteral("includeSelf"), true).toBool();

    const bool need_build = checkNeedsBuild(QList<int64_t>(target_ds.begin(), target_ds.end()));

    std::set<int64_t> build_ds = target_ds;
    if (need_build)
    {
        std::set<int64_t> existing_ds;
        std::vector<std::string> failed;
        if (readIndexScope(impl_->scopeFile(), existing_ds, failed))
        {
            build_ds.insert(existing_ds.begin(), existing_ds.end());
        }
    }

    std::vector<GalleryImage> gallery;
    std::unordered_map<int64_t, fs::path> image_paths;
    for (const int64_t ds_id : target_ds)
    {
        const auto img_ids = impl_->data_manager->imageIdsForDatasets({ds_id});
        for (const int64_t iid : img_ids)
        {
            const QString p = impl_->data_manager->imagePath(iid);
            if (QFileInfo::exists(p))
            {
                const auto fsp = toFsPath(p);
                gallery.push_back({iid, ds_id, fsp});
                image_paths[iid] = fsp;
            }
        }
    }

    if (gallery.empty())
    {
        setLastError(QString("选定数据集中没有可搜索的图像"));
        return false;
    }

    std::vector<irt::features::DinoImageItem> build_items;
    if (need_build)
    {
        std::unordered_set<int64_t> seen_ids;
        for (const int64_t ds_id : build_ds)
        {
            const auto img_ids = impl_->data_manager->imageIdsForDatasets({ds_id});
            for (const int64_t iid : img_ids)
            {
                if (seen_ids.insert(iid).second)
                {
                    const QString p = impl_->data_manager->imagePath(iid);
                    if (QFileInfo::exists(p))
                    {
                        const auto fsp = toFsPath(p);
                        build_items.push_back({iid, fsp});
                        image_paths[iid] = fsp;
                    }
                }
            }
        }
    }

    if (impl_->query.query_image_id >= 0 && !impl_->query.query_image_path.isEmpty())
    {
        image_paths[impl_->query.query_image_id] = toFsPath(impl_->query.query_image_path);
    }

    RegionJob job;
    job.query              = impl_->query;
    job.gallery            = std::move(gallery);
    job.build_items        = std::move(build_items);
    job.image_paths        = std::move(image_paths);
    job.build_dataset_ids  = std::move(build_ds);
    job.target_dataset_ids = std::move(target_ds);
    job.target_class_id    = target_class_id;
    job.top_k              = top_k;
    job.include_self       = include_self;
    job.build_required     = need_build;
    job.config             = config;
    job.index_root         = impl_->indexDir();
    job.scope_file         = impl_->scopeFile();
    job.stop               = std::make_shared<std::atomic_bool>(false);

    impl_->stop_flag       = job.stop;
    impl_->active_job      = job;
    impl_->commit_started  = false;
    impl_->partial_response.reset();
    impl_->partial_count   = 0;
    emit partialChanged();

    impl_->state          = need_build ? State::Building : State::Searching;
    impl_->busy           = true;
    impl_->computing      = true;
    impl_->status_text    = need_build ? QString("正在建立特征索引...") : QString("正在检索相似区域...");
    impl_->progress_value = 0.0;
    impl_->progress_text  = impl_->status_text;
    impl_->error_text     = QString();

    emit busyChanged();
    emit computingChanged();
    emit statusTextChanged();
    emit progressValueChanged();
    emit progressTextChanged();
    emit errorTextChanged();

    const QString task_id = QStringLiteral("region_search_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    impl_->current_task_id = task_id;
    ui::ProgressManager::getInstance()->startTask(QString("区域检索"), task_id);

    const QString build_status = need_build
                                     ? (impl_->build_reason.isEmpty()
                                            ? QStringLiteral("需要建立特征索引")
                                            : QString("需要建立特征索引 (%1)").arg(impl_->build_reason))
                                     : QStringLiteral("使用已有特征索引");

    const QString start_msg = QString("开始区域检索: 查询标注 ID=%1, 类别=%2, 搜索数据集=%3 个, 图库图像=%4 张, TopK=%5, %6")
                                  .arg(impl_->query.query_label_id)
                                  .arg(queryClassName())
                                  .arg(job.target_dataset_ids.size())
                                  .arg(job.gallery.size())
                                  .arg(top_k)
                                  .arg(build_status);

    spdlog::info("{}", start_msg.toUtf8().constData());
    addProgressMessage(spdlog::level::info, start_msg);

    const auto controller = QPointer<RegionSearchController>(this);
    const auto stop_flag  = job.stop;

    auto progress_fn = [controller, stop_flag](double pct, const QString &text) {
        if (!controller || (stop_flag && stop_flag->load(std::memory_order_relaxed)))
            return;
        QMetaObject::invokeMethod(
            controller.data(),
            [controller, pct, text]() {
                if (controller && !controller->impl_->closing)
                {
                    controller->onTaskProgress(pct, text);
                }
            },
            Qt::QueuedConnection);
    };

    auto complete_fn = [controller](TaskOutcome outcome) {
        if (!controller)
            return;
        QMetaObject::invokeMethod(
            controller.data(),
            [controller, outcome = std::move(outcome)]() mutable {
                if (controller && !controller->impl_->closing)
                {
                    controller->impl_->handleTaskOutcome(controller.data(), outcome);
                }
            },
            Qt::QueuedConnection);
    };

    QThread *work_thread = QThread::create(
        [job = std::move(job), progress_fn, complete_fn]() mutable {
            TaskOutcome outcome = runRegionJob(std::move(job), progress_fn);
            complete_fn(std::move(outcome));
        });

    connect(work_thread, &QThread::finished, work_thread, &QObject::deleteLater);
    impl_->worker_thread = work_thread;
    work_thread->start();

    return true;
}

bool RegionSearchController::search(const QVariantList &ids, const QVariantList &search_scope)
{
    if (ids.isEmpty())
    {
        setLastError(QString("请选择要检索的标注"));
        return false;
    }

    bool ok = false;
    const int64_t label_id = ids.first().toLongLong(&ok);
    if (!ok || label_id < 0 || !captureQuery(label_id))
    {
        setLastError(QString("无效的查询标注"));
        return false;
    }

    const auto parsed_scope = parseDatasetClassScope(search_scope);
    if (parsed_scope.empty())
    {
        setLastError(QString("请至少选择一个搜索数据集"));
        return false;
    }

    QVariantList dataset_ids;
    for (const auto &pair : parsed_scope)
    {
        dataset_ids.append(QVariant::fromValue(pair.first));
    }

    namespace generated_field = dltool::settings::generated::field;
    const int acc_key = static_cast<int>(dltool::settings::generated::AccessorKey::RegionSearch);
    auto *gs = dltool::settings::GlobalSettings::getInstance();

    int top_k = 50;
    bool include_self = true;
    if (gs != nullptr)
    {
        top_k = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::TopK), 50).toInt();
        include_self = gs->valueForField(acc_key, static_cast<int>(generated_field::RegionSearch::Key::IncludeSameImage), true).toBool();
    }

    QVariantMap options;
    options[QStringLiteral("datasetIds")] = dataset_ids;
    options[QStringLiteral("targetClassId")] = impl_->query.query_class_id;
    options[QStringLiteral("topK")] = top_k > 0 ? top_k : 50;
    options[QStringLiteral("includeSelf")] = include_self;

    return start(options);
}

void RegionSearchController::rebuildIndex(const QList<int64_t> &dataset_ids)
{
    if (impl_->busy || impl_->computing)
        return;
    impl_->needs_build = true;
    emit scopeChanged();

    QVariantMap opts;
    QVariantList ds;
    for (const int64_t id : dataset_ids)
    {
        ds.append(id);
    }
    opts[QStringLiteral("datasetIds")] = ds;
    start(opts);
}

bool RegionSearchController::applyProfileToSettings(const QString &profile_path)
{
    if (impl_->applying_profile || profile_path.isEmpty())
    {
        return false;
    }

    QString effective_path = profile_path;
    if (!QFileInfo::exists(effective_path))
    {
        const QString app_dir = QCoreApplication::applicationDirPath();
        const QString cand = QDir(app_dir).filePath(profile_path);
        if (QFileInfo::exists(cand))
        {
            effective_path = cand;
        }
        else
        {
            return false;
        }
    }

    QFile f(effective_path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return false;
    }

    const std::string yaml_text = QString::fromUtf8(f.readAll()).toStdString();
    irt::features::DinoRegionSearchConfig cfg;
    try
    {
        cfg = irt::features::dinoConfigFromYaml(yaml_text);
    }
    catch (const std::exception &e)
    {
        spdlog::warn("解析 profile 失败: {}", e.what());
        return false;
    }

    auto *gs = dltool::settings::GlobalSettings::getInstance();
    if (gs == nullptr)
        return false;

    impl_->applying_profile = true;

    namespace gen_field = dltool::settings::generated::field;
    const int acc_key = static_cast<int>(dltool::settings::generated::AccessorKey::RegionSearch);

    // 模型
    if (!cfg.model.model_name.empty())
        gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ModelName), QString::fromStdString(cfg.model.model_name));
    if (!cfg.model.weights_file.empty())
        gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::WeightsPath), fromFsPath(cfg.model.weights_file));
    if (cfg.model.encoder_edge > 0)
        gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::EncoderEdge), cfg.model.encoder_edge);

    // 图库与特征
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ViewOverlap), cfg.gallery_views.view_overlap);
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::QuantizeInt8), cfg.descriptors.quantize_int8);

    // 运行时
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ModelPrecision), cfg.runtime.model_precision == irt::model::ModelPrecision::FP16 ? 1 : 0);
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ModelBatchSize), static_cast<int>(cfg.runtime.model_batch_size));
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ScanBackend), cfg.runtime.scan_backend == irt::features::DinoScanBackend::Cuda ? 1 : 0);
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::QueryDeadlineMs), static_cast<int>(cfg.runtime.query_deadline_ms));

    // 粗排与检索
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::CoarseK), static_cast<int>(cfg.coarse_scan.coarse_k));
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::CoarseDedupIou), cfg.coarse_scan.coarse_dedup_iou);
    if (cfg.coarse_scan.final_k > 0)
        gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::TopK), static_cast<int>(cfg.coarse_scan.final_k));

    // 精排与匹配
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::FineVerifyK), static_cast<int>(cfg.fine_match.fine_verify_k));
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::FineMatchCosineThreshold), cfg.fine_match.fine_match_cosine_threshold);
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::FineNmsIou), cfg.fine_match.fine_nms_iou);
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ConsistencyMode), cfg.fine_match.consistency_mode == irt::features::DinoConsistencyMode::Instance ? 1 : 0);

    // 打分权重
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ScoreWeightTemplate), cfg.fine_match.score_weight_template);
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ScoreWeightCoverage), cfg.fine_match.score_weight_coverage);
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::ScoreWeightConsistency), cfg.fine_match.score_weight_consistency);

    // 判定阈值
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::EnableDecisionThreshold), cfg.decision.enable_decision_threshold);
    gs->setFieldValue(acc_key, static_cast<int>(gen_field::RegionSearch::Key::DecisionThreshold), cfg.decision.decision_threshold);

    impl_->applying_profile = false;
    return true;
}

void RegionSearchController::cancel()
{
    if (impl_->state == State::Committing)
    {
        return;
    }
    if (impl_->stop_flag)
    {
        impl_->stop_flag->store(true, std::memory_order_release);
    }
    if (impl_->computing)
    {
        impl_->state       = State::Cancelling;
        impl_->status_text = QString("正在取消...");
        emit statusTextChanged();
        spdlog::info("正在取消区域检索任务...");
        addProgressMessage(spdlog::level::info, QString("正在取消区域检索任务..."));
    }
    else
    {
        impl_->state = State::Idle;
        impl_->busy  = false;
        if (!impl_->current_task_id.isEmpty())
        {
            ui::ProgressManager::getInstance()->finishTask(impl_->current_task_id, false);
            impl_->current_task_id.clear();
        }
        emit busyChanged();
    }
}

void RegionSearchController::generateReturnedPartial()
{
    if (impl_->state == State::AwaitingPartial && impl_->partial_response != nullptr)
    {
        if (impl_->current_task_id.isEmpty())
        {
            impl_->current_task_id = QStringLiteral("region_search_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
            ui::ProgressManager::getInstance()->startTask(QString("区域检索生成"), impl_->current_task_id);
        }
        onTaskProgress(1.0, QString("正在生成部分候选标注..."));
        auto partial = impl_->partial_response;
        impl_->partial_response.reset();
        impl_->partial_count = 0;
        emit partialChanged();

        commitResults(partial);
    }
}

void RegionSearchController::onTaskProgress(const double value, const QString &text)
{
    impl_->progress_value = value;
    impl_->progress_text  = text;
    emit progressValueChanged();
    emit progressTextChanged();

    const double pct = std::clamp(value * 100.0, 0.0, 100.0);
    if (!impl_->current_task_id.isEmpty())
    {
        ui::ProgressManager::getInstance()->updateProgress(pct, impl_->current_task_id);
    }
    addProgressMessage(spdlog::level::info, text, impl_->current_task_id);
}

void RegionSearchController::Impl::handleTaskOutcome(RegionSearchController *q, const TaskOutcome &outcome)
{
    computing = false;
    emit q->computingChanged();

    if (closing || (stop_flag && stop_flag->load(std::memory_order_acquire)) || outcome.kind == TaskOutcome::Kind::Cancelled)
    {
        state       = State::Idle;
        busy        = false;
        status_text = QString("已取消");
        emit q->statusTextChanged();
        emit q->busyChanged();

        if (!current_task_id.isEmpty())
        {
            ui::ProgressManager::getInstance()->finishTask(current_task_id, false);
            current_task_id.clear();
        }
        spdlog::info("区域检索任务已取消");
        addProgressMessage(spdlog::level::info, QString("区域检索任务已取消"));

        if (closing)
        {
            irt::features::DinoRegionSearch::releaseRuntime(true);
        }
        return;
    }

    if (outcome.kind == TaskOutcome::Kind::Failed)
    {
        state       = State::Failed;
        busy        = false;
        q->setLastError(outcome.error);
        status_text = QString("检索失败: %1").arg(outcome.error);
        emit q->statusTextChanged();
        emit q->busyChanged();

        if (!current_task_id.isEmpty())
        {
            ui::ProgressManager::getInstance()->finishTask(current_task_id, false);
            current_task_id.clear();
        }
        addProgressMessage(spdlog::level::err, QString("区域检索失败: %1").arg(outcome.error));
        ui::SignalHelper::notifyError(QString("区域检索失败"), outcome.error);
        return;
    }

    if (outcome.kind == TaskOutcome::Kind::Partial)
    {
        state            = State::AwaitingPartial;
        busy             = false;
        partial_response = outcome.response;
        partial_count    = outcome.response ? static_cast<int>(outcome.response->results.size()) : 0;
        status_text      = QString("查询未完成，已返回 %1 个候选").arg(partial_count);
        emit q->partialChanged();
        emit q->statusTextChanged();
        emit q->busyChanged();

        if (!current_task_id.isEmpty())
        {
            ui::ProgressManager::getInstance()->finishTask(current_task_id, true);
            current_task_id.clear();
        }
        const QString msg = QString("区域检索查询超时截断，获得 %1 个部分候选").arg(partial_count);
        spdlog::warn("{}", msg.toUtf8().constData());
        addProgressMessage(spdlog::level::warn, msg);
        ui::SignalHelper::notifyWarn(QString("区域检索未完全完成"), msg);
        return;
    }

    // TaskOutcome::Kind::Completed
    q->commitResults(outcome.response);
}

void RegionSearchController::prepareTestJob(const int64_t target_class_id)
{
    impl_->active_job.query           = impl_->query;
    impl_->active_job.target_class_id = target_class_id;
    impl_->active_job.gallery.clear();
    impl_->active_job.image_paths.clear();
    if (impl_->data_manager != nullptr)
    {
        const auto img_ids = impl_->data_manager->allImageIds();
        for (const int64_t iid : img_ids)
        {
            const QString p = impl_->data_manager->imagePath(iid);
            const auto fsp = toFsPath(p);
            impl_->active_job.gallery.push_back({iid, impl_->data_manager->imageDatasetId(iid), fsp});
            impl_->active_job.image_paths[iid] = fsp;
        }
    }
    impl_->commit_started = false;
}

void RegionSearchController::commitResults(const std::shared_ptr<irt::features::DinoSearchResponse> &response)
{
    if (impl_->commit_started || impl_->data_manager == nullptr)
    {
        return;
    }
    impl_->commit_started = true;

    if (response == nullptr || response->results.empty())
    {
        impl_->data_manager->setRegionSearchResults({}, true);
        impl_->returned_count = 0;
        impl_->created_count  = 0;
        impl_->reused_count   = 0;
        impl_->skipped_count  = 0;
        impl_->state          = State::Done;
        impl_->busy           = false;
        impl_->status_text    = QString("检索完成，未找到相似区域");
        emit summaryChanged();
        emit statusTextChanged();
        emit busyChanged();

        if (!impl_->current_task_id.isEmpty())
        {
            ui::ProgressManager::getInstance()->updateProgress(100, impl_->current_task_id);
            ui::ProgressManager::getInstance()->finishTask(impl_->current_task_id, true);
            impl_->current_task_id.clear();
        }
        spdlog::info("区域检索完成: 未找到相似区域");
        addProgressMessage(spdlog::level::info, QString("区域检索完成: 未找到相似区域"));
        ui::SignalHelper::notifyInfo(QString("区域检索完成"), QString("未找到相似区域"));
        return;
    }

    impl_->state       = State::Committing;
    impl_->busy        = true;
    impl_->status_text = QString("正在生成标注...");
    emit busyChanged();
    emit statusTextChanged();

    if (!impl_->current_task_id.isEmpty())
    {
        ui::ProgressManager::getInstance()->updateProgress(95, impl_->current_task_id);
    }
    addProgressMessage(spdlog::level::info, QString("正在解析检索结果并生成标注..."));

    GenerationSummary summary;
    summary.returned = static_cast<int>(response->results.size());

    int64_t source_tag_id = impl_->data_manager->findTagClassId(QString("区域检索生成"));
    std::vector<AcceptedItem> accepted;

    for (const auto &hit : response->results)
    {
        const int64_t image_id = hit.image_id;
        if (image_id < 0)
        {
            ++summary.unmapped;
            continue;
        }

        QSize img_size = impl_->data_manager->imageSize(image_id);
        if (!img_size.isValid() || img_size.isEmpty())
        {
            const QString img_path = impl_->data_manager->imagePath(image_id);
            QImageReader reader(img_path);
            img_size = reader.size();
        }
        if (!img_size.isValid() || img_size.isEmpty())
        {
            ++summary.invalid;
            continue;
        }

        const float x0 = std::clamp(hit.bbox.x0, 0.0f, static_cast<float>(img_size.width()));
        const float y0 = std::clamp(hit.bbox.y0, 0.0f, static_cast<float>(img_size.height()));
        const float x1 = std::clamp(hit.bbox.x1, 0.0f, static_cast<float>(img_size.width()));
        const float y1 = std::clamp(hit.bbox.y1, 0.0f, static_cast<float>(img_size.height()));
        const float w  = x1 - x0;
        const float h  = y1 - y0;

        if (w <= 1.0f || h <= 1.0f || !std::isfinite(w) || !std::isfinite(h))
        {
            ++summary.invalid;
            continue;
        }

        const QRectF cand_rect(x0, y0, w, h);

        // 检查自身排除 (同图且 IoU >= 0.90)
        if (image_id == impl_->active_job.query.query_image_id)
        {
            if (computeIoU(cand_rect, impl_->active_job.query.query_rect) >= 0.90)
            {
                ++summary.self_skipped;
                continue;
            }
        }

        // 检查本批已接受项空间去重 (IoU >= 0.90)
        bool dup_in_accepted = false;
        for (const auto &acc : accepted)
        {
            if (acc.image_id == image_id && computeIoU(cand_rect, acc.rect) >= 0.90)
            {
                dup_in_accepted = true;
                break;
            }
        }
        if (dup_in_accepted)
        {
            ++summary.duplicate_skipped;
            continue;
        }

        // 检查图像上已有标注
        const auto existing_label_ids = impl_->data_manager->imageLabelIds(image_id);
        int64_t best_reuse_id = -1;
        double  best_iou      = -1.0;
        bool    has_conflict  = false;

        for (const int64_t el_id : existing_label_ids)
        {
            const QVariantMap el_data = impl_->data_manager->labelData(el_id);
            const double el_x = el_data.value(QStringLiteral("x")).toDouble();
            const double el_y = el_data.value(QStringLiteral("y")).toDouble();
            const double el_w = el_data.value(QStringLiteral("width")).toDouble();
            const double el_h = el_data.value(QStringLiteral("height")).toDouble();
            const QRectF el_rect(el_x, el_y, el_w, el_h);

            const double iou = computeIoU(cand_rect, el_rect);
            if (iou >= 0.90)
            {
                const int64_t el_class = impl_->data_manager->labelClassId(el_id);
                const auto tag_set     = impl_->data_manager->labelTagIds(el_id);
                const bool is_region_generated = (source_tag_id >= 0 && tag_set.count(source_tag_id) > 0);

                if (!is_region_generated || el_class != impl_->active_job.target_class_id)
                {
                    // 人工标注或类别冲突，跳过
                    has_conflict = true;
                    break;
                }

                if (iou > best_iou || (std::abs(iou - best_iou) < 1e-6 && el_id < best_reuse_id))
                {
                    best_iou      = iou;
                    best_reuse_id = el_id;
                }
            }
        }

        if (has_conflict)
        {
            ++summary.duplicate_skipped;
            continue;
        }

        if (best_reuse_id >= 0)
        {
            if (best_reuse_id == impl_->active_job.query.query_label_id)
            {
                ++summary.self_skipped;
                continue;
            }
            accepted.push_back({true, best_reuse_id, image_id, cand_rect});
            ++summary.reused;
        }
        else
        {
            if (static_cast<int>(accepted.size()) >= impl_->active_job.top_k)
            {
                ++summary.limit_skipped;
                continue;
            }
            accepted.push_back({false, -1, image_id, cand_rect});
        }
    }

    std::vector<int64_t>     draft_image_ids;
    std::vector<int64_t>     draft_class_ids;
    std::vector<QVariantMap> draft_datas;

    for (const auto &item : accepted)
    {
        if (!item.is_existing)
        {
            draft_image_ids.push_back(item.image_id);
            draft_class_ids.push_back(impl_->active_job.target_class_id);
            draft_datas.push_back(createLabelDataMap(impl_->data_manager->method(), item.rect));
        }
    }

    std::vector<int64_t> added_ids;
    if (!draft_image_ids.empty())
    {
        if (source_tag_id < 0)
        {
            impl_->data_manager->addTagClass(QString("区域检索生成"), {});
            source_tag_id = impl_->data_manager->findTagClassId(QString("区域检索生成"));
        }

        QString err_msg;
        const bool ok = impl_->data_manager->addLabelsWithIds(
            draft_image_ids, draft_class_ids, draft_datas, &added_ids, &err_msg);

        if (!ok)
        {
            impl_->state       = State::Failed;
            impl_->busy        = false;
            setLastError(QString("标注写入数据库失败: %1").arg(err_msg));
            impl_->status_text = impl_->error_text;
            emit statusTextChanged();
            emit busyChanged();

            if (!impl_->current_task_id.isEmpty())
            {
                ui::ProgressManager::getInstance()->finishTask(impl_->current_task_id, false);
                impl_->current_task_id.clear();
            }
            addProgressMessage(spdlog::level::err, impl_->error_text);
            ui::SignalHelper::notifyError(QString("区域检索失败"), impl_->error_text);
            return;
        }

        if (source_tag_id >= 0 && !added_ids.empty())
        {
            impl_->data_manager->setLabelsTag(added_ids, source_tag_id);
        }
    }

    summary.created = static_cast<int>(added_ids.size());
    summary.tag_id  = source_tag_id;

    size_t new_idx = 0;
    for (const auto &item : accepted)
    {
        if (item.is_existing)
        {
            summary.result_ids.push_back(item.id);
        }
        else if (new_idx < added_ids.size())
        {
            summary.result_ids.push_back(added_ids[new_idx++]);
        }
    }

    impl_->returned_count = summary.returned;
    impl_->created_count  = summary.created;
    impl_->reused_count   = summary.reused;
    impl_->skipped_count  = summary.self_skipped + summary.duplicate_skipped
                            + summary.invalid + summary.unmapped + summary.limit_skipped;

    impl_->data_manager->setRegionSearchResults(summary.result_ids, true);

    impl_->state       = State::Done;
    impl_->busy        = false;
    impl_->status_text = QString("完成: 新增 %1 个，复用 %2 个，跳过 %3 个")
                             .arg(summary.created)
                             .arg(summary.reused)
                             .arg(impl_->skipped_count);

    emit summaryChanged();
    emit statusTextChanged();
    emit busyChanged();

    const QString final_summary = QString("区域检索完成: 候选 %1 个，新增标注 %2 个，复用标注 %3 个，跳过 %4 个 (同图排除 %5, 空间去重 %6, 无效/越界 %7, 数量限制 %8)")
                                      .arg(summary.returned)
                                      .arg(summary.created)
                                      .arg(summary.reused)
                                      .arg(impl_->skipped_count)
                                      .arg(summary.self_skipped)
                                      .arg(summary.duplicate_skipped)
                                      .arg(summary.invalid + summary.unmapped)
                                      .arg(summary.limit_skipped);

    spdlog::info("{}", final_summary.toUtf8().constData());
    addProgressMessage(spdlog::level::info, final_summary);

    if (!impl_->current_task_id.isEmpty())
    {
        ui::ProgressManager::getInstance()->updateProgress(100, impl_->current_task_id);
        ui::ProgressManager::getInstance()->finishTask(impl_->current_task_id, true);
        impl_->current_task_id.clear();
    }

    ui::SignalHelper::notifySuccess(QString("区域检索完成"), impl_->status_text);
}

void RegionSearchController::showLatestResults()
{
    if (impl_->data_manager != nullptr)
    {
        auto *filter = impl_->data_manager->globalFilter();
        if (filter != nullptr)
        {
            filter->setFilterEnabled(dltool::data::GlobalFilter::FilterType::Custom, true);
            filter->setFilter(dltool::data::GlobalFilter::FilterType::Custom,
                              {static_cast<qint64>(dltool::data::GlobalFilter::CustomCondition::RegionSearchResult)});
        }
    }
    emit requestNavigateToReview();
}

void RegionSearchController::shutdown()
{
    impl_->closing = true;
    cancel();

    if (impl_->worker_thread != nullptr && impl_->worker_thread->isRunning())
    {
        impl_->worker_thread->wait();
    }

    if (!impl_->current_task_id.isEmpty())
    {
        ui::ProgressManager::getInstance()->finishTask(impl_->current_task_id, false);
        impl_->current_task_id.clear();
    }

    irt::features::DinoRegionSearch::releaseRuntime(true);
}

void RegionSearchController::requestShutdown()
{
    impl_->closing = true;
    cancel();
}

} // namespace dltool::feature
