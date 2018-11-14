// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QWidget>

namespace Ui {
class ConfigureGameList;
}

class ConfigureGameList : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureGameList(QWidget* parent = nullptr);
    ~ConfigureGameList();

    void applyConfiguration();

private:
    void RequestGameListUpdate();

    void setConfiguration();

    void changeEvent(QEvent*) override;
    void RetranslateUI();

    void InitializeIconSizeComboBox();
    void InitializeRowComboBoxes();

    std::unique_ptr<Ui::ConfigureGameList> ui;
};
