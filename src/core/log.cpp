#include "core/log.h"

#include <cstdio>

namespace core
{
namespace
{

bool verbose = false;

std::string_view tag(Level level)
{
	switch (level)
	{
	case Level::Debug: return "[.]";
	case Level::Info: return "[i]";
	case Level::Warn: return "[!]";
	case Level::Error: return "[x]";
	}
	return "[?]";
}

}

void setVerbose(bool on)
{
	verbose = on;
}

void write(Level level, std::string_view message)
{
	if (level == Level::Debug && !verbose)
		return;
	std::FILE* stream = level == Level::Error || level == Level::Warn ? stderr : stdout;
	std::fprintf(stream, "%.*s %.*s\n", static_cast<int>(tag(level).size()), tag(level).data(),
	             static_cast<int>(message.size()), message.data());
	std::fflush(stream);
}

}
