// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QWidget>

struct PerGameValuesChange;

namespace Ui {
class ConfigureAudio;
}

class ConfigureAudio : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureAudio(QWidget* parent = nullptr);
    ~ConfigureAudio();

    void setPerGame(bool per_game);
    void loadValuesChange(const PerGameValuesChange& change);
    void mergeValuesChange(PerGameValuesChange& change);

    void applyConfiguration();
    void retranslateUi();

public slots:
    void updateAudioDevices(int sink_index);

private:
    void setConfiguration();
    void setOutputSinkFromSinkID();
    void setAudioDeviceFromDeviceID();
    void setVolumeIndicatorText(int percentage);

    std::unique_ptr<Ui::ConfigureAudio> ui;
};
