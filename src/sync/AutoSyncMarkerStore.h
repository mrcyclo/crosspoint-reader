#pragma once

#include <AutoSyncCore.h>

#include <optional>
#include <string>

namespace autosync {

std::optional<Marker> loadMarker(const std::string& epubPath);
void saveMarker(const std::string& epubPath, const Marker& marker);

}  // namespace autosync
