#include "KOReaderUpload.h"

#include <HalStorage.h>
#include <Logging.h>

#include <utility>

#include "KOReaderCredentialStore.h"

namespace {

KOReaderRichPosition richPosition(const SavedProgressPosition& koPos, const CrossPointPosition& pos) {
  KOReaderRichPosition rich;
  const float pct = koPos.percentage < 0.0f ? 0.0f : koPos.percentage > 1.0f ? 1.0f : koPos.percentage;
  rich.pctQ = static_cast<uint32_t>(pct * 1000000.0f + 0.5f);
  rich.spineIndex = static_cast<uint16_t>(pos.spineIndex);
  rich.pageNumber = static_cast<uint16_t>(pos.pageNumber);
  rich.totalPages = static_cast<uint16_t>(pos.totalPages > 0 ? pos.totalPages : 1);
  if (pos.hasParagraphIndex) {
    rich.paragraphIndex = pos.paragraphIndex;
  }
  rich.xpath = koPos.xpath;
  return rich;
}

KOReaderMetadata metadata(const std::string& epubPath, const std::shared_ptr<Epub>& epub) {
  KOReaderMetadata meta;
  const auto lastSlash = epubPath.rfind('/');
  meta.filename = (lastSlash != std::string::npos) ? epubPath.substr(lastSlash + 1) : epubPath;
  if (epub) {
    meta.title = epub->getTitle();
    meta.authors = epub->getAuthor();
    if (KOREADER_STORE.supportsExtendedMetadata()) {
      Epub::SyncMetadata syncMetadata;
      if (epub->loadSyncMetadata(syncMetadata)) {
        meta.isbn = std::move(syncMetadata.isbn);
        meta.asin = std::move(syncMetadata.asin);
        meta.series = std::move(syncMetadata.series);
        meta.seriesIndex = syncMetadata.seriesIndex;
      } else {
        LOG_DBG("KOSync", "Could not read extended EPUB metadata; sending core metadata only");
      }
    }
  } else {
    LOG_ERR("KOSync", "Epub unavailable for metadata; sending filename only");
  }
  // Plugin sidecar ("<book>.meta.json") fields let a custom sync server route progress by a service book id.
  Storage.readFileToString("KOSync", epubPath + ".meta.json", 2 * 1024, meta.extraJson);
  return meta;
}

}  // namespace

KOReaderProgress buildKOReaderUpload(const std::string& epubPath, const std::string& documentHash,
                                     const SavedProgressPosition& koPos, const CrossPointPosition& pos,
                                     const std::shared_ptr<Epub>& epub) {
  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = koPos.xpath;
  progress.percentage = koPos.percentage;
  // The HTTP client also refuses to serialize the extension for servers without rich progress.
  if (KOREADER_STORE.supportsRichProgress()) {
    progress.position = richPosition(koPos, pos);
  }
  if (KOREADER_STORE.getSendMetadata()) {
    progress.metadata = metadata(epubPath, epub);
  }
  return progress;
}
