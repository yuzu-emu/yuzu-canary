// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>

#include "audio_core/sink.h"
#include "audio_core/sink_details.h"
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_audio.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_audio.h"

ConfigureAudio::ConfigureAudio(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureAudio>()) {
    ui->setupUi(this);

    ui->output_sink_combo_box->clear();
    ui->output_sink_combo_box->addItem("auto");
    for (const auto& sink_detail : AudioCore::g_sink_details) {
        ui->output_sink_combo_box->addItem(sink_detail.id);
    }

    connect(ui->volume_slider, &QSlider::valueChanged, this,
            &ConfigureAudio::setVolumeIndicatorText);
    connect(ui->volume_slider, &QSlider::valueChanged, this, [this]() {
        if (!ui->volume_checkbox->isHidden())
            ui->volume_checkbox->setChecked(true);
    });

    this->setConfiguration();
    connect(ui->output_sink_combo_box,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &ConfigureAudio::updateAudioDevices);

    ui->output_sink_combo_box->setEnabled(!Core::System::GetInstance().IsPoweredOn());
    ui->audio_device_combo_box->setEnabled(!Core::System::GetInstance().IsPoweredOn());
}

ConfigureAudio::~ConfigureAudio() = default;

void ConfigureAudio::setPerGame(bool per_game) {
    ui->toggle_audio_stretching->setTristate(per_game);
    ui->output_sink_checkbox->setHidden(!per_game);
    ui->audio_device_checkbox->setHidden(!per_game);
    ui->volume_checkbox->setHidden(!per_game);
}

void ConfigureAudio::loadValuesChange(const PerGameValuesChange& change) {
    ui->toggle_audio_stretching->setCheckState(
        change.enable_audio_stretching
            ? (Settings::values->enable_audio_stretching ? Qt::Checked : Qt::Unchecked)
            : Qt::PartiallyChecked);
    ui->output_sink_checkbox->setChecked(change.sink_id);
    ui->audio_device_checkbox->setChecked(change.audio_device_id);
    ui->volume_checkbox->setChecked(change.volume);
}

void ConfigureAudio::mergeValuesChange(PerGameValuesChange& change) {
    change.enable_audio_stretching =
        ui->toggle_audio_stretching->checkState() != Qt::PartiallyChecked;
    change.sink_id = ui->output_sink_checkbox->isChecked();
    change.audio_device_id = ui->audio_device_checkbox->isChecked();
    change.volume = ui->volume_checkbox->isChecked();
}

void ConfigureAudio::setConfiguration() {
    setOutputSinkFromSinkID();

    // The device list cannot be pre-populated (nor listed) until the output sink is known.
    updateAudioDevices(ui->output_sink_combo_box->currentIndex());

    setAudioDeviceFromDeviceID();

    ui->toggle_audio_stretching->setChecked(Settings::values->enable_audio_stretching);
    ui->volume_slider->setValue(Settings::values->volume * ui->volume_slider->maximum());
    setVolumeIndicatorText(ui->volume_slider->sliderPosition());
}

void ConfigureAudio::setOutputSinkFromSinkID() {
    int new_sink_index = 0;

    const QString sink_id = QString::fromStdString(Settings::values->sink_id);
    for (int index = 0; index < ui->output_sink_combo_box->count(); index++) {
        if (ui->output_sink_combo_box->itemText(index) == sink_id) {
            new_sink_index = index;
            break;
        }
    }

    ui->output_sink_combo_box->setCurrentIndex(new_sink_index);
}

void ConfigureAudio::setAudioDeviceFromDeviceID() {
    int new_device_index = -1;

    const QString device_id = QString::fromStdString(Settings::values->audio_device_id);
    for (int index = 0; index < ui->audio_device_combo_box->count(); index++) {
        if (ui->audio_device_combo_box->itemText(index) == device_id) {
            new_device_index = index;
            break;
        }
    }

    ui->audio_device_combo_box->setCurrentIndex(new_device_index);
}

void ConfigureAudio::setVolumeIndicatorText(int percentage) {
    ui->volume_indicator->setText(tr("%1%", "Volume percentage (e.g. 50%)").arg(percentage));
}

void ConfigureAudio::applyConfiguration() {
    Settings::values->sink_id =
        ui->output_sink_combo_box->itemText(ui->output_sink_combo_box->currentIndex())
            .toStdString();
    Settings::values->enable_audio_stretching =
        ui->toggle_audio_stretching->checkState() == Qt::PartiallyChecked
            ? Settings::values.default_game.enable_audio_stretching
            : ui->toggle_audio_stretching->isChecked();
    Settings::values->audio_device_id =
        ui->audio_device_combo_box->itemText(ui->audio_device_combo_box->currentIndex())
            .toStdString();
    Settings::values->volume =
        static_cast<float>(ui->volume_slider->sliderPosition()) / ui->volume_slider->maximum();
}

void ConfigureAudio::updateAudioDevices(int sink_index) {
    ui->audio_device_combo_box->clear();
    ui->audio_device_combo_box->addItem(AudioCore::auto_device_name);

    const std::string sink_id = ui->output_sink_combo_box->itemText(sink_index).toStdString();
    const std::vector<std::string> device_list = AudioCore::GetSinkDetails(sink_id).list_devices();
    for (const auto& device : device_list) {
        ui->audio_device_combo_box->addItem(QString::fromStdString(device));
    }
}

void ConfigureAudio::retranslateUi() {
    ui->retranslateUi(this);
}
