/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_AUTONOMY_ENROLLMENT_H
#define PLAYERBOTS_AUTONOMY_ENROLLMENT_H
#include <atomic>
#include <cstdint>
namespace Playerbots::AutonomyEnrollment
{
inline constexpr std::uint32_t ContractVersion = 1;
// Read-only, process-lifetime callback. World thread: login ingress and again
// before materializing an asynchronous login. No provider preserves legacy.
using LoginVeto = bool (*)(std::uint32_t);
inline std::atomic<LoginVeto> Veto{nullptr};
inline bool Register(LoginVeto veto)
{
    if (!veto)
        return false;
    LoginVeto expected = nullptr;
    return Veto.compare_exchange_strong(expected, veto, std::memory_order_acq_rel) || expected == veto;
}
inline bool IsOwner(LoginVeto veto) { return Veto.load(std::memory_order_acquire) == veto; }
inline bool LoginBlocked(std::uint32_t guid)
{
    auto veto = Veto.load(std::memory_order_acquire);
    return veto && veto(guid);
}
}  // namespace Playerbots::AutonomyEnrollment
#endif
