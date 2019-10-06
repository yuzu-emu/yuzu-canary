// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QColorDialog>
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_graphics.h"
#include "yuzu/configuration/configure_graphics.h"

namespace {
enum class Resolution : int {
    Auto,
    Scale1x,
    Scale2x,
    Scale3x,
    Scale4x,
};

float ToResolutionFactor(Resolution option) {
    switch (option) {
    case Resolution::Auto:
        return 0.f;
    case Resolution::Scale1x:
        return 1.f;
    case Resolution::Scale2x:
        return 2.f;
    case Resolution::Scale3x:
        return 3.f;
    case Resolution::Scale4x:
        return 4.f;
    }
    return 0.f;
}

Resolution FromResolutionFactor(float factor) {
    if (factor == 0.f) {
        return Resolution::Auto;
    } else if (factor == 1.f) {
        return Resolution::Scale1x;
    } else if (factor == 2.f) {
        return Resolution::Scale2x;
    } else if (factor == 3.f) {
        return Resolution::Scale3x;
    } else if (factor == 4.f) {
        return Resolution::Scale4x;
    }
    return Resolution::Auto;
}
} // Anonymous namespace

ConfigureGraphics::ConfigureGraphics(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGraphics) {
    ui->setupUi(this);

    SetConfiguration();

    connect(ui->bg_button, &QPushButton::clicked, this, [this] {
        const QColor new_bg_color = QColorDialog::getColor(bg_color);
        if (!new_bg_color.isValid()) {
            return;
        }
        UpdateBackgroundColorButton(new_bg_color);
    });
    connect(ui->brightness_reset, &QPushButton::pressed, this,
            [this] { ui->brightness_slider->setValue(100); });
}

ConfigureGraphics::~ConfigureGraphics() = default;

void ConfigureGraphics::SetConfiguration() {
    const bool runtime_lock = !Core::System::GetInstance().IsPoweredOn();

    ui->resolution_factor_combobox->setCurrentIndex(
        static_cast<int>(FromResolutionFactor(Settings::values.resolution_factor)));
    ui->use_disk_shader_cache->setEnabled(runtime_lock);
    ui->use_disk_shader_cache->setChecked(Settings::values.use_disk_shader_cache);
    ui->use_accurate_gpu_emulation->setChecked(Settings::values.use_accurate_gpu_emulation);
    ui->use_asynchronous_gpu_emulation->setEnabled(runtime_lock);
    ui->use_asynchronous_gpu_emulation->setChecked(Settings::values.use_asynchronous_gpu_emulation);
    ui->force_30fps_mode->setEnabled(runtime_lock);
    ui->force_30fps_mode->setChecked(Settings::values.force_30fps_mode);
    UpdateBackgroundColorButton(QColor::fromRgbF(Settings::values.bg_red, Settings::values.bg_green,
                                                 Settings::values.bg_blue));
    ui->brightness_slider->setValue(Settings::values.backlight_brightness * 100 + 50);
}

void ConfigureGraphics::ApplyConfiguration() {
    Settings::values.resolution_factor =
        ToResolutionFactor(static_cast<Resolution>(ui->resolution_factor_combobox->currentIndex()));
    Settings::values.use_disk_shader_cache = ui->use_disk_shader_cache->isChecked();
    Settings::values.use_accurate_gpu_emulation = ui->use_accurate_gpu_emulation->isChecked();
    Settings::values.use_asynchronous_gpu_emulation =
        ui->use_asynchronous_gpu_emulation->isChecked();
    Settings::values.force_30fps_mode = ui->force_30fps_mode->isChecked();
    Settings::values.bg_red = static_cast<float>(bg_color.redF());
    Settings::values.bg_green = static_cast<float>(bg_color.greenF());
    Settings::values.bg_blue = static_cast<float>(bg_color.blueF());
    Settings::values.backlight_brightness = (ui->brightness_slider->value() - 50.0f) / 100.0f;
}

void ConfigureGraphics::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureGraphics::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureGraphics::UpdateBackgroundColorButton(QColor color) {
    bg_color = color;

    QPixmap pixmap(ui->bg_button->size());
    pixmap.fill(bg_color);

    const QIcon color_icon(pixmap);
    ui->bg_button->setIcon(color_icon);
}
