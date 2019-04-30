// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

namespace DefaultINI {

const char* sdl2_config_file = R"(
[Core]
# Whether to use the Just-In-Time (JIT) compiler for CPU emulation
# 0: Interpreter (slow), 1 (default): JIT (fast)
use_cpu_jit =

# Whether to use multi-core for CPU emulation
# 0 (default): Disabled, 1: Enabled
use_multi_core=

[Renderer]
# Whether to use software or hardware rendering.
# 0: Software, 1 (default): Hardware
use_hw_renderer =

# Whether to use the Just-In-Time (JIT) compiler for shader emulation
# 0: Interpreter (slow), 1 (default): JIT (fast)
use_shader_jit =

# Resolution scale factor
# 0: Auto (scales resolution to window size), 1: Native Switch screen resolution, Otherwise a scale
# factor for the Switch resolution
resolution_factor =

# Whether to enable V-Sync (caps the framerate at 60FPS) or not.
# 0 (default): Off, 1: On
use_vsync =

# Whether to use disk based shader cache
# 0 (default): Off, 1 : On
use_disk_shader_cache =

# Whether to use accurate GPU emulation
# 0 (default): Off (fast), 1 : On (slow)
use_accurate_gpu_emulation =

# Whether to use asynchronous GPU emulation
# 0 : Off (slow), 1 (default): On (fast)
use_asynchronous_gpu_emulation =

# The clear color for the renderer. What shows up on the sides of the bottom screen.
# Must be in range of 0.0-1.0. Defaults to 1.0 for all.
bg_red =
bg_blue =
bg_green =

[Layout]
# Layout for the screen inside the render window.
# 0 (default): Default Top Bottom Screen, 1: Single Screen Only, 2: Large Screen Small Screen
layout_option =

# Toggle custom layout (using the settings below) on or off.
# 0 (default): Off, 1: On
custom_layout =

# Screen placement when using Custom layout option
# 0x, 0y is the top left corner of the render window.
custom_top_left =
custom_top_top =
custom_top_right =
custom_top_bottom =
custom_bottom_left =
custom_bottom_top =
custom_bottom_right =
custom_bottom_bottom =

# Swaps the prominent screen with the other screen.
# For example, if Single Screen is chosen, setting this to 1 will display the bottom screen instead of the top screen.
# 0 (default): Top Screen is prominent, 1: Bottom Screen is prominent
swap_screen =

[Data Storage]
# Whether to create a virtual SD card.
# 1 (default): Yes, 0: No
use_virtual_sd =

[System]
# Whether the system is docked
# 1: Yes, 0 (default): No
use_docked_mode =

# Allow the use of NFC in games
# 1 (default): Yes, 0 : No
enable_nfc =

# Sets the seed for the RNG generator built into the switch
# rng_seed will be ignored and randomly generated if rng_seed_enabled is false
rng_seed_enabled =
rng_seed =

# Sets the current time (in seconds since 12:00 AM Jan 1, 1970) that will be used by the time service
# This will auto-increment, with the time set being the time the game is started
# This override will only occur if custom_rtc_enabled is true, otherwise the current time is used
custom_rtc_enabled =
custom_rtc =

# Sets the account username, max length is 32 characters
# yuzu (default)
username = yuzu

# Sets the systems language index
# 0: Japanese, 1: English (default), 2: French, 3: German, 4: Italian, 5: Spanish, 6: Chinese,
# 7: Korean, 8: Dutch, 9: Portuguese, 10: Russian, 11: Taiwanese, 12: British English, 13: Canadian French,
# 14: Latin American Spanish, 15: Simplified Chinese, 16: Traditional Chinese
language_index =

# The system region that yuzu will use during emulation
# -1: Auto-select (default), 0: Japan, 1: USA, 2: Europe, 3: Australia, 4: China, 5: Korea, 6: Taiwan
region_value =

[Miscellaneous]
# A filter which removes logs below a certain logging level.
# Examples: *:Debug Kernel.SVC:Trace Service.*:Critical
log_filter = *:Trace

[Debugging]
# Arguments to be passed to argv/argc in the emulated program. It is preferable to use the testing service datastring
program_args=
# Determines whether or not yuzu will dump the ExeFS of all games it attempts to load while loading them
dump_exefs=false
# Determines whether or not yuzu will dump all NSOs it attempts to load while loading them
dump_nso=false

[WebService]
# Whether or not to enable telemetry
# 0: No, 1 (default): Yes
enable_telemetry =
# URL for Web API
web_api_url = https://api.yuzu-emu.org
# Username and token for yuzu Web Service
# See https://profile.yuzu-emu.org/ for more info
yuzu_username =
yuzu_token =

[AddOns]
# Used to disable add-ons
# List of title IDs of games that will have add-ons disabled (separated by '|'):
title_ids =
# For each title ID, have a key/value pair called `disabled_<title_id>` equal to the names of the add-ons to disable (sep. by '|')
# e.x. disabled_0100000000010000 = Update|DLC <- disables Updates and DLC on Super Mario Odyssey
)";
}
