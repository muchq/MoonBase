import Foundation
#if canImport(Security)
import Security
#endif

/// Where the resume token, which is the player, is kept.
protocol ResumeTokenStore {
    func load() -> String?
    /// False when it could not be kept.
    func save(_ token: String) -> Bool
    func clear()
}

/// Where the token lived through 0.1.2. UserDefaults rides iCloud and device
/// backups, so a restored phone held the same seat as the first; it is read
/// only until the token moves to the Keychain (MovingTokenStore).
struct DefaultsTokenStore: ResumeTokenStore {
    /// The key the app shipped with: renaming it strands every player's seat.
    static let key = "games_hub_resume_token"
    let defaults: UserDefaults

    func load() -> String? { defaults.string(forKey: Self.key) }

    func save(_ token: String) -> Bool {
        defaults.set(token, forKey: Self.key)
        return true
    }

    func clear() { defaults.removeObject(forKey: Self.key) }
}

#if canImport(Security)
/// The token in the Keychain, this device only: never backed up, never
/// migrated to another phone. Always the data-protection keychain, iOS's
/// only one: macOS's older file keychain ignores kSecAttrAccessible.
struct KeychainTokenStore: ResumeTokenStore {
    private let query: [String: Any] = [
        kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "com.muchq.wordchains.hub",
        kSecAttrAccount as String: "resume-token",
        kSecUseDataProtectionKeychain as String: true,
    ]

    func load() -> String? {
        var request = query
        request[kSecReturnData as String] = true
        request[kSecMatchLimit as String] = kSecMatchLimitOne
        var found: CFTypeRef?
        guard SecItemCopyMatching(request as CFDictionary, &found) == errSecSuccess, let data = found as? Data else {
            return nil
        }
        return String(data: data, encoding: .utf8)
    }

    func save(_ token: String) -> Bool {
        let attributes: [String: Any] = [
            kSecValueData as String: Data(token.utf8),
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
        ]
        let updated = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
        guard updated == errSecItemNotFound else { return updated == errSecSuccess }
        return SecItemAdd(query.merging(attributes) { $1 } as CFDictionary, nil) == errSecSuccess
    }

    func clear() { SecItemDelete(query as CFDictionary) }

    /// The saved item's kSecAttrAccessible, for the test that pins it.
    func accessibility() -> String? {
        var request = query
        request[kSecReturnAttributes as String] = true
        request[kSecMatchLimit as String] = kSecMatchLimitOne
        var found: CFTypeRef?
        guard SecItemCopyMatching(request as CFDictionary, &found) == errSecSuccess,
              let attributes = found as? [String: Any] else { return nil }
        return attributes[kSecAttrAccessible as String] as? String
    }
}
#endif

/// The app's store: the Keychain, and the old copy in UserDefaults until the
/// token moves. Every dial saves the token it minted, which is the move; a
/// save the Keychain refuses keeps the token where it was, and still read,
/// since losing it loses the player.
struct MovingTokenStore: ResumeTokenStore {
    let keychain: ResumeTokenStore
    let legacy: ResumeTokenStore

    /// The Keychain's token is newer than any old copy.
    func load() -> String? { keychain.load() ?? legacy.load() }

    func save(_ token: String) -> Bool {
        guard keychain.save(token) else { return legacy.save(token) }
        legacy.clear()
        return true
    }

    func clear() {
        keychain.clear()
        legacy.clear()
    }
}

enum ResumeToken {
    /// The store the app uses.
    static func store(defaults: UserDefaults = .standard) -> ResumeTokenStore {
        let legacy = DefaultsTokenStore(defaults: defaults)
        #if canImport(Security)
        return MovingTokenStore(keychain: KeychainTokenStore(), legacy: legacy)
        #else
        return legacy
        #endif
    }
}
