#pragma once
#include <string>

bool KpmInstall(const std::string& package, const std::string& path);
bool KpmRemove(const std::string& package);

std::string KpmGetCachePath();
