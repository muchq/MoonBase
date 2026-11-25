import XCTest
@testable import WordChains

// The resume token is the player. It lives in the Keychain, device-only, so
// a restored backup can't put a second phone on the same seat; a token from
// before, in UserDefaults, moves there once.
final class ResumeTokenTests: XCTestCase {

    final class MemoryStore: ResumeTokenStore {
        var token: String?
        var refusesSaves = false
        init(_ token: String? = nil) { self.token = token }
        func load() -> String? { token }
        func save(_ token: String) -> Bool {
            guard !refusesSaves else { return false }
            self.token = token
            return true
        }
        func clear() { token = nil }
    }

    // MARK: - Moving the old token
    // The app's store reads the Keychain, then the old copy; every dial saves
    // the token it minted, which moves it. A move that can't happen leaves
    // the token where it was, still read: losing it loses the player.

    private func store(keychain: MemoryStore, legacy: MemoryStore) -> ResumeTokenStore {
        MovingTokenStore(keychain: keychain, legacy: legacy)
    }

    func testAnOldTokenIsReadUntilItMoves() {
        let legacy = MemoryStore("r-old"), keychain = MemoryStore()
        XCTAssertEqual(store(keychain: keychain, legacy: legacy).load(), "r-old")
    }

    func testSavingMovesTheTokenToTheKeychainAndDropsTheOldCopy() {
        let legacy = MemoryStore("r-old"), keychain = MemoryStore()
        XCTAssertTrue(store(keychain: keychain, legacy: legacy).save("r-1"))
        XCTAssertEqual(keychain.token, "r-1")
        XCTAssertNil(legacy.token)
    }

    // The Keychain's token is newer: an old one never shadows it.
    func testAKeychainTokenWinsOverAnOldOne() {
        let legacy = MemoryStore("r-old"), keychain = MemoryStore("r-new")
        XCTAssertEqual(store(keychain: keychain, legacy: legacy).load(), "r-new")
    }

    // The Keychain refusing a save must not cost the player: the token is
    // kept, and read, where it was.
    func testWhenTheKeychainRefusesTheTokenStaysWhereItWasAndIsStillRead() {
        let legacy = MemoryStore("r-old"), keychain = MemoryStore()
        keychain.refusesSaves = true
        let tokens = store(keychain: keychain, legacy: legacy)
        XCTAssertEqual(tokens.load(), "r-old")
        XCTAssertTrue(tokens.save("r-1"))
        XCTAssertNil(keychain.token)
        XCTAssertEqual(legacy.token, "r-1")
        XCTAssertEqual(tokens.load(), "r-1")
    }

    func testNoTokenStaysNoToken() {
        XCTAssertNil(store(keychain: MemoryStore(), legacy: MemoryStore()).load())
    }

    func testClearingClearsBoth() {
        let legacy = MemoryStore("r-old"), keychain = MemoryStore("r-new")
        store(keychain: keychain, legacy: legacy).clear()
        XCTAssertNil(keychain.token)
        XCTAssertNil(legacy.token)
    }

    // MARK: - The old store

    func testTheUserDefaultsStoreReadsWritesAndClearsItsKey() {
        let suite = "resume-token-test-\(UUID())"
        let defaults = UserDefaults(suiteName: suite)!
        defer { defaults.removePersistentDomain(forName: suite) }
        let store = DefaultsTokenStore(defaults: defaults)
        XCTAssertNil(store.load())
        XCTAssertTrue(store.save("r-1"))
        XCTAssertEqual(defaults.string(forKey: DefaultsTokenStore.key), "r-1")
        XCTAssertEqual(store.load(), "r-1")
        store.clear()
        XCTAssertNil(store.load())
    }

    // The key the app shipped with: a rename would strand every player's seat.
    func testTheOldKeyIsTheOneThatShipped() {
        XCTAssertEqual(DefaultsTokenStore.key, "games_hub_resume_token")
    }

    // MARK: - The Keychain store

    #if canImport(Security)
    // An unhosted or unsigned test bundle may have no access to the
    // data-protection keychain (errSecMissingEntitlement; `swift test` on
    // macOS has none): that is the runner, not the store, so the test says
    // so and skips.
    func testTheKeychainStoreSavesOverwritesAndClears() throws {
        let store = KeychainTokenStore()
        store.clear()
        defer { store.clear() }
        guard store.save("r-1") else { throw XCTSkip("no Keychain access in this test host") }
        XCTAssertEqual(store.load(), "r-1")
        XCTAssertTrue(store.save("r-2"))
        XCTAssertEqual(store.load(), "r-2")
        // Never backed up, never synced: this device only.
        XCTAssertEqual(store.accessibility(), kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly as String)
        store.clear()
        XCTAssertNil(store.load())
    }
    #endif
}
