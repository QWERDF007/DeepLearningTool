#pragma once

#include "dltool/settings/Export.h"

#include <QObject>
#include <QString>
#include <QtQml>

namespace dltool::database {
class SettingsDataBase;
}

namespace dltool::settings {

/**
 * @brief UI 相关设置
 *
 * 包含界面显示、交互等相关的配置
 */
class SETTINGS_API UISettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(UISettings)
    QML_UNCREATABLE("UISettings is managed by GlobalSettings")

    // 图像单元格缩放 - DEPRECATED: 已迁移到 DataSettings
    // Q_PROPERTY(double imageCellScale READ imageCellScale WRITE setImageCellScale NOTIFY imageCellScaleChanged)
    // Q_PROPERTY(
    //     double imageCellScaleFrom READ imageCellScaleFrom WRITE setImageCellScaleFrom NOTIFY imageCellScaleFromChanged)
    // Q_PROPERTY(double imageCellScaleTo READ imageCellScaleTo WRITE setImageCellScaleTo NOTIFY imageCellScaleToChanged)
    // Q_PROPERTY(double imageCellScaleStepSize READ imageCellScaleStepSize WRITE setImageCellScaleStepSize NOTIFY
    //                imageCellScaleStepSizeChanged)

    // 图像亮度
    Q_PROPERTY(double imageBrightness READ imageBrightness WRITE setImageBrightness NOTIFY imageBrightnessChanged)
    Q_PROPERTY(double imageBrightnessFrom READ imageBrightnessFrom CONSTANT)
    Q_PROPERTY(double imageBrightnessTo READ imageBrightnessTo CONSTANT)
    Q_PROPERTY(double imageBrightnessStepSize READ imageBrightnessStepSize CONSTANT)

    // 图像对比度
    Q_PROPERTY(double imageContrast READ imageContrast WRITE setImageContrast NOTIFY imageContrastChanged)
    Q_PROPERTY(double imageContrastFrom READ imageContrastFrom CONSTANT)
    Q_PROPERTY(double imageContrastTo READ imageContrastTo CONSTANT)
    Q_PROPERTY(double imageContrastStepSize READ imageContrastStepSize CONSTANT)

    // 主题
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)

    // 语言
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    explicit UISettings(QObject *parent = nullptr);
    ~UISettings();

    // DEPRECATED: imageCellScale 已迁移到 DataSettings
    // double imageCellScale() const
    // {
    //     return image_cell_scale_;
    // }

    // void setImageCellScale(double value);

    // double imageCellScaleFrom() const
    // {
    //     return image_cell_scale_from_;
    // }

    // void setImageCellScaleFrom(double value);

    // double imageCellScaleTo() const
    // {
    //     return image_cell_scale_to_;
    // }

    // void setImageCellScaleTo(double value);

    // double imageCellScaleStepSize() const
    // {
    //     return image_cell_scale_step_size_;
    // }

    // void setImageCellScaleStepSize(double value);

    double imageBrightness() const
    {
        return image_brightness_;
    }

    void setImageBrightness(double value);

    double imageBrightnessFrom() const
    {
        return image_brightness_from_;
    }

    double imageBrightnessTo() const
    {
        return image_brightness_to_;
    }

    double imageBrightnessStepSize() const
    {
        return image_brightness_step_size_;
    }

    double imageContrast() const
    {
        return image_contrast_;
    }

    void setImageContrast(double value);

    double imageContrastFrom() const
    {
        return image_contrast_from_;
    }

    double imageContrastTo() const
    {
        return image_contrast_to_;
    }

    double imageContrastStepSize() const
    {
        return image_contrast_step_size_;
    }

    QString theme() const
    {
        return theme_;
    }

    void setTheme(const QString &value);

    QString language() const
    {
        return language_;
    }

    void setLanguage(const QString &value);

    /**
     * @brief 从数据库加载设置
     * @param database 设置数据库
     */
    void load(database::SettingsDataBase *database);

    /**
     * @brief 保存设置到数据库
     * @param database 设置数据库
     */
    void save(database::SettingsDataBase *database);

    /**
     * @brief 重置所有设置为默认值
     */
    void reset();

signals:
    // DEPRECATED: imageCellScale 信号已迁移到 DataSettings
    // void imageCellScaleChanged();
    // void imageCellScaleFromChanged();
    // void imageCellScaleToChanged();
    // void imageCellScaleStepSizeChanged();
    void imageBrightnessChanged();
    void imageContrastChanged();
    void themeChanged();
    void languageChanged();

private:
    // DEPRECATED: imageCellScale 已迁移到 DataSettings
    // double image_cell_scale_{1.0};
    // double image_cell_scale_from_{0.5};
    // double image_cell_scale_to_{4.0};
    // double image_cell_scale_step_size_{0.25};

    double image_brightness_{0.0};
    double image_brightness_from_{-1.0};
    double image_brightness_to_{1.0};
    double image_brightness_step_size_{0.1};

    double image_contrast_{0.0};
    double image_contrast_from_{-1.0};
    double image_contrast_to_{1.0};
    double image_contrast_step_size_{0.1};

    QString theme_{"dark"};
    QString language_{"zh_CN"};
};

} // namespace dltool::settings
