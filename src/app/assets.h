#pragma once

#include <cstdint>
#include <span>

namespace app
{

std::span<const std::uint8_t> resource(int id);

}
