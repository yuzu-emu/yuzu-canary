// Copyright 2019 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/file_sys/system_archive/mii_model.h"
#include "core/file_sys/vfs_vector.h"

namespace FileSys::SystemArchive {

namespace MiiModelData {

constexpr std::array<u8, 0x10> NFTR_STANDARD{'N',  'F',  'T',  'R',  0x01, 0x00, 0x00, 0x00,
                                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr std::array<u8, 0x10> NFSR_STANDARD{'N',  'F',  'S',  'R',  0x01, 0x00, 0x00, 0x00,
                                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

constexpr auto TEXTURE_LOW_LINEAR = NFTR_STANDARD;
constexpr auto TEXTURE_LOW_SRGB = NFTR_STANDARD;
constexpr auto TEXTURE_MID_LINEAR = NFTR_STANDARD;
constexpr auto TEXTURE_MID_SRGB = NFTR_STANDARD;
constexpr auto SHAPE_HIGH = NFSR_STANDARD;
constexpr auto SHAPE_MID = NFSR_STANDARD;

} // namespace MiiModelData

VirtualDir MiiModel() {
    auto out = std::make_shared<VectorVfsDirectory>(std::vector<VirtualFile>{},
                                                    std::vector<VirtualDir>{}, "data");

    out->AddFile(std::make_shared<ArrayVfsFile<MiiModelData::TEXTURE_LOW_LINEAR.size()>>(
        MiiModelData::TEXTURE_LOW_LINEAR, "NXTextureLowLinear.dat"));
    out->AddFile(std::make_shared<ArrayVfsFile<MiiModelData::TEXTURE_LOW_SRGB.size()>>(
        MiiModelData::TEXTURE_LOW_SRGB, "NXTextureLowSRGB.dat"));
    out->AddFile(std::make_shared<ArrayVfsFile<MiiModelData::TEXTURE_MID_LINEAR.size()>>(
        MiiModelData::TEXTURE_MID_LINEAR, "NXTextureMidLinear.dat"));
    out->AddFile(std::make_shared<ArrayVfsFile<MiiModelData::TEXTURE_MID_SRGB.size()>>(
        MiiModelData::TEXTURE_MID_SRGB, "NXTextureMidSRGB.dat"));
    out->AddFile(std::make_shared<ArrayVfsFile<MiiModelData::SHAPE_HIGH.size()>>(
        MiiModelData::SHAPE_HIGH, "ShapeHigh.dat"));
    out->AddFile(std::make_shared<ArrayVfsFile<MiiModelData::SHAPE_MID.size()>>(
        MiiModelData::SHAPE_MID, "ShapeMid.dat"));

    return std::move(out);
}

} // namespace FileSys::SystemArchive
