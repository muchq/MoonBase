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
/// once, to move the token to the Keychain.
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
/// migrated to another phone.
struct KeychainTokenStore: ResumeTokenStore {
    private let query: [String: Any] = [
        kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "com.muchq.wordchains.hub",
        kSecAttrAccount as String: "resume-token",
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
}
#endif

enum ResumeToken {
    /// Moves a token from the old store once. A token already in the new
    /// store is newer and wins; a move that fails keeps the old copy, since
    /// losing the token loses the player.
    static func migrate(from legacy: ResumeTokenStore, to store: ResumeTokenStore) {
        guard let old = legacy.load() else { return }
        if store.load() == nil, !store.save(old) { return }
        legacy.clear()
    }

    /// The store the app uses: the Keychain, with any old token moved in.
    static func store(defaults: UserDefaults = .standard) -> ResumeTokenStore {
        let legacy = DefaultsTokenStore(defaults: defaults)
        #if canImport(Security)
        let keychain = KeychainTokenStore()
        migrate(from: legacy, to: keychain)
        return keychain
        #else
        return legacy
        #endif
    }
}
