#include "app/settings.h"
#include "core/cancel.h"
#include "core/log.h"
#include "core/str.h"
#include "update/plan.h"
#include "update/progress.h"
#include "update/updater.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>

#include "cli.h"

namespace wf::cli
{

namespace
{

void usage()
{
	std::puts("WFUpdate - Warframe content updater\n"
	          "\n"
	          "  --root <dir>      install root (default: LauncherExe's grandparent, else\n"
	          "                    %LOCALAPPDATA%\\Warframe\\Downloaded\\<branch>)\n"
	          "  --branch <name>   public | test | dev            (default public)\n"
	          "  --lang <code>     two-letter language (default: registry Language, else en)\n"
	          "  --only <text>     restrict to paths containing <text>\n"
	          "  --check           plan only, download nothing\n"
	          "  --verify          hash .cache and .toc as well as everything else\n"
	          "  --steam           this install is a Steam install (default: LauncherExe path)\n"
	          "  --no-steam        force off\n"
	          "  --eos             this install uses the EOS SDK (default: LauncherExe path)\n"
	          "  --no-eos          force off\n"
	          "  --dx12            keep the DirectX 12 caches (default: GraphicsAPI == 1)\n"
	          "  --no-dx12         force off\n"
	          "  --verbose         per-file detail\n"
	          "  --settings        print the launcher settings and exit\n"
	          "  --settings-write  write back the loaded settings and exit\n"
	          "  --help\n"
	          "\n"
	          "  -shot <path>      (as argv[1]) launch the GUI and save a screenshot to <path>\n"
	          "  -t <seconds>      delay before the -shot capture           (default 0.6)\n"
	          "  -panel            (with -shot) open the settings panel before capturing\n"
	          "\n"
	          "exit: 0 ok, 1 failed, 2 cancelled, 3 --check found work to do");
}

std::optional<wf::Branch> parseBranch(std::wstring_view text)
{
	if (core::equalsNoCase(text, L"public"))
		return wf::Branch::Public;
	if (core::equalsNoCase(text, L"test"))
		return wf::Branch::Test;
	if (core::equalsNoCase(text, L"dev"))
		return wf::Branch::Dev;
	return std::nullopt;
}

class ConsoleProgress final : public wf::Progress
{
};

}

int run(int argc, wchar_t** argv)
{
	const std::span<wchar_t*> args(argv, static_cast<std::size_t>(argc));

	wf::Options options;
	std::filesystem::path root;
	bool languageGiven = false;
	std::optional<bool> steam;
	std::optional<bool> eos;
	std::optional<bool> dx12;
	bool wantSettings = false;
	bool wantSettingsWrite = false;

	const auto value = [&args](std::size_t& i) -> std::optional<std::wstring_view>
	{
		if (i + 1 >= args.size())
			return std::nullopt;
		return std::wstring_view(args[++i]);
	};

	for (std::size_t i = 1; i < args.size(); ++i)
	{
		const std::wstring_view flag(args[i]);
		if (flag == L"--help" || flag == L"-h")
		{
			usage();
			return 0;
		}
		if (flag == L"--check")
		{
			options.dryRun = true;
		}
		else if (flag == L"--verify")
		{
			options.config.hashCaches = true;
		}
		else if (flag == L"--steam")
		{
			steam = true;
		}
		else if (flag == L"--no-steam")
		{
			steam = false;
		}
		else if (flag == L"--eos")
		{
			eos = true;
		}
		else if (flag == L"--no-eos")
		{
			eos = false;
		}
		else if (flag == L"--dx12")
		{
			dx12 = true;
		}
		else if (flag == L"--no-dx12")
		{
			dx12 = false;
		}
		else if (flag == L"--verbose")
		{
			core::setVerbose(true);
		}
		else if (flag == L"--settings")
		{
			wantSettings = true;
		}
		else if (flag == L"--settings-write")
		{
			wantSettingsWrite = true;
		}
		else if (flag == L"--root")
		{
			const auto given = value(i);
			if (!given)
			{
				core::error("--root needs a directory");
				return 2;
			}
			root = *given;
		}
		else if (flag == L"--branch")
		{
			const auto given = value(i);
			const auto branch = given ? parseBranch(*given) : std::nullopt;
			if (!branch)
			{
				core::error("--branch needs public, test or dev");
				return 2;
			}
			options.config.branch = *branch;
		}
		else if (flag == L"--lang")
		{
			const auto given = value(i);
			if (!given || given->size() != 2)
			{
				core::error("--lang needs a two-letter code");
				return 2;
			}
			options.config.language = *given;
			languageGiven = true;
		}
		else if (flag == L"--only")
		{
			const auto given = value(i);
			if (!given)
			{
				core::error("--only needs some text to match");
				return 2;
			}
			options.only = *given;
		}
		else
		{
			core::error("unknown option {}", core::narrow(flag));
			usage();
			return 2;
		}
	}

	const app::Settings settings = app::Settings::load();

	if (wantSettingsWrite)
	{
		if (!settings.save())
		{
			core::error("settings save failed");
			return 1;
		}
		core::info("settings written");
		return 0;
	}

	if (!languageGiven)
		options.config.language = settings.language;
	options.config.steam = steam.value_or(settings.steam());
	options.config.eosSdk = eos.value_or(settings.eos());
	options.config.dx12 = dx12.value_or(settings.dx12());
	options.config.forceHttps = !settings.allowNetworkCaches;
	options.config.root = root.empty() ? settings.installRoot(options.config.branch) : root;
	if (options.config.root.empty())
	{
		core::error("could not resolve an install root, pass --root");
		return 2;
	}

	if (wantSettings)
	{
		core::info("graphicsApi {}", static_cast<std::uint32_t>(settings.graphicsApi));
		core::info("gpuPreference {}", static_cast<std::uint32_t>(settings.gpuPreference));
		core::info("windowMode {}", static_cast<std::uint32_t>(settings.windowMode));
		core::info("language {}", core::narrow(settings.language));
		core::info("audioLanguage {}", core::narrow(settings.audioLanguage));
		core::info("shaderCache {}", settings.shaderCache);
		core::info("bulkDownload {}", settings.bulkDownload);
		core::info("aggressiveDownload {}", settings.aggressiveDownload);
		core::info("launcherGpu {}", settings.launcherGpu);
		core::info("allowNetworkCaches {}", settings.allowNetworkCaches);
		core::info("root {}", settings.installRoot(options.config.branch).string());
		core::info("steam {} eos {} dx12 {}", settings.steam(), settings.eos(), settings.dx12());
		return 0;
	}

	core::installCancelHandler();

	ConsoleProgress progress;
	options.progress = &progress;

	const auto summary = wf::run(options);
	if (!summary)
	{
		core::error("{}", core::narrow(wf::describe(summary.error())));
		return 1;
	}

	if (options.dryRun)
	{
		core::info("{} queued, {} up to date, {} filtered", summary->queued, summary->upToDate,
		           summary->filtered);
		if (summary->cancelled)
		{
			core::warn("cancelled while checking");
			return 2;
		}
		return summary->queued == 0 ? 0 : 3;
	}

	core::info("{} updated, {} failed, {} downloaded", summary->updated, summary->failed,
	           core::formatBytes(summary->downloaded));
	if (summary->cancelled)
	{
		core::warn("cancelled with {} of {} done", summary->updated, summary->queued);
		return 2;
	}
	return summary->failed == 0 ? 0 : 1;
}

}
