// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include <QStandardItemModel>
#include "core/settings.h"
#include "ui_configure_hotkeys.h"
#include "yuzu/configuration/configure_hotkeys.h"
#include "yuzu/hotkeys.h"
#include "yuzu/util/sequence_dialog/sequence_dialog.h"

ConfigureHotkeys::ConfigureHotkeys(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureHotkeys>()) {
    ui->setupUi(this);
    setFocusPolicy(Qt::ClickFocus);

    model = new QStandardItemModel(this);
    model->setColumnCount(3);
    model->setHorizontalHeaderLabels({tr("Action"), tr("Hotkey"), tr("Context")});

    connect(ui->hotkey_list, &QTreeView::doubleClicked, this, &ConfigureHotkeys::Configure);
    ui->hotkey_list->setModel(model);

    // TODO(Kloen): Make context configurable as well (hiding the column for now)
    ui->hotkey_list->hideColumn(2);

    ui->hotkey_list->setColumnWidth(0, 200);
    ui->hotkey_list->resizeColumnToContents(1);
}

ConfigureHotkeys::~ConfigureHotkeys() = default;

void ConfigureHotkeys::EmitHotkeysChanged() {
    emit HotkeysChanged(GetUsedKeyList());
}

QList<QKeySequence> ConfigureHotkeys::GetUsedKeyList() const {
    QList<QKeySequence> list;
    for (int r = 0; r < model->rowCount(); r++) {
        const QStandardItem* parent = model->item(r, 0);
        for (int r2 = 0; r2 < parent->rowCount(); r2++) {
            const QStandardItem* keyseq = parent->child(r2, 1);
            list << QKeySequence::fromString(keyseq->text(), QKeySequence::NativeText);
        }
    }
    return list;
}

void ConfigureHotkeys::Populate(const HotkeyRegistry& registry) {
    for (const auto& group : registry.hotkey_groups) {
        auto* parent_item = new QStandardItem(group.first);
        parent_item->setEditable(false);
        for (const auto& hotkey : group.second) {
            auto* action = new QStandardItem(hotkey.first);
            auto* keyseq =
                new QStandardItem(hotkey.second.keyseq.toString(QKeySequence::NativeText));
            action->setEditable(false);
            keyseq->setEditable(false);
            parent_item->appendRow({action, keyseq});
        }
        model->appendRow(parent_item);
    }

    ui->hotkey_list->expandAll();
}

void ConfigureHotkeys::Configure(QModelIndex index) {
    if (index.parent() == QModelIndex())
        return;

    index = index.sibling(index.row(), 1);
    auto* model = ui->hotkey_list->model();
    auto previous_key = model->data(index);

    auto* hotkey_dialog = new SequenceDialog;
    int return_code = hotkey_dialog->exec();

    auto key_sequence = hotkey_dialog->GetSequence();

    if (return_code == QDialog::Rejected || key_sequence.isEmpty())
        return;

    if (IsUsedKey(key_sequence) && key_sequence != QKeySequence(previous_key.toString())) {
        QMessageBox::critical(this, tr("Error in inputted key"),
                              tr("You're using a key that's already bound."));
    } else {
        model->setData(index, key_sequence.toString(QKeySequence::NativeText));
        EmitHotkeysChanged();
    }
}

bool ConfigureHotkeys::IsUsedKey(QKeySequence key_sequence) {
    return GetUsedKeyList().contains(key_sequence);
}

void ConfigureHotkeys::applyConfiguration(HotkeyRegistry& registry) {
    for (int key_id = 0; key_id < model->rowCount(); key_id++) {
        const QStandardItem* parent = model->item(key_id, 0);
        for (int key_column_id = 0; key_column_id < parent->rowCount(); key_column_id++) {
            const QStandardItem* action = parent->child(key_column_id, 0);
            const QStandardItem* keyseq = parent->child(key_column_id, 1);
            for (auto& [group, sub_actions] : registry.hotkey_groups) {
                if (group != parent->text())
                    continue;
                for (auto& [action_name, hotkey] : sub_actions) {
                    if (action_name != action->text())
                        continue;
                    hotkey.keyseq = QKeySequence(keyseq->text());
                }
            }
        }
    }

    registry.SaveHotkeys();
    Settings::Apply();
}

void ConfigureHotkeys::retranslateUi() {
    ui->retranslateUi(this);
}
