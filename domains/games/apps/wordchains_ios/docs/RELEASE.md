# Word Chains iOS — Release & App Store Publishing

## Overview

The iOS release pipeline mirrors the wordchains CLI release process:

1. **Version detection** — CI watches `Info.plist` for `CFBundleShortVersionString` changes
2. **Tagging** — auto-creates `wordchains-ios-v<VERSION>` git tags
3. **Build** — Bazel builds the iOS app on macOS runners (`rules_apple` + `rules_swift`)
4. **Release** — creates a GitHub Release with the IPA artifact
5. **App Store upload** — pushes the IPA to App Store Connect via `xcrun altool`

## Build

```sh
# Generate the word graph (reuses the wordchains Rust CLI generator)
bazel build //domains/games/apps/wordchains:generate_graph_json

# Build the iOS app
bazel build //domains/games/apps/wordchains_ios:WordChains \
  --ios_minimum_os=17.0 \
  --apple_platform_type=ios
```

## Setup TODOs

The following must be completed before the first App Store release.

### 1. Apple Developer Account

- [ ] Enroll in the [Apple Developer Program](https://developer.apple.com/programs/) ($99/year)
- [ ] Accept all agreements in [App Store Connect](https://appstoreconnect.apple.com/)

### 2. App Store Connect — App Registration

- [ ] Create a new app record in App Store Connect
  - Bundle ID: `com.muchq.wordchains`
  - SKU: `wordchains-ios`
  - Primary language: English
- [ ] Fill in app metadata (description, keywords, category: Games > Word)
- [ ] Upload screenshots for required device sizes:
  - iPhone 6.9" (iPhone 16 Pro Max)
  - iPhone 6.7" (iPhone 15 Plus)
  - iPad Pro 13" (6th gen)
- [ ] Set age rating (likely 4+, no objectionable content)
- [ ] Set pricing (Free)

### 3. App Store Connect API Key (for CI)

This is required for automated uploads from GitHub Actions.

- [ ] Go to [App Store Connect > Users and Access > Integrations > App Store Connect API](https://appstoreconnect.apple.com/access/integrations/api)
- [ ] Generate a new API key with **App Manager** role
- [ ] Note the **Key ID** and **Issuer ID**
- [ ] Download the `.p8` private key file (only available once)
- [ ] Add the following GitHub Actions secrets to the `muchq/MoonBase` repository:

| Secret Name | Value |
|---|---|
| `APP_STORE_CONNECT_API_KEY_ID` | The Key ID from App Store Connect |
| `APP_STORE_CONNECT_ISSUER_ID` | The Issuer ID from App Store Connect |
| `APP_STORE_CONNECT_API_KEY` | Base64-encoded contents of the `.p8` file: `base64 -i AuthKey_XXXXXXXXXX.p8` |

### 4. Code Signing

- [ ] Create an **iOS Distribution** certificate in the Apple Developer portal
- [ ] Create a provisioning profile for `com.muchq.wordchains` (App Store distribution)
- [ ] For CI signing, add these additional GitHub Actions secrets:

| Secret Name | Value |
|---|---|
| `IOS_DISTRIBUTION_CERT_P12` | Base64-encoded .p12 distribution certificate |
| `IOS_DISTRIBUTION_CERT_PASSWORD` | Password for the .p12 file |
| `IOS_PROVISIONING_PROFILE` | Base64-encoded .mobileprovision file |

- [ ] Update the BUILD.bazel `ios_application` rule with the provisioning profile once created

### 5. App Icon

- [ ] Design a 1024x1024 app icon
- [ ] Place it at `Sources/Resources/Assets.xcassets/AppIcon.appiconset/icon-1024.png`
- [ ] Update the `Contents.json` to reference the filename

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
3. CI auto-tags and builds
4. IPA uploads to App Store Connect (once secrets are configured)
5. Submit for review in App Store Connect

## Manual Release (Tag Push)

```sh
git tag -a wordchains-ios-v0.2.0 -m "Release wordchains-ios v0.2.0"
git push origin wordchains-ios-v0.2.0
```

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
