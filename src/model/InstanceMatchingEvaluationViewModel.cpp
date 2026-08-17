#include "model/InstanceMatchingEvaluationViewModel.h"

#include "model/EvaluationResult.h"
#include "model/ModelEvaluationProtocol.h"

namespace dltool::model {

InstanceMatchingEvaluationViewModel::InstanceMatchingEvaluationViewModel(QObject *parent)
    : ModelEvaluationViewModel(parent)
{
}

QVariantList InstanceMatchingEvaluationViewModel::precisionRecallClasses() const
{
    return precision_recall_classes_;
}

void InstanceMatchingEvaluationViewModel::applyMethodSpecificData(const EvaluationResult &result)
{
    /**
     * @brief 从 PR 曲线图表描述符派生类别选项。
     *
     * 与 QML 侧 chartDescriptorForDisplay/refreshPrecisionRecallClasses 的
     * 旧逻辑一致：只取 series_kind == class 的数据集，使用图表自带名称与
     * 边框颜色，避免 ViewModel 重复维护调色板。
     */
    QVariantList options;
    const QString precision_recall_id = evaluation::chartIdKey(evaluation::ChartId::PrecisionRecall);
    for (const QVariantMap &chart : result.charts)
    {
        if (chart.value(evaluation::fieldName(evaluation::Field::ChartId)).toString() != precision_recall_id)
            continue;
        const QVariantList datasets
            = chart.value(evaluation::fieldName(evaluation::Field::Data)).toMap()
                  .value(evaluation::fieldName(evaluation::Field::Datasets)).toList();
        for (const QVariant &value : datasets)
        {
            const QVariantMap dataset = value.toMap();
            const evaluation::SeriesKind series_kind = evaluation::seriesKindFromKey(
                dataset.value(evaluation::fieldName(evaluation::Field::SeriesKind)).toString());
            if (series_kind != evaluation::SeriesKind::Class)
                continue;
            QVariantMap option;
            option.insert(QStringLiteral("classId"), dataset.value(evaluation::fieldName(evaluation::Field::ClassId)));
            option.insert(QStringLiteral("name"),
                          dataset.value(evaluation::fieldName(evaluation::Field::ClassName)).toString());
            option.insert(QStringLiteral("color"), dataset.value(QStringLiteral("borderColor")).toString());
            options.push_back(option);
        }
        break;
    }
    if (precision_recall_classes_ == options)
        return;
    precision_recall_classes_ = options;
    emit methodDataChanged();
}

} // namespace dltool::model
