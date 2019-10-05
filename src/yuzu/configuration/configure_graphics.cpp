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
    Scanner,
    Scale1x,
    Scale2x,
    Scale3x,
    Scale4x,
};

float ToResolutionFactor(Resolution option) {
    switch (option) {
    case Resolution::Scanner:
        return 1.f;
    case Resolution::Scale1x:
        return 1.f;
    case Resolution::Scale2x:
        return 2.f;
    case Resolution::Scale3x:
        return 3.f;
    case Resolution::Scale4x:
        return 4.f;
    }
    return 1.f;
}

Resolution FromResolutionFactor(float factor, bool scanner_on) {
    if (scanner_on) {
        return Resolution::Scanner;
    } else if (factor == 1.f) {
        return Resolution::Scale1x;
    } else if (factor == 2.f) {
        return Resolution::Scale2x;
    } else if (factor == 3.f) {
        return Resolution::Scale3x;
    } else if (factor == 4.f) {
        return Resolution::Scale4x;
    }
    return Resolution::Scale1x;
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
}

ConfigureGraphics::~ConfigureGraphics() = default;

void ConfigureGraphics::SetConfiguration() {
    const bool runtime_lock = !Core::System::GetInstance().IsPoweredOn();

    ui->resolution_factor_combobox->setEnabled(runtime_lock);
    ui->resolution_factor_combobox->setCurrentIndex(static_cast<int>(FromResolutionFactor(
        Settings::values.resolution_factor, Settings::values.use_resolution_scanner)));
    ui->use_disk_shader_cache->setEnabled(runtime_lock);
    ui->use_disk_shader_cache->setChecked(Settings::values.use_disk_shader_cache);
    ui->use_accurate_gpu_emulation->setChecked(Settings::values.use_accurate_gpu_emulation);
    ui->use_asynchronous_gpu_emulation->setEnabled(runtime_lock);
    ui->use_asynchronous_gpu_emulation->setChecked(Settings::values.use_asynchronous_gpu_emulation);
    ui->force_30fps_mode->setEnabled(runtime_lock);
    ui->force_30fps_mode->setChecked(Settings::values.force_30fps_mode);
    UpdateBackgroundColorButton(QColor::fromRgbF(Settings::values.bg_red, Settings::values.bg_green,
                                                 Settings::values.bg_blue));
}

void ConfigureGraphics::ApplyConfiguration() {
    const auto resolution = static_cast<Resolution>(ui->resolution_factor_combobox->currentIndex());
    Settings::values.resolution_factor = ToResolutionFactor(resolution);
    Settings::values.use_disk_shader_cache = ui->use_disk_shader_cache->isChecked();
    Settings::values.use_accurate_gpu_emulation = ui->use_accurate_gpu_emulation->isChecked();
    Settings::values.use_asynchronous_gpu_emulation =
        ui->use_asynchronous_gpu_emulation->isChecked();
    Settings::values.use_resolution_scanner = resolution == Resolution::Scanner;
    Settings::values.force_30fps_mode = ui->force_30fps_mode->isChecked();
    Settings::values.bg_red = static_cast<float>(bg_color.redF());
    Settings::values.bg_green = static_cast<float>(bg_color.greenF());
    Settings::values.bg_blue = static_cast<float>(bg_color.blueF());
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
