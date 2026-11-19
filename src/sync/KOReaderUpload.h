#pragma once

#include <Epub.h>

#include <memory>
#include <string>

#include "CrossPointPosition.h"
#include "KOReaderSyncClient.h"
#include "ProgressMapper.h"

// Builds the PUT body from the stored KOSync options. epub supplies the metadata; null sends the filename only.
KOReaderProgress buildKOReaderUpload(const std::string& epubPath, const std::string& documentHash,
                                     const SavedProgressPosition& koPos, const CrossPointPosition& pos,
                                     const std::shared_ptr<Epub>& epub);
