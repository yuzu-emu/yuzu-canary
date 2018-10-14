// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <QFileDialog>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>
#include "common/common_paths.h"
#include "common/logging/backend.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_system.h"
#include "yuzu/configuration/configure_system.h"
#include "yuzu/main.h"

static std::string GetImagePath(Service::Account::UUID uuid) {
    return FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
           "/system/save/8000000000000010/su/avators/" + uuid.FormatSwitch() + ".jpg";
}

static const std::array<int, 12> days_in_month = {{
    31,
    29,
    31,
    30,
    31,
    30,
    31,
    31,
    30,
    31,
    30,
    31,
}};

// Same backup JPEG used by acc IProfile::GetImage if no jpeg found
static constexpr std::array<u8, 107> backup_jpeg{
    0xff, 0xd8, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x02,
    0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x06, 0x04, 0x04, 0x04, 0x04, 0x04, 0x08, 0x06, 0x06, 0x05,
    0x06, 0x09, 0x08, 0x0a, 0x0a, 0x09, 0x08, 0x09, 0x09, 0x0a, 0x0c, 0x0f, 0x0c, 0x0a, 0x0b, 0x0e,
    0x0b, 0x09, 0x09, 0x0d, 0x11, 0x0d, 0x0e, 0x0f, 0x10, 0x10, 0x11, 0x10, 0x0a, 0x0c, 0x12, 0x13,
    0x12, 0x10, 0x13, 0x0f, 0x10, 0x10, 0x10, 0xff, 0xc9, 0x00, 0x0b, 0x08, 0x00, 0x01, 0x00, 0x01,
    0x01, 0x01, 0x11, 0x00, 0xff, 0xcc, 0x00, 0x06, 0x00, 0x10, 0x10, 0x05, 0xff, 0xda, 0x00, 0x08,
    0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0xd2, 0xcf, 0x20, 0xff, 0xd9,
};

ConfigureSystem::ConfigureSystem(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureSystem),
      profile_manager(std::make_unique<Service::Account::ProfileManager>()) {
    ui->setupUi(this);
    connect(ui->combo_birthmonth,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &ConfigureSystem::updateBirthdayComboBox);
    connect(ui->button_regenerate_console_id, &QPushButton::clicked, this,
            &ConfigureSystem::refreshConsoleID);

    layout = new QVBoxLayout;
    tree_view = new QTreeView;
    item_model = new QStandardItemModel(tree_view);
    tree_view->setModel(item_model);

    tree_view->setAlternatingRowColors(true);
    tree_view->setSelectionMode(QHeaderView::SingleSelection);
    tree_view->setSelectionBehavior(QHeaderView::SelectRows);
    tree_view->setVerticalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setHorizontalScrollMode(QHeaderView::ScrollPerPixel);
    tree_view->setSortingEnabled(true);
    tree_view->setEditTriggers(QHeaderView::NoEditTriggers);
    tree_view->setUniformRowHeights(true);
    tree_view->setIconSize({64, 64});
    tree_view->setContextMenuPolicy(Qt::NoContextMenu);

    item_model->insertColumns(0, 1);
    item_model->setHeaderData(0, Qt::Horizontal, "Users");

    // We must register all custom types with the Qt Automoc system so that we are able to use it
    // with signals/slots. In this case, QList falls under the umbrells of custom types.
    qRegisterMetaType<QList<QStandardItem*>>("QList<QStandardItem*>");

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(tree_view);

    ui->scrollArea->setLayout(layout);

    connect(tree_view, &QTreeView::clicked, this, &ConfigureSystem::SelectUser);

    connect(ui->pm_add, &QPushButton::pressed, this, &ConfigureSystem::AddUser);
    connect(ui->pm_rename, &QPushButton::pressed, this, &ConfigureSystem::RenameUser);
    connect(ui->pm_remove, &QPushButton::pressed, this, &ConfigureSystem::DeleteUser);
    connect(ui->pm_set_image, &QPushButton::pressed, this, &ConfigureSystem::SetUserImage);

    scene = new QGraphicsScene;
    ui->current_user_icon->setScene(scene);

    this->setConfiguration();
}

ConfigureSystem::~ConfigureSystem() = default;

void ConfigureSystem::setConfiguration() {
    enabled = !Core::System::GetInstance().IsPoweredOn();

    ui->combo_language->setCurrentIndex(Settings::values.language_index);

    item_model->removeRows(0, item_model->rowCount());
    list_items.clear();

    PopulateUserList();
    UpdateCurrentUser();
}

static QPixmap GetIcon(Service::Account::UUID uuid) {
    const auto icon_url = QString::fromStdString(GetImagePath(uuid));
    QPixmap icon{icon_url};

    if (!icon) {
        icon.fill(Qt::black);
        icon.loadFromData(backup_jpeg.data(), backup_jpeg.size());
    }

    return icon;
}

void ConfigureSystem::PopulateUserList() {
    const auto& profiles = profile_manager->GetAllUsers();
    for (const auto& user : profiles) {
        Service::Account::ProfileBase profile;
        if (!profile_manager->GetProfileBase(user, profile))
            continue;

        const auto username = Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(profile.username.data()), profile.username.size());

        list_items.push_back(QList<QStandardItem*>{new QStandardItem{
            GetIcon(user).scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
            QString::fromStdString(username + '\n' + user.FormatSwitch())}});
    }

    for (const auto& item : list_items)
        item_model->appendRow(item);
}

void ConfigureSystem::UpdateCurrentUser() {
    ui->pm_add->setEnabled(profile_manager->GetUserCount() < Service::Account::MAX_USERS);

    const auto& current_user = profile_manager->GetUser(Settings::values.current_user);
    ASSERT(current_user != boost::none);
    const auto username = GetAccountUsername(*current_user);

    scene->clear();
    scene->addPixmap(
        GetIcon(*current_user).scaled(48, 48, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    ui->current_user_username->setText(QString::fromStdString(username));
}

void ConfigureSystem::ReadSystemSettings() {}

std::string ConfigureSystem::GetAccountUsername(Service::Account::UUID uuid) {
    Service::Account::ProfileBase profile;
    if (!profile_manager->GetProfileBase(uuid, profile))
        return "";
    return Common::StringFromFixedZeroTerminatedBuffer(
        reinterpret_cast<const char*>(profile.username.data()), profile.username.size());
}

void ConfigureSystem::applyConfiguration() {
    if (!enabled)
        return;

    Settings::values.language_index = ui->combo_language->currentIndex();
    Settings::Apply();
}

void ConfigureSystem::updateBirthdayComboBox(int birthmonth_index) {
    if (birthmonth_index < 0 || birthmonth_index >= 12)
        return;

    // store current day selection
    int birthday_index = ui->combo_birthday->currentIndex();

    // get number of days in the new selected month
    int days = days_in_month[birthmonth_index];

    // if the selected day is out of range,
    // reset it to 1st
    if (birthday_index < 0 || birthday_index >= days)
        birthday_index = 0;

    // update the day combo box
    ui->combo_birthday->clear();
    for (int i = 1; i <= days; ++i) {
        ui->combo_birthday->addItem(QString::number(i));
    }

    // restore the day selection
    ui->combo_birthday->setCurrentIndex(birthday_index);
}

void ConfigureSystem::refreshConsoleID() {
    QMessageBox::StandardButton reply;
    QString warning_text = tr("This will replace your current virtual Switch with a new one. "
                              "Your current virtual Switch will not be recoverable. "
                              "This might have unexpected effects in games. This might fail, "
                              "if you use an outdated config savegame. Continue?");
    reply = QMessageBox::critical(this, tr("Warning"), warning_text,
                                  QMessageBox::No | QMessageBox::Yes);
    if (reply == QMessageBox::No)
        return;
    u64 console_id{};
    ui->label_console_id->setText(
        tr("Console ID: 0x%1").arg(QString::number(console_id, 16).toUpper()));
}

void ConfigureSystem::SelectUser(const QModelIndex& index) {
    Settings::values.current_user =
        std::clamp<std::size_t>(index.row(), 0, profile_manager->GetUserCount() - 1);

    UpdateCurrentUser();

    ui->pm_remove->setEnabled(profile_manager->GetUserCount() >= 2);
    ui->pm_rename->setEnabled(true);
    ui->pm_set_image->setEnabled(true);
}

void ConfigureSystem::AddUser() {
    Service::Account::UUID uuid;
    uuid.Generate();

    bool ok = false;
    const auto username =
        QInputDialog::getText(this, tr("Enter Username"), tr("Enter a username for the new user:"),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;

    profile_manager->CreateNewUser(uuid, username.toStdString());

    item_model->appendRow(new QStandardItem{
        GetIcon(uuid).scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
        QString::fromStdString(username.toStdString() + '\n' + uuid.FormatSwitch())});
}

void ConfigureSystem::RenameUser() {
    const auto user = tree_view->currentIndex().row();
    const auto uuid = profile_manager->GetUser(user);
    ASSERT(uuid != boost::none);
    const auto username = GetAccountUsername(*uuid);

    Service::Account::ProfileBase profile;
    if (!profile_manager->GetProfileBase(*uuid, profile))
        return;

    bool ok = false;
    const auto new_username =
        QInputDialog::getText(this, tr("Enter Username"), tr("Enter a new username:"),
                              QLineEdit::Normal, QString::fromStdString(username), &ok);

    if (!ok)
        return;

    const auto username_std = new_username.toStdString();
    if (username_std.size() > profile.username.size()) {
        std::copy_n(username_std.begin(), std::min(profile.username.size(), username_std.size()),
                    profile.username.begin());
    } else {
        std::copy(username_std.begin(), username_std.end(), profile.username.begin());
    }

    profile_manager->SetProfileBase(*uuid, profile);

    item_model->setItem(
        user, 0,
        new QStandardItem{
            GetIcon(*uuid).scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
            QString::fromStdString(username_std + '\n' + uuid->FormatSwitch())});
    UpdateCurrentUser();
}

void ConfigureSystem::DeleteUser() {
    const auto index = tree_view->currentIndex().row();
    const auto uuid = profile_manager->GetUser(index);
    ASSERT(uuid != boost::none);
    const auto username = GetAccountUsername(*uuid);

    const auto confirm = QMessageBox::question(
        this, tr("Confirm Delete"),
        tr("You are about to delete user with name %1. Are you sure?").arg(username.c_str()));

    if (confirm == QMessageBox::No)
        return;

    if (Settings::values.current_user == tree_view->currentIndex().row())
        Settings::values.current_user = 0;
    UpdateCurrentUser();

    if (!profile_manager->RemoveUser(*uuid))
        return;

    item_model->removeRows(tree_view->currentIndex().row(), 1);
    tree_view->clearSelection();

    ui->pm_remove->setEnabled(false);
    ui->pm_rename->setEnabled(false);
}

void ConfigureSystem::SetUserImage() {
    const auto index = tree_view->currentIndex().row();
    const auto uuid = profile_manager->GetUser(index);
    ASSERT(uuid != boost::none);
    const auto username = GetAccountUsername(*uuid);

    const auto file = QFileDialog::getOpenFileName(this, tr("Select User Image"), QString(),
                                                   "JPEG Images (*.jpg *.jpeg)");

    if (file.isEmpty())
        return;

    FileUtil::Delete(GetImagePath(*uuid));

    const auto raw_path =
        FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) + "/system/save/8000000000000010";
    if (FileUtil::Exists(raw_path) && !FileUtil::IsDirectory(raw_path))
        FileUtil::Delete(raw_path);

    FileUtil::CreateFullPath(GetImagePath(*uuid));
    FileUtil::Copy(file.toStdString(), GetImagePath(*uuid));

    item_model->setItem(
        index, 0,
        new QStandardItem{
            GetIcon(*uuid).scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation),
            QString::fromStdString(username + '\n' + uuid->FormatSwitch())});
    UpdateCurrentUser();
}
