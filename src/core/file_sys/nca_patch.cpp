// Copyright 2018 yuzu emulator team
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "core/crypto/aes_util.h"
#include "core/file_sys/nca_patch.h"

namespace FileSys {

BKTR::BKTR(VirtualFile base_romfs_, VirtualFile bktr_romfs_, RelocationBlock relocation_,
           std::vector<RelocationBucket> relocation_buckets_, SubsectionBlock subsection_,
           std::vector<SubsectionBucket> subsection_buckets_, bool is_encrypted_,
           Core::Crypto::Key128 key_, u64 base_offset_, u64 ivfc_offset_,
           std::array<u8, 8> section_ctr_)
    : base_romfs(std::move(base_romfs_)), bktr_romfs(std::move(bktr_romfs_)),
      relocation(relocation_), relocation_buckets(std::move(relocation_buckets_)),
      subsection(subsection_), subsection_buckets(std::move(subsection_buckets_)),
      encrypted(is_encrypted_), key(key_), base_offset(base_offset_), ivfc_offset(ivfc_offset_),
      section_ctr(section_ctr_) {
    for (size_t i = 0; i < relocation.number_buckets - 1; ++i) {
        relocation_buckets[i].entries.push_back({relocation.base_offsets[i + 1], 0, 0});
    }

    for (size_t i = 0; i < subsection.number_buckets - 1; ++i) {
        subsection_buckets[i].entries.push_back({subsection_buckets[i + 1].entries[0].address_patch,
                                                 {0},
                                                 subsection_buckets[i + 1].entries[0].ctr});
    }

    relocation_buckets.back().entries.push_back({relocation.size, 0, 0});
}

BKTR::~BKTR() = default;

size_t BKTR::Read(u8* data, size_t length, size_t offset) const {
    // Read out of bounds.
    if (offset >= relocation.size)
        return 0;
    const auto relocation = GetRelocationEntry(offset);
    const auto section_offset = offset - relocation.address_patch + relocation.address_source;
    const auto bktr_read = relocation.from_patch;

    const auto next_relocation = GetNextRelocationEntry(offset);

    if (offset + length >= next_relocation.address_patch) {
        const u64 partition = next_relocation.address_patch - offset;
        return Read(data, partition, offset) +
               Read(data + partition, length - partition, offset + partition);
    }

    if (!bktr_read) {
        ASSERT_MSG(section_offset > ivfc_offset, "Offset calculation negative.");
        return base_romfs->Read(data, length, section_offset);
    }

    if (!encrypted) {
        return bktr_romfs->Read(data, length, section_offset);
    }

    const auto subsection = GetSubsectionEntry(section_offset);
    Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(key, Core::Crypto::Mode::CTR);

    // Calculate AES IV
    std::vector<u8> iv(16);
    auto subsection_ctr = subsection.ctr;
    auto offset_iv = section_offset + base_offset;
    for (size_t i = 0; i < section_ctr.size(); ++i)
        iv[i] = section_ctr[0x8 - i - 1];
    offset_iv >>= 4;
    for (size_t i = 0; i < sizeof(u64); ++i) {
        iv[0xF - i] = static_cast<u8>(offset_iv & 0xFF);
        offset_iv >>= 8;
    }
    for (size_t i = 0; i < sizeof(u32); ++i) {
        iv[0x7 - i] = static_cast<u8>(subsection_ctr & 0xFF);
        subsection_ctr >>= 8;
    }
    cipher.SetIV(iv);

    const auto next_subsection = GetNextSubsectionEntry(section_offset);

    if (section_offset + length > next_subsection.address_patch) {
        const u64 partition = next_subsection.address_patch - section_offset;
        return Read(data, partition, offset) +
               Read(data + partition, length - partition, offset + partition);
    }

    const auto block_offset = section_offset & 0xF;
    if (block_offset != 0) {
        auto block = bktr_romfs->ReadBytes(0x10, section_offset & ~0xF);
        cipher.Transcode(block.data(), block.size(), block.data(), Core::Crypto::Op::Decrypt);
        if (length + block_offset < 0x10) {
            std::memcpy(data, block.data() + block_offset, std::min(length, block.size()));
            return std::min(length, block.size());
        }

        const auto read = 0x10 - block_offset;
        std::memcpy(data, block.data() + block_offset, read);
        return read + Read(data + read, length - read, offset + read);
    }

    const auto raw_read = bktr_romfs->Read(data, length, section_offset);
    cipher.Transcode(data, raw_read, data, Core::Crypto::Op::Decrypt);
    return raw_read;
}

template <bool Subsection, typename BlockType, typename BucketType>
std::pair<size_t, size_t> BKTR::SearchBucketEntry(u64 offset, BlockType block,
                                                  BucketType buckets) const {
    if constexpr (Subsection) {
        const auto last_bucket = buckets[block.number_buckets - 1];
        if (offset >= last_bucket.entries[last_bucket.number_entries].address_patch)
            return {block.number_buckets - 1, last_bucket.number_entries};
    } else {
        ASSERT_MSG(offset <= block.size, "Offset is out of bounds in BKTR relocation block.");
    }

    size_t bucket_id = std::count_if(block.base_offsets.begin() + 1,
                                     block.base_offsets.begin() + block.number_buckets,
                                     [&offset](u64 base_offset) { return base_offset < offset; });

    const auto bucket = buckets[bucket_id];

    if (bucket.number_entries == 1)
        return {bucket_id, 0};

    size_t low = 0;
    size_t mid = 0;
    size_t high = bucket.number_entries - 1;
    while (low <= high) {
        mid = (low + high) / 2;
        if (bucket.entries[mid].address_patch > offset) {
            high = mid - 1;
        } else {
            if (mid == bucket.number_entries - 1 ||
                bucket.entries[mid + 1].address_patch > offset) {
                return {bucket_id, mid};
            }

            low = mid + 1;
        }
    }

    UNREACHABLE_MSG("Offset could not be found in BKTR block.");
}

RelocationEntry BKTR::GetRelocationEntry(u64 offset) const {
    const auto res = SearchBucketEntry<false>(offset, relocation, relocation_buckets);
    return relocation_buckets[res.first].entries[res.second];
}

RelocationEntry BKTR::GetNextRelocationEntry(u64 offset) const {
    const auto res = SearchBucketEntry<false>(offset, relocation, relocation_buckets);
    const auto bucket = relocation_buckets[res.first];
    if (res.second + 1 < bucket.entries.size())
        return bucket.entries[res.second + 1];
    return relocation_buckets[res.first + 1].entries[0];
}

SubsectionEntry BKTR::GetSubsectionEntry(u64 offset) const {
    const auto res = SearchBucketEntry<true>(offset, subsection, subsection_buckets);
    return subsection_buckets[res.first].entries[res.second];
}

SubsectionEntry BKTR::GetNextSubsectionEntry(u64 offset) const {
    const auto res = SearchBucketEntry<true>(offset, subsection, subsection_buckets);
    const auto bucket = subsection_buckets[res.first];
    if (res.second + 1 < bucket.entries.size())
        return bucket.entries[res.second + 1];
    return subsection_buckets[res.first + 1].entries[0];
}

std::string BKTR::GetName() const {
    return base_romfs->GetName();
}

size_t BKTR::GetSize() const {
    return relocation.size;
}

bool BKTR::Resize(size_t new_size) {
    return false;
}

std::shared_ptr<VfsDirectory> BKTR::GetContainingDirectory() const {
    return base_romfs->GetContainingDirectory();
}

bool BKTR::IsWritable() const {
    return false;
}

bool BKTR::IsReadable() const {
    return true;
}

size_t BKTR::Write(const u8* data, size_t length, size_t offset) {
    return 0;
}

bool BKTR::Rename(std::string_view name) {
    return base_romfs->Rename(name);
}

} // namespace FileSys
