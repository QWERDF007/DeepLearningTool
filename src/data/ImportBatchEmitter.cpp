#include "data/DataIO.h"

namespace dltool::data {

DataIO::ImportBatchEmitter::ImportBatchEmitter(DataIO &owner, const int64_t dataset_id)
    : owner_(owner), dataset_id_(dataset_id)
{
    image_paths_.reserve(ImportBatchImageCount);
    image_widths_.reserve(ImportBatchImageCount);
    image_heights_.reserve(ImportBatchImageCount);
}

void DataIO::ImportBatchEmitter::pushImage(const QString &image_path)
{
    image_paths_.push_back(image_path);
}

void DataIO::ImportBatchEmitter::pushImage(const QString &image_path, const int64_t width, const int64_t height)
{
    image_paths_.push_back(image_path);
    image_widths_.push_back(width);
    image_heights_.push_back(height);
}

void DataIO::ImportBatchEmitter::pushLabel(ImportedLabel label)
{
    labels_.push_back(std::move(label));
}

void DataIO::ImportBatchEmitter::pushLabelClass(const QString &name, const QString &color)
{
    label_class_info_[name] = color;
}

bool DataIO::ImportBatchEmitter::flushIfFullByImages(const int64_t processed, const int64_t total)
{
    if (image_paths_.size() < ImportBatchImageCount)
        return true;
    return flushOnce(processed, total);
}

bool DataIO::ImportBatchEmitter::flushIfFullByLabels(const int64_t processed, const int64_t total)
{
    if (labels_.size() < ImportBatchImageCount)
        return true;
    return flushOnce(processed, total);
}

bool DataIO::ImportBatchEmitter::flush(const int64_t processed, const int64_t total)
{
    return flushOnce(processed, total);
}

bool DataIO::ImportBatchEmitter::flushOnce(const int64_t processed, const int64_t total)
{
    if (image_paths_.empty() && labels_.empty())
        return true;

    emit owner_.dataBatchReady(dataset_id_, std::move(image_paths_), std::move(image_widths_),
                               std::move(image_heights_), std::move(label_class_info_), std::move(labels_),
                               processed, total);

    image_paths_.clear();
    image_widths_.clear();
    image_heights_.clear();
    label_class_info_.clear();
    labels_.clear();
    image_paths_.reserve(ImportBatchImageCount);
    image_widths_.reserve(ImportBatchImageCount);
    image_heights_.reserve(ImportBatchImageCount);
    return !owner_.isCancelRequested();
}

} // namespace dltool::data
