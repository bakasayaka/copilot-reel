#pragma once
#include "app_types.h"

namespace reel {
std::wstring ConfigPath();
std::vector<AppTarget> LoadTargets(std::wstring& error);
bool SaveTargets(const std::vector<AppTarget>& targets, std::wstring& error);
// Apps taken out of the reel keep their settings in parked.ini beside apps.ini, most recent
// first. A missing file is an empty list; a damaged one is left in place, ignored and reported.
std::wstring ParkedPath();
std::vector<AppTarget> LoadParked(std::wstring& error);
bool SaveParked(const std::vector<AppTarget>& parked, std::wstring& error);
}
