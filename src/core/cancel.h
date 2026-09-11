#pragma once

namespace core
{

void installCancelHandler();
bool cancelled() noexcept;
void requestCancel() noexcept;
void resetCancel() noexcept;

}
