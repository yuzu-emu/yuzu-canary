// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/swap.h"
#include "core/file_sys/ips_layer.h"
#include "core/file_sys/vfs_vector.h"

namespace FileSys {

enum class IPSFileType {
    IPS,
    IPS32,
    Error,
};

static IPSFileType IdentifyMagic(const std::vector<u8>& magic) {
    if (magic.size() != 5)
        return IPSFileType::Error;
    if (magic == std::vector<u8>{'P', 'A', 'T', 'C', 'H'})
        return IPSFileType::IPS;
    if (magic == std::vector<u8>{'I', 'P', 'S', '3', '2'})
        return IPSFileType::IPS32;
    return IPSFileType::Error;
}

VirtualFile PatchIPS(const VirtualFile& in, const VirtualFile& ips) {
    if (in == nullptr || ips == nullptr)
        return nullptr;

    const auto type = IdentifyMagic(ips->ReadBytes(0x5));
    if (type == IPSFileType::Error)
        return nullptr;

    auto in_data = in->ReadAllBytes();

    std::vector<u8> temp(type == IPSFileType::IPS ? 3 : 4);
    u64 offset = 5; // After header
    while (ips->Read(temp.data(), temp.size(), offset) == temp.size()) {
        offset += temp.size();
        if (type == IPSFileType::IPS32 && temp == std::vector<u8>{'E', 'E', 'O', 'F'} ||
            type == IPSFileType::IPS && temp == std::vector<u8>{'E', 'O', 'F'}) {
            break;
        }

        u32 real_offset{};
        if (type == IPSFileType::IPS32)
            real_offset = (temp[0] << 24) | (temp[1] << 16) | (temp[2] << 8) | temp[3];
        else
            real_offset = (temp[0] << 16) | (temp[1] << 8) | temp[2];

        u16 data_size{};
        if (ips->ReadObject(&data_size, offset) != sizeof(u16))
            return nullptr;
        data_size = Common::swap16(data_size);
        offset += sizeof(u16);

        if (data_size == 0) { // RLE
            u16 rle_size{};
            if (ips->ReadObject(&rle_size, offset) != sizeof(u16))
                return nullptr;
            rle_size = Common::swap16(data_size);
            offset += sizeof(u16);

            const auto data = ips->ReadByte(offset++);
            if (data == boost::none)
                return nullptr;

            if (real_offset + rle_size > in_data.size())
                rle_size = in_data.size() - real_offset;
            std::memset(in_data.data() + real_offset, data.get(), rle_size);
        } else { // Standard Patch
            auto read = data_size;
            if (real_offset + read > in_data.size())
                read = in_data.size() - real_offset;
            if (ips->Read(in_data.data() + real_offset, read, offset) != data_size)
                return nullptr;
            offset += data_size;
        }
    }

    if (temp != std::vector<u8>{'E', 'E', 'O', 'F'} && temp != std::vector<u8>{'E', 'O', 'F'})
        return nullptr;
    return std::make_shared<VectorVfsFile>(in_data, in->GetName(), in->GetContainingDirectory());
}

} // namespace FileSys
