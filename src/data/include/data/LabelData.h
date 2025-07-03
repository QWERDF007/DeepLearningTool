#pragma once

#include "DataExport.h"

#include <QObject>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <utility>

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

    virtual void fromQVariantMap(const QVariantMap &data);

    virtual const QVariantMap &dataMap();

protected:
    QVariantMap data_map_;
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
    void                 fromBlob(const std::vector<uint8_t> &blob) override;

    void fromQVariantMap(const QVariantMap &data) override;

    static std::pair<std::vector<QString>, std::vector<QString>> columns();
};

class DATA_API SegLabelData_t : public LabelData_t
{
public:
    int type() const override;

    std::vector<uint8_t> toBlob() const override;

    void fromBlob(const std::vector<uint8_t> &blob) override;
    void fromQVariantMap(const QVariantMap &data) override;
};

using LabelDataFactory = std::function<std::unique_ptr<LabelData_t>()>;

DATA_API LabelDataFactory createLabelDataFactory(const int type);

DATA_API std::pair<std::vector<QString>, std::vector<QString>> LabelDataColumns(const int type);

} // namespace dltool::data
