# Word Chains iOS — Release & App Store Publishing

## Overview

The iOS release pipeline mirrors the wordchains CLI release process:

1. **Version detection** — after each green `main` build, CI reads
   `CFBundleShortVersionString` from `Info.plist` and releases it if no
   `wordchains-ios-v<VERSION>` tag exists
2. **Tagging** — creates that tag; the tag is the record that the version shipped
3. **Build** — Bazel builds the iOS app on macOS runners (`rules_apple` + `rules_swift`).
   With the code-signing secrets configured, this is a signed **device (arm64)** build;
   without them it falls back to an unsigned simulator build (not App Store uploadable)
4. **Release** — creates a GitHub Release with the Bazel-produced IPA artifact
5. **App Store upload** — pushes the IPA to App Store Connect (TestFlight) via
   `fastlane pilot` using the App Store Connect API key (skipped with a warning
   until the secrets are configured)

## Testing Before Publication

### Run unit tests (fastest)

A `Package.swift` is provided for running tests locally via Swift Package Manager.
This exercises the WordGraph BFS algorithms, `isOneLetterAway` validation, and
graph query helpers, and the race client's wire codec, state and text
(`Sources/Hub`) — the same logic paths used in the iOS app.

```sh
cd domains/games/apps/wordchains_ios
swift test
```


### Run in the iOS Simulator (Xcode)

To test the full app UI on a simulator:

```sh
cd domains/games/apps/wordchains_ios
open Package.swift
```

In Xcode:
1. Select the **WordChains** scheme
2. Choose an iPhone simulator (e.g. iPhone 17, iOS 27)
3. Press **Cmd+R** to build and run

> **Note:** The app loads `word_graph.json` from the bundle at startup. When
> running via SPM in Xcode, the graph won't be bundled automatically. For
> full-app testing with the real graph, use the Bazel build (see below) or
> manually copy the generated graph into the simulator's app container.

### Build with Bazel

The canonical build uses Bazel (the repo's pinned version, currently 9.x —
`rules_apple` 4.5+ supports Bazel 9). These are the same commands the
`test-ios` CI job runs:

```sh
# Build the iOS app (simulator):
bazel build //domains/games/apps/wordchains_ios:WordChains \
  --ios_minimum_os=17.0 \
  --apple_platform_type=ios

# Run the Bazel-based unit tests:
# (swift.enable_testing is needed for @testable imports because the repo
# defaults to -c opt; the explicit simulator device avoids the runner
# pairing an ancient device type with the newest iOS runtime.)
bazel test //domains/games/apps/wordchains_ios:WordChainsTests \
  --ios_minimum_os=17.0 \
  --features=swift.enable_testing \
  --ios_simulator_device="iPhone 17" \
  --ios_simulator_version=27.0
```

> **Simulator runtime:** The build requires an iOS simulator runtime matching
> your Xcode SDK version. If you see "No simulator runtime version available",
> install the matching runtime: **Xcode > Settings > Platforms > + (iOS)**.

### Manual testing checklist

Before submitting to the App Store, verify these scenarios:

- [ ] **Quick Play** — start a game, enter valid words, reach the target word
- [ ] **Daily Challenge** — same puzzle generates for the same date
- [ ] **Time Attack** — timer counts down, new puzzle loads after solving
- [ ] **Input validation** — wrong length, not in dictionary, not one-letter-away, already used
- [ ] **Hint system** — lightbulb button fills the next word on the shortest path
- [ ] **Scoring** — optimal solve gives 150 pts (100 base + 50 bonus), extra steps reduce score
- [ ] **Difficulty levels** — Easy (3 letters), Medium (4), Hard (5)
- [ ] **Dark mode** — colors remain readable in dark appearance
- [ ] **Race friends** — create a room, join it from the web lobby (muchq.com/games) by
  its code, open and join a race table from both, race to the end, Next race; a
  refused word shows the hub's reason; backgrounding and returning keeps the seat;
  alone at a table, **Practise** races with no wins, and a join ends it

## Build

```sh
# Simulator build (what CI's test-ios job does):
bazel build //domains/games/apps/wordchains_ios:WordChains \
  --ios_minimum_os=17.0 \
  --apple_platform_type=ios

# Signed device build for the App Store (what the release workflow does;
# requires the distribution cert in a keychain and the provisioning profile
# at domains/games/apps/wordchains_ios/WordChains.mobileprovision):
bazel build //domains/games/apps/wordchains_ios:WordChains \
  --ios_minimum_os=17.0 \
  --ios_multi_cpus=arm64 \
  --define=ios_release_signing=true
```

## Known Issues

- **`manual` tags on the Apple bundling targets:** `ios_application` and
  `ios_unit_test` transition to an iOS target platform before
  `target_compatible_with` is evaluated, so constraints alone can't keep
  Linux CI off them (Linux analysis fails resolving an Apple C++ toolchain
  instead of skipping). They are tagged `manual` and built by explicit label
  in the `test-ios` CI job and the release workflow.

## Setup TODOs

The following must be completed before the first App Store release.

### 1. Apple Developer Account

- [x] Enroll in the [Apple Developer Program](https://developer.apple.com/programs/) ($99/year)
- [ ] Accept all agreements in [App Store Connect](https://appstoreconnect.apple.com/)

### 2. App Store Connect — App Registration

- [x] Create a new app record in App Store Connect
  - Bundle ID: `com.muchq.wordchains`
  - SKU: `wordchains-ios`
  - Primary language: English
- [ ] Fill in app metadata (description, keywords, category: Games > Word)
- [ ] Upload screenshots for required device sizes:
  - iPhone 6.9" (iPhone 16 Pro Max)
  - iPhone 6.7" (iPhone 15 Plus)
- [ ] Set age rating (likely 4+, no objectionable content)
- [ ] Set pricing (Free)

### 3. App Store Connect API Key (for CI)

This is required for automated uploads from GitHub Actions.

- [x] Go to [App Store Connect > Users and Access > Integrations > App Store Connect API](https://appstoreconnect.apple.com/access/integrations/api)
- [x] Generate a new API key with **App Manager** role
- [x] Note the **Key ID** and **Issuer ID**
- [x] Download the `.p8` private key file (only available once)
- [x] Add the following GitHub Actions secrets to the `muchq/MoonBase` repository:

| Secret Name | Value |
|---|---|
| `APP_STORE_CONNECT_API_KEY_ID` | The Key ID from App Store Connect |
| `APP_STORE_CONNECT_ISSUER_ID` | The Issuer ID from App Store Connect |
| `APP_STORE_CONNECT_API_KEY` | Base64-encoded contents of the `.p8` file: `base64 -i AuthKey_XXXXXXXXXX.p8` |

### 4. Code Signing

- [x] Create an **iOS Distribution** certificate in the Apple Developer portal
- [x] Create a provisioning profile for `com.muchq.wordchains` (App Store distribution)
- [x] For CI signing, add these additional GitHub Actions secrets:

| Secret Name | Value |
|---|---|
| `IOS_DISTRIBUTION_CERT_P12` | Base64-encoded .p12 distribution certificate |
| `IOS_DISTRIBUTION_CERT_PASSWORD` | Password for the .p12 file |
| `IOS_PROVISIONING_PROFILE` | Base64-encoded .mobileprovision file |

The CI wiring already exists: when the secrets are present, the release
workflow imports the certificate into a throwaway keychain, decodes the
profile to `domains/games/apps/wordchains_ios/WordChains.mobileprovision`
(gitignored), and builds a signed device IPA with
`--ios_multi_cpus=arm64 --define=ios_release_signing=true`. Until then it
falls back to an unsigned simulator build and skips the App Store upload.

> **First-release check:** entitlements are derived from the provisioning
> profile by `rules_apple`. If the first signed build fails codesigning or
> upload validation, an explicit `entitlements` attribute on the
> `ios_application` may be needed.

### 5. App Icon

A placeholder icon ships at
`Sources/Resources/Assets.xcassets/AppIcon.appiconset/icon-1024.png`. To replace
it, keep the filename or update `Contents.json`: 1024x1024 PNG, RGB with no
alpha. `:app_store_test` enforces those rules, plus `arm64` in
`UIRequiredDeviceCapabilities` and `ITSAppUsesNonExemptEncryption` = false, on
Linux CI.

### 6. Word Graph Bundle

The app bundles `word_graph.json` — the same graph format produced by the Rust
`wordchains` CLI. The Bazel build generates this automatically via the
`//domains/games/apps/wordchains:generate_graph_json` rule.

No manual steps needed here; the BUILD.bazel aliases this artifact.

## Versioning

- **`CFBundleShortVersionString`** (marketing version) — bump in `Info.plist` to trigger a release
- **`CFBundleVersion`** (build number) — auto-set from `github.run_number` in CI

To release a new version:

1. Bump `CFBundleShortVersionString` in `Info.plist`
2. Merge to `main`
3. Once `main` is green, CI tags `wordchains-ios-v<VERSION>` and builds a signed device IPA
4. IPA uploads to App Store Connect / TestFlight via `fastlane pilot`
   (once secrets are configured)
5. Submit for review in App Store Connect (still manual; automatable later
   with `fastlane deliver` or the App Store Connect API)

The tag is created before the build. If the build or upload fails, fix it and
bump the version: the fix's merge releases the new one. Re-running the failed
run replays its own copy of the workflow, broken step included, and a deleted
tag is only noticed by the next green `main`. Pushing a tag by hand releases
nothing: it marks that version as already shipped.

## Racing on the games hub

**Race friends** on the menu speaks the games hub's play stream
(`wss://api.muchq.com/games/v2/play`, `domains/games/apis/games_hub`): the
session mint, then the JSON-text socket, the same protocol the web lobby speaks.
`Sources/Hub` holds the wire codec (`HubWire`), the race state and the client
(`RaceSession`), and the socket (`HubSocket`); the hub checks every word, so
the app holds no race rules. The resume token is kept in the Keychain, this
device only, so a restored backup can't put a second phone on the same seat;
a token from 0.1.2 or earlier is read from `UserDefaults` until the first
dial moves it there (and stays there if the Keychain refuses it), which is why
the privacy manifest still declares `UserDefaults`. There
is no chat. The single-player modes need no network.

**App Privacy** (App Store Connect, and `Sources/Resources/PrivacyInfo.xcprivacy`,
which `:app_store_test` requires while the code uses `UserDefaults`): racing sends
a server-minted player id (**Identifiers → User ID**) and the race's words
(**Gameplay Content**) to the hub, for app functionality only, not linked to the
person, no tracking.

**App Review notes:** alone at a table, a reviewer can **Practise**: **Race
friends → Create a room → Open a race table → Practise** races the clock with
nothing recorded. To see a real race, open `https://muchq.com/games/room/<code>`
in a browser, join the same table as a second seat (which ends any practice),
and tap **Start the race** in the app.

## Architecture

The iOS app reuses the `wordchains` Rust library indirectly:

- **Same graph format** — loads `word_graph.json` (JSON-serialized `Graph` struct)
- **Same algorithms** — BFS shortest path and all-shortest-paths are reimplemented in Swift
  following the exact same algorithm structure as `graph.rs` in the Rust lib
- **Same dictionary** — built from the `@english_words` Bazel external dependency

```
wordchains (Rust lib)
  └─ generate-graph ──► word_graph.json ──► bundled in iOS app
                                               │
wordchains_ios (Swift)                         │
  └─ WordGraph.swift ◄─── loads JSON ─────────┘
  └─ shortestPath()    (same BFS as Rust bfs_for_target)
  └─ allShortestPaths() (same BFS as Rust find_all_shortest_paths)
```
