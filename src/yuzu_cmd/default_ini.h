// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

namespace DefaultINI {

const char* sdl2_config_file = R"(
[Controls]
# The input devices and parameters for each Switch native input
# It should be in the format of "engine:[engine_name],[param1]:[value1],[param2]:[value2]..."
# Escape characters $0 (for ':'), $1 (for ',') and $2 (for '$') can be used in values

# for button input, the following devices are available:
#  - "keyboard" (default) for keyboard input. Required parameters:
#      - "code": the code of the key to bind
#  - "sdl" for joystick input using SDL. Required parameters:
#      - "joystick": the index of the joystick to bind
#      - "button"(optional): the index of the button to bind
#      - "hat"(optional): the index of the hat to bind as direction buttons
#      - "axis"(optional): the index of the axis to bind
#      - "direction"(only used for hat): the direction name of the hat to bind. Can be "up", "down", "left" or "right"
#      - "threshold"(only used for axis): a float value in (-1.0, 1.0) which the button is
#          triggered if the axis value crosses
#      - "direction"(only used for axis): "+" means the button is triggered when the axis value
#          is greater than the threshold; "-" means the button is triggered when the axis value
#          is smaller than the threshold
button_a=
button_b=
button_x=
button_y=
button_lstick=
button_rstick=
button_l=
button_r=
button_zl=
button_zr=
button_plus=
button_minus=
button_dleft=
button_dup=
button_dright=
button_ddown=
button_lstick_left=
button_lstick_up=
button_lstick_right=
button_lstick_down=
button_sl=
button_sr=
button_home=
button_screenshot=

# for analog input, the following devices are available:
#  - "analog_from_button" (default) for emulating analog input from direction buttons. Required parameters:
#      - "up", "down", "left", "right": sub-devices for each direction.
#          Should be in the format as a button input devices using escape characters, for example, "engine$0keyboard$1code$00"
#      - "modifier": sub-devices as a modifier.
#      - "modifier_scale": a float number representing the applied modifier scale to the analog input.
#          Must be in range of 0.0-1.0. Defaults to 0.5
#  - "sdl" for joystick input using SDL. Required parameters:
#      - "joystick": the index of the joystick to bind
#      - "axis_x": the index of the axis to bind as x-axis (default to 0)
#      - "axis_y": the index of the axis to bind as y-axis (default to 1)
lstick=
rstick=

# for motion input, the following devices are available:
#  - "motion_emu" (default) for emulating motion input from mouse input. Required parameters:
#      - "update_period": update period in milliseconds (default to 100)
#      - "sensitivity": the coefficient converting mouse movement to tilting angle (default to 0.01)
motion_device=

# for touch input, the following devices are available:
#  - "emu_window" (default) for emulating touch input from mouse input to the emulation window. No parameters required
touch_device=

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

# Turns on the frame limiter, which will limit frames output to the target game speed
# 0: Off, 1: On (default)
use_frame_limit =

# Limits the speed of the game to run no faster than this value as a percentage of target speed
# 1 - 9999: Speed limit as a percentage of target game speed. 100 (default)
frame_limit =

# Whether to use accurate framebuffers
# 0 (default): Off (fast), 1 : On (slow)
use_accurate_framebuffers =

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

[Audio]
# Which audio output engine to use.
# auto (default): Auto-select, null: No audio output, cubeb: Cubeb audio engine (if available)
output_engine =

# Whether or not to enable the audio-stretching post-processing effect.
# This effect adjusts audio speed to match emulation speed and helps prevent audio stutter,
# at the cost of increasing audio latency.
# 0: No, 1 (default): Yes
enable_audio_stretching =

# Which audio device to use.
# auto (default): Auto-select
output_device =

# Output volume.
# 1.0 (default): 100%, 0.0; mute
volume =

[Data Storage]
# Whether to create a virtual SD card.
# 1 (default): Yes, 0: No
use_virtual_sd =

[System]
# Whether the system is docked
# 1: Yes, 0 (default): No
use_docked_mode =

# Sets the account username, max length is 32 characters
# yuzu (default)
username =

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
# Port for listening to GDB connections.
use_gdbstub=false
gdbstub_port=24689

[WebService]
# Whether or not to enable telemetry
# 0: No, 1 (default): Yes
enable_telemetry =
# Endpoint URL for submitting telemetry data
telemetry_endpoint_url =
# Endpoint URL to verify the username and token
verify_endpoint_url =
# Username and token for yuzu Web Service
# See https://services.citra-emu.org/ for more info
yuzu_username =
yuzu_token =
)";
}
