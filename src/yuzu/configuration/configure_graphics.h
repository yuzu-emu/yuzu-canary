// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QWidget>

struct PerGameValuesChange;

namespace Ui {
class ConfigureGraphics;
}

class ConfigureGraphics : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureGraphics(QWidget* parent = nullptr);
    ~ConfigureGraphics();

    void applyConfiguration();

    void setPerGame(bool per_game);
    void loadValuesChange(const PerGameValuesChange& change);
    void mergeValuesChange(PerGameValuesChange& change);

private:
    void setConfiguration();

private:
    std::unique_ptr<Ui::ConfigureGraphics> ui;
    QColor bg_color;
};
