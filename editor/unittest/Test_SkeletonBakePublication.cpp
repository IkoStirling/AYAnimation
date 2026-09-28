#include "../src/SkeletonBakePublication.h"
#include <AYIO/File.h>
#include <AYTestFixtures.h>

namespace {
namespace fs = std::filesystem;
using namespace ayt::anim::editor::detail;

struct PublicationFixture {
  ayt::test::ScratchDirectory scratch{"bake-publication"};
  fs::path root = scratch.path();
  fs::path workspace = root / "workspace";
  std::vector<BakePublicationFile> files;
  PublicationFixture() {
    fs::create_directory(workspace);
    for (int i = 0; i < 3; ++i) {
      files.push_back({workspace / ("new-" + std::to_string(i)),
                       root / ("target-" + std::to_string(i))});
      CHECK(ayt::io::File::writeAllText(files.back().staged.string(),
                                        "new-" + std::to_string(i)));
      if (i != 1)
        CHECK(ayt::io::File::writeAllText(files.back().target.string(),
                                          "old-" + std::to_string(i)));
    }
  }
};

std::error_code nativeRename(const fs::path &from, const fs::path &to) {
  std::error_code error;
  fs::rename(from, to, error);
  return error;
}
} // namespace

TEST_SUITE(SkeletonBakePublicationTests)

TEST_CASE(mixed_create_replace_publishes_all_and_cleans_backups) {
  PublicationFixture fixture;
  const auto result = publishBakeFiles(fixture.files, fixture.workspace);
  CHECK(result.committed);
  CHECK(result.error.empty());
  for (int i = 0; i < 3; ++i) {
    CHECK(ayt::io::File::readAllText(fixture.files[i].target.string()) ==
          "new-" + std::to_string(i));
    CHECK(!fs::exists(fixture.workspace / ("old-" + std::to_string(i))));
  }
}

TEST_CASE(failed_install_rolls_back_replaced_and_new_outputs) {
  PublicationFixture fixture;
  const auto result = publishBakeFiles(
      fixture.files, fixture.workspace, [&](const auto &from, const auto &to) {
        return from == fixture.files[2].staged
                   ? std::make_error_code(std::errc::invalid_argument)
                   : nativeRename(from, to);
      });
  CHECK(!result.committed);
  CHECK(!result.error.empty());
  CHECK(result.retainedBackup.empty());
  CHECK(ayt::io::File::readAllText(fixture.files[0].target.string()) ==
        "old-0");
  CHECK(!fs::exists(fixture.files[1].target));
  CHECK(ayt::io::File::readAllText(fixture.files[2].target.string()) ==
        "old-2");
}

TEST_CASE(failed_rollback_retains_old_bytes_and_reports_exact_path) {
  PublicationFixture fixture;
  const auto backup = fixture.workspace / "old-0";
  const auto result = publishBakeFiles(
      fixture.files, fixture.workspace, [&](const auto &from, const auto &to) {
        return from == fixture.files[2].staged || from == backup
                   ? std::make_error_code(std::errc::invalid_argument)
                   : nativeRename(from, to);
      });
  CHECK(!result.committed);
  CHECK(result.retainedBackup == backup);
  CHECK(result.error.find("rollback failed") != std::string::npos);
  CHECK(result.error.find(backup.generic_string()) != std::string::npos);
  CHECK(ayt::io::File::readAllText(backup.string()) == "old-0");
  CHECK(ayt::io::File::readAllText(fixture.files[0].target.string()) ==
        "new-0");
  CHECK(!fs::exists(fixture.files[1].target));
}

TEST_CASE(preflight_rejects_unsafe_targets_without_writes) {
  PublicationFixture fixture;
  auto duplicate = fixture.files;
  duplicate.push_back(fixture.files[0]);
  CHECK(!publishBakeFiles(duplicate, fixture.workspace).committed);
  fs::create_directory(fixture.files[1].target);
  CHECK(!publishBakeFiles(fixture.files, fixture.workspace).committed);
  CHECK(fs::is_directory(fixture.files[1].target));
  CHECK(ayt::io::File::readAllText(fixture.files[0].target.string()) ==
        "old-0");
  const auto backup = fixture.workspace / "old-0";
  CHECK(ayt::io::File::writeAllText(backup.string(), "recover-me"));
  CHECK(!publishBakeFiles({fixture.files[0]}, fixture.workspace).committed);
  CHECK(ayt::io::File::readAllText(backup.string()) == "recover-me");
}

TEST_CASE(callback_exception_rolls_back_all_outputs) {
  PublicationFixture fixture;
  const auto result = publishBakeFiles(
      fixture.files, fixture.workspace,
      [&](const auto &from, const auto &to) -> std::error_code {
        if (from == fixture.files[2].staged)
          throw std::runtime_error("injected failure");
        return nativeRename(from, to);
      });
  CHECK(!result.committed);
  CHECK(!result.warnings.empty());
  CHECK(ayt::io::File::readAllText(fixture.files[0].target.string()) ==
        "old-0");
  CHECK(!fs::exists(fixture.files[1].target));
}

TEST_SUITE_END
