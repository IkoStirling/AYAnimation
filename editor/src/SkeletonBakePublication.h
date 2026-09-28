#pragma once

#include <AYIO/FileTransaction.h>

namespace ayt::anim::editor::detail {

struct BakePublicationFile {
  std::filesystem::path staged;
  std::filesystem::path target;
};

// Internal deterministic filesystem seam; not an engine/gameplay API.
// Production passes no override. The workspace is exclusively reserved by
// caller.
using BakeRename = std::function<std::error_code(
    const std::filesystem::path &, const std::filesystem::path &)>;
ayt::io::FileTransactionResult
publishBakeFiles(const std::vector<BakePublicationFile> &files,
                 const std::filesystem::path &workspace,
                 BakeRename renameOverride = {});

} // namespace ayt::anim::editor::detail
