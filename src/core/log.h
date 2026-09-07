#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace core
{

enum class Level
{
	Debug,
	Info,
	Warn,
	Error
};

void setVerbose(bool on);
void write(Level level, std::string_view message);

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args)
{
	write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args)
{
	write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args)
{
	write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
	write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}
