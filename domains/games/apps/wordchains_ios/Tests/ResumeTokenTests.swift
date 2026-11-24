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

    func testAnOldTokenMovesToTheKeychainAndLeavesUserDefaults() {
        let legacy = MemoryStore("r-old"), keychain = MemoryStore()
        ResumeToken.migrate(from: legacy, to: keychain)
        XCTAssertEqual(keychain.token, "r-old")
        XCTAssertNil(legacy.token)
    }

    // The Keychain's token is newer: an old one never overwrites it.
    func testAKeychainTokenWinsAndTheOldOneIsDropped() {
        let legacy = MemoryStore("r-old"), keychain = MemoryStore("r-new")
        ResumeToken.migrate(from: legacy, to: keychain)
        XCTAssertEqual(keychain.token, "r-new")
        XCTAssertNil(legacy.token)
    }

    // Losing the token is losing the player: a failed save keeps the old copy.
    func testAFailedMoveKeepsTheOldToken() {
        let legacy = MemoryStore("r-old"), keychain = MemoryStore()
        keychain.refusesSaves = true
        ResumeToken.migrate(from: legacy, to: keychain)
        XCTAssertNil(keychain.token)
        XCTAssertEqual(legacy.token, "r-old")
    }

    func testNoTokenStaysNoToken() {
        let legacy = MemoryStore(), keychain = MemoryStore()
        ResumeToken.migrate(from: legacy, to: keychain)
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
}
