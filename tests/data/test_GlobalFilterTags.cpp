#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include "core/CoreDef.h"
#include "data/DataManager.h"
#include "data/DataViewModels.h"
#include "data/FilterItemsModel.h"
#include "data/GlobalFilter.h"
#include "data/ImageTags.h"
#include "data/LabelData.h"
#include "data/Labels.h"
#include "database/DataBase.h"

using namespace dltool::data;

class TestGlobalFilterTags : public QObject
{
    Q_OBJECT

private slots:
    void testImageTagAndLabelTagSeparation();
    void testLabelTagCrossViewSemantics();
    void testDynamicTagFilterItems();
    void testSelectedLabelsInfoModelDisplaysLabelTag();
};

void TestGlobalFilterTags::testImageTagAndLabelTagSeparation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString project_path = QDir(dir.path()).filePath(QStringLiteral("tags_test.dlpro"));
    dltool::database::ProjectDataBase database(project_path);
    QString err;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVERIFY(database.initProject(QStringLiteral("TagsTest"),
                                 static_cast<int>(dltool::core::DeepLearningMethod::Segmentation),
                                 project_path, QStringLiteral("desc"), dir.path(), now, now, err));

    int64_t ds_id = -1;
    QVERIFY(database.addDataset(QStringLiteral("dataset_1"), ds_id, err));

    // 添加类别
    int64_t class_id = -1;
    QVERIFY(database.addLabelClass(QStringLiteral("defect"), QStringLiteral("#ff0000"), QStringLiteral(""), 0, {}, class_id, err));

    // 添加图像 1 和 图像 2
    std::vector<int64_t> img_ids;
    QVERIFY(database.addImages(ds_id, {QStringLiteral("img1.png"), QStringLiteral("img2.png")}, img_ids, err));
    QCOMPARE(img_ids.size(), 2);
    int64_t img1_id = img_ids[0];
    int64_t img2_id = img_ids[1];

    // 添加标注
    dltool::data::SegLabelData_t seg;
    seg.points = {QPointF(0, 0), QPointF(10, 0), QPointF(10, 10)};
    std::vector<int64_t> lbl_ids;
    QVERIFY(database.addLabels({img1_id, img1_id}, {class_id, class_id},
                               {static_cast<int64_t>(dltool::core::DeepLearningMethod::Segmentation),
                                static_cast<int64_t>(dltool::core::DeepLearningMethod::Segmentation)},
                               {seg.toBlob(), seg.toBlob()}, lbl_ids, err));
    QCOMPARE(lbl_ids.size(), 2);
    int64_t label1_id = lbl_ids[0];
    int64_t label2_id = lbl_ids[1];

    // 添加两个 Tag 类别
    int64_t tag_img_id = -1;
    int64_t tag_lbl_id = -1;
    QVERIFY(database.addTagClass(QStringLiteral("ImgTagA"), {}, tag_img_id, err));
    QVERIFY(database.addTagClass(QStringLiteral("LblTagB"), {}, tag_lbl_id, err));

    // 绑定 Tag:
    // img1 绑定 tag_img_id
    // label1 绑定 tag_lbl_id (注意：同图的 label2 没有绑定任何 tag)
    QVERIFY(database.addTagsToImages({img1_id}, tag_img_id, err));
    QVERIFY(database.addTagsToLabels({label1_id}, tag_lbl_id, err));

    dltool::data::DataManager data_manager(
        static_cast<int>(dltool::core::DeepLearningMethod::Segmentation), &database, dir.path());
    data_manager.waitForOperations();

    GlobalFilter *filter = data_manager.globalFilter();
    QVERIFY(filter != nullptr);

    // 1. 验证按图像 Tag 过滤：仅影响图像，不向标注无条件扩散
    filter->setFilter(GlobalFilter::FilterType::ImageTag, {tag_img_id});
    filter->setFilterEnabled(GlobalFilter::FilterType::ImageTag, true);

    QVERIFY(filter->acceptsImage(img1_id));
    QVERIFY(!filter->acceptsImage(img2_id));

    // 2. 验证按标注 Tag 过滤：
    // - label1 绑定了 tag_lbl_id，应当通过
    // - label2 位于同一张图像上但未绑定 tag_lbl_id，严禁通过！
    filter->clearFilter(GlobalFilter::FilterType::ImageTag);
    filter->setFilterEnabled(GlobalFilter::FilterType::ImageTag, false);

    filter->setFilter(GlobalFilter::FilterType::LabelTag, {tag_lbl_id});
    filter->setFilterEnabled(GlobalFilter::FilterType::LabelTag, true);

    QVERIFY(filter->acceptsLabel(label1_id));
    QVERIFY(!filter->acceptsLabel(label2_id));

    // 3. 验证图像 Tag 对标注的约束继承：
    // 若图像被 ImageTag 过滤排除，其所属标注即使满足 LabelTag 也不可显示
    filter->setFilter(GlobalFilter::FilterType::ImageTag, {99999}); // 不匹配 img1
    filter->setFilterEnabled(GlobalFilter::FilterType::ImageTag, true);

    QVERIFY(!filter->acceptsLabel(label1_id));
    QVERIFY(!filter->acceptsLabel(label2_id));
}

void TestGlobalFilterTags::testLabelTagCrossViewSemantics()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString project_path = QDir(dir.path()).filePath(QStringLiteral("cross_test.dlpro"));
    dltool::database::ProjectDataBase database(project_path);
    QString err;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVERIFY(database.initProject(QStringLiteral("CrossTest"),
                                 static_cast<int>(dltool::core::DeepLearningMethod::Segmentation),
                                 project_path, QStringLiteral("desc"), dir.path(), now, now, err));

    int64_t ds_id = -1;
    QVERIFY(database.addDataset(QStringLiteral("dataset_1"), ds_id, err));
    int64_t class_id = -1;
    QVERIFY(database.addLabelClass(QStringLiteral("defect"), QStringLiteral("#ff0000"), QStringLiteral(""), 0, {}, class_id, err));

    std::vector<int64_t> img_ids;
    QVERIFY(database.addImages(ds_id, {QStringLiteral("img1.png"), QStringLiteral("img2.png")}, img_ids, err));
    QCOMPARE(img_ids.size(), 2);
    int64_t img1_id = img_ids[0];
    int64_t img2_id = img_ids[1];

    dltool::data::SegLabelData_t seg;
    seg.points = {QPointF(0, 0), QPointF(10, 0), QPointF(10, 10)};
    std::vector<int64_t> lbl_ids;
    QVERIFY(database.addLabels({img1_id, img2_id}, {class_id, class_id},
                               {static_cast<int64_t>(dltool::core::DeepLearningMethod::Segmentation),
                                static_cast<int64_t>(dltool::core::DeepLearningMethod::Segmentation)},
                               {seg.toBlob(), seg.toBlob()}, lbl_ids, err));
    QCOMPARE(lbl_ids.size(), 2);
    int64_t label1_id = lbl_ids[0];
    int64_t label2_id = lbl_ids[1];
    Q_UNUSED(label2_id);

    int64_t tag_lbl_id = -1;
    QVERIFY(database.addTagClass(QStringLiteral("Cluster_2_1"), {}, tag_lbl_id, err));
    QVERIFY(database.addTagsToLabels({label1_id}, tag_lbl_id, err));

    dltool::data::DataManager data_manager(
        static_cast<int>(dltool::core::DeepLearningMethod::Segmentation), &database, dir.path());
    data_manager.waitForOperations();

    GlobalFilter *filter = data_manager.globalFilter();
    QVERIFY(filter != nullptr);

    // 启用 LabelTag: {tag_lbl_id}
    filter->setFilter(GlobalFilter::FilterType::LabelTag, {tag_lbl_id});
    filter->setFilterEnabled(GlobalFilter::FilterType::LabelTag, true);

    // 联动包含：img1 包含带有 tag_lbl_id 的标注，图库应通过
    QVERIFY(filter->acceptsImage(img1_id));
    // img2 没有任何标注拥有 tag_lbl_id，图库应排除
    QVERIFY(!filter->acceptsImage(img2_id));
}

void TestGlobalFilterTags::testDynamicTagFilterItems()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString project_path = QDir(dir.path()).filePath(QStringLiteral("dyn_test.dlpro"));
    dltool::database::ProjectDataBase database(project_path);
    QString err;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVERIFY(database.initProject(QStringLiteral("DynTest"),
                                 static_cast<int>(dltool::core::DeepLearningMethod::Segmentation),
                                 project_path, QStringLiteral("desc"), dir.path(), now, now, err));

    int64_t ds_id = -1;
    QVERIFY(database.addDataset(QStringLiteral("dataset_1"), ds_id, err));
    int64_t class_id = -1;
    QVERIFY(database.addLabelClass(QStringLiteral("defect"), QStringLiteral("#ff0000"), QStringLiteral(""), 0, {}, class_id, err));

    std::vector<int64_t> img_ids;
    QVERIFY(database.addImages(ds_id, {QStringLiteral("img1.png")}, img_ids, err));
    QCOMPARE(img_ids.size(), 1);
    int64_t img1_id = img_ids[0];

    dltool::data::SegLabelData_t seg;
    seg.points = {QPointF(0, 0), QPointF(10, 0), QPointF(10, 10)};
    std::vector<int64_t> lbl_ids;
    QVERIFY(database.addLabels({img1_id}, {class_id},
                               {static_cast<int64_t>(dltool::core::DeepLearningMethod::Segmentation)},
                               {seg.toBlob()}, lbl_ids, err));
    QCOMPARE(lbl_ids.size(), 1);
    int64_t label1_id = lbl_ids[0];

    int64_t tag_img_id = -1;
    int64_t tag_lbl_id = -1;
    QVERIFY(database.addTagClass(QStringLiteral("OnlyImage"), {}, tag_img_id, err));
    QVERIFY(database.addTagClass(QStringLiteral("OnlyLabel"), {}, tag_lbl_id, err));

    QVERIFY(database.addTagsToImages({img1_id}, tag_img_id, err));
    QVERIFY(database.addTagsToLabels({label1_id}, tag_lbl_id, err));

    dltool::data::DataManager data_manager(
        static_cast<int>(dltool::core::DeepLearningMethod::Segmentation), &database, dir.path());
    data_manager.waitForOperations();

    // 验证 imageTagFilterItems 仅包含 tag_img_id
    auto *img_filter_items = data_manager.imageTagFilterItems();
    QVERIFY(img_filter_items != nullptr);
    QCOMPARE(img_filter_items->rowCount(), 1);
    QCOMPARE(img_filter_items->data(img_filter_items->index(0, 0), FilterItemsModel::IdRole).toLongLong(), tag_img_id);

    // 验证 labelTagFilterItems 仅包含 tag_lbl_id
    auto *lbl_filter_items = data_manager.labelTagFilterItems();
    QVERIFY(lbl_filter_items != nullptr);
    QCOMPARE(lbl_filter_items->rowCount(), 1);
    QCOMPARE(lbl_filter_items->data(lbl_filter_items->index(0, 0), FilterItemsModel::IdRole).toLongLong(), tag_lbl_id);
}

void TestGlobalFilterTags::testSelectedLabelsInfoModelDisplaysLabelTag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString project_path = QDir(dir.path()).filePath(QStringLiteral("info_test.dlpro"));
    dltool::database::ProjectDataBase database(project_path);
    QString err;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QVERIFY(database.initProject(QStringLiteral("InfoTest"),
                                 static_cast<int>(dltool::core::DeepLearningMethod::Segmentation),
                                 project_path, QStringLiteral("desc"), dir.path(), now, now, err));

    int64_t ds_id = -1;
    QVERIFY(database.addDataset(QStringLiteral("dataset_1"), ds_id, err));
    int64_t class_id = -1;
    QVERIFY(database.addLabelClass(QStringLiteral("defect"), QStringLiteral("#ff0000"), QStringLiteral(""), 0, {}, class_id, err));

    std::vector<int64_t> img_ids;
    QVERIFY(database.addImages(ds_id, {QStringLiteral("img1.png")}, img_ids, err));
    QCOMPARE(img_ids.size(), 1);
    int64_t img1_id = img_ids[0];

    dltool::data::SegLabelData_t seg;
    seg.points = {QPointF(0, 0), QPointF(10, 0), QPointF(10, 10)};
    std::vector<int64_t> lbl_ids;
    QVERIFY(database.addLabels({img1_id, img1_id}, {class_id, class_id},
                               {static_cast<int64_t>(dltool::core::DeepLearningMethod::Segmentation),
                                static_cast<int64_t>(dltool::core::DeepLearningMethod::Segmentation)},
                               {seg.toBlob(), seg.toBlob()}, lbl_ids, err));
    QCOMPARE(lbl_ids.size(), 2);
    int64_t label1_id = lbl_ids[0];
    int64_t label2_id = lbl_ids[1];
    Q_UNUSED(label2_id);

    int64_t tag_img_id = -1;
    int64_t tag_lbl_id = -1;
    QVERIFY(database.addTagClass(QStringLiteral("ImageQualityBad"), {}, tag_img_id, err));
    QVERIFY(database.addTagClass(QStringLiteral("Cluster_A"), {}, tag_lbl_id, err));

    QVERIFY(database.addTagsToImages({img1_id}, tag_img_id, err));
    QVERIFY(database.addTagsToLabels({label1_id}, tag_lbl_id, err));

    dltool::data::DataManager data_manager(
        static_cast<int>(dltool::core::DeepLearningMethod::Segmentation), &database, dir.path());
    data_manager.waitForOperations();

    auto *info_model = data_manager.selectedLabelsInfo();
    QVERIFY(info_model != nullptr);

    auto *label_instances = data_manager.labelInstances();
    QVERIFY(label_instances != nullptr);

    // 选中 label1 (拥有标注 Tag "Cluster_A")
    int label1_row = -1;
    for (int r = 0; r < label_instances->rowCount(); ++r)
    {
        if (label_instances->data(label_instances->index(r, 0), LabelInstancesViewModel::LabelIdRole).toLongLong() == label1_id)
        {
            label1_row = r;
            break;
        }
    }
    QVERIFY(label1_row >= 0);
    label_instances->selection()->select(label_instances->index(label1_row, 0),
                                         QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    info_model->refresh();

    // 应该显示标注 Tag "Cluster_A;"，绝不能显示图像 Tag "ImageQualityBad;"
    QVERIFY(info_model->tagText().contains(QStringLiteral("Cluster_A")));
    QVERIFY(!info_model->tagText().contains(QStringLiteral("ImageQualityBad")));
}

QTEST_MAIN(TestGlobalFilterTags)
#include "test_GlobalFilterTags.moc"
