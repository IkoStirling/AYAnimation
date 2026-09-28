#include "SkeletonBakePublication.h"

#include <AYIO/PathSafety.h>
#include <AYIO/detail/PublicationRetry.h>

namespace ayt::anim::editor::detail {
namespace fs = std::filesystem;

ayt::io::FileTransactionResult
publishBakeFiles(const std::vector<BakePublicationFile> &files,
                 const fs::path &workspace, BakeRename renameOverride) {
  ayt::io::FileTransactionResult result;
  struct Entry {
    BakePublicationFile file;
    fs::path backup;
    bool existed = false;
    bool retired = false;
    bool installed = false;
  };
  std::vector<Entry> entries;
  std::error_code ec;
  for (const auto &file : files) {
    Entry entry;
    entry.file = {fs::absolute(file.staged, ec).lexically_normal(), {}};
    if (ec) {
      result.error = ec.message();
      return result;
    }
    entry.file.target = fs::absolute(file.target, ec).lexically_normal();
    if (ec) {
      result.error = ec.message();
      return result;
    }
    const auto stagedStatus = fs::symlink_status(entry.file.staged, ec);
    if (ec || !fs::is_regular_file(stagedStatus)) {
      result.error =
          "Expected regular staged bake file: " + file.staged.string();
      return result;
    }
    const auto targetStatus = fs::symlink_status(entry.file.target, ec);
    if (ec && ec != std::errc::no_such_file_or_directory) {
      result.error =
          "Unable to inspect bake destination: " + file.target.string();
      return result;
    }
    ec.clear();
    entry.existed = fs::exists(targetStatus);
    if (entry.existed && !fs::is_regular_file(targetStatus)) {
      result.error =
          "Bake destination is not a regular file: " + file.target.string();
      return result;
    }
    for (const auto &prior : entries) {
      if (ayt::io::path::isLexicallyWithin(prior.file.target,
                                           entry.file.target) ||
          ayt::io::path::isLexicallyWithin(entry.file.target,
                                           prior.file.target)) {
        result.error =
            "Duplicate/overlapping bake destination: " + file.target.string();
        return result;
      }
    }
    entry.backup = workspace / ("old-" + std::to_string(entries.size()));
    if (fs::exists(entry.backup, ec) || ec) {
      result.error =
          "Bake recovery path already exists: " + entry.backup.string();
      result.retainedBackup = entry.backup;
      return result; // never clear a potentially irreplaceable backup
    }
    entries.push_back(std::move(entry));
  }
  const auto rename = [&](const fs::path &from, const fs::path &to) {
    return ayt::io::detail::retryPublishFileOperation(
        [&](std::error_code &error) {
          try {
            if (renameOverride)
              error = renameOverride(from, to);
            else
              fs::rename(from, to, error);
          } catch (const std::exception &exception) {
            error = std::make_error_code(std::errc::io_error);
            result.warnings.push_back(exception.what());
          } catch (...) {
            error = std::make_error_code(std::errc::io_error);
          }
        },
        [](auto, auto) {});
  };
  const auto rollback = [&] {
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
      if (it->retired) {
        // Atomic file replacement: don't delete target before restore.
        const auto restored = rename(it->backup, it->file.target);
        if (restored.error) {
          result.error +=
              "; rollback failed: " +
              ayt::io::detail::publishFileError(
                  "restore bake output", it->backup, it->file.target, restored);
          if (result.retainedBackup.empty())
            result.retainedBackup = it->backup;
        } else
          it->retired = false;
      } else if (!it->existed && it->installed) {
        const auto removed = ayt::io::detail::retryPublishFileOperation(
            [&](std::error_code &error) { fs::remove(it->file.target, error); },
            [](auto, auto) {});
        if (removed.error)
          result.error += "; rollback failed to remove new output: " +
                          it->file.target.string() + ": " +
                          removed.error.message();
      }
    }
  };
  for (auto &entry : entries)
    if (entry.existed) {
      const auto retired = rename(entry.file.target, entry.backup);
      if (retired.error) {
        result.error = ayt::io::detail::publishFileError(
            "preserve bake output", entry.file.target, entry.backup, retired);
        rollback();
        return result;
      }
      entry.retired = true;
    }
  for (auto &entry : entries) {
    const auto installed = rename(entry.file.staged, entry.file.target);
    if (installed.error) {
      result.error = ayt::io::detail::publishFileError(
          "install bake output", entry.file.staged, entry.file.target,
          installed);
      rollback();
      return result;
    }
    entry.installed = true;
  }
  result.committed = true;
  for (const auto &entry : entries)
    if (entry.retired) {
      const auto removed = ayt::io::detail::retryPublishFileOperation(
          [&](std::error_code &error) { fs::remove(entry.backup, error); },
          [](auto, auto) {});
      if (removed.error) {
        result.warnings.push_back(
            "Unable to clean committed bake backup: " + entry.backup.string() +
            ": " + removed.error.message());
        if (result.retainedBackup.empty())
          result.retainedBackup = entry.backup;
      }
    }
  return result;
}
} // namespace ayt::anim::editor::detail
