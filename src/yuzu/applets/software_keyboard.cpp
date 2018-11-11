// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <locale>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include "common/logging/backend.h"
#include "common/string_util.h"
#include "yuzu/applets/software_keyboard.h"

QtSoftwareKeyboardValidator::QtSoftwareKeyboardValidator(
    Frontend::SoftwareKeyboardApplet::Parameters parameters)
    : parameters(std::move(parameters)) {}

QValidator::State QtSoftwareKeyboardValidator::validate(QString& input, int&) const {
    if (input.size() > parameters.max_length)
        return Invalid;
    if (parameters.disable_space && input.contains(' '))
        return Invalid;
    if (parameters.disable_address && input.contains('@'))
        return Invalid;
    if (parameters.disable_percent && input.contains('%'))
        return Invalid;
    if (parameters.disable_slash && (input.contains('/') || input.contains('\\')))
        return Invalid;
    if (parameters.disable_number &&
        std::any_of(input.begin(), input.end(), [](QChar c) { return c.isDigit(); }))
        return Invalid;

    if (parameters.disable_download_code &&
        std::any_of(input.begin(), input.end(), [](QChar c) { return c == 'O' || c == 'I'; }))
        return Invalid;

    return Acceptable;
}

QtSoftwareKeyboardDialog::QtSoftwareKeyboardDialog(
    QWidget* parent, Frontend::SoftwareKeyboardApplet::Parameters parameters_)
    : QDialog(parent), parameters(std::move(parameters_)) {
    layout = new QVBoxLayout;

    header_label = new QLabel(QString::fromStdU16String(parameters.header_text));
    header_label->setFont({header_label->font().family(), 12, QFont::Bold});
    if (header_label->text().isEmpty())
        header_label->setText(tr("Enter text:"));

    sub_label = new QLabel(QString::fromStdU16String(parameters.sub_text));
    sub_label->setFont({sub_label->font().family(), sub_label->font().pointSize(),
                        sub_label->font().weight(), true});
    sub_label->setHidden(parameters.sub_text.empty());

    guide_label = new QLabel(QString::fromStdU16String(parameters.guide_text));
    guide_label->setHidden(parameters.guide_text.empty());

    line_edit = new QLineEdit;
    line_edit->setValidator(new QtSoftwareKeyboardValidator(parameters));
    line_edit->setMaxLength(static_cast<int>(parameters.max_length));
    line_edit->setText(QString::fromStdU16String(parameters.initial_text));
    line_edit->setCursorPosition(
        parameters.cursor_at_beginning ? 0 : static_cast<int>(parameters.initial_text.size()));
    line_edit->setEchoMode(parameters.password ? QLineEdit::Password : QLineEdit::Normal);

    buttons = new QDialogButtonBox;
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    buttons->addButton(parameters.submit_text.empty()
                           ? tr("OK")
                           : QString::fromStdU16String(parameters.submit_text),
                       QDialogButtonBox::AcceptRole);

    connect(buttons, &QDialogButtonBox::accepted, this, &QtSoftwareKeyboardDialog::Submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QtSoftwareKeyboardDialog::Reject);
    layout->addWidget(header_label);
    layout->addWidget(sub_label);
    layout->addWidget(guide_label);
    layout->addWidget(line_edit);
    layout->addWidget(buttons);
    setLayout(layout);
    setWindowTitle("Software Keyboard");
}

void QtSoftwareKeyboardDialog::Submit() {
    ok = true;
    text = line_edit->text().toStdU16String();
    accept();
}

void QtSoftwareKeyboardDialog::Reject() {
    ok = false;
    text = Common::UTF8ToUTF16("");
    accept();
}

QtSoftwareKeyboard::QtSoftwareKeyboard(QWidget& parent) : parent(parent) {}

bool QtSoftwareKeyboard::GetText(Parameters parameters, std::u16string& text) {
    QtSoftwareKeyboardDialog dialog(&parent, parameters);
    dialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.exec();

    text = dialog.text;
    return dialog.ok;
}
