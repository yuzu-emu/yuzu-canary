// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>

namespace Loader {
class AppLoader;
}

namespace Ui {
class ConfigurePerGameDialog;
}

class ConfigurePerGameDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConfigurePerGameDialog(QWidget* parent, Loader::AppLoader& file,
                                    const PerGameValuesChange& change);
    ~ConfigurePerGameDialog();

    PerGameValuesChange applyConfiguration();

private:
    void setConfiguration();

    std::unique_ptr<Ui::ConfigurePerGameDialog> ui;
};
