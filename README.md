# Merge log

Scroll down for the original README.md!

Base revision: f7090bacc5c796c79fc2f871bce6145a6e9373e5

|Pull Request|Commit|Title|Author|Merged?|
|----|----|----|----|----|
|[1](https://github.com/yuzu-emu/yuzu-canary/pull/1)|[a8dfe54](https://github.com/yuzu-emu/yuzu-canary/pull/1/files/)|Canary Base|[chris062689](https://github.com/chris062689)|Yes|
|[2143](https://github.com/yuzu-emu/yuzu/pull/2143)|[e596885](https://github.com/yuzu-emu/yuzu/pull/2143/files/)|gl_rasterizer_cache: Create texture views for array discrepancies|[ReinUsesLisp](https://github.com/ReinUsesLisp)|Yes|
|[2141](https://github.com/yuzu-emu/yuzu/pull/2141)|[8207461](https://github.com/yuzu-emu/yuzu/pull/2141/files/)|speed up memory page mapping|[Annomatg](https://github.com/Annomatg)|Yes|
|[2139](https://github.com/yuzu-emu/yuzu/pull/2139)|[6dd4097](https://github.com/yuzu-emu/yuzu/pull/2139/files/)|video_core/dma_pusher: The full list of headers at once.|[degasus](https://github.com/degasus)|Yes|
|[2129](https://github.com/yuzu-emu/yuzu/pull/2129)|[a8d4927](https://github.com/yuzu-emu/yuzu/pull/2129/files/)|Correct CNTPCT from using CPU Cycles to using Clock Cycles|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[2121](https://github.com/yuzu-emu/yuzu/pull/2121)|[ed2a3c7](https://github.com/yuzu-emu/yuzu/pull/2121/files/)|Improve the Accuracy of the Rasterizer Cache through a Texception Pass|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[2119](https://github.com/yuzu-emu/yuzu/pull/2119)|[bb41683](https://github.com/yuzu-emu/yuzu/pull/2119/files/)|rasterizer_cache_gl: Only do fast layered copy on the same format.|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[2118](https://github.com/yuzu-emu/yuzu/pull/2118)|[10682ad](https://github.com/yuzu-emu/yuzu/pull/2118/files/)|shader_decompiler: Improve Accuracy of Attribute Interpolation.|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[2055](https://github.com/yuzu-emu/yuzu/pull/2055)|[1642d6f](https://github.com/yuzu-emu/yuzu/pull/2055/files/)|Asynchronous GPU command processing|[bunnei](https://github.com/bunnei)|Yes|
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

yuzu only emulates a subset of Switch hardware and therefore is generally only useful for running/debugging homebrew applications. At this time, yuzu cannot play any commercial games without major problems. yuzu can boot some games, to varying degrees of success, but does not implement any of the necessary GPU features to render 3D graphics.

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
