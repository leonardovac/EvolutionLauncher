#include "update/http.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <format>
#include <ranges>
#include <vector>

namespace wf
{
namespace
{

constexpr DWORD connectTimeoutMs = 20'000;
constexpr DWORD sendTimeoutMs = 30'000;
constexpr DWORD receiveTimeoutMs = 60'000;
constexpr std::size_t readChunk = 32u * 1024u;
// WinHTTP defaults to 2 per server, which would quietly serialise every worker past the second
constexpr DWORD maxConnectionsPerServer = 16;

}

std::expected<Session, HttpError> Session::open()
{
	Internet handle(::WinHttpOpen(nullptr, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
	                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!handle)
		return std::unexpected(HttpError::SessionOpen);
	::WinHttpSetTimeouts(handle.get(), static_cast<int>(connectTimeoutMs),
	                     static_cast<int>(connectTimeoutMs), static_cast<int>(sendTimeoutMs),
	                     static_cast<int>(receiveTimeoutMs));
	DWORD connections = maxConnectionsPerServer;
	::WinHttpSetOption(handle.get(), WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &connections,
	                   sizeof(connections));
	return Session(std::move(handle));
}

std::expected<Connection, HttpError> Connection::open(const Session& session,
                                                      std::wstring_view url)
{
	const std::wstring text(url);
	std::array<wchar_t, 256> host{};
	std::array<wchar_t, 1024> path{};

	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	parts.lpszHostName = host.data();
	parts.dwHostNameLength = static_cast<DWORD>(host.size());
	parts.lpszUrlPath = path.data();
	parts.dwUrlPathLength = static_cast<DWORD>(path.size());

	if (!::WinHttpCrackUrl(text.c_str(), 0, 0, &parts))
		return std::unexpected(HttpError::BadUrl);

	Internet handle(::WinHttpConnect(session.handle(), host.data(), parts.nPort, 0));
	if (!handle)
		return std::unexpected(HttpError::Connect);

	return Connection(std::move(handle), std::wstring(path.data()),
	                  parts.nScheme == INTERNET_SCHEME_HTTPS);
}

std::expected<void, HttpError> Connection::fetch(std::wstring_view path, std::uint64_t resumeFrom,
                                                 const BodySink& sink,
                                                 std::uint64_t through) const
{
	const std::wstring object = basePath_ + std::wstring(path);
	const DWORD flags = secure_ ? WINHTTP_FLAG_SECURE : 0u;

	Internet request(::WinHttpOpenRequest(handle_.get(), L"GET", object.c_str(), nullptr,
	                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
	if (!request)
		return std::unexpected(HttpError::OpenRequest);

	DWORD disable = WINHTTP_DISABLE_COOKIES;
	::WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));

	std::wstring headers;
	if (through != 0)
		headers = std::format(L"Range: bytes={}-{}", resumeFrom, through);
	else if (resumeFrom != 0)
		headers = std::format(L"Range: bytes={}-", resumeFrom);

	if (!::WinHttpSendRequest(request.get(),
	                          headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
	                          headers.empty() ? 0u : static_cast<DWORD>(headers.size()),
	                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
		return std::unexpected(HttpError::Send);

	if (!::WinHttpReceiveResponse(request.get(), nullptr))
		return std::unexpected(HttpError::Receive);

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	if (!::WinHttpQueryHeaders(request.get(),
	                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
	                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
	                           WINHTTP_NO_HEADER_INDEX))
		return std::unexpected(HttpError::Status);

	constexpr std::array<DWORD, 2> gone{404u, 410u};
	if (std::ranges::contains(gone, status))
		return std::unexpected(HttpError::NotFound);

	if (resumeFrom != 0 || through != 0)
	{
		if (status != 206)
			return std::unexpected(HttpError::Status);

		std::array<wchar_t, 256> range{};
		DWORD rangeSize = static_cast<DWORD>(range.size() * sizeof(wchar_t));
		if (!::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_CONTENT_RANGE,
		                           WINHTTP_HEADER_NAME_BY_INDEX, range.data(), &rangeSize,
		                           WINHTTP_NO_HEADER_INDEX))
			return std::unexpected(HttpError::ContentRange);

		unsigned long long first = 0;
		unsigned long long last = 0;
		unsigned long long total = 0;
		if (::swscanf_s(range.data(), L"bytes %llu-%llu/%llu", &first, &last, &total) != 3)
			return std::unexpected(HttpError::ContentRange);
		if (first != resumeFrom)
			return std::unexpected(HttpError::ContentRange);
	}
	else if (status != 200)
	{
		return std::unexpected(HttpError::Status);
	}

	std::vector<std::uint8_t> buffer(readChunk);
	for (;;)
	{
		DWORD read = 0;
		if (!::WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()),
		                       &read))
			return std::unexpected(HttpError::Receive);
		if (read == 0)
			break;
		if (!sink(std::span<const std::uint8_t>(buffer.data(), read)))
			return std::unexpected(HttpError::SinkFailed);
	}
	return {};
}

bool permanent(HttpError error) noexcept
{
	constexpr std::array<HttpError, 3> fatal{HttpError::NotFound, HttpError::BadUrl,
	                                         HttpError::SinkFailed};
	return std::ranges::contains(fatal, error);
}

std::wstring_view describe(HttpError error)
{
	switch (error)
	{
	case HttpError::SessionOpen: return L"WinHttpOpen failed";
	case HttpError::BadUrl: return L"malformed base url";
	case HttpError::Connect: return L"WinHttpConnect failed";
	case HttpError::OpenRequest: return L"WinHttpOpenRequest failed";
	case HttpError::Send: return L"WinHttpSendRequest failed";
	case HttpError::Receive: return L"read failed";
	case HttpError::Status: return L"unexpected status";
	case HttpError::NotFound: return L"404/410";
	case HttpError::ContentRange: return L"bad Content-Range";
	case HttpError::SinkFailed: return L"local write failed";
	}
	return L"unknown";
}

}
