# Merge log

Scroll down for the original README.md!

Base revision: 06ac6460d31036dddf7e4ae12355391035cc30ca

|Pull Request|Commit|Title|Author|Merged?|
|----|----|----|----|----|
|[1](https://github.com/yuzu-emu/yuzu-canary/pull/1)|[a8dfe54](https://github.com/yuzu-emu/yuzu-canary/pull/1/files/)|Canary Base|[chris062689](https://github.com/chris062689)|Yes|
|[2237](https://github.com/yuzu-emu/yuzu/pull/2237)|[2eaf6c4](https://github.com/yuzu-emu/yuzu/pull/2237/files/)|gpu: Use host address for caching instead of guest address.|[bunnei](https://github.com/bunnei)|Yes|
|[2224](https://github.com/yuzu-emu/yuzu/pull/2224)|[7ad3d4e](https://github.com/yuzu-emu/yuzu/pull/2224/files/)|hwopus: Leverage multistream API for decoding regular Opus packets|[lioncash](https://github.com/lioncash)|Yes|
|[2221](https://github.com/yuzu-emu/yuzu/pull/2221)|[cd2921a](https://github.com/yuzu-emu/yuzu/pull/2221/files/)|set_sys: Implement GetFirmwareVersion(2) for libnx hosversion|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[2135](https://github.com/yuzu-emu/yuzu/pull/2135)|[2677cc8](https://github.com/yuzu-emu/yuzu/pull/2135/files/)|DO NOT MERGE Implemented a lil edge case on RecreateSurface.|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[2129](https://github.com/yuzu-emu/yuzu/pull/2129)|[a8d4927](https://github.com/yuzu-emu/yuzu/pull/2129/files/)|Correct CNTPCT from using CPU Cycles to using Clock Cycles|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[1933](https://github.com/yuzu-emu/yuzu/pull/1933)|[52ac641](https://github.com/yuzu-emu/yuzu/pull/1933/files/)|file_sys: Implement parser and interpreter for game memory cheats|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[1703](https://github.com/yuzu-emu/yuzu/pull/1703)|[09d8109](https://github.com/yuzu-emu/yuzu/pull/1703/files/)|[DO NOT MERGE] nvdrv: Stub nvdec/vic ioctls to bypass nvdec movies|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[1012](https://github.com/yuzu-emu/yuzu/pull/1012)|[7b98ac7](https://github.com/yuzu-emu/yuzu/pull/1012/files/)|filesystem: Create directory if it dosen't exist on open|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[615](https://github.com/yuzu-emu/yuzu/pull/615)|[c15b401](https://github.com/yuzu-emu/yuzu/pull/615/files/)|Ignore asserts - DO NOT CHECK IN|[bunnei](https://github.com/bunnei)|Yes|


End of merge log. You can find the original README.md below the break.

------

yuzu emulator
=============
[![Travis CI Build Status](https://travis-ci.org/yuzu-emu/yuzu.svg?branch=master)](https://travis-ci.org/yuzu-emu/yuzu)
[![AppVeyor CI Build Status](https://ci.appveyor.com/api/projects/status/77k97svb2usreu68?svg=true)](https://ci.appveyor.com/project/bunnei/yuzu)

yuzu is an experimental open-source emulator for the Nintendo Switch from the creators of [Citra](https://citra-emu.org/).

It is written in C++ with portability in mind, with builds actively maintained for Windows, Linux and macOS. The emulator is currently only useful for homebrew development and research purposes.

yuzu only emulates a subset of Switch hardware and therefore is generally only useful for running/debugging homebrew applications. At this time, yuzu cannot play any commercial games without major problems. yuzu can boot some games, to varying degrees of success.

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
