// Copyright 2018 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <mutex>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include "common/file_util.h"
#include "common/string_util.h"
#include "core/hle/lock.h"
#include "yuzu/applets/profile_select.h"
#include "yuzu/main.h"

// Same backup JPEG used by acc IProfile::GetImage if no jpeg found
constexpr std::array<u8, 107> backup_jpeg{
    0xff, 0xd8, 0xff, 0xdb, 0x00, 0x43, 0x00, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x02,
    0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x06, 0x04, 0x04, 0x04, 0x04, 0x04, 0x08, 0x06, 0x06, 0x05,
    0x06, 0x09, 0x08, 0x0a, 0x0a, 0x09, 0x08, 0x09, 0x09, 0x0a, 0x0c, 0x0f, 0x0c, 0x0a, 0x0b, 0x0e,
    0x0b, 0x09, 0x09, 0x0d, 0x11, 0x0d, 0x0e, 0x0f, 0x10, 0x10, 0x11, 0x10, 0x0a, 0x0c, 0x12, 0x13,
    0x12, 0x10, 0x13, 0x0f, 0x10, 0x10, 0x10, 0xff, 0xc9, 0x00, 0x0b, 0x08, 0x00, 0x01, 0x00, 0x01,
    0x01, 0x01, 0x11, 0x00, 0xff, 0xcc, 0x00, 0x06, 0x00, 0x10, 0x10, 0x05, 0xff, 0xda, 0x00, 0x08,
    0x01, 0x01, 0x00, 0x00, 0x3f, 0x00, 0xd2, 0xcf, 0x20, 0xff, 0xd9,
};

QString FormatUserEntryText(const QString& username, Service::Account::UUID uuid) {
    return QtProfileSelectionDialog::tr(
               "%1\n%2", "%1 is the profile username, %2 is the formatted UUID (e.g. "
                         "00112233-4455-6677-8899-AABBCCDDEEFF))")
        .arg(username, QString::fromStdString(uuid.FormatSwitch()));
}

QString GetImagePath(Service::Account::UUID uuid) {
    const auto path = FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                      "/system/save/8000000000000010/su/avators/" + uuid.FormatSwitch() + ".jpg";
    return QString::fromStdString(path);
}

QPixmap GetIcon(Service::Account::UUID uuid) {
    QPixmap icon{GetImagePath(uuid)};

    if (!icon) {
        icon.fill(Qt::black);
        icon.loadFromData(backup_jpeg.data(), static_cast<u32>(backup_jpeg.size()));
    }

    return icon.scaled(64, 64, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QtProfileSelectionDialog::QtProfileSelectionDialog(QWidget* parent)
    : QDialog(parent), profile_manager(std::make_unique<Service::Account::ProfileManager>()) {
    outer_layout = new QVBoxLayout;

    instruction_label = new QLabel(tr("Select a user:"));

    scroll_area = new QScrollArea;

    buttons = new QDialogButtonBox;
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole);

    connect(buttons, &QDialogButtonBox::accepted, this, &QtProfileSelectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QtProfileSelectionDialog::reject);

    outer_layout->addWidget(instruction_label);
    outer_layout->addWidget(scroll_area);
    outer_layout->addWidget(buttons);

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

    scroll_area->setLayout(layout);

    connect(tree_view, &QTreeView::clicked, this, &QtProfileSelectionDialog::SelectUser);

    const auto& profiles = profile_manager->GetAllUsers();
    for (const auto& user : profiles) {
        Service::Account::ProfileBase profile;
        if (!profile_manager->GetProfileBase(user, profile))
            continue;

        const auto username = Common::StringFromFixedZeroTerminatedBuffer(
            reinterpret_cast<const char*>(profile.username.data()), profile.username.size());

        list_items.push_back(QList<QStandardItem*>{new QStandardItem{
            GetIcon(user), FormatUserEntryText(QString::fromStdString(username), user)}});
    }

    for (const auto& item : list_items)
        item_model->appendRow(item);

    setLayout(outer_layout);
    setWindowTitle(tr("Profile Selector"));
    resize(550, 400);
}

QtProfileSelectionDialog::~QtProfileSelectionDialog() = default;

void QtProfileSelectionDialog::accept() {
    ok = true;
    QDialog::accept();
}

void QtProfileSelectionDialog::reject() {
    ok = false;
    user_index = 0;
    QDialog::reject();
}

bool QtProfileSelectionDialog::GetStatus() const {
    return ok;
}

u32 QtProfileSelectionDialog::GetIndex() const {
    return user_index;
}

void QtProfileSelectionDialog::SelectUser(const QModelIndex& index) {
    user_index = index.row();
}

QtProfileSelector::QtProfileSelector(GMainWindow& parent) {
    connect(this, &QtProfileSelector::MainWindowSelectProfile, &parent,
            &GMainWindow::ProfileSelectorSelectProfile, Qt::QueuedConnection);
    connect(&parent, &GMainWindow::ProfileSelectorFinishedSelection, this,
            &QtProfileSelector::MainWindowFinishedSelection, Qt::DirectConnection);
}

QtProfileSelector::~QtProfileSelector() = default;

void QtProfileSelector::SelectProfile(
    std::function<void(std::optional<Service::Account::UUID>)> callback) const {
    this->callback = std::move(callback);
    emit MainWindowSelectProfile();
}

void QtProfileSelector::MainWindowFinishedSelection(std::optional<Service::Account::UUID> uuid) {
    // Acquire the HLE mutex
    std::lock_guard<std::recursive_mutex> lock(HLE::g_hle_lock);
    callback(uuid);
}
