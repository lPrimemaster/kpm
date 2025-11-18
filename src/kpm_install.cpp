#include "../kpm.h"
#include "logger.inl"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <cstdio>

#include <curl/curl.h>
#include <yaml-cpp/exceptions.h>
#include <yaml-cpp/node/parse.h>
#include <yaml-cpp/yaml.h>
#include <archive.h>
#include <archive_entry.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifndef _WIN32
#include <sys/utsname.h>
#else
#include <windows.h>
#include <sys/stat.h>

#ifndef S_ISDIR
#define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#endif
#endif

KPM_SET_LOG_PREFIX(KpmInstall);

static std::stringstream _manifest_stream;
static std::string _kpm_install_prefix;
static std::string _kpm_cache_path;

enum class KpmMediaType
{
	LOCAL,
	REMOTE,
	GITHUB
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

#ifdef WIN32
#undef WIN32
#endif

//kpm supported os
enum class KpmOs
{
	WIN32,
	LINUX,
	DARWIN
};

template<typename T> requires (std::is_same_v<T, nlohmann::json> || std::is_same_v<T, std::string> || std::is_same_v<T, YAML::Node>)
static std::optional<T> KpmGet(const std::string& url)
{
	CURL* curl = curl_easy_init();

	if(!curl)
	{
		KpmLogError("Failed to init CURL.");
		return std::nullopt;
	}

	std::string buffer;
	const auto write_handle = +[](void* ptr, size_t size, size_t nmemb, void* userdata) -> std::size_t {
		auto* stream = reinterpret_cast<std::string*>(userdata);
		std::size_t total_size = size * nmemb;
		stream->append(static_cast<char*>(ptr), total_size);
		return total_size;
	};

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_handle);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kpm-Client-App");

	CURLcode res = curl_easy_perform(curl);
	if(res != CURLE_OK)
	{
		KpmLogError("Failed to fetch github api info for given repository.");
		curl_easy_cleanup(curl);
		return std::nullopt;
	}

	curl_easy_cleanup(curl);

	if constexpr (std::is_same_v<T, nlohmann::json>)
	{
		return nlohmann::json::parse(buffer);
	}

	if constexpr (std::is_same_v<T, std::string>)
	{
		return buffer;
	}

	if constexpr (std::is_same_v<T, YAML::Node>)
	{
		return YAML::Load(buffer);
	}
}

template<typename T>
static std::int32_t KpmJsonFindInArray(const nlohmann::json& json, const std::string& key, const T& value)
{
	auto it = std::find_if(json.begin(), json.end(), [&](const nlohmann::json& obj) { return obj.contains(key) && obj[key] == value; });
	return it == json.end() ? -1 : std::distance(json.begin(), it);
}

static bool KpmCheckGithubRepo(const std::string& package)
{
	// The repository name can only contain ASCII letters, digits, and the characters ., -, and _
	std::regex re("^\\w+\\/[\\w|\\-|\\.|_]+");
	return std::regex_match(package, re);
}

static KpmMediaType KpmDetectMedia(const std::string& package)
{
	if(std::filesystem::exists(std::filesystem::path(package)))
	{
		KpmLogTrace("Media type = LOCAL");
		return KpmMediaType::LOCAL;
	}

	if(KpmCheckGithubRepo(package))
	{
		KpmLogTrace("Media type = GITHUB");
		return KpmMediaType::GITHUB;
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

std::string KpmGetCachePath()
{
	if(!_kpm_cache_path.empty())
	{
		return _kpm_cache_path;
	}

	switch (KpmDetectOs())
	{
		case KpmOs::WIN32:
		{
			_kpm_cache_path = std::string(std::getenv("APPDATA")) + "\\kpm\\"; 
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
			const char* home = std::getenv("PROGRAMFILES");
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
		// if(!S_ISDIR(archive_entry_filetype(entry)))
		// {
		KpmInstallManifestAddPath(filepath);
		// }

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

static std::optional<YAML::Node> KpmReadConfigFile(const std::string& file)
{
	try
	{
		return YAML::Load(file);
	}
	catch (const YAML::ParserException& e)
	{
		KpmLogError("YAML parsing error: {}", e.what());
		return std::nullopt;
    }
	catch (const YAML::Exception& e)
	{
		KpmLogError("YAML error: {}", e.what());
		return std::nullopt;
    }
}

static std::optional<std::string> KpmGithubFetchEndpoint(const std::string& repo, const YAML::Node& config)
{
	auto info = KpmGet<nlohmann::json>("https://api.github.com/repos/" + repo + "/releases");
	if(!info.has_value())
	{
		return std::nullopt;
	}

	auto json = info.value();

	if(json.is_object() && json.contains("message") && json["message"] == "Not Found")
	{
		KpmLogError("Failed to find the github repo: {}", repo);
		return std::nullopt;
	}

	if(json.is_array() && json.empty())
	{
		KpmLogError("Found repo {}, but no release is available.", repo);
		return std::nullopt;
	}

	std::int32_t index = 0; // latest release by default
	std::string tag = config["dist"]["tag"].as<std::string>();

	if(tag != "latest")
	{
		std::int32_t candidate = KpmJsonFindInArray(json, "tag_name", tag);

		if(candidate < 0)
		{
			KpmLogError("Could not find candidate tag {}.", tag);
			KpmLogWarning("Defaulting to latest tag available ('latest').");
		}

		index = candidate > 0 ? candidate : 0;
	}

	std::string endpoint = json[index]["assets"][0]["browser_download_url"];
	return endpoint.substr(0, endpoint.rfind('/'));
}

static std::vector<std::string> KpmLaunchScript(const std::filesystem::path& path, const YAML::Node& config)
{
	// Init python
	// NOTE: (César) Prevent bytecode generation
	
	PyConfig pyconfig;

	PyConfig_InitPythonConfig(&pyconfig);
	pyconfig.write_bytecode = 0;

	PyStatus pystatus = Py_InitializeFromConfig(&pyconfig);

	if(PyStatus_Exception(pystatus))
	{
		PyConfig_Clear(&pyconfig);
		Py_ExitStatusException(pystatus);
	}

	PyConfig_Clear(&pyconfig);

	auto abs_path = std::filesystem::absolute(path);

	auto* sys_path = PySys_GetObject("path");
	PyList_Append(sys_path, PyUnicode_FromString(abs_path.parent_path().string().c_str()));

	KpmLogTrace("Running: {}", path.filename().string());
	
	auto* script_name = PyUnicode_FromString(path.filename().replace_extension("").string().c_str());
	auto* module = PyImport_Import(script_name);
	Py_DECREF(script_name);

	if(!module)
	{
		PyErr_Print();
		KpmLogError("User must define a 'post_install' function.");
		KpmLogError("Failed to launch python user script.");
		Py_Finalize();
		return {};
	}

	auto* post_install_func = PyObject_GetAttrString(module, "post_install");
	if(!post_install_func || !PyCallable_Check(post_install_func))
	{
		PyErr_Print();
		Py_XDECREF(post_install_func);
		Py_DECREF(module);
		Py_Finalize();
		return {};
	}

	auto* args = PyTuple_Pack(
		3,
		PyUnicode_FromString(config["metadata"]["name"].as<std::string>().c_str()),
		PyUnicode_FromString(KpmGetCachePath().c_str()),
		PyUnicode_FromString(KpmGetInstallPath(config).c_str())
	);

	auto* result = PyObject_CallObject(post_install_func, args);
	Py_DECREF(args);

	if(!result || !PyDict_Check(result))
	{
		PyErr_Print();
		KpmLogError("Function 'post_install' must return a dictionary.");
		Py_XDECREF(result);
		Py_DECREF(post_install_func);
		Py_DECREF(module);
		Py_Finalize();
		return {};
	}

	auto* items = PyDict_GetItemString(result, "additional_files");
	std::vector<std::string> additional_files;

	if(!items || !PyList_Check(items))
	{
		PyErr_Print();
		KpmLogError("'additional_files' must return be a list.");
		Py_XDECREF(result);
		Py_DECREF(post_install_func);
		Py_DECREF(module);
		Py_Finalize();
		return {};
	}

	auto size = PyList_Size(items);
	for(Py_ssize_t i = 0; i < size; i++)
	{
		auto* item = PyList_GetItem(items, i);
		if(!PyUnicode_Check(item))
		{
			KpmLogError("additional_files[{}] is not a string.", i);
			continue;
		}

		additional_files.emplace_back(PyUnicode_AsUTF8(item));
	}

	Py_DECREF(result);
	Py_DECREF(post_install_func);
	Py_DECREF(module);
	Py_Finalize();

	return additional_files;
}

static void KpmPopulateManifestUserFile(const std::vector<std::string>& files)
{
	for(const auto& file : files)
	{
		// Check if file is in fact there
		const auto filepath = std::filesystem::path(file);
		if(!std::filesystem::exists(filepath))
		{
			KpmLogWarning("Additional file <{}> not found. Ignoring...", filepath.string());
			continue;
		}

		KpmInstallManifestAddPath(filepath.string());
	}
}

static void KpmRunUserPostInstallScript(const YAML::Node& config)
{
	if(!config["dist"]["deploy"])
	{
		// There is no user deploy script
		// Silently ignore
		return;
	}

	const auto path = std::filesystem::path(KpmGetInstallPath(config) + config["dist"]["deploy"].as<std::string>());
	if(!std::filesystem::exists(path))
	{
		KpmLogError("Deploy script referenced, but file <{}> does not exist.", path.filename().string());
		return;
	}

	// All ok, load the script and run it
	auto additional_files = KpmLaunchScript(path, config);

	if(!additional_files.empty())
	{
		KpmPopulateManifestUserFile(additional_files);
	}
}

static bool KpmInstallFromMemory(const std::string& data) noexcept
{
	std::optional<std::string> plat_tag = KpmGetPackagePlatformTag();
	if(!plat_tag.has_value())
	{
		KpmLogError("Could not find a valid or compatible system <os>_<arch> tag.");
		return false;
	}

	YAML::Node config = KpmReadConfigFile(data).value_or(YAML::Node{});

	if(!KpmValidateConfig(config))
	{
		KpmLogError("Invalid package config.");
		return false;
	}

	std::unordered_map<std::string, std::string> platform_map;
	std::string endpoint = config["dist"]["endpoint"].as<std::string>();

	// Resolve the endpoint if this is a github repo
	if(KpmCheckGithubRepo(endpoint))
	{
		auto candidate = KpmGithubFetchEndpoint(endpoint, config);
		if(!candidate.has_value())
		{
			KpmLogError("Invalid github repository or config.");
			return false;
		}

		endpoint = candidate.value();
	}

	if(!endpoint.ends_with('/'))
	{
		endpoint += '/';
	}

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
	
	KpmRunUserPostInstallScript(config);

	return KpmWriteManifest(config);
}

static std::tuple<bool, std::string> KpmGithubSupportsKpm(const std::string& repo)
{
	auto json_info_c = KpmGet<nlohmann::json>("https://api.github.com/repos/" + repo + "/contents");
	if(json_info_c && !json_info_c.value().empty())
	{
		std::int32_t index = KpmJsonFindInArray(json_info_c.value(), "path", "kpm.yaml");
		if(index == -1)
		{
			// Try yml
			index = KpmJsonFindInArray(json_info_c.value(), "path", "kpm.yml");
			if(index == -1)
			{
				return {false, ""};
			}
		}
		return {true, json_info_c.value()[index]["download_url"]};
	}
	return {false, ""};
}

static std::string KpmGithubProcessPackage(const std::string& repo)
{
	// Check if repo is valid
	auto [gh_support, gh_yaml_url] = KpmGithubSupportsKpm(repo);
	if(!gh_support)
	{
		return "";
	}

	return gh_yaml_url;
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

	std::string url = package;

	switch (KpmDetectMedia(package))
	{
		case KpmMediaType::LOCAL:
			return KpmInstallFromFile(package);
			break;
		case KpmMediaType::GITHUB:
			url = KpmGithubProcessPackage(package);
			[[fallthrough]];
		case KpmMediaType::REMOTE:
			return KpmInstallFromUrl(url);
	}
	return false;
}
