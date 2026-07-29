#include "ClippingsManager.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

namespace ClippingsManager {
bool appendKindleExport(const std::string& bookTitle, const std::string& author, const std::string& chapterTitle,
                        const int page, const std::string& text) {
  constexpr char EXPORT_PATH[] = "/My Clippings.txt";
  constexpr size_t MAX_EXPORT_TEXT = 2000;
  FsFile file = Storage.open(EXPORT_PATH, O_WRONLY | O_CREAT | O_AT_END);
  if (!file) {
    LOG_ERR("CLIP", "Unable to append %s", EXPORT_PATH);
    return false;
  }

  std::string header = bookTitle;
  if (!author.empty()) header += " (" + author + ")";
  header += "\n- Your Highlight";
  if (!chapterTitle.empty()) header += " | " + chapterTitle;
  header += " | Page " + std::to_string(page) + "\n\n";
  const size_t textLength = std::min(text.size(), MAX_EXPORT_TEXT);
  const bool ok =
      file.write(reinterpret_cast<const uint8_t*>(header.data()), header.size()) == header.size() &&
      file.write(reinterpret_cast<const uint8_t*>(text.data()), textLength) == textLength &&
      file.write(reinterpret_cast<const uint8_t*>("\n==========\n"), 12) == 12;
  if (ok) file.flush();
  file.close();
  return ok;
}
}  // namespace ClippingsManager
