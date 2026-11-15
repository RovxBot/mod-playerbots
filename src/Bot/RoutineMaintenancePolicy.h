/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_ROUTINE_MAINTENANCE_POLICY_H
#define PLAYERBOTS_ROUTINE_MAINTENANCE_POLICY_H

#include <array>
#include <atomic>
#include <cstdint>

namespace Playerbots::RoutineMaintenance
{
inline constexpr std::uint32_t ContractVersion = 1;

enum class Action : std::uint8_t
{
    Randomize,
    LevelReset,
    Refresh,
    RandomTeleport,
    GroupTeardown,
    PopulationTrim,
    LegacyGuildAssignment,
    AccountDeletion,
    Count
};

// Process-lifetime function only: no captured world objects, SQL, configuration
// reads, or map-thread mutations. Registration cannot replace an existing owner.
using Provider = bool (*)(std::uint32_t characterGuid, Action action);
inline std::atomic<Provider> PolicyProvider{nullptr};
inline std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(Action::Count)> VetoCounts{};

inline bool RegisterProvider(Provider provider)
{
    if (!provider)
        return false;
    Provider expected = nullptr;
    return PolicyProvider.compare_exchange_strong(expected, provider, std::memory_order_acq_rel) ||
           expected == provider;
}

inline bool IsProvider(Provider provider) { return PolicyProvider.load(std::memory_order_acquire) == provider; }

inline void UnregisterProvider(Provider provider)
{
    PolicyProvider.compare_exchange_strong(provider, nullptr, std::memory_order_acq_rel);
}

inline bool ShouldBlockRoutineMaintenance(std::uint32_t characterGuid, Action action)
{
    Provider provider = PolicyProvider.load(std::memory_order_acquire);
    if (!provider || !provider(characterGuid, action))
        return false;
    VetoCounts[static_cast<std::size_t>(action)].fetch_add(1, std::memory_order_relaxed);
    return true;
}

inline std::uint64_t GetVetoCount(Action action)
{
    return VetoCounts[static_cast<std::size_t>(action)].load(std::memory_order_relaxed);
}
}  // namespace Playerbots::RoutineMaintenance

#endif
