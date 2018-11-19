// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <mutex>
#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include "core/hle/lock.h"
#include "yuzu/applets/software_keyboard.h"
#include "yuzu/main.h"

QtSoftwareKeyboardValidator::QtSoftwareKeyboardValidator(
    Core::Frontend::SoftwareKeyboardParameters parameters)
    : parameters(std::move(parameters)) {}

QValidator::State QtSoftwareKeyboardValidator::validate(QString& input, int& pos) const {
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
        std::any_of(input.begin(), input.end(), [](QChar c) { return c.isDigit(); })) {
        return Invalid;
    }

    if (parameters.disable_download_code &&
        std::any_of(input.begin(), input.end(), [](QChar c) { return c == 'O' || c == 'I'; })) {
        return Invalid;
    }

    return Acceptable;
}

QtSoftwareKeyboardDialog::QtSoftwareKeyboardDialog(
    QWidget* parent, Core::Frontend::SoftwareKeyboardParameters parameters_)
    : QDialog(parent), parameters(std::move(parameters_)) {
    layout = new QVBoxLayout;

    header_label = new QLabel(QString::fromStdU16String(parameters.header_text));
    header_label->setFont({header_label->font().family(), 11, QFont::Bold});
    if (header_label->text().isEmpty())
        header_label->setText(tr("Enter text:"));

    sub_label = new QLabel(QString::fromStdU16String(parameters.sub_text));
    sub_label->setFont({sub_label->font().family(), sub_label->font().pointSize(),
                        sub_label->font().weight(), true});
    sub_label->setHidden(parameters.sub_text.empty());

    guide_label = new QLabel(QString::fromStdU16String(parameters.guide_text));
    guide_label->setHidden(parameters.guide_text.empty());

    length_label = new QLabel(QStringLiteral("0/%1").arg(parameters.max_length));
    length_label->setAlignment(Qt::AlignRight);
    length_label->setFont({length_label->font().family(), 8});

    line_edit = new QLineEdit;
    line_edit->setValidator(new QtSoftwareKeyboardValidator(parameters));
    line_edit->setMaxLength(static_cast<int>(parameters.max_length));
    line_edit->setText(QString::fromStdU16String(parameters.initial_text));
    line_edit->setCursorPosition(
        parameters.cursor_at_beginning ? 0 : static_cast<int>(parameters.initial_text.size()));
    line_edit->setEchoMode(parameters.password ? QLineEdit::Password : QLineEdit::Normal);

    connect(line_edit, &QLineEdit::textChanged, this, [this](const QString& text) {
        length_label->setText(QStringLiteral("%1/%2").arg(text.size()).arg(parameters.max_length));
    });

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
    layout->addWidget(length_label);
    layout->addWidget(line_edit);
    layout->addWidget(buttons);
    setLayout(layout);
    setWindowTitle(tr("Software Keyboard"));
}

QtSoftwareKeyboardDialog::~QtSoftwareKeyboardDialog() = default;

void QtSoftwareKeyboardDialog::Submit() {
    ok = true;
    text = line_edit->text().toStdU16String();
    accept();
}

void QtSoftwareKeyboardDialog::Reject() {
    ok = false;
    text.clear();
    accept();
}

std::u16string QtSoftwareKeyboardDialog::GetText() const {
    return text;
}

bool QtSoftwareKeyboardDialog::GetStatus() const {
    return ok;
}

QtSoftwareKeyboard::QtSoftwareKeyboard(GMainWindow& main_window) {
    connect(this, &QtSoftwareKeyboard::MainWindowGetText, &main_window,
            &GMainWindow::SoftwareKeyboardGetText, Qt::QueuedConnection);
    connect(this, &QtSoftwareKeyboard::MainWindowTextCheckDialog, &main_window,
            &GMainWindow::SoftwareKeyboardInvokeCheckDialog, Qt::BlockingQueuedConnection);
    connect(&main_window, &GMainWindow::SoftwareKeyboardFinishedText, this,
            &QtSoftwareKeyboard::MainWindowFinishedText, Qt::QueuedConnection);
}

QtSoftwareKeyboard::~QtSoftwareKeyboard() = default;

void QtSoftwareKeyboard::RequestText(std::function<void(std::optional<std::u16string>)> out,
                                     Core::Frontend::SoftwareKeyboardParameters parameters) const {
    text_output = out;
    emit MainWindowGetText(parameters);
}

void QtSoftwareKeyboard::SendTextCheckDialog(std::u16string error_message,
                                             std::function<void()> finished_check) const {
    this->finished_check = finished_check;
    emit MainWindowTextCheckDialog(error_message);
}

void QtSoftwareKeyboard::MainWindowFinishedText(std::optional<std::u16string> text) {
    // Acquire the HLE mutex
    std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);
    text_output(text);
}

void QtSoftwareKeyboard::MainWindowFinishedCheckDialog() {
    // Acquire the HLE mutex
    std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);
    finished_check();
}
