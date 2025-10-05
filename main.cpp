// Brief : Proof of concept
#include <CLI/CLI.hpp>
#include "kpm.h"

int main(int argc, char* argv[])
{
	CLI::App app {"KISS package manager.\nJust keep it simple.", "kpm"};

	CLI::App* install = app.add_subcommand("install", "Install a package.");
	CLI::App* pack    = app.add_subcommand("pack", "Create a package.");
	CLI::App* remove  = app.add_subcommand("remove", "Remove a package.");

	std::string package_name;
	std::string install_prefix;

	install->add_option("package", package_name, "The package YAML file.")->required();
	install->add_option("--prefix", install_prefix, "Where to install the package.");

	remove->add_option("package", package_name, "The package to remove.")->required();

	// idea is something as simple as:
	// kpm install <file>.yaml : e.g. install package from local file
	// kpm install <url>.yaml  : e.g. install package from url file
	// kpm install <file/url> --install-dir='/opt/custom/dir' : e.g. install package and put it at specified location
	// kpm remove <package> : e.g. remove package from system
	// kpm pack kpm.yaml : e.g. create a new package using the build script this calls user code to create package files on the given system
	
	// Ideas for this simplicity manager
	// 1. Install directly from human readable format <yaml proposed>
	// 2. Get currect arch and os
	// 3. Fetch user provided correct binary package (e.g. win_amd64.tar.gz or unix_x86_64.tar.gz, etc etc)
	// 4. If user did not provide prebuild binaries for our arch-os combo, try to compile from source (much like python sdist)
	// 5. Installing where and when is controlled by the user (package creator) <yaml> config
	// 6. We are able to overwrite user configs as the installing user
	// 7. Package creator is allowed to run scripts on our system (KISS, if you are afraid of malware, just read the <url>.yaml)
	// 8. Save manifest of installation so that we can kpm remove <package> whenever we want
	// 9. Basically slowly add support for all types of packages for easy configurations, for example:
	// 		- Runtimes (executables)
	// 		- Libraries (.a, .so, .dll)
	// 		- Source files, i.e.: header files, python libraries
	// 		- CMake configure files (so that the end user can find_package(<package>)
	// 		- Other build systems configure files
	// 10. In reallity all these supports can be scripted by the package author
	// 11. Generate install manifest so that the end user knows what he is installing and where (this could be done at package time)
	// 12. Need a kpm cache for package registry etc

	CLI11_PARSE(app, argc, argv);

	if(install->parsed())
	{
		KpmInstall(package_name, install_prefix);
	}
	else if(remove->parsed())
	{
	}
	else if(pack->parsed())
	{
	}
	else
	{
		std::cout << app.help() << std::endl;
	}

	return 0;
}
