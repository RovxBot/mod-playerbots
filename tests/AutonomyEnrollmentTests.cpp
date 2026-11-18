/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#include <cassert>
#include <iostream>

#include "AutonomyEnrollment.h"
namespace A = Playerbots::AutonomyEnrollment;
bool Reserved(std::uint32_t guid) { return guid == 187; }
bool Other(std::uint32_t) { return true; }
int main()
{
    assert(!A::LoginBlocked(187));
    assert(!A::Register(nullptr));
    assert(A::Register(Reserved) && A::Register(Reserved));
    assert(!A::Register(Other));
    assert(A::IsOwner(Reserved) && A::LoginBlocked(187) && !A::LoginBlocked(188));
    std::cout << "Enrollment login callback ownership and per-character scope passed\n";
}
