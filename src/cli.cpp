#include "app/launch.h"
#include "app/settings.h"
#include "app/sideload.h"
#include "app/titles.h"
#include "app/versions.h"
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
	std::puts("Launcher - custom launcher for Digital Extremes games\n"
	          "\n"
	          "  --root <dir>      install root for this run (default: launcher.json root, else\n"
	          "                    DownloadDir, else LauncherExe's grandparent, else\n"
	          "                    %LOCALAPPDATA%\\<title>\\Downloaded\\<branch>)\n"
	          "  --set-root <dir>  record <dir> as this title's install root and exit\n"
	          "  --title <name>    warframe | soulframe           (default warframe)\n"
	          "  --branch <name>   public | test | dev            (default public)\n"
	          "  --lang <code>     two-letter language (default: registry Language, else en)\n"
	          "  --only <text>     restrict to paths containing <text>\n"
	          "  --check           plan and purge unlisted, download nothing\n"
	          "  --verify          hash .cache and .toc as well as everything else\n"
	          "  --stale           report files the index does not list, delete nothing\n"
	          "  --purge-print     preview the unlisted files a real run would delete\n"
	          "  --steam           this install is a Steam install (default: LauncherExe path)\n"
	          "  --no-steam        force off\n"
	          "  --eos             this install uses the EOS SDK (default: LauncherExe path)\n"
	          "  --no-eos          force off\n"
	          "  --dx12            keep the DirectX 12 caches (default: GraphicsAPI == 1)\n"
	          "  --no-dx12         force off\n"
	          "  --bulk            fetch the bulk .cache/.toc content (default: EnableBulkDownload)\n"
	          "  --no-bulk         force off\n"
	          "  --jobs N          parallel downloads, 1-16 (default: 4)\n"
	          "  --verbose         per-file detail\n"
	          "  --settings        print the launcher settings and exit\n"
	          "  --settings-write  write back the loaded settings and exit\n"
	          "  --versions        print the launcher and engine versions and exit\n"
	          "  --launch-print    print the game command line and exit\n"
	          "  --defrag-print    print the cache defragment command line and exit\n"
	          "  --help\n"
	          "\n"
	          "  -shot <path>      (as argv[1]) launch the GUI and save a screenshot to <path>\n"
	          "  -t <seconds>      delay before the -shot capture           (default 0.6)\n"
	          "  -panel            (with -shot) open the settings panel before capturing\n"
	          "  -menu             (with -shot) open the rail menu before capturing\n"
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
public:
	void onIndex(std::size_t entries, std::size_t rejected) override
	{
		core::info("index: {} entries, {} rejected", entries, rejected);
	}
	void onPlan(std::size_t queued, std::uint64_t downloadBytes, std::size_t filtered,
	            std::size_t skipped, std::size_t upToDate, std::size_t bulkSkipped) override
	{
		core::info("{} filtered, {} skipped, {} up to date, {} queued ({} to download)", filtered,
		           skipped, upToDate, queued, core::formatBytes(downloadBytes));
		if (bulkSkipped != 0)
			core::info("{} cache files skipped: bulk download is off", bulkSkipped);
	}
	void onEntryStart(std::size_t index, std::size_t count, std::wstring_view installPath,
	                  std::uint64_t wireSize) override
	{
		core::info("[{}/{}] {} ({})", index, count, core::narrow(installPath),
		           core::formatBytes(wireSize));
	}
	void onEntryFailed(std::wstring_view installPath, std::wstring_view reason) override
	{
		core::error("{}: {}", core::narrow(installPath), core::narrow(reason));
	}
	void onStale(std::wstring_view installPath, std::uint64_t bytes) override
	{
		core::debug("  unlisted {} ({})", core::narrow(installPath), core::formatBytes(bytes));
	}
	// the console is the narrowing boundary the module no longer has to be
	void onLog(core::Level level, std::wstring_view message) override
	{
		core::write(level, core::narrow(message));
	}
};

}

int run(int argc, wchar_t** argv)
{
	const std::span<wchar_t*> args(argv, static_cast<std::size_t>(argc));

	wf::Options options;
	std::filesystem::path root;
	std::filesystem::path setRoot;
	bool languageGiven = false;
	std::optional<bool> steam;
	std::optional<bool> eos;
	std::optional<bool> dx12;
	std::optional<bool> bulk;
		std::optional<std::size_t> jobs;
	bool wantSettings = false;
	bool wantSettingsWrite = false;
	bool wantVersions = false;
	bool wantLaunchPrint = false;
	bool wantDefragPrint = false;

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
		else if (flag == L"--stale")
		{
			options.staleReport = true;
		}
		else if (flag == L"--purge-print")
		{
			options.purgePrint = true;
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
		else if (flag == L"--bulk")
		{
			bulk = true;
		}
		else if (flag == L"--no-bulk")
		{
			bulk = false;
		}
		else if (flag == L"--jobs" && i + 1 < argc)
		{
			jobs = static_cast<std::size_t>(::_wtoi(argv[++i]));
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
		else if (flag == L"--versions")
		{
			wantVersions = true;
		}
		else if (flag == L"--launch-print")
		{
			wantLaunchPrint = true;
		}
		else if (flag == L"--defrag-print")
		{
			wantDefragPrint = true;
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
		else if (flag == L"--set-root")
		{
			const auto given = value(i);
			if (!given)
			{
				core::error("--set-root needs a directory");
				return 2;
			}
			setRoot = *given;
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
		else if (flag == L"--title")
		{
			const auto given = value(i);
			const auto title = given ? app::parseTitleName(*given) : std::nullopt;
			if (!title)
			{
				core::error("--title needs warframe or soulframe");
				return 2;
			}
			options.config.title = *title;
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
		else if (flag.starts_with(L"-registry:"))
		{
			// read straight off the command line by app::registryTag; not a CLI option
		}
		else
		{
			core::error("unknown option {}", core::narrow(flag));
			usage();
			return 2;
		}
	}

	ConsoleProgress progress;
	wf::LauncherConfig launcher = wf::LauncherConfig::load(wf::RunContext{&progress});
	const app::Settings settings = app::Settings::load(options.config.title, launcher);

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
	options.config.bulkDownload = bulk.value_or(settings.bulkDownload);
	options.jobs = jobs.value_or(wf::defaultJobs);
	options.config.forceHttps = !settings.allowNetworkCaches;
	options.config.sideload = settings.sideload;
	options.config.launcher = std::move(launcher);

	if (!setRoot.empty())
	{
		app::Settings next = settings;
		switch (next.adoptInstallRoot(setRoot))
		{
		case app::RootProbe::HasGame:
			break;
		case app::RootProbe::Empty:
			core::info("no game in {}, a full install will land there", setRoot.string());
			break;
		case app::RootProbe::Occupied:
			core::error("{} holds other files; pass the game's folder or an empty one",
			            setRoot.string());
			return 2;
		case app::RootProbe::Unusable:
			core::error("could not read {}", setRoot.string());
			return 2;
		}
		if (!next.save(&settings))
		{
			core::error("could not record the install root");
			return 2;
		}
		core::info("root {}", next.installRoot(wf::Branch::Public).string());
		return 0;
	}

	if (wantSettings)
	{
		core::info("graphicsApi {}", static_cast<std::uint32_t>(settings.graphicsApi));
		core::info("gpuPreference {}", static_cast<std::uint32_t>(settings.gpuPreference));
		core::info("windowMode {}", static_cast<std::uint32_t>(settings.windowMode));
		core::info("language {}", core::narrow(settings.language));
		core::info("audioLanguage {}", core::narrow(settings.audioLanguage));
		core::info("shaderCache {}", settings.shaderCache);
		core::info("allowNetworkCaches {}", settings.allowNetworkCaches);
		core::info("sideload {}", settings.sideload);
		core::info("root {}", settings.installRoot(options.config.branch).string());
		core::info("steam {} eos {} dx12 {}", settings.steam(), settings.eos(), settings.dx12());
		return 0;
	}

	// neither needs an install root, so both answer on a machine with no game present
	if (wantVersions)
	{
		core::info("launcher {}", core::narrow(app::launcherVersion()));
		const auto build = app::gameBuildVersion(settings, options.config.branch);
		core::info("game build {}", build ? core::narrow(*build) : "unknown");
		return 0;
	}

	options.config.root = root.empty() ? settings.installRoot(options.config.branch) : root;
	if (options.config.root.empty())
	{
		core::error("could not resolve an install root, pass --root");
		return 2;
	}

	if (wantLaunchPrint)
	{
		const std::wstring line = app::buildGameCommandLine(settings, options.config.branch, options.config.root);
		if (line.empty())
		{
			core::error("could not build the game command line");
			return 1;
		}
		core::info("{}", core::narrow(line));
		return 0;
	}

	if (wantDefragPrint)
	{
		std::wstring line = app::buildGameCommandLine(settings, options.config.branch, options.config.root);
		if (line.empty())
		{
			core::error("could not build the game command line");
			return 1;
		}
		line += L" -applet:/EE/Types/Framework/CacheDefraggerIOCP /Tools/CachePlan.txt";
		core::info("{}", core::narrow(line));
		return 0;
	}

	static core::CancelToken cancel;  // the console handler needs it to outlive this scope
	core::installCancelHandler(cancel);

	options.ctx.progress = &progress;
	options.ctx.cancel = &cancel;

	const auto summary = wf::run(options);
	if (!summary)
	{
		core::error("{}", core::narrow(wf::describe(summary.error())));
		return 1;
	}

	// patch only after a real update, never on a --check preview
	if (summary->mainExe && settings.sideload && !options.dryRun && !options.purgePrint
	    && !options.staleReport)
		app::ensureSideloaded(options.config.launcher, *summary->mainExe, options.config.root);

	if (options.purgePrint)
	{
		core::info("{} unlisted files, {}, {} failed", summary->purgeFiles,
		           core::formatBytes(summary->purgeBytes), summary->purgeFailed);
		if (summary->cancelled)
		{
			core::warn("cancelled while walking");
			return 2;
		}
		return 0;
	}

	if (options.staleReport)
	{
		core::info("{} unlisted files, {}", summary->staleFiles,
		           core::formatBytes(summary->staleBytes));
		if (summary->cancelled)
		{
			core::warn("cancelled while walking");
			return 2;
		}
		return 0;
	}

	if (options.dryRun)
	{
		core::info("{} queued, {} up to date, {} filtered, {} skipped", summary->queued,
		           summary->upToDate, summary->filtered, summary->skipped);
		if (summary->bulkSkipped != 0)
			core::info("{} cache files skipped: bulk download is off", summary->bulkSkipped);
		if (summary->cacheDiffers != 0)
			core::info("{} cache files differ from the index and were left alone",
			           summary->cacheDiffers);
		core::info("{} unlisted files removed, {} failed", summary->purgeFiles, summary->purgeFailed);
		if (summary->cancelled)
		{
			core::warn("cancelled while checking");
			return 2;
		}
		return summary->queued == 0 ? 0 : 3;
	}

	core::info("{} updated, {} failed, {} downloaded", summary->updated, summary->failed,
	           core::formatBytes(summary->downloaded));
	core::info("{} unlisted files removed, {} failed", summary->purgeFiles, summary->purgeFailed);
	if (summary->cancelled)
	{
		core::warn("cancelled with {} of {} done", summary->updated, summary->queued);
		return 2;
	}
	return summary->failed == 0 ? 0 : 1;
}

}
