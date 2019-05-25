// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "yuzu/debugger/wait_tree.h"
#include "yuzu/util/util.h"

#include "common/assert.h"
#include "core/core.h"
#include "core/hle/kernel/handle_table.h"
#include "core/hle/kernel/mutex.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/readable_event.h"
#include "core/hle/kernel/scheduler.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/kernel/wait_object.h"
#include "core/memory.h"

WaitTreeItem::WaitTreeItem() = default;
WaitTreeItem::~WaitTreeItem() = default;

QColor WaitTreeItem::GetColor() const {
    return QColor(Qt::GlobalColor::black);
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeItem::GetChildren() const {
    return {};
}

void WaitTreeItem::Expand() {
    if (IsExpandable() && !expanded) {
        children = GetChildren();
        for (std::size_t i = 0; i < children.size(); ++i) {
            children[i]->parent = this;
            children[i]->row = i;
        }
        expanded = true;
    }
}

WaitTreeItem* WaitTreeItem::Parent() const {
    return parent;
}

const std::vector<std::unique_ptr<WaitTreeItem>>& WaitTreeItem::Children() const {
    return children;
}

bool WaitTreeItem::IsExpandable() const {
    return false;
}

std::size_t WaitTreeItem::Row() const {
    return row;
}

std::vector<std::unique_ptr<WaitTreeThread>> WaitTreeItem::MakeThreadItemList() {
    std::vector<std::unique_ptr<WaitTreeThread>> item_list;
    std::size_t row = 0;
    auto add_threads = [&](const std::vector<Kernel::SharedPtr<Kernel::Thread>>& threads) {
        for (std::size_t i = 0; i < threads.size(); ++i) {
            item_list.push_back(std::make_unique<WaitTreeThread>(*threads[i]));
            item_list.back()->row = row;
            ++row;
        }
    };

    const auto& system = Core::System::GetInstance();
    add_threads(system.GlobalScheduler().GetThreadList());

    return item_list;
}

WaitTreeText::WaitTreeText(QString t) : text(std::move(t)) {}
WaitTreeText::~WaitTreeText() = default;

QString WaitTreeText::GetText() const {
    return text;
}

WaitTreeMutexInfo::WaitTreeMutexInfo(VAddr mutex_address, const Kernel::HandleTable& handle_table)
    : mutex_address(mutex_address) {
    mutex_value = Memory::Read32(mutex_address);
    owner_handle = static_cast<Kernel::Handle>(mutex_value & Kernel::Mutex::MutexOwnerMask);
    owner = handle_table.Get<Kernel::Thread>(owner_handle);
}

WaitTreeMutexInfo::~WaitTreeMutexInfo() = default;

QString WaitTreeMutexInfo::GetText() const {
    return tr("waiting for mutex 0x%1").arg(mutex_address, 16, 16, QLatin1Char{'0'});
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeMutexInfo::GetChildren() const {
    const bool has_waiters = (mutex_value & Kernel::Mutex::MutexHasWaitersFlag) != 0;

    std::vector<std::unique_ptr<WaitTreeItem>> list;
    list.push_back(std::make_unique<WaitTreeText>(tr("has waiters: %1").arg(has_waiters)));
    list.push_back(std::make_unique<WaitTreeText>(
        tr("owner handle: 0x%1").arg(owner_handle, 8, 16, QLatin1Char{'0'})));
    if (owner != nullptr) {
        list.push_back(std::make_unique<WaitTreeThread>(*owner));
    }
    return list;
}

WaitTreeCallstack::WaitTreeCallstack(const Kernel::Thread& thread) : thread(thread) {}
WaitTreeCallstack::~WaitTreeCallstack() = default;

QString WaitTreeCallstack::GetText() const {
    return tr("Call stack");
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeCallstack::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list;

    constexpr std::size_t BaseRegister = 29;
    u64 base_pointer = thread.GetContext().cpu_registers[BaseRegister];

    while (base_pointer != 0) {
        const u64 lr = Memory::Read64(base_pointer + sizeof(u64));
        if (lr == 0) {
            break;
        }

        list.push_back(std::make_unique<WaitTreeText>(
            tr("0x%1").arg(lr - sizeof(u32), 16, 16, QLatin1Char{'0'})));

        base_pointer = Memory::Read64(base_pointer);
    }

    return list;
}

WaitTreeWaitObject::WaitTreeWaitObject(const Kernel::WaitObject& o) : object(o) {}
WaitTreeWaitObject::~WaitTreeWaitObject() = default;

WaitTreeExpandableItem::WaitTreeExpandableItem() = default;
WaitTreeExpandableItem::~WaitTreeExpandableItem() = default;

bool WaitTreeExpandableItem::IsExpandable() const {
    return true;
}

QString WaitTreeWaitObject::GetText() const {
    return tr("[%1]%2 %3")
        .arg(object.GetObjectId())
        .arg(QString::fromStdString(object.GetTypeName()),
             QString::fromStdString(object.GetName()));
}

std::unique_ptr<WaitTreeWaitObject> WaitTreeWaitObject::make(const Kernel::WaitObject& object) {
    switch (object.GetHandleType()) {
    case Kernel::HandleType::ReadableEvent:
        return std::make_unique<WaitTreeEvent>(static_cast<const Kernel::ReadableEvent&>(object));
    case Kernel::HandleType::Thread:
        return std::make_unique<WaitTreeThread>(static_cast<const Kernel::Thread&>(object));
    default:
        return std::make_unique<WaitTreeWaitObject>(object);
    }
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeWaitObject::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list;

    const auto& threads = object.GetWaitingThreads();
    if (threads.empty()) {
        list.push_back(std::make_unique<WaitTreeText>(tr("waited by no thread")));
    } else {
        list.push_back(std::make_unique<WaitTreeThreadList>(threads));
    }
    return list;
}

QString WaitTreeWaitObject::GetResetTypeQString(Kernel::ResetType reset_type) {
    switch (reset_type) {
    case Kernel::ResetType::Automatic:
        return tr("automatic reset");
    case Kernel::ResetType::Manual:
        return tr("manual reset");
    }
    UNREACHABLE();
    return {};
}

WaitTreeObjectList::WaitTreeObjectList(
    const std::vector<Kernel::SharedPtr<Kernel::WaitObject>>& list, bool w_all)
    : object_list(list), wait_all(w_all) {}

WaitTreeObjectList::~WaitTreeObjectList() = default;

QString WaitTreeObjectList::GetText() const {
    if (wait_all)
        return tr("waiting for all objects");
    return tr("waiting for one of the following objects");
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeObjectList::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list(object_list.size());
    std::transform(object_list.begin(), object_list.end(), list.begin(),
                   [](const auto& t) { return WaitTreeWaitObject::make(*t); });
    return list;
}

WaitTreeThread::WaitTreeThread(const Kernel::Thread& thread) : WaitTreeWaitObject(thread) {}
WaitTreeThread::~WaitTreeThread() = default;

QString WaitTreeThread::GetText() const {
    const auto& thread = static_cast<const Kernel::Thread&>(object);
    QString status;
    switch (thread.GetStatus()) {
    case Kernel::ThreadStatus::Running:
        status = tr("running");
        break;
    case Kernel::ThreadStatus::Ready:
        status = tr("ready");
        break;
    case Kernel::ThreadStatus::Paused:
        status = tr("paused");
        break;
    case Kernel::ThreadStatus::WaitHLEEvent:
        status = tr("waiting for HLE return");
        break;
    case Kernel::ThreadStatus::WaitSleep:
        status = tr("sleeping");
        break;
    case Kernel::ThreadStatus::WaitIPC:
        status = tr("waiting for IPC reply");
        break;
    case Kernel::ThreadStatus::WaitSynch:
        status = tr("waiting for objects");
        break;
    case Kernel::ThreadStatus::WaitMutex:
        status = tr("waiting for mutex");
        break;
    case Kernel::ThreadStatus::WaitCondVar:
        status = tr("waiting for condition variable");
        break;
    case Kernel::ThreadStatus::WaitArb:
        status = tr("waiting for address arbiter");
        break;
    case Kernel::ThreadStatus::Dormant:
        status = tr("dormant");
        break;
    case Kernel::ThreadStatus::Dead:
        status = tr("dead");
        break;
    }

    const auto& context = thread.GetContext();
    const QString pc_info = tr(" PC = 0x%1 LR = 0x%2")
                                .arg(context.pc, 8, 16, QLatin1Char{'0'})
                                .arg(context.cpu_registers[30], 8, 16, QLatin1Char{'0'});
    return QStringLiteral("%1%2 (%3) ").arg(WaitTreeWaitObject::GetText(), pc_info, status);
}

QColor WaitTreeThread::GetColor() const {
    const auto& thread = static_cast<const Kernel::Thread&>(object);
    switch (thread.GetStatus()) {
    case Kernel::ThreadStatus::Running:
        return QColor(Qt::GlobalColor::darkGreen);
    case Kernel::ThreadStatus::Ready:
        return QColor(Qt::GlobalColor::darkBlue);
    case Kernel::ThreadStatus::Paused:
        return QColor(Qt::GlobalColor::lightGray);
    case Kernel::ThreadStatus::WaitHLEEvent:
    case Kernel::ThreadStatus::WaitIPC:
        return QColor(Qt::GlobalColor::darkRed);
    case Kernel::ThreadStatus::WaitSleep:
        return QColor(Qt::GlobalColor::darkYellow);
    case Kernel::ThreadStatus::WaitSynch:
    case Kernel::ThreadStatus::WaitMutex:
    case Kernel::ThreadStatus::WaitCondVar:
    case Kernel::ThreadStatus::WaitArb:
        return QColor(Qt::GlobalColor::red);
    case Kernel::ThreadStatus::Dormant:
        return QColor(Qt::GlobalColor::darkCyan);
    case Kernel::ThreadStatus::Dead:
        return QColor(Qt::GlobalColor::gray);
    default:
        return WaitTreeItem::GetColor();
    }
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeThread::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list(WaitTreeWaitObject::GetChildren());

    const auto& thread = static_cast<const Kernel::Thread&>(object);

    QString processor;
    switch (thread.GetProcessorID()) {
    case Kernel::ThreadProcessorId::THREADPROCESSORID_IDEAL:
        processor = tr("ideal");
        break;
    case Kernel::ThreadProcessorId::THREADPROCESSORID_0:
    case Kernel::ThreadProcessorId::THREADPROCESSORID_1:
    case Kernel::ThreadProcessorId::THREADPROCESSORID_2:
    case Kernel::ThreadProcessorId::THREADPROCESSORID_3:
        processor = tr("core %1").arg(thread.GetProcessorID());
        break;
    default:
        processor = tr("Unknown processor %1").arg(thread.GetProcessorID());
        break;
    }

    list.push_back(std::make_unique<WaitTreeText>(tr("processor = %1").arg(processor)));
    list.push_back(
        std::make_unique<WaitTreeText>(tr("ideal core = %1").arg(thread.GetIdealCore())));
    list.push_back(
        std::make_unique<WaitTreeText>(tr("affinity mask = %1").arg(thread.GetAffinityMask())));
    list.push_back(std::make_unique<WaitTreeText>(tr("thread id = %1").arg(thread.GetThreadID())));
    list.push_back(std::make_unique<WaitTreeText>(tr("priority = %1(current) / %2(normal)")
                                                      .arg(thread.GetPriority())
                                                      .arg(thread.GetNominalPriority())));
    list.push_back(std::make_unique<WaitTreeText>(
        tr("last running ticks = %1").arg(thread.GetLastRunningTicks())));

    const VAddr mutex_wait_address = thread.GetMutexWaitAddress();
    if (mutex_wait_address != 0) {
        const auto& handle_table = thread.GetOwnerProcess()->GetHandleTable();
        list.push_back(std::make_unique<WaitTreeMutexInfo>(mutex_wait_address, handle_table));
    } else {
        list.push_back(std::make_unique<WaitTreeText>(tr("not waiting for mutex")));
    }

    if (thread.GetStatus() == Kernel::ThreadStatus::WaitSynch) {
        list.push_back(std::make_unique<WaitTreeObjectList>(thread.GetWaitObjects(),
                                                            thread.IsSleepingOnWait()));
    }

    list.push_back(std::make_unique<WaitTreeCallstack>(thread));

    return list;
}

WaitTreeEvent::WaitTreeEvent(const Kernel::ReadableEvent& object) : WaitTreeWaitObject(object) {}
WaitTreeEvent::~WaitTreeEvent() = default;

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeEvent::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list(WaitTreeWaitObject::GetChildren());

    list.push_back(std::make_unique<WaitTreeText>(
        tr("reset type = %1")
            .arg(GetResetTypeQString(
                static_cast<const Kernel::ReadableEvent&>(object).GetResetType()))));
    return list;
}

WaitTreeThreadList::WaitTreeThreadList(const std::vector<Kernel::SharedPtr<Kernel::Thread>>& list)
    : thread_list(list) {}
WaitTreeThreadList::~WaitTreeThreadList() = default;

QString WaitTreeThreadList::GetText() const {
    return tr("waited by thread");
}

std::vector<std::unique_ptr<WaitTreeItem>> WaitTreeThreadList::GetChildren() const {
    std::vector<std::unique_ptr<WaitTreeItem>> list(thread_list.size());
    std::transform(thread_list.begin(), thread_list.end(), list.begin(),
                   [](const auto& t) { return std::make_unique<WaitTreeThread>(*t); });
    return list;
}

WaitTreeModel::WaitTreeModel(QObject* parent) : QAbstractItemModel(parent) {}
WaitTreeModel::~WaitTreeModel() = default;

QModelIndex WaitTreeModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent))
        return {};

    if (parent.isValid()) {
        WaitTreeItem* parent_item = static_cast<WaitTreeItem*>(parent.internalPointer());
        parent_item->Expand();
        return createIndex(row, column, parent_item->Children()[row].get());
    }

    return createIndex(row, column, thread_items[row].get());
}

QModelIndex WaitTreeModel::parent(const QModelIndex& index) const {
    if (!index.isValid())
        return {};

    WaitTreeItem* parent_item = static_cast<WaitTreeItem*>(index.internalPointer())->Parent();
    if (!parent_item) {
        return QModelIndex();
    }
    return createIndex(static_cast<int>(parent_item->Row()), 0, parent_item);
}

int WaitTreeModel::rowCount(const QModelIndex& parent) const {
    if (!parent.isValid())
        return static_cast<int>(thread_items.size());

    WaitTreeItem* parent_item = static_cast<WaitTreeItem*>(parent.internalPointer());
    parent_item->Expand();
    return static_cast<int>(parent_item->Children().size());
}

int WaitTreeModel::columnCount(const QModelIndex&) const {
    return 1;
}

QVariant WaitTreeModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};

    switch (role) {
    case Qt::DisplayRole:
        return static_cast<WaitTreeItem*>(index.internalPointer())->GetText();
    case Qt::ForegroundRole:
        return static_cast<WaitTreeItem*>(index.internalPointer())->GetColor();
    default:
        return {};
    }
}

void WaitTreeModel::ClearItems() {
    thread_items.clear();
}

void WaitTreeModel::InitItems() {
    thread_items = WaitTreeItem::MakeThreadItemList();
}

WaitTreeWidget::WaitTreeWidget(QWidget* parent) : QDockWidget(tr("Wait Tree"), parent) {
    setObjectName(QStringLiteral("WaitTreeWidget"));
    view = new QTreeView(this);
    view->setHeaderHidden(true);
    setWidget(view);
    setEnabled(false);
}

WaitTreeWidget::~WaitTreeWidget() = default;

void WaitTreeWidget::OnDebugModeEntered() {
    if (!Core::System::GetInstance().IsPoweredOn())
        return;
    model->InitItems();
    view->setModel(model);
    setEnabled(true);
}

void WaitTreeWidget::OnDebugModeLeft() {
    setEnabled(false);
    view->setModel(nullptr);
    model->ClearItems();
}

void WaitTreeWidget::OnEmulationStarting(EmuThread* emu_thread) {
    model = new WaitTreeModel(this);
    view->setModel(model);
    setEnabled(false);
}

void WaitTreeWidget::OnEmulationStopping() {
    view->setModel(nullptr);
    delete model;
    setEnabled(false);
}
