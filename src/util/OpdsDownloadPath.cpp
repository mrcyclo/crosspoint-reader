#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "OpdsFilename.h"

std::string opdsDownloadPath(const std::string& author, const std::string& title) {
  const char* folder = SETTINGS.opdsDownloadFolder;
  bool haveFolder = folder[0] != '\0';
  // exists() first: every caller checks it before mkdir(). On a real failure
  // fall back to SD root so the download is never lost.
  if (haveFolder && !Storage.exists(folder) && !Storage.mkdir(folder)) {
    LOG_ERR("OPDS", "mkdir failed for %s, using SD root", folder);
    haveFolder = false;
  }
  std::string path;
  path.reserve(96);
  if (haveFolder) {
    path += folder;
  }
  path += '/';
  path += opdsBookFilename(author, title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  return path;
}
