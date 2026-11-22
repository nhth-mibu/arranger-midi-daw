#pragma once

#include <cstdint>

/** Addresses of the Roland D-05 user reverb slots 17-32. */
namespace arranger::d05_reverb
{

constexpr int firstSlot = 17;
constexpr int slotCount = 16;
constexpr int bytesPerSlot = 188;

/** The slot's address, for example { 0x03, 0x60, 0x00 } for slot 17. */
std::uint32_t address (int slot);

/** The RQ1 size for one slot, as three 7-bit address bytes. */
void size (std::uint8_t out[3]);

} // namespace arranger::d05_reverb
