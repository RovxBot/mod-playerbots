#include "RaidBwlMultipliers.h"

float BwlGenericMultiplier::GetValue(Action* /*action*/)
{
    if (!helper.IsInBwl())
    {
        return 1.0f;
    }

    return 1.0f;
}
