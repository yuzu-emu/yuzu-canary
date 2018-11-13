// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <utility>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include "common/assert.h"
#include "common/param_package.h"
#include "input_common/main.h"
#include "ui_configure_mouse_advanced.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_mouse_advanced.h"

static QString GetKeyName(int key_code) {
    switch (key_code) {
    case Qt::Key_Shift:
        return QObject::tr("Shift");
    case Qt::Key_Control:
        return QObject::tr("Ctrl");
    case Qt::Key_Alt:
        return QObject::tr("Alt");
    case Qt::Key_Meta:
        return "";
    default:
        return QKeySequence(key_code).toString();
    }
}

static QString ButtonToText(const Common::ParamPackage& param) {
    if (!param.Has("engine")) {
        return QObject::tr("[not set]");
    } else if (param.Get("engine", "") == "keyboard") {
        return GetKeyName(param.Get("code", 0));
    } else if (param.Get("engine", "") == "sdl") {
        if (param.Has("hat")) {
            return QString(QObject::tr("Hat %1 %2"))
                .arg(param.Get("hat", "").c_str(), param.Get("direction", "").c_str());
        }
        if (param.Has("axis")) {
            return QString(QObject::tr("Axis %1%2"))
                .arg(param.Get("axis", "").c_str(), param.Get("direction", "").c_str());
        }
        if (param.Has("button")) {
            return QString(QObject::tr("Button %1")).arg(param.Get("button", "").c_str());
        }
        return QString();
    } else {
        return QObject::tr("[unknown]");
    }
}

ConfigureMouseAdvanced::ConfigureMouseAdvanced(QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigureMouseAdvanced>()),
      timeout_timer(std::make_unique<QTimer>()), poll_timer(std::make_unique<QTimer>()) {
    ui->setupUi(this);
    setFocusPolicy(Qt::ClickFocus);

    button_map = {
        ui->left_button, ui->right_button, ui->middle_button, ui->forward_button, ui->back_button,
    };

    for (int button_id = 0; button_id < Settings::NativeMouseButton::NumMouseButtons; button_id++) {
        if (!button_map[button_id])
            continue;
        button_map[button_id]->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button_map[button_id], &QPushButton::released, [=]() {
            handleClick(
                button_map[button_id],
                [=](const Common::ParamPackage& params) { buttons_param[button_id] = params; },
                InputCommon::Polling::DeviceType::Button);
        });
        connect(button_map[button_id], &QPushButton::customContextMenuRequested,
                [=](const QPoint& menu_location) {
                    QMenu context_menu;
                    context_menu.addAction(tr("Clear"), [&] {
                        buttons_param[button_id].Clear();
                        button_map[button_id]->setText(tr("[not set]"));
                    });
                    context_menu.addAction(tr("Restore Default"), [&] {
                        buttons_param[button_id] =
                            Common::ParamPackage{InputCommon::GenerateKeyboardParam(
                                Config::default_mouse_buttons[button_id])};
                        button_map[button_id]->setText(ButtonToText(buttons_param[button_id]));
                    });
                    context_menu.exec(button_map[button_id]->mapToGlobal(menu_location));
                });
    }

    connect(ui->buttonClearAll, &QPushButton::released, [this] { ClearAll(); });
    connect(ui->buttonRestoreDefaults, &QPushButton::released, [this]() { restoreDefaults(); });

    timeout_timer->setSingleShot(true);
    connect(timeout_timer.get(), &QTimer::timeout, [this]() { setPollingResult({}, true); });

    connect(poll_timer.get(), &QTimer::timeout, [this]() {
        Common::ParamPackage params;
        for (auto& poller : device_pollers) {
            params = poller->GetNextInput();
            if (params.Has("engine")) {
                setPollingResult(params, false);
                return;
            }
        }
    });

    loadConfiguration();
    resize(0, 0);
}

ConfigureMouseAdvanced::~ConfigureMouseAdvanced() = default;

void ConfigureMouseAdvanced::applyConfiguration() {
    std::transform(buttons_param.begin(), buttons_param.end(),
                   Settings::values.mouse_buttons.begin(),
                   [](const Common::ParamPackage& param) { return param.Serialize(); });
}

void ConfigureMouseAdvanced::loadConfiguration() {
    std::transform(Settings::values.mouse_buttons.begin(), Settings::values.mouse_buttons.end(),
                   buttons_param.begin(),
                   [](const std::string& str) { return Common::ParamPackage(str); });
    updateButtonLabels();
}

void ConfigureMouseAdvanced::restoreDefaults() {
    for (int button_id = 0; button_id < Settings::NativeMouseButton::NumMouseButtons; button_id++) {
        buttons_param[button_id] = Common::ParamPackage{
            InputCommon::GenerateKeyboardParam(Config::default_mouse_buttons[button_id])};
    }

    updateButtonLabels();
}

void ConfigureMouseAdvanced::ClearAll() {
    for (int i = 0; i < Settings::NativeMouseButton::NumMouseButtons; ++i) {
        if (button_map[i] && button_map[i]->isEnabled())
            buttons_param[i].Clear();
    }

    updateButtonLabels();
}

void ConfigureMouseAdvanced::updateButtonLabels() {
    for (int button = 0; button < Settings::NativeMouseButton::NumMouseButtons; button++) {
        button_map[button]->setText(ButtonToText(buttons_param[button]));
    }
}

void ConfigureMouseAdvanced::handleClick(
    QPushButton* button, std::function<void(const Common::ParamPackage&)> new_input_setter,
    InputCommon::Polling::DeviceType type) {
    button->setText(tr("[press key]"));
    button->setFocus();

    const auto iter = std::find(button_map.begin(), button_map.end(), button);
    ASSERT(iter != button_map.end());
    const auto index = std::distance(button_map.begin(), iter);
    ASSERT(index < Settings::NativeButton::NumButtons && index >= 0);

    input_setter = new_input_setter;

    device_pollers = InputCommon::Polling::GetPollers(type);

    // Keyboard keys can only be used as button devices
    want_keyboard_keys = type == InputCommon::Polling::DeviceType::Button;

    for (auto& poller : device_pollers) {
        poller->Start();
    }

    grabKeyboard();
    grabMouse();
    timeout_timer->start(5000); // Cancel after 5 seconds
    poll_timer->start(200);     // Check for new inputs every 200ms
}

void ConfigureMouseAdvanced::setPollingResult(const Common::ParamPackage& params, bool abort) {
    releaseKeyboard();
    releaseMouse();
    timeout_timer->stop();
    poll_timer->stop();
    for (auto& poller : device_pollers) {
        poller->Stop();
    }

    if (!abort) {
        (*input_setter)(params);
    }

    updateButtonLabels();
    input_setter = std::nullopt;
}

void ConfigureMouseAdvanced::keyPressEvent(QKeyEvent* event) {
    if (!input_setter || !event)
        return;

    if (event->key() != Qt::Key_Escape) {
        if (want_keyboard_keys) {
            setPollingResult(Common::ParamPackage{InputCommon::GenerateKeyboardParam(event->key())},
                             false);
        } else {
            // Escape key wasn't pressed and we don't want any keyboard keys, so don't stop polling
            return;
        }
    }
    setPollingResult({}, true);
}
