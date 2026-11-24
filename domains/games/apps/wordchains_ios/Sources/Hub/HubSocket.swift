import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif

/// The hub's stream over URLSession: mint a session, open the socket with
/// its ticket, read frames until it drops, and dial again on the web's
/// schedule (ReconnectPolicy). The resume token is the player, kept in
/// UserDefaults so a relaunch, or a drop, comes back to the same seat
/// within the hub's grace.
///
/// A native client sends no Origin header, which the hub's origin gate
/// admits (games_hub smithy_contract_test, RequireOriginContract).
@MainActor
final class HubSocket: HubTransport {
    static let resumeTokenKey = "games_hub_resume_token"
    private static let mintTimeout: TimeInterval = 10
    /// How the hub closes a wire whose seat a newer connection took.
    private static let supersededCloseCode = 4001

    var events: HubTransportEvents?

    private let playURL: URL
    private let urlSession: URLSession
    private let defaults: UserDefaults
    private var dialer = Dialer()
    private var socket: URLSessionWebSocketTask?
    private var tasks: [Task<Void, Never>] = []

    init(playURL: URL = HubEndpoints.production, urlSession: URLSession = .shared, defaults: UserDefaults = .standard) {
        self.playURL = playURL
        self.urlSession = urlSession
        self.defaults = defaults
    }

    func connect() { run(dialer.connect()) }
    func disconnect() { run(dialer.disconnect()) }
    func resume() { run(dialer.resume()) }

    func send(_ command: HubCommand) {
        guard let socket else { return }
        socket.send(.string(String(decoding: command.encoded(), as: UTF8.self))) { _ in }
    }

    /// Carries out what the Dialer decided; it alone decides.
    private func run(_ effects: [Dialer.Effect]) {
        for effect in effects {
            switch effect {
            case let .status(status):
                events?.status(status)
            case .hangUp:
                tasks.forEach { $0.cancel() }
                tasks = []
                socket?.cancel(with: .goingAway, reason: nil)
                socket = nil
            case let .dial(gen):
                tasks.append(Task { [weak self] in await self?.dial(gen) })
            case let .wait(gen, delay):
                tasks.append(Task { [weak self] in
                    try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
                    guard let self else { return }
                    self.run(self.dialer.retryDue(gen))
                })
            }
        }
    }

    private func dial(_ gen: Int) async {
        let minted: HubSession
        do {
            minted = try await mint(resumeToken: defaults.string(forKey: Self.resumeTokenKey))
        } catch {
            run(dialer.dropped(gen))
            return
        }
        // Hung up, or replaced, during the mint: no socket.
        guard dialer.isCurrent(gen) else { return }
        defaults.set(minted.resumeToken, forKey: Self.resumeTokenKey)
        socket?.cancel(with: .goingAway, reason: nil)
        let socket = urlSession.webSocketTask(
            with: HubEndpoints.socketURL(play: playURL, ticket: minted.ticket),
            protocols: [HubEndpoints.subprotocol]
        )
        self.socket = socket
        socket.resume()
        await read(socket, gen)
    }

    private func mint(resumeToken: String?) async throws -> HubSession {
        var request = URLRequest(url: HubEndpoints.sessionURL(play: playURL), timeoutInterval: Self.mintTimeout)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "content-type")
        request.httpBody = HubEndpoints.mintBody(resumeToken: resumeToken)
        let (data, response) = try await urlSession.data(for: request)
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            throw URLError(.badServerResponse)
        }
        return try JSONDecoder().decode(HubSession.self, from: data)
    }

    private func read(_ socket: URLSessionWebSocketTask, _ gen: Int) async {
        while true {
            let message: URLSessionWebSocketTask.Message
            do {
                message = try await socket.receive()
            } catch {
                if socket === self.socket { self.socket = nil }
                run(dialer.dropped(gen, superseded: socket.closeCode.rawValue == Self.supersededCloseCode))
                return
            }
            // A socket a reconnect replaced has no say.
            guard dialer.isCurrent(gen) else { return }
            let data: Data
            switch message {
            case let .string(text): data = Data(text.utf8)
            case let .data(bytes): data = bytes
            @unknown default: continue
            }
            // A frame this client can't read is skipped, not fatal.
            guard let frame = try? HubFrame.decode(data) else { continue }
            switch frame {
            case .sessionReady:
                run(dialer.admitted(gen))
            case let .refused(exception, message):
                run(dialer.refused(gen, exception: exception, message: message))
            default:
                break
            }
            events?.frame(frame)
        }
    }
}
