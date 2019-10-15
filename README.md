# Merge log

Scroll down for the original README.md!

Base revision: cab2619aeb111bd6c5dbcc5adc0d2e8154a1e8fc

|Pull Request|Commit|Title|Author|Merged?|
|----|----|----|----|----|
|[1](https://github.com/yuzu-emu/yuzu-canary/pull/1)|[a8dfe54](https://github.com/yuzu-emu/yuzu-canary/pull/1/files/)|Canary Base|[chris062689](https://github.com/chris062689)|Yes|
|[2945](https://github.com/yuzu-emu/yuzu/pull/2945)|[d8497fe](https://github.com/yuzu-emu/yuzu/pull/2945/files/)|nifm: Only return that there's an internet connection when there's a BCATServer|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[2914](https://github.com/yuzu-emu/yuzu/pull/2914)|[38137a7](https://github.com/yuzu-emu/yuzu/pull/2914/files/)|Fermi2D: limit blit area to only available area|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[2912](https://github.com/yuzu-emu/yuzu/pull/2912)|[cfc2f30](https://github.com/yuzu-emu/yuzu/pull/2912/files/)|General fixes to Async GPU|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[2859](https://github.com/yuzu-emu/yuzu/pull/2859)|[c1e9ca4](https://github.com/yuzu-emu/yuzu/pull/2859/files/)|hid: Stub SetNpadJoyAssignmentModeSingle and GetNpadHandheldActivationMode|[Morph1984](https://github.com/Morph1984)|Yes|
|[2710](https://github.com/yuzu-emu/yuzu/pull/2710)|[abb2c5f](https://github.com/yuzu-emu/yuzu/pull/2710/files/)|am/kernel: Fix various bugs related to applet signalling and software keyboard|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[2542](https://github.com/yuzu-emu/yuzu/pull/2542)|[7aeb676](https://github.com/yuzu-emu/yuzu/pull/2542/files/)|lbl: Implement brightness and backlight services|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[2365](https://github.com/yuzu-emu/yuzu/pull/2365)|[b6c6758](https://github.com/yuzu-emu/yuzu/pull/2365/files/)|Workaround to Mutex Corruption|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[1703](https://github.com/yuzu-emu/yuzu/pull/1703)|[09d8109](https://github.com/yuzu-emu/yuzu/pull/1703/files/)|nvdrv: Stub nvdec/vic ioctls to bypass nvdec movies|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[1340](https://github.com/yuzu-emu/yuzu/pull/1340)|[c359c00](https://github.com/yuzu-emu/yuzu/pull/1340/files/)|Implement a Better Ignore Assert|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[1012](https://github.com/yuzu-emu/yuzu/pull/1012)|[7b98ac7](https://github.com/yuzu-emu/yuzu/pull/1012/files/)|filesystem: Create directory if it dosen't exist on open|[DarkLordZach](https://github.com/DarkLordZach)|Yes|


End of merge log. You can find the original README.md below the break.

------

yuzu emulator
=============
[![Travis CI Build Status](https://travis-ci.org/yuzu-emu/yuzu.svg?branch=master)](https://travis-ci.org/yuzu-emu/yuzu)
[![Azure Mainline CI Build Status](https://dev.azure.com/yuzu-emu/yuzu/_apis/build/status/yuzu%20mainline?branchName=master)](https://dev.azure.com/yuzu-emu/yuzu/)

yuzu is an experimental open-source emulator for the Nintendo Switch from the creators of [Citra](https://citra-emu.org/).

It is written in C++ with portability in mind, with builds actively maintained for Windows and Linux. The emulator is capable of running several commercial games.

yuzu only emulates a subset of Switch hardware and therefore most commercial games **do not** run at full speed or are not fully functional.

Do you want to check which games are compatible and which ones are not? Please visit our [Compatibility page](https://yuzu-emu.org/game/)!

yuzu is licensed under the GPLv2 (or any later version). Refer to the license.txt file included.

Check out our [website](https://yuzu-emu.org/)!

For development discussion, please join us on [Discord](https://discord.gg/XQV6dn9).

### Development

Most of the development happens on GitHub. It's also where [our central repository](https://github.com/yuzu-emu/yuzu) is hosted.

If you want to contribute please take a look at the [Contributor's Guide](CONTRIBUTING.md) and [Developer Information](https://github.com/yuzu-emu/yuzu/wiki/Developer-Information). You should as well contact any of the developers on Discord in order to know about the current state of the emulator.

### Building

* __Windows__: [Windows Build](https://github.com/yuzu-emu/yuzu/wiki/Building-For-Windows)
* __Linux__: [Linux Build](https://github.com/yuzu-emu/yuzu/wiki/Building-For-Linux)
* __macOS__: [macOS Build](https://github.com/yuzu-emu/yuzu/wiki/Building-for-macOS)


### Support
We happily accept monetary donations or donated games and hardware. Please see our [donations page](https://yuzu-emu.org/donate/) for more information on how you can contribute to yuzu. Any donations received will go towards things like:
* Switch consoles to explore and reverse-engineer the hardware
* Switch games for testing, reverse-engineering, and implementing new features
* Web hosting and infrastructure setup
* Software licenses (e.g. Visual Studio, IDA Pro, etc.)
* Additional hardware (e.g. GPUs as-needed to improve rendering support, other peripherals to add support for, etc.)

We also more than gladly accept used Switch consoles, preferably ones with firmware 3.0.0 or lower! If you would like to give yours away, don't hesitate to join our [Discord](https://discord.gg/VXqngT3) and talk to bunnei. You may also contact: donations@yuzu-emu.org.
