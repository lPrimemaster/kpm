#include "../kpm.h"
#include "../kpm_logger.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>


static std::optional<std::vector<std::string>> KpmReadManifest(const std::string& package)
{
	std::string package_manifest_file = KpmGetCachePath() + package + ".manifest";
	std::ifstream file(package_manifest_file);

	KpmLogTrace("Reading manifest file: {}", package_manifest_file);

	if(!file.is_open())
	{
		KpmLogError("Failed to read manifest file.");
		KpmLogWarning("Package {} may not be installed.", package);
		return std::nullopt;
	}

	std::string line;
	std::vector<std::string> files;

	while(std::getline(file, line))
	{
		files.push_back(line);
	}

	return files;
}

static std::tuple<std::vector<std::string>, std::vector<std::string>> KpmOrderFiles(const std::vector<std::string>& files)
{
	std::vector<std::string> ofiles;
	std::vector<std::string> odirs;

	for(const auto& file : files)
	{
		if(std::filesystem::is_directory(file))
		{
			odirs.push_back(file);
		}
		else
		{
			ofiles.push_back(file);
		}
	}

	return { ofiles, odirs };
}

static bool KpmRemoveFiles(const std::vector<std::string>& files)
{
	auto [ofiles, odirs] = KpmOrderFiles(files);
	bool ok = true;
	for(const auto& file : ofiles)
	{
		if(std::filesystem::exists(file))
		{
			KpmLogTrace("Removing file: {}", file);
			if(!std::filesystem::remove(file))
			{
				KpmLogError("Failed to remove file {}.", file);
				ok = false;
			}
		}
		else
		{
			KpmLogError("Failed to remove file {}. Does not exist.", file);
		}
	}

	for(const auto& dir : odirs)
	{
		if(std::filesystem::exists(dir))
		{
			if(std::filesystem::is_empty(dir))
			{
				KpmLogTrace("Removing empty dir: {}", dir);
				if(!std::filesystem::remove_all(dir))
				{
					KpmLogError("Failed to remove dir {}.", dir);
					ok = false;
				}
			}
		}
		else
		{
			KpmLogError("Failed to remove dir {}. Does not exist.", dir);
		}
	}

	return ok;
}

static bool KpmRemoveManifest(const std::string& package)
{
	std::string package_manifest_file = KpmGetCachePath() + package + ".manifest";
	if(std::filesystem::exists(package_manifest_file))
	{
		if(!std::filesystem::remove(package_manifest_file))
		{
			KpmLogError("Failed to remove manifest file {}.", package_manifest_file);
			return false;
		}
	}
	else
	{
		KpmLogError("Failed to remove manifest file {}. Does not exist.", package_manifest_file);
		return false;
	}

	KpmLogInfo("Successfully removed package {}.", package);
	return true;
}

bool KpmRemove(const std::string &package)
{
	auto files = KpmReadManifest(package);

	if(!files.has_value())
	{
		KpmLogError("Failed to remove package {}.", package);
		return false;
	}

	if(KpmRemoveFiles(files.value()))
	{
		return KpmRemoveManifest(package);
	}

	return false;
}
