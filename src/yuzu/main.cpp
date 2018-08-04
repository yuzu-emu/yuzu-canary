// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cinttypes>
#include <clocale>
#include <memory>
#include <thread>
#include <glad/glad.h>
#define QT_NO_OPENGL
#include <QDesktopWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QtGui>
#include <QtWidgets>
#include "common/common_paths.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/logging/log.h"
#include "common/logging/text_formatter.h"
#include "common/microprofile.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/crypto/key_manager.h"
#include "core/gdbstub/gdbstub.h"
#include "core/loader/loader.h"
#include "core/settings.h"
#include "video_core/debug_utils/debug_utils.h"
#include "yuzu/about_dialog.h"
#include "yuzu/bootmanager.h"
#include "yuzu/configuration/config.h"
#include "yuzu/configuration/configure_dialog.h"
#include "yuzu/debugger/console.h"
#include "yuzu/debugger/graphics/graphics_breakpoints.h"
#include "yuzu/debugger/graphics/graphics_surface.h"
#include "yuzu/debugger/profiler.h"
#include "yuzu/debugger/wait_tree.h"
#include "yuzu/game_list.h"
#include "yuzu/hotkeys.h"
#include "yuzu/main.h"
#include "yuzu/ui_settings.h"

#ifdef QT_STATICPLUGIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

/**
 * "Callouts" are one-time instructional messages shown to the user. In the config settings, there
 * is a bitfield "callout_flags" options, used to track if a message has already been shown to the
 * user. This is 32-bits - if we have more than 32 callouts, we should retire and recyle old ones.
 */
enum class CalloutFlag : uint32_t {
    Telemetry = 0x1,
};

static void ShowCalloutMessage(const QString& message, CalloutFlag flag) {
    if (UISettings::values.callout_flags & static_cast<uint32_t>(flag)) {
        return;
    }

    UISettings::values.callout_flags |= static_cast<uint32_t>(flag);

    QMessageBox msg;
    msg.setText(message);
    msg.setStandardButtons(QMessageBox::Ok);
    msg.setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    msg.setStyleSheet("QLabel{min-width: 900px;}");
    msg.exec();
}

void GMainWindow::ShowCallouts() {}

GMainWindow::GMainWindow() : config(new Config()), emu_thread(nullptr) {

    debug_context = Tegra::DebugContext::Construct();

    setAcceptDrops(true);
    ui.setupUi(this);
    statusBar()->hide();

    default_theme_paths = QIcon::themeSearchPaths();
    UpdateUITheme();

    InitializeWidgets();
    InitializeDebugWidgets();
    InitializeRecentFileMenuActions();
    InitializeHotkeys();

    SetDefaultUIGeometry();
    RestoreUIState();

    ConnectMenuEvents();
    ConnectWidgetEvents();
    LOG_INFO(Frontend, "yuzu Version: {} | {}-{}", Common::g_build_name, Common::g_scm_branch,
             Common::g_scm_desc);

    setWindowTitle(QString("yuzu %1| %2-%3")
                       .arg(Common::g_build_name, Common::g_scm_branch, Common::g_scm_desc));
    show();

    game_list->PopulateAsync(UISettings::values.gamedir, UISettings::values.gamedir_deepscan);

    // Show one-time "callout" messages to the user
    ShowCallouts();

    QStringList args = QApplication::arguments();
    if (args.length() >= 2) {
        BootGame(args[1]);
    }
}

GMainWindow::~GMainWindow() {
    // will get automatically deleted otherwise
    if (render_window->parent() == nullptr)
        delete render_window;
}

void GMainWindow::InitializeWidgets() {
    render_window = new GRenderWindow(this, emu_thread.get());
    render_window->hide();

    game_list = new GameList(this);
    ui.horizontalLayout->addWidget(game_list);

    // Create status bar
    message_label = new QLabel();
    // Configured separately for left alignment
    message_label->setVisible(false);
    message_label->setFrameStyle(QFrame::NoFrame);
    message_label->setContentsMargins(4, 0, 4, 0);
    message_label->setAlignment(Qt::AlignLeft);
    statusBar()->addPermanentWidget(message_label, 1);

    emu_speed_label = new QLabel();
    emu_speed_label->setToolTip(
        tr("Current emulation speed. Values higher or lower than 100% "
           "indicate emulation is running faster or slower than a Switch."));
    game_fps_label = new QLabel();
    game_fps_label->setToolTip(tr("How many frames per second the game is currently displaying. "
                                  "This will vary from game to game and scene to scene."));
    emu_frametime_label = new QLabel();
    emu_frametime_label->setToolTip(
        tr("Time taken to emulate a Switch frame, not counting framelimiting or v-sync. For "
           "full-speed emulation this should be at most 16.67 ms."));

    for (auto& label : {emu_speed_label, game_fps_label, emu_frametime_label}) {
        label->setVisible(false);
        label->setFrameStyle(QFrame::NoFrame);
        label->setContentsMargins(4, 0, 4, 0);
        statusBar()->addPermanentWidget(label, 0);
    }
    statusBar()->setVisible(true);
    setStyleSheet("QStatusBar::item{border: none;}");
}

void GMainWindow::InitializeDebugWidgets() {
    QMenu* debug_menu = ui.menu_View_Debugging;

#if MICROPROFILE_ENABLED
    microProfileDialog = new MicroProfileDialog(this);
    microProfileDialog->hide();
    debug_menu->addAction(microProfileDialog->toggleViewAction());
#endif

    graphicsBreakpointsWidget = new GraphicsBreakPointsWidget(debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphicsBreakpointsWidget);
    graphicsBreakpointsWidget->hide();
    debug_menu->addAction(graphicsBreakpointsWidget->toggleViewAction());

    graphicsSurfaceWidget = new GraphicsSurfaceWidget(debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphicsSurfaceWidget);
    graphicsSurfaceWidget->hide();
    debug_menu->addAction(graphicsSurfaceWidget->toggleViewAction());

    waitTreeWidget = new WaitTreeWidget(this);
    addDockWidget(Qt::LeftDockWidgetArea, waitTreeWidget);
    waitTreeWidget->hide();
    debug_menu->addAction(waitTreeWidget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, waitTreeWidget,
            &WaitTreeWidget::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, waitTreeWidget,
            &WaitTreeWidget::OnEmulationStopping);
}

void GMainWindow::InitializeRecentFileMenuActions() {
    for (int i = 0; i < max_recent_files_item; ++i) {
        actions_recent_files[i] = new QAction(this);
        actions_recent_files[i]->setVisible(false);
        connect(actions_recent_files[i], &QAction::triggered, this, &GMainWindow::OnMenuRecentFile);

        ui.menu_recent_files->addAction(actions_recent_files[i]);
    }

    UpdateRecentFiles();
}

void GMainWindow::InitializeHotkeys() {
    RegisterHotkey("Main Window", "Load File", QKeySequence::Open);
    RegisterHotkey("Main Window", "Start Emulation");
    RegisterHotkey("Main Window", "Continue/Pause", QKeySequence(Qt::Key_F4));
    RegisterHotkey("Main Window", "Fullscreen", QKeySequence::FullScreen);
    RegisterHotkey("Main Window", "Exit Fullscreen", QKeySequence(Qt::Key_Escape),
                   Qt::ApplicationShortcut);
    RegisterHotkey("Main Window", "Toggle Speed Limit", QKeySequence("CTRL+Z"),
                   Qt::ApplicationShortcut);
    LoadHotkeys();

    connect(GetHotkey("Main Window", "Load File", this), &QShortcut::activated, this,
            &GMainWindow::OnMenuLoadFile);
    connect(GetHotkey("Main Window", "Start Emulation", this), &QShortcut::activated, this,
            &GMainWindow::OnStartGame);
    connect(GetHotkey("Main Window", "Continue/Pause", this), &QShortcut::activated, this, [&] {
        if (emulation_running) {
            if (emu_thread->IsRunning()) {
                OnPauseGame();
            } else {
                OnStartGame();
            }
        }
    });
    connect(GetHotkey("Main Window", "Fullscreen", render_window), &QShortcut::activated,
            ui.action_Fullscreen, &QAction::trigger);
    connect(GetHotkey("Main Window", "Fullscreen", render_window), &QShortcut::activatedAmbiguously,
            ui.action_Fullscreen, &QAction::trigger);
    connect(GetHotkey("Main Window", "Exit Fullscreen", this), &QShortcut::activated, this, [&] {
        if (emulation_running) {
            ui.action_Fullscreen->setChecked(false);
            ToggleFullscreen();
        }
    });
    connect(GetHotkey("Main Window", "Toggle Speed Limit", this), &QShortcut::activated, this, [&] {
        Settings::values.toggle_framelimit = !Settings::values.toggle_framelimit;
        UpdateStatusBar();
    });
}

void GMainWindow::SetDefaultUIGeometry() {
    // geometry: 55% of the window contents are in the upper screen half, 45% in the lower half
    const QRect screenRect = QApplication::desktop()->screenGeometry(this);

    const int w = screenRect.width() * 2 / 3;
    const int h = screenRect.height() / 2;
    const int x = (screenRect.x() + screenRect.width()) / 2 - w / 2;
    const int y = (screenRect.y() + screenRect.height()) / 2 - h * 55 / 100;

    setGeometry(x, y, w, h);
}

void GMainWindow::RestoreUIState() {
    restoreGeometry(UISettings::values.geometry);
    restoreState(UISettings::values.state);
    render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
#if MICROPROFILE_ENABLED
    microProfileDialog->restoreGeometry(UISettings::values.microprofile_geometry);
    microProfileDialog->setVisible(UISettings::values.microprofile_visible);
#endif

    game_list->LoadInterfaceLayout();

    ui.action_Single_Window_Mode->setChecked(UISettings::values.single_window_mode);
    ToggleWindowMode();

    ui.action_Fullscreen->setChecked(UISettings::values.fullscreen);

    ui.action_Display_Dock_Widget_Headers->setChecked(UISettings::values.display_titlebar);
    OnDisplayTitleBars(ui.action_Display_Dock_Widget_Headers->isChecked());

    ui.action_Show_Filter_Bar->setChecked(UISettings::values.show_filter_bar);
    game_list->setFilterVisible(ui.action_Show_Filter_Bar->isChecked());

    ui.action_Show_Status_Bar->setChecked(UISettings::values.show_status_bar);
    statusBar()->setVisible(ui.action_Show_Status_Bar->isChecked());
    Debugger::ToggleConsole();
}

void GMainWindow::ConnectWidgetEvents() {
    connect(game_list, &GameList::GameChosen, this, &GMainWindow::OnGameListLoadFile);
    connect(game_list, &GameList::OpenSaveFolderRequested, this,
            &GMainWindow::OnGameListOpenSaveFolder);

    connect(this, &GMainWindow::EmulationStarting, render_window,
            &GRenderWindow::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, render_window,
            &GRenderWindow::OnEmulationStopping);

    connect(&status_bar_update_timer, &QTimer::timeout, this, &GMainWindow::UpdateStatusBar);
}

void GMainWindow::ConnectMenuEvents() {
    // File
    connect(ui.action_Load_File, &QAction::triggered, this, &GMainWindow::OnMenuLoadFile);
    connect(ui.action_Load_Folder, &QAction::triggered, this, &GMainWindow::OnMenuLoadFolder);
    connect(ui.action_Select_Game_List_Root, &QAction::triggered, this,
            &GMainWindow::OnMenuSelectGameListRoot);
    connect(ui.action_Exit, &QAction::triggered, this, &QMainWindow::close);

    // Emulation
    connect(ui.action_Start, &QAction::triggered, this, &GMainWindow::OnStartGame);
    connect(ui.action_Pause, &QAction::triggered, this, &GMainWindow::OnPauseGame);
    connect(ui.action_Stop, &QAction::triggered, this, &GMainWindow::OnStopGame);
    connect(ui.action_Configure, &QAction::triggered, this, &GMainWindow::OnConfigure);

    // View
    connect(ui.action_Single_Window_Mode, &QAction::triggered, this,
            &GMainWindow::ToggleWindowMode);
    connect(ui.action_Display_Dock_Widget_Headers, &QAction::triggered, this,
            &GMainWindow::OnDisplayTitleBars);
    ui.action_Show_Filter_Bar->setShortcut(tr("CTRL+F"));
    connect(ui.action_Show_Filter_Bar, &QAction::triggered, this, &GMainWindow::OnToggleFilterBar);
    connect(ui.action_Show_Status_Bar, &QAction::triggered, statusBar(), &QStatusBar::setVisible);

    // Fullscreen
    ui.action_Fullscreen->setShortcut(GetHotkey("Main Window", "Fullscreen", this)->key());
    connect(ui.action_Fullscreen, &QAction::triggered, this, &GMainWindow::ToggleFullscreen);

    // Help
    connect(ui.action_About, &QAction::triggered, this, &GMainWindow::OnAbout);
}

void GMainWindow::OnDisplayTitleBars(bool show) {
    QList<QDockWidget*> widgets = findChildren<QDockWidget*>();

    if (show) {
        for (QDockWidget* widget : widgets) {
            QWidget* old = widget->titleBarWidget();
            widget->setTitleBarWidget(nullptr);
            if (old != nullptr)
                delete old;
        }
    } else {
        for (QDockWidget* widget : widgets) {
            QWidget* old = widget->titleBarWidget();
            widget->setTitleBarWidget(new QWidget());
            if (old != nullptr)
                delete old;
        }
    }
}

bool GMainWindow::SupportsRequiredGLExtensions() {
    QStringList unsupported_ext;

    if (!GLAD_GL_ARB_program_interface_query)
        unsupported_ext.append("ARB_program_interface_query");
    if (!GLAD_GL_ARB_separate_shader_objects)
        unsupported_ext.append("ARB_separate_shader_objects");
    if (!GLAD_GL_ARB_vertex_attrib_binding)
        unsupported_ext.append("ARB_vertex_attrib_binding");
    if (!GLAD_GL_ARB_vertex_type_10f_11f_11f_rev)
        unsupported_ext.append("ARB_vertex_type_10f_11f_11f_rev");

    // Extensions required to support some texture formats.
    if (!GLAD_GL_EXT_texture_compression_s3tc)
        unsupported_ext.append("EXT_texture_compression_s3tc");
    if (!GLAD_GL_ARB_texture_compression_rgtc)
        unsupported_ext.append("ARB_texture_compression_rgtc");
    if (!GLAD_GL_ARB_texture_compression_bptc)
        unsupported_ext.append("ARB_texture_compression_bptc");
    if (!GLAD_GL_ARB_depth_buffer_float)
        unsupported_ext.append("ARB_depth_buffer_float");

    for (const QString& ext : unsupported_ext)
        LOG_CRITICAL(Frontend, "Unsupported GL extension: {}", ext.toStdString());

    return unsupported_ext.empty();
}

bool GMainWindow::LoadROM(const QString& filename) {
    // Shutdown previous session if the emu thread is still active...
    if (emu_thread != nullptr)
        ShutdownGame();

    render_window->InitRenderTarget();
    render_window->MakeCurrent();

    if (!gladLoadGL()) {
        QMessageBox::critical(this, tr("Error while initializing OpenGL 3.3 Core!"),
                              tr("Your GPU may not support OpenGL 3.3, or you do not "
                                 "have the latest graphics driver."));
        return false;
    }

    if (!SupportsRequiredGLExtensions()) {
        QMessageBox::critical(
            this, tr("Error while initializing OpenGL Core!"),
            tr("Your GPU may not support one or more required OpenGL extensions. Please "
               "ensure you have the latest graphics driver. See the log for more details."));
        return false;
    }

    Core::System& system{Core::System::GetInstance()};

    system.SetGPUDebugContext(debug_context);

    const Core::System::ResultStatus result{system.Load(*render_window, filename.toStdString())};

    render_window->DoneCurrent();

    if (result != Core::System::ResultStatus::Success) {
        switch (result) {
        case Core::System::ResultStatus::ErrorGetLoader:
            LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", filename.toStdString());
            QMessageBox::critical(this, tr("Error while loading ROM!"),
                                  tr("The ROM format is not supported."));
            break;
        case Core::System::ResultStatus::ErrorUnsupportedArch:
            LOG_CRITICAL(Frontend, "Unsupported architecture detected!", filename.toStdString());
            QMessageBox::critical(this, tr("Error while loading ROM!"),
                                  tr("The ROM uses currently unusable 32-bit architecture"));
            break;
        case Core::System::ResultStatus::ErrorSystemMode:
            LOG_CRITICAL(Frontend, "Failed to load ROM!");
            QMessageBox::critical(this, tr("Error while loading ROM!"),
                                  tr("Could not determine the system mode."));
            break;

        case Core::System::ResultStatus::ErrorLoader_ErrorMissingKeys: {
            const auto reg_found = Core::Crypto::KeyManager::KeyFileExists(false);
            const auto title_found = Core::Crypto::KeyManager::KeyFileExists(true);

            std::string file_text;

            if (!reg_found && !title_found) {
                file_text = "A proper key file (prod.keys, dev.keys, or title.keys) could not be "
                            "found. You will need to dump your keys from your switch to continue.";
            } else if (reg_found && title_found) {
                file_text =
                    "Both key files were found in your config directory, but the correct key could"
                    "not be found. You may be missing a titlekey or general key, depending on "
                    "the game.";
            } else if (reg_found) {
                file_text =
                    "The regular keys file (prod.keys/dev.keys) was found in your config, but the "
                    "titlekeys file (title.keys) was not. You are either missing the correct "
                    "titlekey or missing a general key required to decrypt the game.";
            } else {
                file_text = "The title keys file (title.keys) was found in your config, but "
                            "the regular keys file (prod.keys/dev.keys) was not. Unfortunately, "
                            "having the titlekey is not enough, you need additional general keys "
                            "to properly decrypt the game. You should double-check to make sure "
                            "your keys are correct.";
            }

            QMessageBox::critical(
                this, tr("Error while loading ROM!"),
                tr(("The game you are trying to load is encrypted and the required keys to load "
                    "the game could not be found in your configuration. " +
                    file_text + " Please refer to the yuzu wiki for help.")
                       .c_str()));
            break;
        }
        case Core::System::ResultStatus::ErrorLoader_ErrorDecrypting: {
            QMessageBox::critical(
                this, tr("Error while loading ROM!"),
                tr("There was a general error while decrypting the game. This means that the keys "
                   "necessary were found, but were either incorrect, the game itself was not a "
                   "valid game or the game uses an unhandled cryptographic scheme. Please double "
                   "check that you have the correct "
                   "keys."));
            break;
        }
        case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
            QMessageBox::critical(this, tr("Error while loading ROM!"),
                                  tr("The ROM format is not supported."));
            break;

        case Core::System::ResultStatus::ErrorVideoCore:
            QMessageBox::critical(
                this, tr("An error occured in the video core."),
                tr("yuzu has encountered an error while running the video core, please see the "
                   "log for more details."
                   "For more information on accessing the log, please see the following page: "
                   "<a href='https://community.citra-emu.org/t/how-to-upload-the-log-file/296'>How "
                   "to "
                   "Upload the Log File</a>."
                   "Ensure that you have the latest graphics drivers for your GPU."));

            break;

        default:
            QMessageBox::critical(
                this, tr("Error while loading ROM!"),
                tr("An unknown error occured. Please see the log for more details."));
            break;
        }
        return false;
    }
    Core::Telemetry().AddField(Telemetry::FieldType::App, "Frontend", "Qt");
    return true;
}

void GMainWindow::BootGame(const QString& filename) {
    LOG_INFO(Frontend, "yuzu starting...");
    StoreRecentFile(filename); // Put the filename on top of the list

    if (!LoadROM(filename))
        return;

    // Create and start the emulation thread
    emu_thread = std::make_unique<EmuThread>(render_window);
    emit EmulationStarting(emu_thread.get());
    render_window->moveContext();
    emu_thread->start();

    connect(render_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    // BlockingQueuedConnection is important here, it makes sure we've finished refreshing our views
    // before the CPU continues
    connect(emu_thread.get(), &EmuThread::DebugModeEntered, waitTreeWidget,
            &WaitTreeWidget::OnDebugModeEntered, Qt::BlockingQueuedConnection);
    connect(emu_thread.get(), &EmuThread::DebugModeLeft, waitTreeWidget,
            &WaitTreeWidget::OnDebugModeLeft, Qt::BlockingQueuedConnection);

    // Update the GUI
    if (ui.action_Single_Window_Mode->isChecked()) {
        game_list->hide();
    }
    status_bar_update_timer.start(2000);

    render_window->show();
    render_window->setFocus();

    emulation_running = true;
    if (ui.action_Fullscreen->isChecked()) {
        ShowFullscreen();
    }
    OnStartGame();
}

void GMainWindow::ShutdownGame() {
    emu_thread->RequestStop();

    emit EmulationStopping();

    // Wait for emulation thread to complete and delete it
    emu_thread->wait();
    emu_thread = nullptr;

    // The emulation is stopped, so closing the window or not does not matter anymore
    disconnect(render_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);

    // Update the GUI
    ui.action_Start->setEnabled(false);
    ui.action_Start->setText(tr("Start"));
    ui.action_Pause->setEnabled(false);
    ui.action_Stop->setEnabled(false);
    render_window->hide();
    game_list->show();
    game_list->setFilterFocus();

    // Disable status bar updates
    status_bar_update_timer.stop();
    message_label->setVisible(false);
    emu_speed_label->setVisible(false);
    game_fps_label->setVisible(false);
    emu_frametime_label->setVisible(false);

    emulation_running = false;
}

void GMainWindow::StoreRecentFile(const QString& filename) {
    UISettings::values.recent_files.prepend(filename);
    UISettings::values.recent_files.removeDuplicates();
    while (UISettings::values.recent_files.size() > max_recent_files_item) {
        UISettings::values.recent_files.removeLast();
    }

    UpdateRecentFiles();
}

void GMainWindow::UpdateRecentFiles() {
    unsigned int num_recent_files =
        std::min(UISettings::values.recent_files.size(), static_cast<int>(max_recent_files_item));

    for (unsigned int i = 0; i < num_recent_files; i++) {
        QString text = QString("&%1. %2").arg(i + 1).arg(
            QFileInfo(UISettings::values.recent_files[i]).fileName());
        actions_recent_files[i]->setText(text);
        actions_recent_files[i]->setData(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setToolTip(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setVisible(true);
    }

    for (int j = num_recent_files; j < max_recent_files_item; ++j) {
        actions_recent_files[j]->setVisible(false);
    }

    // Grey out the recent files menu if the list is empty
    if (num_recent_files == 0) {
        ui.menu_recent_files->setEnabled(false);
    } else {
        ui.menu_recent_files->setEnabled(true);
    }
}

void GMainWindow::OnGameListLoadFile(QString game_path) {
    BootGame(game_path);
}

void GMainWindow::OnGameListOpenSaveFolder(u64 program_id) {
    UNIMPLEMENTED();
}

void GMainWindow::OnMenuLoadFile() {
    QString extensions;
    for (const auto& piece : game_list->supported_file_extensions)
        extensions += "*." + piece + " ";

    extensions += "main ";

    QString file_filter = tr("Switch Executable") + " (" + extensions + ")";
    file_filter += ";;" + tr("All Files (*.*)");

    QString filename = QFileDialog::getOpenFileName(this, tr("Load File"),
                                                    UISettings::values.roms_path, file_filter);
    if (!filename.isEmpty()) {
        UISettings::values.roms_path = QFileInfo(filename).path();

        BootGame(filename);
    }
}

void GMainWindow::OnMenuLoadFolder() {
    QDir dir = QFileDialog::getExistingDirectory(this, tr("Open Extracted ROM Directory"));

    QStringList matching_main = dir.entryList(QStringList("main"), QDir::Files);
    if (matching_main.size() == 1) {
        BootGame(dir.path() + DIR_SEP + matching_main[0]);
    } else {
        QMessageBox::warning(this, tr("Invalid Directory Selected"),
                             tr("The directory you have selected does not contain a 'main' file."));
    }
}

void GMainWindow::OnMenuSelectGameListRoot() {
    QString dir_path = QFileDialog::getExistingDirectory(this, tr("Select Directory"));
    if (!dir_path.isEmpty()) {
        UISettings::values.gamedir = dir_path;
        game_list->PopulateAsync(dir_path, UISettings::values.gamedir_deepscan);
    }
}

void GMainWindow::OnMenuRecentFile() {
    QAction* action = qobject_cast<QAction*>(sender());
    assert(action);

    QString filename = action->data().toString();
    QFileInfo file_info(filename);
    if (file_info.exists()) {
        BootGame(filename);
    } else {
        // Display an error message and remove the file from the list.
        QMessageBox::information(this, tr("File not found"),
                                 tr("File \"%1\" not found").arg(filename));

        UISettings::values.recent_files.removeOne(filename);
        UpdateRecentFiles();
    }
}

void GMainWindow::OnStartGame() {
    emu_thread->SetRunning(true);
    qRegisterMetaType<Core::System::ResultStatus>("Core::System::ResultStatus");
    qRegisterMetaType<std::string>("std::string");
    connect(emu_thread.get(), &EmuThread::ErrorThrown, this, &GMainWindow::OnCoreError);

    ui.action_Start->setEnabled(false);
    ui.action_Start->setText(tr("Continue"));

    ui.action_Pause->setEnabled(true);
    ui.action_Stop->setEnabled(true);
}

void GMainWindow::OnPauseGame() {
    emu_thread->SetRunning(false);

    ui.action_Start->setEnabled(true);
    ui.action_Pause->setEnabled(false);
    ui.action_Stop->setEnabled(true);
}

void GMainWindow::OnStopGame() {
    ShutdownGame();
}

void GMainWindow::ToggleFullscreen() {
    if (!emulation_running) {
        return;
    }
    if (ui.action_Fullscreen->isChecked()) {
        ShowFullscreen();
    } else {
        HideFullscreen();
    }
}

void GMainWindow::ShowFullscreen() {
    if (ui.action_Single_Window_Mode->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        ui.menubar->hide();
        statusBar()->hide();
        showFullScreen();
    } else {
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
        render_window->showFullScreen();
    }
}

void GMainWindow::HideFullscreen() {
    if (ui.action_Single_Window_Mode->isChecked()) {
        statusBar()->setVisible(ui.action_Show_Status_Bar->isChecked());
        ui.menubar->show();
        showNormal();
        restoreGeometry(UISettings::values.geometry);
    } else {
        render_window->showNormal();
        render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
    }
}

void GMainWindow::ToggleWindowMode() {
    if (ui.action_Single_Window_Mode->isChecked()) {
        // Render in the main window...
        render_window->BackupGeometry();
        ui.horizontalLayout->addWidget(render_window);
        render_window->setFocusPolicy(Qt::ClickFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->setFocus();
            game_list->hide();
        }

    } else {
        // Render in a separate window...
        ui.horizontalLayout->removeWidget(render_window);
        render_window->setParent(nullptr);
        render_window->setFocusPolicy(Qt::NoFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->RestoreGeometry();
            game_list->show();
        }
    }
}

void GMainWindow::OnConfigure() {
    ConfigureDialog configureDialog(this);
    auto old_theme = UISettings::values.theme;
    auto result = configureDialog.exec();
    if (result == QDialog::Accepted) {
        configureDialog.applyConfiguration();
        if (UISettings::values.theme != old_theme)
            UpdateUITheme();
        config->Save();
    }
}

void GMainWindow::OnAbout() {
    AboutDialog aboutDialog(this);
    aboutDialog.exec();
}

void GMainWindow::OnToggleFilterBar() {
    game_list->setFilterVisible(ui.action_Show_Filter_Bar->isChecked());
    if (ui.action_Show_Filter_Bar->isChecked()) {
        game_list->setFilterFocus();
    } else {
        game_list->clearFilter();
    }
}

void GMainWindow::UpdateStatusBar() {
    if (emu_thread == nullptr) {
        status_bar_update_timer.stop();
        return;
    }

    auto results = Core::System::GetInstance().GetAndResetPerfStats();

    emu_speed_label->setText(tr("Speed: %1%").arg(results.emulation_speed * 100.0, 0, 'f', 0));
    game_fps_label->setText(tr("Game: %1 FPS").arg(results.game_fps, 0, 'f', 0));
    emu_frametime_label->setText(tr("Frame: %1 ms").arg(results.frametime * 1000.0, 0, 'f', 2));

    emu_speed_label->setVisible(true);
    game_fps_label->setVisible(true);
    emu_frametime_label->setVisible(true);
}

void GMainWindow::OnCoreError(Core::System::ResultStatus result, std::string details) {
    QMessageBox::StandardButton answer;
    QString status_message;
    const QString common_message = tr(
        "The game you are trying to load requires additional files from your Switch to be dumped "
        "before playing.<br/><br/>For more information on dumping these files, please see the "
        "following wiki page: <a "
        "href='https://yuzu-emu.org/wiki/"
        "dumping-system-archives-and-the-shared-fonts-from-a-switch-console/'>Dumping System "
        "Archives and the Shared Fonts from a Switch Console</a>.<br/><br/>Would you like to quit "
        "back to the game list? Continuing emulation may result in crashes, corrupted save "
        "data, or other bugs.");
    switch (result) {
    case Core::System::ResultStatus::ErrorSystemFiles: {
        QString message = "yuzu was unable to locate a Switch system archive";
        if (!details.empty()) {
            message.append(tr(": %1. ").arg(details.c_str()));
        } else {
            message.append(". ");
        }
        message.append(common_message);

        answer = QMessageBox::question(this, tr("System Archive Not Found"), message,
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        status_message = "System Archive Missing";
        break;
    }

    case Core::System::ResultStatus::ErrorSharedFont: {
        QString message = tr("yuzu was unable to locate the Switch shared fonts. ");
        message.append(common_message);
        answer = QMessageBox::question(this, tr("Shared Fonts Not Found"), message,
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        status_message = "Shared Font Missing";
        break;
    }

    default:
        answer = QMessageBox::question(
            this, tr("Fatal Error"),
            tr("yuzu has encountered a fatal error, please see the log for more details. "
               "For more information on accessing the log, please see the following page: "
               "<a href='https://community.citra-emu.org/t/how-to-upload-the-log-file/296'>How to "
               "Upload the Log File</a>.<br/><br/>Would you like to quit back to the game list? "
               "Continuing emulation may result in crashes, corrupted save data, or other bugs."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        status_message = "Fatal Error encountered";
        break;
    }

    if (answer == QMessageBox::Yes) {
        if (emu_thread) {
            ShutdownGame();
        }
    } else {
        // Only show the message if the game is still running.
        if (emu_thread) {
            emu_thread->SetRunning(true);
            message_label->setText(status_message);
            message_label->setVisible(true);
        }
    }
}

bool GMainWindow::ConfirmClose() {
    if (emu_thread == nullptr || !UISettings::values.confirm_before_closing)
        return true;

    QMessageBox::StandardButton answer =
        QMessageBox::question(this, tr("yuzu"), tr("Are you sure you want to close yuzu?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer != QMessageBox::No;
}

void GMainWindow::closeEvent(QCloseEvent* event) {
    if (!ConfirmClose()) {
        event->ignore();
        return;
    }

    if (ui.action_Fullscreen->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
    }
    UISettings::values.state = saveState();
#if MICROPROFILE_ENABLED
    UISettings::values.microprofile_geometry = microProfileDialog->saveGeometry();
    UISettings::values.microprofile_visible = microProfileDialog->isVisible();
#endif
    UISettings::values.single_window_mode = ui.action_Single_Window_Mode->isChecked();
    UISettings::values.fullscreen = ui.action_Fullscreen->isChecked();
    UISettings::values.display_titlebar = ui.action_Display_Dock_Widget_Headers->isChecked();
    UISettings::values.show_filter_bar = ui.action_Show_Filter_Bar->isChecked();
    UISettings::values.show_status_bar = ui.action_Show_Status_Bar->isChecked();
    UISettings::values.first_start = false;

    game_list->SaveInterfaceLayout();
    SaveHotkeys();

    // Shutdown session if the emu thread is active...
    if (emu_thread != nullptr)
        ShutdownGame();

    render_window->close();

    QWidget::closeEvent(event);
}

static bool IsSingleFileDropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    return mimeData->hasUrls() && mimeData->urls().length() == 1;
}

void GMainWindow::dropEvent(QDropEvent* event) {
    if (IsSingleFileDropEvent(event) && ConfirmChangeGame()) {
        const QMimeData* mimeData = event->mimeData();
        QString filename = mimeData->urls().at(0).toLocalFile();
        BootGame(filename);
    }
}

void GMainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (IsSingleFileDropEvent(event)) {
        event->acceptProposedAction();
    }
}

void GMainWindow::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

bool GMainWindow::ConfirmChangeGame() {
    if (emu_thread == nullptr)
        return true;

    auto answer = QMessageBox::question(
        this, tr("yuzu"),
        tr("Are you sure you want to stop the emulation? Any unsaved progress will be lost."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer != QMessageBox::No;
}

void GMainWindow::filterBarSetChecked(bool state) {
    ui.action_Show_Filter_Bar->setChecked(state);
    emit(OnToggleFilterBar());
}

void GMainWindow::UpdateUITheme() {
    QStringList theme_paths(default_theme_paths);
    if (UISettings::values.theme != UISettings::themes[0].second &&
        !UISettings::values.theme.isEmpty()) {
        QString theme_uri(":" + UISettings::values.theme + "/style.qss");
        QFile f(theme_uri);
        if (!f.exists()) {
            LOG_ERROR(Frontend, "Unable to set style, stylesheet file not found");
        } else {
            f.open(QFile::ReadOnly | QFile::Text);
            QTextStream ts(&f);
            qApp->setStyleSheet(ts.readAll());
            GMainWindow::setStyleSheet(ts.readAll());
        }
        theme_paths.append(QStringList{":/icons/default", ":/icons/" + UISettings::values.theme});
        QIcon::setThemeName(":/icons/" + UISettings::values.theme);
    } else {
        qApp->setStyleSheet("");
        GMainWindow::setStyleSheet("");
        theme_paths.append(QStringList{":/icons/default"});
        QIcon::setThemeName(":/icons/default");
    }
    QIcon::setThemeSearchPaths(theme_paths);
    emit UpdateThemedIcons();
}

#ifdef main
#undef main
#endif

static void InitializeLogging() {
    Log::Filter log_filter;
    log_filter.ParseFilterString(Settings::values.log_filter);
    Log::SetGlobalFilter(log_filter);

    const std::string& log_dir = FileUtil::GetUserPath(FileUtil::UserPath::LogDir);
    FileUtil::CreateFullPath(log_dir);
    Log::AddBackend(std::make_unique<Log::FileBackend>(log_dir + LOG_FILE));
}

int main(int argc, char* argv[]) {
    MicroProfileOnThreadCreate("Frontend");
    SCOPE_EXIT({ MicroProfileShutdown(); });

    // Init settings params
    QCoreApplication::setOrganizationName("yuzu team");
    QCoreApplication::setApplicationName("yuzu");

    QApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity);
    QApplication app(argc, argv);

    // Qt changes the locale and causes issues in float conversion using std::to_string() when
    // generating shaders
    setlocale(LC_ALL, "C");

    GMainWindow main_window;
    // After settings have been loaded by GMainWindow, apply the filter
    InitializeLogging();
    main_window.show();
    return app.exec();
}
