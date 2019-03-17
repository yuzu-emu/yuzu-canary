// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>

#include "common/page_table.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "core/memory_setup.h"
#include "tests/core/arm/arm_test_common.h"

namespace ArmTests {

TestEnvironment::TestEnvironment(bool mutable_memory_)
    : mutable_memory(mutable_memory_),
      test_memory(std::make_shared<TestMemory>(this)), kernel{Core::System::GetInstance()} {
    auto process = Kernel::Process::Create(Core::System::GetInstance(), "");
    kernel.MakeCurrentProcess(process.get());
    page_table = &process->VMManager().page_table;

    std::fill(page_table->pointers.begin(), page_table->pointers.end(), nullptr);
    page_table->special_regions.clear();
    std::fill(page_table->attributes.begin(), page_table->attributes.end(),
              Common::PageType::Unmapped);

    Memory::MapIoRegion(*page_table, 0x00000000, 0x80000000, test_memory);
    Memory::MapIoRegion(*page_table, 0x80000000, 0x80000000, test_memory);

    Memory::SetCurrentPageTable(page_table);
}

TestEnvironment::~TestEnvironment() {
    Memory::UnmapRegion(*page_table, 0x80000000, 0x80000000);
    Memory::UnmapRegion(*page_table, 0x00000000, 0x80000000);
}

void TestEnvironment::SetMemory64(VAddr vaddr, u64 value) {
    SetMemory32(vaddr + 0, static_cast<u32>(value));
    SetMemory32(vaddr + 4, static_cast<u32>(value >> 32));
}

void TestEnvironment::SetMemory32(VAddr vaddr, u32 value) {
    SetMemory16(vaddr + 0, static_cast<u16>(value));
    SetMemory16(vaddr + 2, static_cast<u16>(value >> 16));
}

void TestEnvironment::SetMemory16(VAddr vaddr, u16 value) {
    SetMemory8(vaddr + 0, static_cast<u8>(value));
    SetMemory8(vaddr + 1, static_cast<u8>(value >> 8));
}

void TestEnvironment::SetMemory8(VAddr vaddr, u8 value) {
    test_memory->data[vaddr] = value;
}

std::vector<WriteRecord> TestEnvironment::GetWriteRecords() const {
    return write_records;
}

void TestEnvironment::ClearWriteRecords() {
    write_records.clear();
}

TestEnvironment::TestMemory::~TestMemory() {}

std::optional<bool> TestEnvironment::TestMemory::IsValidAddress(VAddr addr) {
    return true;
}

std::optional<u8> TestEnvironment::TestMemory::Read8(VAddr addr) {
    const auto iter = data.find(addr);

    if (iter == data.end()) {
        // Some arbitrary data
        return static_cast<u8>(addr);
    }

    return iter->second;
}

std::optional<u16> TestEnvironment::TestMemory::Read16(VAddr addr) {
    return *Read8(addr) | static_cast<u16>(*Read8(addr + 1)) << 8;
}

std::optional<u32> TestEnvironment::TestMemory::Read32(VAddr addr) {
    return *Read16(addr) | static_cast<u32>(*Read16(addr + 2)) << 16;
}

std::optional<u64> TestEnvironment::TestMemory::Read64(VAddr addr) {
    return *Read32(addr) | static_cast<u64>(*Read32(addr + 4)) << 32;
}

bool TestEnvironment::TestMemory::ReadBlock(VAddr src_addr, void* dest_buffer, std::size_t size) {
    VAddr addr = src_addr;
    u8* data = static_cast<u8*>(dest_buffer);

    for (std::size_t i = 0; i < size; i++, addr++, data++) {
        *data = *Read8(addr);
    }

    return true;
}

bool TestEnvironment::TestMemory::Write8(VAddr addr, u8 data) {
    env->write_records.emplace_back(8, addr, data);
    if (env->mutable_memory)
        env->SetMemory8(addr, data);
    return true;
}

bool TestEnvironment::TestMemory::Write16(VAddr addr, u16 data) {
    env->write_records.emplace_back(16, addr, data);
    if (env->mutable_memory)
        env->SetMemory16(addr, data);
    return true;
}

bool TestEnvironment::TestMemory::Write32(VAddr addr, u32 data) {
    env->write_records.emplace_back(32, addr, data);
    if (env->mutable_memory)
        env->SetMemory32(addr, data);
    return true;
}

bool TestEnvironment::TestMemory::Write64(VAddr addr, u64 data) {
    env->write_records.emplace_back(64, addr, data);
    if (env->mutable_memory)
        env->SetMemory64(addr, data);
    return true;
}

bool TestEnvironment::TestMemory::WriteBlock(VAddr dest_addr, const void* src_buffer,
                                             std::size_t size) {
    VAddr addr = dest_addr;
    const u8* data = static_cast<const u8*>(src_buffer);

    for (std::size_t i = 0; i < size; i++, addr++, data++) {
        env->write_records.emplace_back(8, addr, *data);
        if (env->mutable_memory)
            env->SetMemory8(addr, *data);
    }

    return true;
}

} // namespace ArmTests
