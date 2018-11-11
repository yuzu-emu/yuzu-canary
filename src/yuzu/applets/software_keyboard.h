// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once
#include <QDialog>
#include <QValidator>
#include "common/assert.h"
#include "core/frontend/applets/software_keyboard.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class QtSoftwareKeyboard;

class QtSoftwareKeyboardValidator final : public QValidator {
public:
    explicit QtSoftwareKeyboardValidator(Frontend::SoftwareKeyboardApplet::Parameters parameters);
    State validate(QString&, int&) const override;

private:
    Frontend::SoftwareKeyboardApplet::Parameters parameters;
};

class QtSoftwareKeyboardDialog final : public QDialog {
    Q_OBJECT

public:
    QtSoftwareKeyboardDialog(QWidget* parent,
                             Frontend::SoftwareKeyboardApplet::Parameters parameters);
    void Submit();
    void Reject();

private:
    bool ok = false;
    std::u16string text;

    QDialogButtonBox* buttons;
    QLabel* header_label;
    QLabel* sub_label;
    QLabel* guide_label;
    QLineEdit* line_edit;
    QVBoxLayout* layout;

    Frontend::SoftwareKeyboardApplet::Parameters parameters;

    friend class QtSoftwareKeyboard;
};

class QtSoftwareKeyboard final : public QObject, public Frontend::SoftwareKeyboardApplet {
public:
    explicit QtSoftwareKeyboard(QWidget& parent);
    bool GetText(Parameters parameters, std::u16string& text) override;

    ~QtSoftwareKeyboard() {
        UNREACHABLE();
    }

private:
    QWidget& parent;
};
