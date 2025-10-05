#include "../kpm.h"
#include "logger.inl"

#include <filesystem>
#include <fstream>
#include <optional>

#include <curl/curl.h>
#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/yaml.h>
#include <archive.h>
#include <archive_entry.h>

#ifndef _WIN32
#include <sys/utsname.h>
#else
#include <windows.h>
#endif

KPM_SET_LOG_PREFIX(KpmInstall);

static std::stringstream _manifest_stream;
static std::string _kpm_install_prefix;
static std::string _kpm_cache_path;

enum class KpmMediaType
{
	LOCAL,
	REMOTE
};

// kpm supported archs
enum class KpmArch
{
	AMD64,
	I386,

	ARM64,

	POWERPC,
	POWERPC64,

	UNKNOWN
};

//kpm supported os
enum class KpmOs
{
	WIN32,
	LINUX,
	DARWIN
};

static KpmMediaType KpmDetectMedia(const std::string& package)
{
	if(std::filesystem::exists(std::filesystem::path(package)))
	{
		KpmLogTrace("Media type = LOCAL");
		return KpmMediaType::LOCAL;
	}

	// Treat it such as if it is not a file, it is an url
	KpmLogTrace("Media type = REMOTE");
	return KpmMediaType::REMOTE;
}

static std::optional<std::vector<std::uint8_t>> KpmDownloadUrlFile(const std::string& url)
{
	KpmLogTrace("Downloading file from url: {}", url);

	CURL* curl = curl_easy_init();
    if (!curl)
	{
        return {};
	}

	std::vector<std::uint8_t> data;
	const auto write_handle = +[](void* ptr, size_t size, size_t nmemb, void* userdata) -> std::size_t {
		auto* stream = reinterpret_cast<std::vector<std::uint8_t>*>(userdata);
		std::size_t total_size = size * nmemb;
		stream->insert(stream->end(), reinterpret_cast<std::uint8_t*>(ptr), reinterpret_cast<std::uint8_t*>(ptr) + total_size);
		return total_size;
	};

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_handle);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	// curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);
	// curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

	KpmLogTrace("KpmDownloadUrlFile() OK.");
    return (res == CURLE_OK) ? std::optional(data) : std::nullopt;
}

static std::optional<std::string> KpmLoadYamlLocal(const std::string& file)
{
	std::ifstream handle(file);
	std::stringstream ss;
	if(handle.is_open())
	{
		ss << handle.rdbuf();
		return ss.str();
	}
	return std::nullopt;
}

static std::optional<std::string> KpmLoadYamlRemote(const std::string& url)
{
	auto data = KpmDownloadUrlFile(url);
	if(!data.has_value())
	{
		return std::nullopt;
	}

	return std::string(reinterpret_cast<char*>(data.value().data()), data.value().size());
}

constexpr KpmOs KpmDetectOs()
{
#ifdef _WIN32
	return KpmOs::WIN32;
#elif defined(__linux__)
	return KpmOs::LINUX;
#elif defined(__APPLE__)
	return KpmOs::DARWIN;
#endif
}

static KpmArch KpmDetectArch()
{
#ifdef _WIN32
	USHORT pm = 0;
	USHORT nm = 0;
	if(IsWow64Process2(GetCurrentProcess(), &pm, &nm))
	{
		switch(nm)
		{
			case IMAGE_FILE_MACHINE_AMD64: return KpmArch::AMD64;
			case IMAGE_FILE_MACHINE_ARM64: return KpmArch::ARM64;
			case IMAGE_FILE_MACHINE_I386:  return KpmArch::I386;
			case IMAGE_FILE_MACHINE_POWERPC: return KpmArch::POWERPC;
		}
	}
#elif defined(__linux__) || defined(__APPLE__)
	struct utsname buffer {};
	if(uname(&buffer) == 0)
	{
		if(std::string(buffer.machine) == "x86_64")
		{
			return KpmArch::AMD64;
		}
		else if(
			std::string(buffer.machine) == "i386" ||
			std::string(buffer.machine) == "i686"
		)
		{
			return KpmArch::I386;
		}
		else if(
			std::string(buffer.machine) == "aarch64_be" ||
			std::string(buffer.machine) == "aarch64" ||
			std::string(buffer.machine) == "armv8b" ||
			std::string(buffer.machine) == "armb8l"
		)
		{
			return KpmArch::ARM64;
		}
		else if(
			std::string(buffer.machine) == "ppc64" ||
			std::string(buffer.machine) == "ppc64le"
		)
		{
			return KpmArch::POWERPC64;
		}
		else if(
			std::string(buffer.machine) == "ppc" ||
			std::string(buffer.machine) == "ppcle"
		)
		{
			return KpmArch::POWERPC;
		}
		else
		{
			return KpmArch::UNKNOWN;
		}
	}
#endif
	return KpmArch::UNKNOWN;
}

static std::string KpmGetOsString(const KpmOs& os)
{
	switch (os)
	{
		case KpmOs::WIN32:
		{
			return "windows";
		}
		case KpmOs::LINUX:
		{
			return "linux";
		}
		case KpmOs::DARWIN:
		{
			return "macos";
		}
	}

	return "";
}

static std::string KpmGetArchString(const KpmArch& arch)
{
	switch (arch)
	{
		case KpmArch::AMD64:
		{
			return "amd64";
		}
		case KpmArch::I386:
		{
			return "i386";
		}
		case KpmArch::ARM64:
		{
			return "arm64";
		}
		case KpmArch::POWERPC:
		{
			return "ppc";
		}
		case KpmArch::POWERPC64:
		{
			return "ppc64";
		}
		case KpmArch::UNKNOWN:
		{
			return "";
		}
	}

	return "";
}

static std::optional<std::string> KpmGetPackagePlatformTag()
{
	KpmOs os = KpmDetectOs();
	KpmArch arch = KpmDetectArch();

	if(arch == KpmArch::UNKNOWN)
	{
		return std::nullopt;
	}

	return KpmGetOsString(os) + "_" + KpmGetArchString(arch);
}

static bool KpmValidateConfig(const YAML::Node& config)
{
	if(!config["dist"] || !config["dist"]["packages"] || !config["dist"]["packages"].IsSequence())
	{
		KpmLogError("<dist>.<packages> field required.");
		return false;
	}

	if(!config["dist"]["endpoint"])
	{
		KpmLogError("<dist>.<endpoint> field required.");
		return false;
	}

	if(!config["metadata"] && !config["metadata"]["name"])
	{
		KpmLogError("<metadata>.<name> field required.");
		return false;
	}

	return true;
}

static bool KpmDeploySource(const std::string& package, const YAML::Node& config)
{
	return false;
}

static std::string KpmGetCachePath()
{
	if(!_kpm_cache_path.empty())
	{
		return _kpm_cache_path;
	}

	switch (KpmDetectOs())
	{
		case KpmOs::WIN32:
		{
			_kpm_cache_path = std::string(std::getenv("%APPDATA%")) + "\\kpm\\"; 
			std::filesystem::create_directories(_kpm_cache_path);
			break;
		}
		case KpmOs::LINUX:
		case KpmOs::DARWIN:
		{
			_kpm_cache_path = std::string(std::getenv("HOME")) + "/.kpm/";
			std::filesystem::create_directories(_kpm_cache_path);
			break;
		}
	}

	return _kpm_cache_path;
}

static std::string KpmGetInstallPath(const YAML::Node& config)
{
	if(!_kpm_install_prefix.empty())
	{
		return _kpm_install_prefix;
	}

	switch (KpmDetectOs())
	{
		case KpmOs::WIN32:
		{
			const char* home = std::getenv("%PROGRAMFILES%");
			_kpm_install_prefix = std::string(home) + "\\" + config["metadata"]["name"].as<std::string>() + "\\";
			break;
		}
		case KpmOs::LINUX:
		case KpmOs::DARWIN:
		{
			const char* home = std::getenv("HOME");
			_kpm_install_prefix = std::string(home) + "/.local/";
			break;
		}
	}

	return _kpm_install_prefix;
}

static void KpmInstallManifestAddPath(const std::string& path)
{
	KpmLogTrace("Adding file to manifest: {}", path);
	_manifest_stream << path << '\n';
}

static bool KpmExtractPackageData(const std::vector<std::uint8_t>& payload, const YAML::Node& config)
{
	int r;
	auto archive_check_ok = [&r](struct archive* archive) -> bool {
		if(r < ARCHIVE_OK && r > ARCHIVE_WARN)
		{
			KpmLogWarning(archive_error_string(archive));
		}
		else if(r < ARCHIVE_WARN)
		{
			KpmLogError(archive_error_string(archive));
			return false;
		}
		return true;
	};

	auto copy_data = [](struct archive* ar, struct archive* aw)
	{
		int r;
		const void* buff;
		size_t size;
		int64_t offset;

		while(true)
		{
			r = archive_read_data_block(ar, &buff, &size, &offset);
			if (r == ARCHIVE_EOF)
			{
				return true;
			}
			if(r < ARCHIVE_OK && r > ARCHIVE_WARN)
			{
				const char* error = archive_error_string(ar);
				KpmLogWarning(error);
			}
			else if(r < ARCHIVE_WARN)
			{
				const char* error = archive_error_string(ar);
				KpmLogError(error);
				return false;
			}
			r = archive_write_data_block(aw, buff, size, offset);
			if(r < ARCHIVE_OK && r > ARCHIVE_WARN)
			{
				const char* error = archive_error_string(aw);
				KpmLogWarning(error);
			}
			else if(r < ARCHIVE_WARN)
			{
				const char* error = archive_error_string(aw);
				KpmLogError(error);
				return false;
			}
		}
	};

	auto archive_prepend_path = [](struct archive_entry* entry, const std::string& path) -> std::string {
		if(!path.ends_with('/') && !path.ends_with('\\'))
		{
			KpmLogError("Parent path for extraction must end with separator.");
			return "";
		}
		const char* entry_path = archive_entry_pathname(entry);
		std::string new_path = (path + entry_path);
		archive_entry_set_pathname(entry, new_path.c_str());
		return new_path;
	};

	struct archive* archive = archive_read_new();
	archive_read_support_filter_gzip(archive);
	archive_read_support_format_tar(archive);
	r = archive_read_open_memory(archive, payload.data(), payload.size());

	if(!archive_check_ok(archive))
	{
		return false;
	}

	struct archive_entry *entry;
	int flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

	struct archive* ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);

	if(!archive_check_ok(ext))
	{
		archive_read_close(archive);
		archive_read_free(archive);
		return false;
	}

	while(true)
	{
		r = archive_read_next_header(archive, &entry);
		if(!archive_check_ok(archive))
		{
			archive_read_close(archive);
			archive_read_free(archive);
			return false;
		}

		if (r == ARCHIVE_EOF)
		{
			break;
		}

		std::string parent = KpmGetInstallPath(config);
		std::string filepath = archive_prepend_path(entry, parent);

		// We don't write directories to the manifest file
		if(!S_ISDIR(archive_entry_filetype(entry)))
		{
			KpmInstallManifestAddPath(filepath);
		}

		r = archive_write_header(ext, entry);
		if(!archive_check_ok(ext))
		{
			archive_read_close(archive);
			archive_read_free(archive);
			archive_write_close(ext);
			archive_write_free(ext);
			return false;
		}

		if(!copy_data(archive, ext))
		{
			archive_read_close(archive);
			archive_read_free(archive);
			archive_write_close(ext);
			archive_write_free(ext);
			return false;
		}
		r = archive_write_finish_entry(ext);
		if(!archive_check_ok(ext))
		{
			archive_read_close(archive);
			archive_read_free(archive);
			archive_write_close(ext);
			archive_write_free(ext);
			return false;
		}
	}

	archive_read_close(archive);
	archive_read_free(archive);
	archive_write_close(ext);
	archive_write_free(ext);

	return true;
}

static bool KpmDeployPrebuild(const std::string& package, const YAML::Node& config)
{
	auto payload = KpmDownloadUrlFile(package);

	if(!payload.has_value() || payload.value().empty())
	{
		return false;
	}

	if(!KpmExtractPackageData(payload.value(), config))
	{
		KpmLogError("Failed to extract payload data.");
		return false;
	}

	return true;
}

static bool KpmWriteManifest(const YAML::Node& config)
{
	std::string package_manifest_file = KpmGetCachePath() + config["metadata"]["name"].as<std::string>() + ".manifest";
	std::ofstream file(package_manifest_file);

	KpmLogTrace("Writing manifest file: {}", package_manifest_file);

	if(!file.is_open())
	{
		KpmLogError("Failed to write manifest file.");
		return false;
	}

	file << _manifest_stream.str();
	_manifest_stream.clear();

	return true;
}

static bool KpmInstallFromMemory(const std::string& data) noexcept
{
	YAML::Node config;
	try
	{
		config = YAML::Load(data);
	}
	catch (const YAML::ParserException& e)
	{
		KpmLogError("YAML parsing error: {}", e.what());
		return false;
    }
	catch (const YAML::Exception& e)
	{
		KpmLogError("YAML error: {}", e.what());
		return false;
    }

	std::optional<std::string> plat_tag = KpmGetPackagePlatformTag();
	if(!plat_tag.has_value())
	{
		KpmLogError("Could not find a valid or compatible system <os>_<arch> tag.");
		return false;
	}

	if(!KpmValidateConfig(config))
	{
		KpmLogError("Invalid package config.");
		return false;
	}

	std::unordered_map<std::string, std::string> platform_map;
	std::string endpoint = config["dist"]["endpoint"].as<std::string>();
	for (const auto& item : config["dist"]["packages"])
	{
		if (item.IsMap() && item.size() == 1)
		{
			auto it = item.begin();
			platform_map[it->first.as<std::string>()] = endpoint + it->second.as<std::string>();
		}
	}

	auto package = platform_map.find(plat_tag.value());
	if(package == platform_map.end())
	{
		auto src_package = platform_map.find("source");
		if(src_package == platform_map.end())
		{
			// We found no binary for our platform
			// And the package author did not provide a source dist
			KpmLogError("Binary distribution for platform <{}> not found and source distribution not available.", plat_tag.value());
			return false;
		}

		KpmLogInfo("Binary distribution for platform <{}> not found. Falling back to source distribution.", plat_tag.value());
		if(!KpmDeploySource(src_package->second, config))
		{
			KpmLogError("Failed to deploy source distribution.");
			return false;
		}
	}
	else
	{
		KpmLogInfo("Found binary distribution for platform <{}>.", plat_tag.value());
		if(!KpmDeployPrebuild(package->second, config))
		{
			KpmLogError("Failed to deploy pre-built files.");
			return false;
		}
	}

	// TODO: (César) If prebuild or source fails during copying files
	// 				 check if there are some dangling files that we need to remove

	return KpmWriteManifest(config);
}

static bool KpmInstallFromUrl(const std::string& url)
{
	std::optional<std::string> data = KpmLoadYamlRemote(url);
	if(!data.has_value())
	{
		KpmLogError("Failed to install from url: {}", url);
		return false;
	}
	return KpmInstallFromMemory(data.value());
}

static bool KpmInstallFromFile(const std::string& file)
{
	std::optional<std::string> data = KpmLoadYamlLocal(file);
	if(!data.has_value())
	{
		KpmLogError("Failed to install from file: {}", file);
		return false;
	}
	return KpmInstallFromMemory(data.value());
}

static void KpmInstallSetPath(const std::string& path)
{
	_kpm_install_prefix = path;
}

bool KpmInstall(const std::string& package, const std::string& path)
{
	if(!path.empty())
	{
		KpmInstallSetPath(path);
	}

	switch (KpmDetectMedia(package))
	{
		case KpmMediaType::LOCAL:
			return KpmInstallFromFile(package);
			break;
		case KpmMediaType::REMOTE:
			return KpmInstallFromUrl(package);
	}
	return false;
}
