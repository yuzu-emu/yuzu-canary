// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <fmt/ostream.h>

#include "common/common_paths.h"
#include "common/detached_tasks.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scm_rev.h"
#include "common/scope_exit.h"
#include "common/string_util.h"
#include "common/telemetry.h"
#include "core/core.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/vfs_real.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"
#include "core/settings.h"
#include "core/telemetry_session.h"
#include "video_core/renderer_base.h"
#include "yuzu_tester/config.h"
#include "yuzu_tester/emu_window/emu_window_sdl2_hide.h"
#include "yuzu_tester/service/yuzutest.h"

#include <getopt.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
// windows.h needs to be included before shellapi.h
#include <windows.h>

#include <shellapi.h>
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia and AMD drivers to use the dedicated GPU by default on laptops with switchable
// graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

static void PrintHelp(const char* argv0) {
    std::cout << "Usage: " << argv0
              << " [options] <filename>\n"
                 "-h, --help            Display this help and exit\n"
                 "-v, --version         Output version information and exit\n"
                 "-d, --datastring      Pass following string as data to test service command #2\n"
                 "-l, --log             Log to console in addition to file (will log to file only "
                 "by default)\n";
}

static void PrintVersion() {
    std::cout << "yuzu [Test Utility] " << Common::g_scm_branch << " " << Common::g_scm_desc
              << std::endl;
}

static void InitializeLogging(bool console) {
    Log::Filter log_filter(Log::Level::Debug);
    log_filter.ParseFilterString(Settings::values.log_filter);
    Log::SetGlobalFilter(log_filter);

    if (console)
        Log::AddBackend(std::make_unique<Log::ColorConsoleBackend>());

    const std::string& log_dir = FileUtil::GetUserPath(FileUtil::UserPath::LogDir);
    FileUtil::CreateFullPath(log_dir);
    Log::AddBackend(std::make_unique<Log::FileBackend>(log_dir + LOG_FILE));
#ifdef _WIN32
    Log::AddBackend(std::make_unique<Log::DebuggerBackend>());
#endif
}

/// Application entry point
int main(int argc, char** argv) {
    Common::DetachedTasks detached_tasks;
    Config config;

    int option_index = 0;

    char* endarg;
#ifdef _WIN32
    int argc_w;
    auto argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);

    if (argv_w == nullptr) {
        std::cout << "Failed to get command line arguments" << std::endl;
        return -1;
    }
#endif
    std::string filepath;

    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"datastring", optional_argument, 0, 'd'},
        {"log", no_argument, 0, 'l'},
        {0, 0, 0, 0},
    };

    bool console_log = false;
    std::string datastring;

    while (optind < argc) {
        int arg = getopt_long(argc, argv, "hvdl::", long_options, &option_index);
        if (arg != -1) {
            switch (static_cast<char>(arg)) {
            case 'h':
                PrintHelp(argv[0]);
                return 0;
            case 'v':
                PrintVersion();
                return 0;
            case 'd':
                datastring = argv[optind];
                ++optind;
                break;
            case 'l':
                console_log = true;
                break;
            }
        } else {
#ifdef _WIN32
            filepath = Common::UTF16ToUTF8(argv_w[optind]);
#else
            filepath = argv[optind];
#endif
            optind++;
        }
    }

    InitializeLogging(console_log);

#ifdef _WIN32
    LocalFree(argv_w);
#endif

    MicroProfileOnThreadCreate("EmuThread");
    SCOPE_EXIT({ MicroProfileShutdown(); });

    if (filepath.empty()) {
        LOG_CRITICAL(Frontend, "Failed to load application: No application specified");
        std::cout << "Failed to load application: No application specified" << std::endl;
        PrintHelp(argv[0]);
        return -1;
    }

    Settings::values.use_gdbstub = false;
    Settings::Apply();

    std::unique_ptr<EmuWindow_SDL2_Hide> emu_window{std::make_unique<EmuWindow_SDL2_Hide>()};

    if (!Settings::values.use_multi_core) {
        // Single core mode must acquire OpenGL context for entire emulation session
        emu_window->MakeCurrent();
    }

    bool finished = false;
    int return_value = 0;
    const auto callback = [&finished,
                           &return_value](std::vector<Service::Yuzu::TestResult> results) {
        finished = true;
        return_value = 0;

        const auto len =
            std::max<u64>(std::max_element(results.begin(), results.end(),
                                           [](const auto& lhs, const auto& rhs) {
                                               return lhs.name.size() < rhs.name.size();
                                           })
                              ->name.size(),
                          9ull);

        std::size_t passed = 0;
        std::size_t failed = 0;

        std::cout << fmt::format("Result [Res Code] | {:<{}} | Extra Data", "Test Name", len)
                  << std::endl;

        for (const auto& res : results) {
            const auto main_res = res.code == 0 ? "PASSED" : "FAILED";
            if (res.code == 0)
                ++passed;
            else
                ++failed;
            std::cout << fmt::format("{} [{:08X}] | {:<{}} | {}", main_res, res.code, res.name, len,
                                     res.data)
                      << std::endl;
        }

        std::cout << std::endl
                  << fmt::format("{:4d} Passed | {:4d} Failed | {:4d} Total | {:2.2f} Passed Ratio",
                                 passed, failed, passed + failed,
                                 static_cast<float>(passed) / (passed + failed))
                  << std::endl
                  << (failed == 0 ? "PASSED" : "FAILED") << std::endl;

        if (failed > 0)
            return_value = -1;
    };

    Core::System& system{Core::System::GetInstance()};
    system.SetFilesystem(std::make_shared<FileSys::RealVfsFilesystem>());
    Service::FileSystem::CreateFactories(*system.GetFilesystem());

    SCOPE_EXIT({ system.Shutdown(); });

    const Core::System::ResultStatus load_result{system.Load(*emu_window, filepath)};

    switch (load_result) {
    case Core::System::ResultStatus::ErrorGetLoader:
        LOG_CRITICAL(Frontend, "Failed to obtain loader for %s!", filepath.c_str());
        return -1;
    case Core::System::ResultStatus::ErrorLoader:
        LOG_CRITICAL(Frontend, "Failed to load ROM!");
        return -1;
    case Core::System::ResultStatus::ErrorNotInitialized:
        LOG_CRITICAL(Frontend, "CPUCore not initialized");
        return -1;
    case Core::System::ResultStatus::ErrorSystemMode:
        LOG_CRITICAL(Frontend, "Failed to determine system mode!");
        return -1;
    case Core::System::ResultStatus::ErrorVideoCore:
        LOG_CRITICAL(Frontend, "Failed to initialize VideoCore!");
        return -1;
    case Core::System::ResultStatus::Success:
        break; // Expected case
    default:
        if (static_cast<u32>(load_result) >
            static_cast<u32>(Core::System::ResultStatus::ErrorLoader)) {
            const u16 loader_id = static_cast<u16>(Core::System::ResultStatus::ErrorLoader);
            const u16 error_id = static_cast<u16>(load_result) - loader_id;
            LOG_CRITICAL(Frontend,
                         "While attempting to load the ROM requested, an error occured. Please "
                         "refer to the yuzu wiki for more information or the yuzu discord for "
                         "additional help.\n\nError Code: {:04X}-{:04X}\nError Description: {}",
                         loader_id, error_id, static_cast<Loader::ResultStatus>(error_id));
        }
    }

    Service::Yuzu::InstallInterfaces(system.ServiceManager(), datastring, callback);

    system.TelemetrySession().AddField(Telemetry::FieldType::App, "Frontend", "SDLHideTester");

    system.Renderer().Rasterizer().LoadDiskResources();

    while (!finished) {
        system.RunLoop();
    }

    detached_tasks.WaitForAllTasks();
    return return_value;
}
