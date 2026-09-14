#pragma once

namespace app
{

enum class JobPhase
{
    Idle,
    Checking,
    // a check-only run that found work; nothing downloads until the user asks
    UpdateReady,
    Updating,
    Ready,
    Failed,
    Cancelled
};

}
