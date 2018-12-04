// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>

#include <QWidget>

class QPushButton;
class QString;
class QTimer;

namespace Ui {
class ConfigureInputSimple;
}

// Used by configuration loader to apply a profile if the input is invalid.
void ApplyInputProfileConfiguration(int profile_index);

class ConfigureInputSimple : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureInputSimple(QWidget* parent = nullptr);
    ~ConfigureInputSimple() override;

    /// Save all button configurations to settings file
    void applyConfiguration();

private:
    /// Load configuration settings.
    void loadConfiguration();

    void OnSelectProfile(int index);
    void OnConfigure();

    std::unique_ptr<Ui::ConfigureInputSimple> ui;
};
