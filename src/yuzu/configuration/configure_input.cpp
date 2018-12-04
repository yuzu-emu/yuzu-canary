// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>

#include <QTimer>

#include "configuration/configure_touchscreen_advanced.h"
#include "core/core.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applet_ae.h"
#include "core/hle/service/am/applet_oe.h"
#include "core/hle/service/hid/controllers/npad.h"
#include "core/hle/service/sm/sm.h"
#include "ui_configure_input.h"
#include "ui_configure_input_player.h"
#include "yuzu/configuration/configure_input.h"
#include "yuzu/configuration/configure_input_player.h"
#include "yuzu/configuration/configure_mouse_advanced.h"

namespace {
template <typename Dialog, typename... Args>
void CallConfigureDialog(ConfigureInput& parent, Args&&... args) {
    parent.applyConfiguration();
    Dialog dialog(&parent, std::forward<Args>(args)...);

    const auto res = dialog.exec();
    if (res == QDialog::Accepted) {
        dialog.applyConfiguration();
    }
}
} // Anonymous namespace

ConfigureInput::ConfigureInput(QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigureInput>()) {
    ui->setupUi(this);

    players_controller = {
        ui->player1_combobox, ui->player2_combobox, ui->player3_combobox, ui->player4_combobox,
        ui->player5_combobox, ui->player6_combobox, ui->player7_combobox, ui->player8_combobox,
    };

    players_configure = {
        ui->player1_configure, ui->player2_configure, ui->player3_configure, ui->player4_configure,
        ui->player5_configure, ui->player6_configure, ui->player7_configure, ui->player8_configure,
    };

    for (auto* controller_box : players_controller) {
        controller_box->addItems({"None", "Pro Controller", "Dual Joycons", "Single Right Joycon",
                                  "Single Left Joycon"});
    }

    this->loadConfiguration();
    updateUIEnabled();

    connect(ui->restore_defaults_button, &QPushButton::pressed, this,
            &ConfigureInput::restoreDefaults);

    for (auto* enabled : players_controller)
        connect(enabled, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                &ConfigureInput::updateUIEnabled);
    connect(ui->use_docked_mode, &QCheckBox::stateChanged, this, &ConfigureInput::updateUIEnabled);
    connect(ui->handheld_connected, &QCheckBox::stateChanged, this,
            &ConfigureInput::updateUIEnabled);
    connect(ui->mouse_enabled, &QCheckBox::stateChanged, this, &ConfigureInput::updateUIEnabled);
    connect(ui->keyboard_enabled, &QCheckBox::stateChanged, this, &ConfigureInput::updateUIEnabled);
    connect(ui->debug_enabled, &QCheckBox::stateChanged, this, &ConfigureInput::updateUIEnabled);
    connect(ui->touchscreen_enabled, &QCheckBox::stateChanged, this,
            &ConfigureInput::updateUIEnabled);

    for (std::size_t i = 0; i < players_configure.size(); ++i) {
        connect(players_configure[i], &QPushButton::pressed, this,
                [this, i] { CallConfigureDialog<ConfigureInputPlayer>(*this, i, false); });
    }

    connect(ui->handheld_configure, &QPushButton::pressed, this,
            [this] { CallConfigureDialog<ConfigureInputPlayer>(*this, 8, false); });

    connect(ui->debug_configure, &QPushButton::pressed, this,
            [this] { CallConfigureDialog<ConfigureInputPlayer>(*this, 9, true); });

    connect(ui->mouse_advanced, &QPushButton::pressed, this,
            [this] { CallConfigureDialog<ConfigureMouseAdvanced>(*this); });

    connect(ui->touchscreen_advanced, &QPushButton::pressed, this,
            [this] { CallConfigureDialog<ConfigureTouchscreenAdvanced>(*this); });
}

ConfigureInput::~ConfigureInput() = default;

void ConfigureInput::OnDockedModeChanged(bool last_state, bool new_state) {
    if (ui->use_docked_mode->isChecked() && ui->handheld_connected->isChecked()) {
        ui->handheld_connected->setChecked(false);
    }

    if (last_state == new_state) {
        return;
    }

    Core::System& system{Core::System::GetInstance()};
    if (!system.IsPoweredOn()) {
        return;
    }
    Service::SM::ServiceManager& sm = system.ServiceManager();

    // Message queue is shared between these services, we just need to signal an operation
    // change to one and it will handle both automatically
    auto applet_oe = sm.GetService<Service::AM::AppletOE>("appletOE");
    auto applet_ae = sm.GetService<Service::AM::AppletAE>("appletAE");
    bool has_signalled = false;

    if (applet_oe != nullptr) {
        applet_oe->GetMessageQueue()->OperationModeChanged();
        has_signalled = true;
    }

    if (applet_ae != nullptr && !has_signalled) {
        applet_ae->GetMessageQueue()->OperationModeChanged();
    }
}

void ConfigureInput::applyConfiguration() {
    for (std::size_t i = 0; i < players_controller.size(); ++i) {
        const auto controller_type_index = players_controller[i]->currentIndex();

        Settings::values.players[i].connected = controller_type_index != 0;

        if (controller_type_index > 0) {
            Settings::values.players[i].type =
                static_cast<Settings::ControllerType>(controller_type_index - 1);
        } else {
            Settings::values.players[i].type = Settings::ControllerType::DualJoycon;
        }
    }

    const bool pre_docked_mode = Settings::values.use_docked_mode;
    Settings::values.use_docked_mode = ui->use_docked_mode->isChecked();
    OnDockedModeChanged(pre_docked_mode, Settings::values.use_docked_mode);
    Settings::values
        .players[Service::HID::Controller_NPad::NPadIdToIndex(Service::HID::NPAD_HANDHELD)]
        .connected = ui->handheld_connected->isChecked();
    Settings::values.debug_pad_enabled = ui->debug_enabled->isChecked();
    Settings::values.mouse_enabled = ui->mouse_enabled->isChecked();
    Settings::values.keyboard_enabled = ui->keyboard_enabled->isChecked();
    Settings::values.touchscreen.enabled = ui->touchscreen_enabled->isChecked();
}

void ConfigureInput::updateUIEnabled() {
    bool hit_disabled = false;
    for (auto* player : players_controller) {
        player->setDisabled(hit_disabled);
        if (hit_disabled)
            player->setCurrentIndex(0);
        if (!hit_disabled && player->currentIndex() == 0)
            hit_disabled = true;
    }

    for (std::size_t i = 0; i < players_controller.size(); ++i) {
        players_configure[i]->setEnabled(players_controller[i]->currentIndex() != 0);
    }

    ui->handheld_connected->setEnabled(!ui->use_docked_mode->isChecked());
    ui->handheld_configure->setEnabled(ui->handheld_connected->isChecked() &&
                                       !ui->use_docked_mode->isChecked());
    ui->mouse_advanced->setEnabled(ui->mouse_enabled->isChecked());
    ui->debug_configure->setEnabled(ui->debug_enabled->isChecked());
    ui->touchscreen_advanced->setEnabled(ui->touchscreen_enabled->isChecked());
}

void ConfigureInput::loadConfiguration() {
    std::stable_partition(
        Settings::values.players.begin(),
        Settings::values.players.begin() +
            Service::HID::Controller_NPad::NPadIdToIndex(Service::HID::NPAD_HANDHELD),
        [](const auto& player) { return player.connected; });

    for (std::size_t i = 0; i < players_controller.size(); ++i) {
        const auto connected = Settings::values.players[i].connected;
        players_controller[i]->setCurrentIndex(
            connected ? static_cast<u8>(Settings::values.players[i].type) + 1 : 0);
    }

    ui->use_docked_mode->setChecked(Settings::values.use_docked_mode);
    ui->handheld_connected->setChecked(
        Settings::values
            .players[Service::HID::Controller_NPad::NPadIdToIndex(Service::HID::NPAD_HANDHELD)]
            .connected);
    ui->debug_enabled->setChecked(Settings::values.debug_pad_enabled);
    ui->mouse_enabled->setChecked(Settings::values.mouse_enabled);
    ui->keyboard_enabled->setChecked(Settings::values.keyboard_enabled);
    ui->touchscreen_enabled->setChecked(Settings::values.touchscreen.enabled);

    updateUIEnabled();
}

void ConfigureInput::restoreDefaults() {
    players_controller[0]->setCurrentIndex(2);

    for (std::size_t i = 1; i < players_controller.size(); ++i) {
        players_controller[i]->setCurrentIndex(0);
    }

    ui->use_docked_mode->setCheckState(Qt::Unchecked);
    ui->handheld_connected->setCheckState(Qt::Unchecked);
    ui->mouse_enabled->setCheckState(Qt::Unchecked);
    ui->keyboard_enabled->setCheckState(Qt::Unchecked);
    ui->debug_enabled->setCheckState(Qt::Unchecked);
    ui->touchscreen_enabled->setCheckState(Qt::Checked);
    updateUIEnabled();
}
