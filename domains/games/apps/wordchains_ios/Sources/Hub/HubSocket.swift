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

    var events: HubTransportEvents?

    private let playURL: URL
    private let urlSession: URLSession
    private let defaults: UserDefaults
    private var socket: URLSessionWebSocketTask?
    private var dialing: Task<Void, Never>?
    private var policy = ReconnectPolicy()
    private var closed = true

    init(playURL: URL = HubEndpoints.production, urlSession: URLSession = .shared, defaults: UserDefaults = .standard) {
        self.playURL = playURL
        self.urlSession = urlSession
        self.defaults = defaults
    }

    /// Idempotent while a dial is under way or a socket is up.
    func connect() {
        closed = false
        policy = ReconnectPolicy()
        guard socket == nil, dialing == nil else { return }
        dial()
    }

    func disconnect() {
        closed = true
        dialing?.cancel()
        dialing = nil
        socket?.cancel(with: .goingAway, reason: nil)
        socket = nil
        events?.status(.idle)
    }

    func send(_ command: HubCommand) {
        guard let socket else { return }
        socket.send(.string(String(decoding: command.encoded(), as: UTF8.self))) { _ in }
    }

    private func dial() {
        events?.status(.connecting)
        dialing = Task { [weak self] in
            guard let self else { return }
            do {
                let minted = try await self.mint(resumeToken: self.defaults.string(forKey: Self.resumeTokenKey))
                self.dialing = nil
                // Hung up during the mint: no socket, or a torn-down stream
                // would hold a seat.
                if self.closed { return }
                self.defaults.set(minted.resumeToken, forKey: Self.resumeTokenKey)
                let socket = self.urlSession.webSocketTask(
                    with: HubEndpoints.socketURL(play: self.playURL, ticket: minted.ticket),
                    protocols: [HubEndpoints.subprotocol]
                )
                self.socket = socket
                socket.resume()
                await self.read(socket)
            } catch {
                self.dialing = nil
                self.dropped(nil)
            }
        }
    }

    private func mint(resumeToken: String?) async throws -> HubSession {
        var request = URLRequest(url: HubEndpoints.sessionURL(play: playURL), timeoutInterval: Self.mintTimeout)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "content-type")
        request.httpBody = try JSONSerialization.data(withJSONObject: resumeToken.map { ["resumeToken": $0] } ?? [:])
        let (data, response) = try await urlSession.data(for: request)
        guard let http = response as? HTTPURLResponse, (200..<300).contains(http.statusCode) else {
            throw URLError(.badServerResponse)
        }
        return try JSONDecoder().decode(HubSession.self, from: data)
    }

    private func read(_ socket: URLSessionWebSocketTask) async {
        while true {
            let message: URLSessionWebSocketTask.Message
            do {
                message = try await socket.receive()
            } catch {
                dropped(socket)
                return
            }
            // A socket a reconnect replaced has no say.
            guard socket === self.socket else { return }
            let data: Data
            switch message {
            case let .string(text): data = Data(text.utf8)
            case let .data(bytes): data = bytes
            @unknown default: continue
            }
            // A frame this client can't read is skipped, not fatal.
            guard let frame = try? HubFrame.decode(data) else { continue }
            if case .sessionReady = frame {
                // Admission, not an open socket, is progress.
                policy.admitted()
                events?.status(.connected)
            }
            events?.frame(frame)
        }
    }

    /// The socket closed or the mint failed: dial again, or give up.
    private func dropped(_ from: URLSessionWebSocketTask?) {
        if let from, from !== socket { return }
        socket = nil
        if closed { return }
        guard let delay = policy.next() else {
            events?.status(.lost("Lost connection to the games hub"))
            return
        }
        events?.status(.connecting)
        dialing = Task { [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
            guard let self, !self.closed, self.socket == nil else { return }
            self.dialing = nil
            self.dial()
        }
    }
}
