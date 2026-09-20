#pragma once

#include "core/win.h"

#include <winhttp.h>

#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace wf
{

enum class HttpError
{
	SessionOpen,
	BadUrl,
	Connect,
	OpenRequest,
	Send,
	Receive,
	Status,
	NotFound,
	ContentRange,
	SinkFailed
};

struct InternetTraits
{
	using Value = HINTERNET;
	static Value invalid() noexcept { return nullptr; }
	static void close(Value value) noexcept { ::WinHttpCloseHandle(value); }
};

using Internet = core::UniqueHandle<InternetTraits>;
using BodySink = std::function<bool(std::span<const std::uint8_t>)>;

class Session
{
public:
	static std::expected<Session, HttpError> open();

	[[nodiscard]] HINTERNET handle() const noexcept { return handle_.get(); }

private:
	explicit Session(Internet handle) noexcept : handle_(std::move(handle)) {}

	Internet handle_;
};

class Connection
{
public:
	static std::expected<Connection, HttpError> open(const Session& session, std::wstring_view url);

	// `through` bounds the Range; 0 means open-ended, which is what a whole entry wants
	std::expected<void, HttpError> fetch(std::wstring_view path, std::uint64_t resumeFrom,
	                                     const BodySink& sink, std::uint64_t through = 0) const;

private:
	Connection(Internet handle, std::wstring basePath, bool secure) noexcept
		: handle_(std::move(handle)), basePath_(std::move(basePath)), secure_(secure)
	{
	}

	Internet handle_;
	std::wstring basePath_;
	bool secure_ = false;
};

bool permanent(HttpError error) noexcept;
std::wstring_view describe(HttpError error);

}
