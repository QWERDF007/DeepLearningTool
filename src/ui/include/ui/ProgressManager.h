#pragma once

#include "common/Singleton.h"
#include "dltool/ui/Export.h"

#include <QQueue>
#include <QString>

namespace dltool::ui {

/**
 * @brief ProgressManager 是一个单例类，用于管理长时间运行任务的进度状态。
 * 
 * 该类提供了一种集中的方式来跟踪和更新后端处理任务的进度信息。
 * 它维护进度百分比、运行状态和消息队列。
 * 
 * 线程安全性：
 * - ProgressManager 设计用于单线程 Qt 事件循环
 * - 对于跨线程调用，请使用 QMetaObject::invokeMethod 配合 Qt::QueuedConnection
 * 
 * 使用示例：
 * @code
 * // 开始任务
 * ProgressManager::getInstance()->startTask("处理图像");
 * 
 * // 更新进度
 * ProgressManager::getInstance()->updateProgress(50);
 * 
 * // 添加消息
 * ProgressManager::getInstance()->addMessage(spdlog::level::info, "正在处理文件 1/10");
 * ProgressManager::getInstance()->addMessage(spdlog::level::err, "加载图像失败");
 * 
 * // 完成任务
 * ProgressManager::getInstance()->completeTask();
 * 
 * // 跨线程示例
 * QMetaObject::invokeMethod(ProgressManager::getInstance(),
 *                          "updateProgress",
 *                          Qt::QueuedConnection,
 *                          Q_ARG(int, 75));
 * @endcode
 */
class UI_API ProgressManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ProgressManager)
    QT_QML_SINGLETON(ProgressManager)

    Q_PROPERTY(double progress READ getProgress NOTIFY progressChanged)
    Q_PROPERTY(bool isRunning READ getIsRunning NOTIFY runningStateChanged)
    Q_PROPERTY(QString message READ getColorfulMessage NOTIFY messageChanged)
    Q_PROPERTY(QString activeTaskId READ activeTaskId NOTIFY activeTaskIdChanged)
    Q_PROPERTY(QString taskName READ getTaskName NOTIFY taskNameChanged)

public:
    /**
     * @brief 获取当前进度值
     * @return 进度百分比 (0-100)
     */
    double getProgress() const
    {
        return progress_;
    }

    /**
     * @brief 获取当前运行状态
     * @return 如果任务正在运行返回 true，否则返回 false
     */
    bool getIsRunning() const
    {
        return is_running_;
    }

    /**
     * @brief 获取当前活动任务的标识符
     * @return 任务标识符
     */
    QString activeTaskId() const
    {
        return active_task_id_;
    }

    /**
     * @brief 获取当前活动任务的名称
     * @return 任务名称
     */
    QString getTaskName() const
    {
        return task_name_;
    }

    /**
     * @brief 获取纯文本消息
     * @return 拼接后的消息字符串
     */
    QString getMessage() const;

    /**
     * @brief 获取消息文本
     * @return 拼接后的消息字符串
     */
    QString getColorfulMessage() const;

    /**
     * @brief 开始新任务
     * 重置进度为 0 并设置运行状态为 true。如果未提供 taskId，则自动生成。
     * @param taskName 任务名称（可选）
     * @param taskId 任务标识符（可选）
     * @return 实际使用的任务标识符
     */
    Q_INVOKABLE QString startTask(const QString &taskName = "", const QString &taskId = "");

    /**
     * @brief 更新进度值
     * 校验任务身份，值会自动限制在 [0, 100] 范围内。
     * @param progress 进度百分比 (0-100)
     * @param taskId 任务标识符（可选）
     */
    Q_INVOKABLE void updateProgress(double progress, const QString &taskId = "");

    /**
     * @brief 向消息队列添加消息
     * 校验任务身份。如果队列超过最大大小，最旧的消息会被移除（FIFO）
     * @param level 消息级别 (spdlog::level::level_enum)
     * @param message 消息文本
     * @param taskId 任务标识符（可选）
     */
    Q_INVOKABLE void addMessage(int level, const QString &message, const QString &taskId = "");

    /**
     * @brief 完成当前任务
     * 校验任务身份。若 success 为 true，设置进度为 100；否则保留当前进度。
     * @param taskId 任务标识符（可选）
     * @param success 是否成功完成
     */
    Q_INVOKABLE void completeTask(const QString &taskId = "", bool success = true);

    /**
     * @brief 结束当前任务（语义化接口，委托给 completeTask）
     * @param taskId 任务标识符（可选）
     * @param success 是否成功完成
     */
    Q_INVOKABLE void finishTask(const QString &taskId = "", bool success = true);

    /**
     * @brief 重置所有状态并清空消息
     */
    Q_INVOKABLE void reset();

signals:
    /**
     * @brief 进度值改变时发射
     */
    void progressChanged();

    /**
     * @brief 运行状态改变时发射
     */
    void runningStateChanged();

    /**
     * @brief 消息队列改变时发射
     */
    void messageChanged();

    /**
     * @brief 当前活动任务标识符改变时发射
     */
    void activeTaskIdChanged();

    /**
     * @brief 当前活动任务名称改变时发射
     */
    void taskNameChanged();

private:
    explicit ProgressManager(QObject *parent = nullptr);
    ~ProgressManager() = default;

    // 禁止拷贝
    ProgressManager(const ProgressManager &)            = delete;
    ProgressManager &operator=(const ProgressManager &) = delete;

    QString generateUniqueTaskId();

    double                          progress_{0.0};
    bool                            is_running_{false};
    bool                            is_anonymous_{false};
    QString                         active_task_id_;
    QString                         task_name_;
    QQueue<std::pair<int, QString>> message_queue_;
    const int                       max_message_size_{100};
    uint64_t                        task_seq_{0};
};

} // namespace dltool::ui
