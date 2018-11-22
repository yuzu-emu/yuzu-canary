# Merge log

Scroll down for the original README.md!

Base revision: f926559ef40bfa34bee4dabeb84b915bdb59a893

|Pull Request|Commit|Title|Author|Merged?|
|----|----|----|----|----|
|[1](https://github.com/yuzu-emu/yuzu-canary/pull/1)|[a8dfe54](https://github.com/yuzu-emu/yuzu-canary/pull/1/files/)|Canary Base|[chris062689](https://github.com/chris062689)|Yes|
|[1770](https://github.com/yuzu-emu/yuzu/pull/1770)|[4b7733c](https://github.com/yuzu-emu/yuzu/pull/1770/files/)|applets: Add StubApplet and use it as fallback when AppletId is not implemented|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[1765](https://github.com/yuzu-emu/yuzu/pull/1765)|[1bee6db](https://github.com/yuzu-emu/yuzu/pull/1765/files/)|audout_u: Add support for multiple IAudioOut streams.|[bunnei](https://github.com/bunnei)|Yes|
|[1764](https://github.com/yuzu-emu/yuzu/pull/1764)|[b027d7c](https://github.com/yuzu-emu/yuzu/pull/1764/files/)|macro_interpreter: Implement AddWithCarry and SubtractWithBorrow.|[bunnei](https://github.com/bunnei)|Yes|
|[1762](https://github.com/yuzu-emu/yuzu/pull/1762)|[7f10db1](https://github.com/yuzu-emu/yuzu/pull/1762/files/)|nvhost_ctrl_gpu: Implement IoctlGetGpuTime.|[bunnei](https://github.com/bunnei)|Yes|
|[1760](https://github.com/yuzu-emu/yuzu/pull/1760)|[d92afc7](https://github.com/yuzu-emu/yuzu/pull/1760/files/)|gl_shader_decompiler: Implement R2P_IMM|[ReinUsesLisp](https://github.com/ReinUsesLisp)|Yes|
|[1744](https://github.com/yuzu-emu/yuzu/pull/1744)|[cfbae58](https://github.com/yuzu-emu/yuzu/pull/1744/files/)|shader_cache: Only lock covered instructions.|[degasus](https://github.com/degasus)|Yes|
|[1737](https://github.com/yuzu-emu/yuzu/pull/1737)|[eb36463](https://github.com/yuzu-emu/yuzu/pull/1737/files/)|Implemented Fast Layered Copy|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[1736](https://github.com/yuzu-emu/yuzu/pull/1736)|[76de2d0](https://github.com/yuzu-emu/yuzu/pull/1736/files/)|Added predicate comparison LessEqualWithNan|[Hexagon12](https://github.com/Hexagon12)|Yes|
|[1735](https://github.com/yuzu-emu/yuzu/pull/1735)|[fab4934](https://github.com/yuzu-emu/yuzu/pull/1735/files/)|Texture decoder: Implemented Tile Width Spacing|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[1725](https://github.com/yuzu-emu/yuzu/pull/1725)|[0368260](https://github.com/yuzu-emu/yuzu/pull/1725/files/)|Update OpenGL's backend version from 3.3 to 4.3|[FernandoS27](https://github.com/FernandoS27)|Yes|
|[1723](https://github.com/yuzu-emu/yuzu/pull/1723)|[97f5c4f](https://github.com/yuzu-emu/yuzu/pull/1723/files/)|RFC: gl_rasterizer: Skip VB upload if the state is clean.|[degasus](https://github.com/degasus)|Yes|
|[1703](https://github.com/yuzu-emu/yuzu/pull/1703)|[09d8109](https://github.com/yuzu-emu/yuzu/pull/1703/files/)|[DO NOT MERGE] nvdrv: Stub nvdec/vic ioctls to bypass nvdec movies|[DarkLordZach](https://github.com/DarkLordZach)|Yes|
|[1698](https://github.com/yuzu-emu/yuzu/pull/1698)|[e6d205e](https://github.com/yuzu-emu/yuzu/pull/1698/files/)|Initial implementation of MapPhysicalMemory|[ogniK5377](https://github.com/ogniK5377)|Yes|
|[1603](https://github.com/yuzu-emu/yuzu/pull/1603)|[eadf2c0](https://github.com/yuzu-emu/yuzu/pull/1603/files/)|Preliminary implementation of LDG (continuation)|[bunnei](https://github.com/bunnei)|Yes|
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
