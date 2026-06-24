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

    Q_PROPERTY(int progress READ getProgress NOTIFY progressChanged)
    Q_PROPERTY(bool isRunning READ getIsRunning NOTIFY runningStateChanged)
    Q_PROPERTY(QString message READ getColorfulMessage NOTIFY messageChanged)

public:
    /**
     * @brief 获取当前进度值
     * @return 进度百分比 (0-100)
     */
    int getProgress() const
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
     * 重置进度为 0 并设置运行状态为 true
     * @param taskName 任务名称（可选）
     */
    Q_INVOKABLE void startTask(const QString &taskName = "");

    /**
     * @brief 更新进度值
     * 值会自动限制在 [0, 100] 范围内
     * @param progress 进度百分比 (0-100)
     */
    Q_INVOKABLE void updateProgress(int progress);

    /**
     * @brief 向消息队列添加消息
     * 如果队列超过最大大小，最旧的消息会被移除（FIFO）
     * @param level 消息级别 (spdlog::level::level_enum)
     * @param message 消息文本
     */
    Q_INVOKABLE void addMessage(int level, const QString &message);

    /**
     * @brief 完成当前任务
     * 设置进度为 100 并设置运行状态为 false
     */
    Q_INVOKABLE void completeTask();

    /**
     * @brief 重置所有进度状态
     * 清空进度、运行状态、任务名称和消息队列
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
     * @brief 消息添加或队列改变时发射
     */
    void messageChanged();

private:
    explicit ProgressManager(QObject *parent = nullptr);
    ~ProgressManager() = default;

    // 禁止拷贝
    ProgressManager(const ProgressManager &)            = delete;
    ProgressManager &operator=(const ProgressManager &) = delete;

    int                             progress_{0};
    bool                            is_running_{false};
    QString                         task_name_;
    QQueue<std::pair<int, QString>> message_queue_;
    const int                       max_message_size_{100};
};

} // namespace dltool::ui
