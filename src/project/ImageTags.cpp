#include "project/ImageTags.h"

#include "data/DataBase.h"
#include "project/Datasets.h"
#include "project/Images.h"

#include <spdlog/spdlog.h>

#include <algorithm>

namespace dltool::project {

ImageTagsListModel::ImageTagsListModel(data::ProjectDataBase *database, ImageInstancesListModel *image_instances,
                                       QObject *parent)
    : QAbstractListModel(parent)
    , database_(database)
    , image_instances_(image_instances)
{
    init();
}

ImageTagsListModel::~ImageTagsListModel() {}

void ImageTagsListModel::init()
{
    if (database_ == nullptr)
    {
        spdlog::error("初始化标签(TagClass/Tag)失败: 数据库未初始化");
        return;
    }

    if (!initTagClass())
        return;
    if (!initImagesTag())
        return;
}

bool ImageTagsListModel::initTagClass()
{
    QString              err_msg;
    std::vector<int64_t> tag_classes_id;
    std::vector<QString> tags_name;
    database_->getAllTagClasses(tag_classes_id, tags_name, err_msg);
    if (!err_msg.isEmpty())
    {
        spdlog::error("查询所有标签(TagClass)失败, error: {}", err_msg.toUtf8().constData());
        return false;
    }
    for (size_t i = 0; i < tag_classes_id.size(); ++i)
    {
        image_tags_.emplace(tag_classes_id[i], new ImageTag(tag_classes_id[i], tags_name[i], this));
    }
    return true;
}

bool ImageTagsListModel::initImagesTag()
{
    if (image_instances_ == nullptr)
    {
        spdlog::error("初始化图像标签(Tag)失败: 图像实例列表未初始化");
        return false;
    }

    connect(image_instances_->selection(), &QItemSelectionModel::selectionChanged, this,
            &ImageTagsListModel::updateStats);
    connect(image_instances_->selection(), &QItemSelectionModel::currentChanged, this,
            &ImageTagsListModel::updateStats);

    QString              err_msg;
    std::vector<int64_t> image_ids, tag_ids;
    database_->getAllTags(image_ids, tag_ids, err_msg);
    if (!err_msg.isEmpty())
    {
        spdlog::error("查询所有图像标签(Tag)失败, error: {}", err_msg.toUtf8().constData());
        return false;
    }

    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        ImageTag *image_tag = getImageTag(tag_ids[i]);
        if (image_tag)
            image_tag->addImageIds({image_ids[i]});
    }
    return true;
}

int ImageTagsListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(image_tags_.size());
}

QVariant ImageTagsListModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return QVariant();
    switch (role)
    {
    case TagIdRole:
        return getTagClassId(index);
    case NameRole:
        return getTagClassName(index);
    case SelectedImagesStatsRole:
        return getSelectedImagesTagStats(index);
    case CurrentImageStatsRole:
        return getCurrentImageTagStats(index);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ImageTagsListModel::roleNames() const
{
    return {
        {              TagIdRole,                "tag_id"},
        {               NameRole,                  "name"},
        {SelectedImagesStatsRole, "selected_images_stats"},
        {  CurrentImageStatsRole,   "current_image_stats"},
    };
}

bool ImageTagsListModel::addTagClass(const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加标签(TagClass)失败: {}, 数据库未初始化", name.toUtf8().constData());
        return false;
    }
    QString err_msg;
    int64_t tag_class_id{-1};
    bool    ok = database_->addTagClass(name, tag_class_id, err_msg);
    if (!ok)
    {
        spdlog::error("添加标签(TagClass)失败: {}, error: {}", name.toUtf8().constData(), err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("添加标签(TagClass): {}", name.toUtf8().constData());
    const int row   = rowCount(); // 添加到队列尾部
    const int count = 1;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    image_tags_.emplace(tag_class_id, new ImageTag(tag_class_id, name, this));
    endInsertRows();
    return true;
}

bool ImageTagsListModel::updateTagClass(const int64_t tag_class_id, const QString &name)
{
    if (database_ == nullptr)
    {
        spdlog::error("更新标签(TagClass)失败: {}, 数据库未初始化", tag_class_id);
        return false;
    }
    auto found = image_tags_.find(tag_class_id);
    if (found == image_tags_.end())
    {
        spdlog::error("更新标签(TagClass)失败: {}, 标签不存在", tag_class_id);
        return false;
    }
    if (found->second->name() == name)
        return true;
    QString err_msg;
    bool    ok = database_->updateTagClass(tag_class_id, name, err_msg);
    if (!ok)
    {
        spdlog::error("更新标签(TagClass)失败: {}, error: {}", tag_class_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("更新标签(TagClass): {} -> {}", found->second->name().toUtf8().constData(), name.toUtf8().constData());
    int idx{0};
    for (const auto &[_, tag] : image_tags_)
    {
        if (tag && tag->id() == tag_class_id)
        {
            tag->setName(name);
            emit dataChanged(index(idx), index(idx), {NameRole});
            break;
        }
        ++idx;
    }
    return true;
}

bool ImageTagsListModel::deleteTagClass(const int64_t tag_class_id)
{
    // TODO: 删除TagClass
    return false;
}

bool ImageTagsListModel::setImagesTag(const std::vector<int64_t> &image_ids, const int64_t tag_id)
{
    if (database_ == nullptr)
    {
        spdlog::error("添加标签(Tag)失败: 数据库未初始化");
        return false;
    }
    if (image_ids.empty())
        return true;
    // TODO: 判断当前图像是否全部包含该标签, 如果包含, 则删除该标签
    std::string op{"添加"}; // 添加, 删除

    std::vector<int64_t> valid_image_ids = getValidImagesId(image_ids, tag_id);

    if (valid_image_ids.empty())
    {
        op              = "删除";
        valid_image_ids = image_ids;
    }
    else
    {
        op = "添加";
    }

    QString err_msg;
    bool    ok{false};
    if (op == "添加")
        ok = database_->addImagesTag(valid_image_ids, tag_id, err_msg);
    else
        ok = database_->deleteImagesTag(valid_image_ids, tag_id, err_msg);
    if (!ok)
    {
        spdlog::error("{}标签(Tag)失败: {}, error: {}", op, tag_id, err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("为 {} 个图像{}标签(Tag)成功, tag_id: {}", image_ids.size(), op, tag_id);
    for (const auto &image_id : image_ids)
    {
        ImageInstance *image_instance = image_instances_->getImageInstance(image_id);
        if (image_instance)
        {
            if (op == "添加")
                image_instance->addTagIds({tag_id});
            else
                image_instance->removeTagIds({tag_id});
        }
        ImageTag *image_tag = getImageTag(tag_id);
        if (image_tag)
        {
            if (op == "添加")
                image_tag->addImageIds({image_id});
            else
                image_tag->removeImageIds({image_id});
        }
    }
    updateStats();
    return true;
}

bool ImageTagsListModel::setImagesTag(const int64_t image_id, const int64_t tag_id)
{
    std::vector<int64_t> image_ids{image_id};
    return setImagesTag(image_ids, tag_id);
}

bool ImageTagsListModel::removeImagesTags(const std::vector<int64_t> &image_ids)
{
    if (database_ == nullptr)
    {
        spdlog::error("删除图像标签(Tag)失败: 数据库未初始化");
        return false;
    }
    if (image_ids.empty())
        return true;
    QString err_msg;
    bool    ok = database_->deleteImagesTagsByImagesId(image_ids, err_msg);
    if (!ok)
    {
        spdlog::error("删除图像标签(Tag)失败: {}, error: {}", image_ids.size(), err_msg.toUtf8().constData());
        return false;
    }
    spdlog::info("删除 {} 个图像标签(Tag)成功", image_ids.size());
    std::map<int64_t, std::vector<int64_t>> tags_image_ids;
    for (const auto &image_id : image_ids)
    {
        ImageInstance *image_instance = image_instances_->getImageInstance(image_id);
        if (image_instance == nullptr)
            continue;
        auto tag_ids = image_instance->removeAllTagIds();
        for (const auto &tag_id : tag_ids)
        {
            tags_image_ids[tag_id].push_back(image_id);
        }
    }
    for (const auto &[tag_id, deleted_image_ids] : tags_image_ids)
    {
        ImageTag *image_tag = getImageTag(tag_id);
        if (image_tag)
        {
            image_tag->removeImageIds(deleted_image_ids);
        }
    }
    updateStats();
    return true;
}

ImageTag *ImageTagsListModel::getImageTag(const int64_t tag_id)
{
    auto found = image_tags_.find(tag_id);
    if (found == image_tags_.end())
        return nullptr;
    return found->second;
}

std::vector<std::vector<int64_t>> ImageTagsListModel::getImagesTagIds(const std::vector<int64_t> &image_ids) const
{
    std::vector<std::vector<int64_t>> images_tag_ids;
    images_tag_ids.reserve(image_ids.size());
    for (size_t i = 0; i < image_ids.size(); ++i)
    {
        const int64_t        image_id = image_ids[i];
        std::vector<int64_t> tag_ids;
        for (const auto &[tag_id, image_tag] : image_tags_)
        {
            if (image_tag->imageIds().count(image_id) > 0)
                tag_ids.push_back(tag_id);
        }
        images_tag_ids.push_back(tag_ids);
    }
    return images_tag_ids;
}

int ImageTagsListModel::getTagClassId(const QModelIndex &index) const
{
    int idx = 0;
    for (const auto &[tag_id, tag] : image_tags_)
    {
        if (index.row() == idx)
            return tag_id;
        ++idx;
    }
    return -1;
}

QVariant ImageTagsListModel::getTagClassName(const QModelIndex &index) const
{
    const int tag_id = getTagClassId(index);
    if (tag_id != -1)
        return image_tags_.at(tag_id)->name();
    return "";
}

QVariant ImageTagsListModel::getSelectedImagesTagStats(const QModelIndex &index) const
{
    if (image_instances_ == nullptr)
    {
        spdlog::error("获取选中图像标签(Tag)统计失败: 图像实例列表未初始化");
        return "";
    }
    const int tag_id = getTagClassId(index);
    if (tag_id == -1)
    {
        spdlog::error("获取选中图像标签(Tag)统计失败: 标签 {} 不存在", tag_id);
        return "";
    }
    const std::vector<int64_t> image_ids = image_instances_->getSelectedImagesId();
    if (image_ids.empty())
        return "";

    const std::vector<ImageInstance *> image_instances = image_instances_->getImageInstances(image_ids);

    int count{0};
    for (const ImageInstance *image_instance : image_instances)
    {
        if (image_instance->tagIds().count(tag_id) > 0)
            ++count;
    }
    return count > 0 ? QString("(%1)").arg(count) : "";
}

QVariant ImageTagsListModel::getCurrentImageTagStats(const QModelIndex &index) const
{
    if (image_instances_ == nullptr)
    {
        spdlog::error("获取当前图像标签(Tag)统计失败: 图像实例列表未初始化");
        return "";
    }
    const int tag_id = getTagClassId(index);
    if (tag_id == -1)
    {
        spdlog::error("获取当前图像标签(Tag)统计失败: 标签 {} 不存在", tag_id);
        return "";
    }

    const int image_id = image_instances_->getCurImageId();
    if (image_id == -1)
        return "";

    const std::vector<ImageInstance *> image_instances = image_instances_->getImageInstances({image_id});

    int count{0};
    if (!image_instances.empty() && image_instances.at(0)->tagIds().count(tag_id) > 0)
        ++count;
    return count > 0 ? QString("(%1)").arg(count) : "";
}

std::vector<int64_t> ImageTagsListModel::getValidImagesId(const std::vector<int64_t> &new_image_ids,
                                                          const int64_t               tag_id)
{
    std::set<int64_t> A{new_image_ids.begin(), new_image_ids.end()};
    std::set<int64_t> B = getImageTag(tag_id)->imageIds();
    if (A.empty() || B.empty())
        return new_image_ids;
    std::set<int64_t> result;
    // 注意: 两个集合需要已排序, 所以不能用 std::unordered_set
    std::set_difference(A.begin(), A.end(), B.begin(), B.end(), std::inserter(result, result.end()));
    return std::vector<int64_t>{result.begin(), result.end()};
}

void ImageTagsListModel::updateStats()
{
    if (image_instances_ == nullptr || image_tags_.empty())
        return;
    emit dataChanged(index(0), index(static_cast<int>(image_tags_.size() - 1)),
                     {SelectedImagesStatsRole, CurrentImageStatsRole});
}

} // namespace dltool::project
