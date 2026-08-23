#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "domains/games/apis/one_d4_worker/retention_policy.h"
#include "gtest/gtest.h"

namespace one_d4_worker {
namespace {

// A path computed at runtime against a layout decided at build time, with
// nothing else comparing the two.
//
// retention_policy_test pins the computation, but only as a concatenation:
// both halves of that assertion encode the same belief about the runfiles
// layout, so it cannot tell a correct suffix from a wrong one. No other test
// can either — the worker's own tests run from inside their runfiles tree,
// which is not the deployed shape (binary beside runfiles, not within), so
// nothing that loads the policy exercises the derived path.
//
// Left unchecked, a wrong suffix is exit 1 with NOT_FOUND on every start of a
// deployed image, with a green build behind it. This compares the path the
// code computes for the deployed argv[0] against the paths pkg_tar writes.

/// The entrypoint bazel/rules/oci.bzl gives this image. linux_amd64_oci_binary
/// calls _create_oci_image(bin_name, bin_name, "/" + bin_name), so the binary
/// lands at the root under its own name.
constexpr char kEntrypoint[] = "/one_d4_worker";

/// The image layer itself, as pkg_tar writes it.
constexpr char kTar[] = "domains/games/apis/one_d4_worker/one_d4_worker_tar.tar";

/// Every path in the tar. A ustar archive is 512-byte blocks and each file
/// entry begins with a header whose first 100 bytes are the name, so the names
/// are readable without a tar library — which matters, because the point is to
/// read what shipped rather than to trust a description of it.
std::vector<std::string> TarEntries() {
  std::ifstream file(kTar, std::ios::binary);
  EXPECT_TRUE(file.good()) << "missing " << kTar;

  std::vector<std::string> names;
  char block[512];
  while (file.read(block, sizeof(block))) {
    if (block[0] == '\0') continue;  // the trailing zero blocks
    const size_t length = ::strnlen(block, 100);
    std::string name(block, length);
    names.push_back(name);

    // Skip the entry's content blocks: the size is octal in bytes [124, 136).
    const std::string size_field(block + 124, 12);
    const long long size = std::strtoll(size_field.c_str(), nullptr, 8);
    if (size > 0) file.seekg((size + 511) / 512 * 512, std::ios::cur);
  }
  return names;
}

bool Contains(const std::vector<std::string>& names, const std::string& wanted) {
  return std::find(names.begin(), names.end(), wanted) != names.end();
}

TEST(RetentionPolicyPackaging, TheDerivedPathIsWhereTheImagePutsTheFile) {
  const std::vector<std::string> names = TarEntries();
  ASSERT_FALSE(names.empty()) << "read no entries at all — the packaging moved";

  // Leading '/' dropped: tar paths are relative to the image root, which is
  // where the entrypoint's leading slash points.
  const std::string derived = RetentionPolicyPath(kEntrypoint).substr(1);

  EXPECT_TRUE(Contains(names, derived))
      << "the worker will look for " << derived << ", which the image does not ship";
}

/// The control. Without it the assertion above would hold against a manifest
/// this test simply failed to parse, or one listing every path imaginable.
TEST(RetentionPolicyPackaging, APathTheImageDoesNotShipIsNotFound) {
  EXPECT_FALSE(Contains(TarEntries(), "one_d4_worker.runfiles/_main/nowhere/at/all.json"));
}

/// And the binary really is at the entrypoint this test assumes, so the
/// argv[0] the derivation starts from is the real one rather than an
/// assumption that has quietly gone stale.
TEST(RetentionPolicyPackaging, TheBinaryIsAtTheEntrypoint) {
  EXPECT_TRUE(Contains(TarEntries(), std::string(kEntrypoint).substr(1)))
      << "no binary at " << kEntrypoint << " — the image layout changed, and the argv[0] this "
      << "derivation starts from is no longer the real one";
}

}  // namespace
}  // namespace one_d4_worker
