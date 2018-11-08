// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/applet_ae.h"
#include "core/hle/service/am/applet_oe.h"
#include "core/hle/service/sm/sm.h"
#include "core/settings.h"
#include "ui_configure_general.h"
#include "yuzu/configuration/configure_general.h"
#include "yuzu/ui_settings.h"

ConfigureGeneral::ConfigureGeneral(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGeneral) {

    ui->setupUi(this);

    for (const auto& theme : UISettings::themes) {
        ui->theme_combobox->addItem(theme.first, theme.second);
    }

    this->setConfiguration();

    ui->use_cpu_jit->setEnabled(!Core::System::GetInstance().IsPoweredOn());
}

ConfigureGeneral::~ConfigureGeneral() = default;

void ConfigureGeneral::setConfiguration() {
    ui->toggle_deepscan->setChecked(UISettings::values.gamedir_deepscan);
    ui->toggle_check_exit->setChecked(UISettings::values.confirm_before_closing);
    ui->theme_combobox->setCurrentIndex(ui->theme_combobox->findData(UISettings::values.theme));
    ui->use_cpu_jit->setChecked(Settings::values.use_cpu_jit);
    ui->use_docked_mode->setChecked(Settings::values.use_docked_mode);
    ui->enable_nfc->setChecked(Settings::values.enable_nfc);
}

void ConfigureGeneral::PopulateHotkeyList(const HotkeyRegistry& registry) {
    ui->widget->Populate(registry);
}

void ConfigureGeneral::OnDockedModeChanged(bool last_state, bool new_state) {
    if (last_state == new_state) {
        return;
    }

    Core::System& system{Core::System::GetInstance()};
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

void ConfigureGeneral::applyConfiguration() {
    UISettings::values.gamedir_deepscan = ui->toggle_deepscan->isChecked();
    UISettings::values.confirm_before_closing = ui->toggle_check_exit->isChecked();
    UISettings::values.theme =
        ui->theme_combobox->itemData(ui->theme_combobox->currentIndex()).toString();

    Settings::values.use_cpu_jit = ui->use_cpu_jit->isChecked();
    const bool pre_docked_mode = Settings::values.use_docked_mode;
    Settings::values.use_docked_mode = ui->use_docked_mode->isChecked();
    OnDockedModeChanged(pre_docked_mode, Settings::values.use_docked_mode);

    Settings::values.enable_nfc = ui->enable_nfc->isChecked();
}
