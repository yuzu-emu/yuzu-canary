// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_graphics.h"
#include "yuzu/configuration/configure_graphics.h"

ConfigureGraphics::ConfigureGraphics(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGraphics) {

    ui->setupUi(this);
    this->setConfiguration();
}

ConfigureGraphics::~ConfigureGraphics() {}

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

void ConfigureGraphics::setConfiguration() {
    ui->resolution_factor_combobox->setCurrentIndex(
        static_cast<int>(FromResolutionFactor(Settings::values.resolution_factor)));
    ui->toggle_framelimit->setChecked(Settings::values.toggle_framelimit);
    ui->use_accurate_framebuffers->setChecked(Settings::values.use_accurate_framebuffers);
}

void ConfigureGraphics::applyConfiguration() {
    Settings::values.resolution_factor =
        ToResolutionFactor(static_cast<Resolution>(ui->resolution_factor_combobox->currentIndex()));
    Settings::values.toggle_framelimit = ui->toggle_framelimit->isChecked();
    Settings::values.use_accurate_framebuffers = ui->use_accurate_framebuffers->isChecked();
    Settings::Apply();
}
