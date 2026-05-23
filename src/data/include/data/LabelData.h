#pragma once

#include "DataExport.h"

#include <QObject>
#include <QPointF>
#include <QRect>
#include <QVariantMap>
#include <memory>
#include <utility>
#include <vector>

namespace dltool::data {

class DATA_API LabelData_t
{
public:
    LabelData_t();
    virtual ~LabelData_t();
    double x{0};
    double y{0};
    double width{0};
    double height{0};

    virtual int type() const = 0;

    virtual std::vector<uint8_t> toBlob() const;
    virtual void                 fromBlob(const std::vector<uint8_t> &blob);

    virtual void fromQVariantMap(const QVariantMap &data, const QRectF &image_rect);

    virtual QVariantMap dataMap();
};

class DATA_API DetLabelData_t : public LabelData_t
{
public:
    DetLabelData_t();
    ;
    ~DetLabelData_t();
    ;
    int type() const override;

    std::vector<uint8_t> toBlob() const override;

    void fromBlob(const std::vector<uint8_t> &blob) override;
    void fromQVariantMap(const QVariantMap &data, const QRectF &image_rect) override;

    static std::pair<std::vector<QString>, std::vector<QString>> columns();
};

class DATA_API SegLabelData_t : public LabelData_t
{
public:
    int type() const override;

    std::vector<uint8_t> toBlob() const override;

    void fromBlob(const std::vector<uint8_t> &blob) override;
    void fromQVariantMap(const QVariantMap &data, const QRectF &image_rect) override;

    QVariantMap dataMap() override;

    static std::pair<std::vector<QString>, std::vector<QString>> columns();

    std::vector<QPointF> points;
};

class DATA_API LabelDataHelper_t
{
public:
    LabelDataHelper_t(const int type);
    virtual ~LabelDataHelper_t();

    virtual std::unique_ptr<LabelData_t> createLabelData() const = 0;

    virtual std::pair<std::vector<QString>, std::vector<QString>> dataColumns() const = 0;

    virtual bool isInside(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr) const = 0;

    virtual QVariantMap hitTestHandle(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr,
                                      const double scale) const = 0;

    virtual QVariantMap getEditedData(const QVariantMap &data, const QPointF &start, const QPointF &end,
                                      const QRectF &image_rect) const = 0;

private:
    int type_;
};

class DATA_API DetLabelDataHelper final : public LabelDataHelper_t
{
public:
    DetLabelDataHelper(const int type);
    ~DetLabelDataHelper();

    std::unique_ptr<LabelData_t> createLabelData() const override;

    std::pair<std::vector<QString>, std::vector<QString>> dataColumns() const override;

    bool isInside(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr) const override;

    QVariantMap hitTestHandle(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr,
                              const double scale) const override;

    QVariantMap getEditedData(const QVariantMap &data, const QPointF &start, const QPointF &end,
                              const QRectF &image_rect) const override;
};

class DATA_API SegLabelDataHelper final : public LabelDataHelper_t
{
public:
    SegLabelDataHelper(const int type);
    ~SegLabelDataHelper();

    std::unique_ptr<LabelData_t> createLabelData() const override;

    std::pair<std::vector<QString>, std::vector<QString>> dataColumns() const override;

    bool isInside(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr) const override;

    QVariantMap hitTestHandle(const QPointF &pos, const std::unique_ptr<LabelData_t> &label_data_ptr,
                              const double scale) const override;

    QVariantMap getEditedData(const QVariantMap &data, const QPointF &start, const QPointF &end,
                              const QRectF &image_rect) const override;
};

DATA_API std::unique_ptr<LabelDataHelper_t> createLabelDataHelper(const int type);

} // namespace dltool::data
