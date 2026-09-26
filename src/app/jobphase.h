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
    // the game's ContentUpdate applet, run after every check or update that ends clean
    UpdatingContent,
    Ready,
    Failed,
    Cancelled
};

}
