#include "../kpm.h"
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <numeric>
#include <vector>
#include <fstream>

static std::vector<std::string> KpmGetInstalledPackagesManifestFiles(const std::string& cache_path)
{
	std::vector<std::string> files;
	for(const auto& file : std::filesystem::directory_iterator(cache_path))
	{
		if(file.is_regular_file() && file.path().extension().string() == ".manifest")
		{
			files.push_back(file.path().filename());
		}
	}
	return files;
}

static std::vector<std::string> KpmReadManifest(const std::string& package)
{
	std::string package_manifest_file = KpmGetCachePath() + package;
	std::ifstream file(package_manifest_file);

	if(!file.is_open())
	{
		return {};
	}

	std::string line;
	std::vector<std::string> files;

	while(std::getline(file, line))
	{
		files.push_back(line);
	}

	return files;
}

static std::string KpmGetPackageName(const std::string& package)
{
	return package.substr(0, package.find_last_of('.'));
}

void KpmList()
{
	std::string cache_path = KpmGetCachePath();

	// Read all the .manifest files present. These are the known installed packages.
	auto packages = KpmGetInstalledPackagesManifestFiles(cache_path);

	std::uint32_t padding = 7; // "Package" string size
	for(const auto& package : packages)
	{
		std::uint32_t pnsize = KpmGetPackageName(package).size();
		if(pnsize > padding)
		{
			padding = pnsize;
		}
	}

	std::cout << "Package";
	for(std::uint32_t i = 0; i < padding - 7; i++) std::cout << " ";
	std::cout << "\tInstalled files\n";

	for(std::uint32_t i = 0; i < padding; i++) std::cout << "-";
	std::cout << "\t---------------\n";

	for(const auto& package : packages)
	{
		auto files = KpmReadManifest(package);
		std::uint32_t nfiles = std::accumulate(files.begin(), files.end(), 0u, [](std::uint32_t sum, const auto& file){
			return sum + (std::filesystem::path(file).has_filename() ? 1 : 0);
		});

		std::cout << KpmGetPackageName(package) << "\t      " << std::setw(3) << nfiles << " files\n";
	}
	std::cout.flush();
}
