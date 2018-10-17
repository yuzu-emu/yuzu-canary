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
    this->setConfiguration();

    ui->frame_limit->setEnabled(Settings::values.use_frame_limit);
    connect(ui->toggle_frame_limit, &QCheckBox::stateChanged, ui->frame_limit,
            &QSpinBox::setEnabled);
    connect(ui->bg_button, &QPushButton::clicked, this, [this] {
        const QColor new_bg_color = QColorDialog::getColor(bg_color);
        if (!new_bg_color.isValid())
            return;
        bg_color = new_bg_color;
        ui->bg_button->setStyleSheet(
            QString("QPushButton { background-color: %1 }").arg(bg_color.name()));
    });
}

ConfigureGraphics::~ConfigureGraphics() = default;

void ConfigureGraphics::setConfiguration() {
    ui->resolution_factor_combobox->setCurrentIndex(
        static_cast<int>(FromResolutionFactor(Settings::values.resolution_factor)));
    ui->toggle_frame_limit->setChecked(Settings::values.use_frame_limit);
    ui->frame_limit->setValue(Settings::values.frame_limit);
    ui->use_accurate_gpu_emulation->setChecked(Settings::values.use_accurate_gpu_emulation);
    bg_color = QColor::fromRgbF(Settings::values.bg_red, Settings::values.bg_green,
                                Settings::values.bg_blue);
    ui->bg_button->setStyleSheet(
        QString("QPushButton { background-color: %1 }").arg(bg_color.name()));
}

void ConfigureGraphics::applyConfiguration() {
    Settings::values.resolution_factor =
        ToResolutionFactor(static_cast<Resolution>(ui->resolution_factor_combobox->currentIndex()));
    Settings::values.use_frame_limit = ui->toggle_frame_limit->isChecked();
    Settings::values.frame_limit = ui->frame_limit->value();
    Settings::values.use_accurate_gpu_emulation = ui->use_accurate_gpu_emulation->isChecked();
    Settings::values.bg_red = static_cast<float>(bg_color.redF());
    Settings::values.bg_green = static_cast<float>(bg_color.greenF());
    Settings::values.bg_blue = static_cast<float>(bg_color.blueF());
}
