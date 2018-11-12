// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QDialog>
#include <QValidator>
#include "common/assert.h"
#include "core/frontend/applets/software_keyboard.h"

class GMainWindow;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class QtSoftwareKeyboard;

class QtSoftwareKeyboardValidator final : public QValidator {
public:
    explicit QtSoftwareKeyboardValidator(Core::Frontend::SoftwareKeyboardParameters parameters);
    State validate(QString& input, int& pos) const override;

private:
    Core::Frontend::SoftwareKeyboardParameters parameters;
};

class QtSoftwareKeyboardDialog final : public QDialog {
    Q_OBJECT

public:
    QtSoftwareKeyboardDialog(QWidget* parent,
                             Core::Frontend::SoftwareKeyboardParameters parameters);
    ~QtSoftwareKeyboardDialog() override;

    void Submit();
    void Reject();

    std::u16string GetText() const;
    bool GetStatus() const;

private:
    bool ok = false;
    std::u16string text;

    QDialogButtonBox* buttons;
    QLabel* header_label;
    QLabel* sub_label;
    QLabel* guide_label;
    QLineEdit* line_edit;
    QVBoxLayout* layout;

    Core::Frontend::SoftwareKeyboardParameters parameters;
};

class QtSoftwareKeyboard final : public QObject, public Core::Frontend::SoftwareKeyboardApplet {
public:
    explicit QtSoftwareKeyboard(GMainWindow& parent);
    ~QtSoftwareKeyboard() override;

    std::optional<std::u16string> GetText(
        Core::Frontend::SoftwareKeyboardParameters parameters) const override;
    void SendTextCheckDialog(std::u16string error_message) const override;

private:
    GMainWindow& main_window;
};
