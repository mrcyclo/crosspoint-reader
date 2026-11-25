#pragma once

#include <string>

namespace autosync {

// When a book last left the screen, kept in RTC memory: it survives deep sleep and silent
// restarts, not power loss. Both calls need a trusted clock; without one nothing counts as recent.
void noteReadingStopped(const std::string& bookPath);
bool bookReadRecently(const std::string& bookPath);

}  // namespace autosync
