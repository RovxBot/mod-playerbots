/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "RoutineMaintenancePolicy.h"

using namespace Playerbots::RoutineMaintenance;
std::atomic<bool> enabled{false};

bool Policy(std::uint32_t guid, Action) { return enabled.load(std::memory_order_acquire) && guid == 42; }

bool Other(std::uint32_t, Action) { return true; }

int main()
{
    for (unsigned action = 0; action < static_cast<unsigned>(Action::Count); ++action)
        assert(!ShouldBlockRoutineMaintenance(42, static_cast<Action>(action)));
    assert(!RegisterProvider(nullptr));
    assert(RegisterProvider(&Policy));
    assert(RegisterProvider(&Policy));
    assert(!RegisterProvider(&Other));
    assert(IsProvider(&Policy));
    for (unsigned action = 0; action < static_cast<unsigned>(Action::Count); ++action)
        assert(!ShouldBlockRoutineMaintenance(42, static_cast<Action>(action)));
    enabled.store(true, std::memory_order_release);
    for (unsigned action = 0; action < static_cast<unsigned>(Action::Count); ++action)
    {
        assert(!ShouldBlockRoutineMaintenance(7, static_cast<Action>(action)));
        assert(ShouldBlockRoutineMaintenance(42, static_cast<Action>(action)));
        assert(GetVetoCount(static_cast<Action>(action)) == 1);
    }
    std::vector<std::thread> readers;
    for (unsigned i = 0; i < 4; ++i)
        readers.emplace_back(
            []
            {
                for (unsigned j = 0; j < 100000; ++j)
                    assert(ShouldBlockRoutineMaintenance(42, Action::Refresh));
            });
    for (auto& reader : readers)
        reader.join();
    assert(GetVetoCount(Action::Refresh) == 400001);
    enabled.store(false, std::memory_order_release);
    assert(!ShouldBlockRoutineMaintenance(42, Action::Refresh));
    UnregisterProvider(&Other);
    assert(IsProvider(&Policy));
    UnregisterProvider(&Policy);
    assert(!IsProvider(&Policy));
    assert(!ShouldBlockRoutineMaintenance(42, Action::Refresh));
    assert(RegisterProvider(&Policy));
    auto const start = std::chrono::steady_clock::now();
    constexpr unsigned lookups = 1000000;
    unsigned count = 0;
    for (unsigned i = 0; i < lookups; ++i)
        count += ShouldBlockRoutineMaintenance(42, Action::Randomize);
    assert(count == 0);
    auto const ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Optional maintenance policy tests passed; disabled provider " << static_cast<double>(ns) / lookups
              << " ns/check (local microbenchmark)\n";
}
