// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include <QWidget>
#include "yuzu/configuration/config.h"

namespace Ui {
class ConfigureTouchscreenAdvanced;
}

class ConfigureTouchscreenAdvanced : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureTouchscreenAdvanced(QWidget* parent);
    ~ConfigureTouchscreenAdvanced() override;

    void applyConfiguration();

private:
    /// Load configuration settings.
    void loadConfiguration();
    /// Restore all buttons to their default values.
    void restoreDefaults();

    std::unique_ptr<Ui::ConfigureTouchscreenAdvanced> ui;
};
