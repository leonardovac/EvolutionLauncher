#pragma once

namespace app
{

enum class JobPhase
{
    Idle,
    Checking,
    Updating,
    Ready,
    Failed,
    Cancelled
};

}
