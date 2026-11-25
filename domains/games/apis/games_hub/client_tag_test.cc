// The client a session mint is counted under: bounded, declared, and
// never minted from what a client sends.

#include "domains/games/apis/games_hub/client_tag.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "tools/cpp/runfiles/runfiles.h"

namespace games_hub {
namespace {

using bazel::tools::cpp::runfiles::Runfiles;

TEST(ClientTag, TheWebAndADeclaredAppVersionAreThemselves) {
  EXPECT_EQ(ClientTagOf("web"), (ClientTag{"web", "none"}));
  EXPECT_EQ(ClientTagOf("ios/0.1.9"), (ClientTag{"ios", "0.1.9"}));
}

TEST(ClientTag, AnAppVersionNotDeclaredIsOther) {
  EXPECT_EQ(ClientTagOf("ios/9.9.9"), (ClientTag{"ios", "other"}));
  EXPECT_EQ(ClientTagOf("ios"), (ClientTag{"ios", "other"}));
  EXPECT_EQ(ClientTagOf("ios/"), (ClientTag{"ios", "other"}));
}

// A client is whoever says so, but only the declared ones are counted as
// such: nothing sent can add a series.
TEST(ClientTag, AnythingElseIsOther) {
  for (const char* sent : {"", "android/1.0.0", "Web", "web/abc", "ios/0.1.9/x", "curl/8.0"}) {
    EXPECT_EQ(ClientTagOf(std::string(sent)), (ClientTag{"other", "none"})) << sent;
  }
  EXPECT_EQ(ClientTagOf(std::nullopt), (ClientTag{"other", "none"}));
}

TEST(ClientTag, EveryTagItCanReturnIsADeclaredSeries) {
  const std::vector<CounterSeries> series = ClientTagSeries();
  const auto declared = [&series](const ClientTag& tag) {
    return std::any_of(series.begin(), series.end(), [&](const CounterSeries& s) {
      return s.name == "hub_session_mints" &&
             s.attributes == std::map<std::string, std::string>{{"client", tag.client},
                                                                {"version", tag.version}};
    });
  };
  for (const ClientTag& tag : {ClientTag{"web", "none"}, ClientTag{"ios", "other"},
                               ClientTag{"other", "none"}}) {
    EXPECT_TRUE(declared(tag)) << tag.client << "/" << tag.version;
  }
  for (std::string_view version : kIosVersions) {
    EXPECT_TRUE(declared({"ios", std::string(version)})) << version;
  }
  EXPECT_EQ(series.size(), 3 + kIosVersions.size());
}

// A release that forgets the hub counts every session it mints as
// other: the app's own version must be declared.
TEST(ClientTag, TheAppsCurrentVersionIsDeclared) {
  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(&error));
  ASSERT_NE(runfiles, nullptr) << error;
  std::ifstream plist(runfiles->Rlocation("_main/domains/games/apps/wordchains_ios/Info.plist"));
  ASSERT_TRUE(plist.is_open());
  std::stringstream text;
  text << plist.rdbuf();
  std::smatch found;
  const std::string body = text.str();
  ASSERT_TRUE(std::regex_search(
      body, found,
      std::regex(R"(<key>CFBundleShortVersionString</key>\s*<string>([^<]+)</string>)")));
  const std::string version = found[1];
  EXPECT_NE(std::find(kIosVersions.begin(), kIosVersions.end(), version), kIosVersions.end())
      << "the app is " << version << "; add it to kIosVersions";
  EXPECT_EQ(ClientTagOf("ios/" + version), (ClientTag{"ios", version}));
}

}  // namespace
}  // namespace games_hub
