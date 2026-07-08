#pragma once

#include "dltool/feature/Export.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QtQml>

namespace dltool::model {
class ModelTaskController;
}

namespace dltool::data {
class DataManager;
}

namespace dltool::feature {

class FEATURE_API FewShotLearningController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FewShotLearningController)
    QML_UNCREATABLE("Can not create FewShotLearningController directly!")
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(QObject *trainDatasetViewModel READ trainDatasetViewModel WRITE setTrainDatasetViewModel NOTIFY
                   trainDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *validationDatasetViewModel READ validationDatasetViewModel WRITE setValidationDatasetViewModel
                   NOTIFY validationDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *testDatasetViewModel READ testDatasetViewModel WRITE setTestDatasetViewModel NOTIFY
                   testDatasetViewModelChanged FINAL)
    Q_PROPERTY(QObject *labelClassViewModel READ labelClassViewModel WRITE setLabelClassViewModel NOTIFY
                   labelClassViewModelChanged FINAL)

public:
    /**
     * @brief 构造函数
     * @param parent 父对象
     */
    explicit FewShotLearningController(dltool::data::DataManager          *data_manager,
                                       dltool::model::ModelTaskController *model_task_controller,
                                       QObject                            *parent = nullptr);
    ~FewShotLearningController() override;

    /**
     * @brief 功能是否启用
     * @return 启用返回 true
     */
    bool enabled() const;

    /**
     * @brief 是否正在运行
     * @return 运行中返回 true
     */
    bool running() const;

    /**
     * @brief 获取最后一次错误信息
     * @return 错误信息文本
     */
    QString lastError() const;

    QObject *trainDatasetViewModel() const;
    void     setTrainDatasetViewModel(QObject *view_model);

    QObject *validationDatasetViewModel() const;
    void     setValidationDatasetViewModel(QObject *view_model);

    QObject *testDatasetViewModel() const;
    void     setTestDatasetViewModel(QObject *view_model);

    QObject *labelClassViewModel() const;
    void     setLabelClassViewModel(QObject *view_model);

    /**
     * @brief 启动小样本学习流程（FS-SAM2），数据集和类别从控制器持有的 ViewModel 读取
     * @return 启动成功返回 true
     */
    Q_INVOKABLE bool startFsSam2();

    /// 清除最后一次错误信息
    Q_INVOKABLE void clearLastError();

    /// 取消当前运行的小样本学习任务
    Q_INVOKABLE void cancel();

signals:
    void enabledChanged();
    void runningChanged();
    void lastErrorChanged();
    void trainDatasetViewModelChanged();
    void validationDatasetViewModelChanged();
    void testDatasetViewModelChanged();
    void labelClassViewModelChanged();

private:
    bool startFsSam2WithIds(const QVariantList &train_dataset_ids, const QVariantList &validation_dataset_ids,
                            const QVariantList &test_dataset_ids, const QVariantList &label_class_ids);

    void setRunning(bool running);
    void setLastError(const QString &last_error);

    QPointer<dltool::data::DataManager>          data_manager_;
    QPointer<dltool::model::ModelTaskController> model_task_controller_;

    bool enabled_{true};  ///< 功能是否启用
    bool running_{false}; ///< 是否正在运行

    QString last_error_; ///< 最后一次错误信息

    QPointer<QObject> train_dataset_view_model_;
    QPointer<QObject> validation_dataset_view_model_;
    QPointer<QObject> test_dataset_view_model_;
    QPointer<QObject> label_class_view_model_;
};

} // namespace dltool::feature
