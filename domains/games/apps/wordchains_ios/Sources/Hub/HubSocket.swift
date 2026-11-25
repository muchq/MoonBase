import Foundation
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
#if canImport(Network)
import Network
#endif

/// The hub's stream over URLSession: mint a session, open the socket with
/// its ticket, read frames until it drops, and dial again on the Dialer's
/// schedule, which also hears whether there is a network at all. The resume token is the player, kept in the
/// Keychain (ResumeToken) so a relaunch, or a drop, comes back to the same
/// seat within the hub's grace.
///
/// A native client sends no Origin header, which the hub's origin gate
/// admits (games_hub smithy_contract_test, RequireOriginContract).
@MainActor
final class HubSocket: HubTransport {
    nonisolated static let mintTimeout: TimeInterval = 10
    /// How the hub closes a wire whose seat a newer connection took.
    private static let supersededCloseCode = 4001

    var events: HubTransportEvents?

    private let playURL: URL
    private let urlSession: URLSession
    private let tokens: ResumeTokenStore
    private var dialer = Dialer()
    private var socket: URLSessionWebSocketTask?
    private var tasks: [Task<Void, Never>] = []
    #if canImport(Network)
    private let path = NWPathMonitor()
    #endif

    init(playURL: URL = HubEndpoints.production, urlSession: URLSession = .shared, tokens: ResumeTokenStore = ResumeToken.store()) {
        self.playURL = playURL
        self.urlSession = urlSession
        self.tokens = tokens
        #if canImport(Network)
        path.pathUpdateHandler = { [weak self] update in
            let online = update.status == .satisfied
            Task { @MainActor in
                guard let self else { return }
                self.run(self.dialer.pathChanged(online: online))
            }
        }
        path.start(queue: .main)
        #endif
    }

    #if canImport(Network)
    deinit { path.cancel() }
    #endif

    func connect() { run(dialer.connect()) }
    func disconnect() { run(dialer.disconnect()) }
    func resume() { run(dialer.resume()) }

    /// A command that can't go out, or fails on the way, is reported so
    /// the session stops waiting on its answer.
    func send(_ command: HubCommand) {
        guard let socket else {
            events?.sendFailed()
            return
        }
        socket.send(.string(String(decoding: command.encoded(), as: UTF8.self))) { [weak self] error in
            guard error != nil else { return }
            Task { @MainActor in
                // A socket a reconnect replaced has no say: its failure is
                // not the newer send's.
                guard let self, self.socket === socket else { return }
                self.events?.sendFailed()
            }
        }
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
            minted = try await mint(resumeToken: tokens.load())
        } catch let refused as MintRefused {
            run(dialer.dropped(gen, .mintRefused(status: refused.status, retryAfter: refused.retryAfter)))
            return
        } catch {
            run(dialer.dropped(gen))
            return
        }
        // Hung up, or replaced, during the mint: no socket.
        guard dialer.isCurrent(gen) else { return }
        _ = tokens.save(minted.resumeToken)
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
        guard let http = response as? HTTPURLResponse else { throw URLError(.badServerResponse) }
        guard (200..<300).contains(http.statusCode) else {
            throw MintRefused(status: http.statusCode,
                              retryAfter: HubEndpoints.retryAfter(http.value(forHTTPHeaderField: "Retry-After")))
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
                run(dialer.dropped(gen, socket.closeCode.rawValue == Self.supersededCloseCode ? .superseded : .lost))
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

/// The mint's non-2xx answer, as the Dialer weighs it.
private struct MintRefused: Error {
    let status: Int
    let retryAfter: TimeInterval?
}
