$version: "2.0"

namespace moonbase.voice

// A room's voice (#1590): who in the room is talking, and the WebRTC
// negotiation between them. Audio never reaches the hub — browsers
// connect to each other, a full mesh capped at six — so the hub keeps
// only who is in voice and relays each signal to the one peer it names.
// Nothing here is persisted, and voice is per instance, like the world.
//
// The way in is the `voice` member of the room's Play stream
// (games.smithy). Voice is the session's room's: joining it needs a
// room, and leaving the room, or the socket closing, leaves voice with it.
//
// Who offers: the joiner offers to each member on its roster, and members
// answer and never offer to a joiner. Renegotiation afterwards is perfect
// negotiation, the lexicographically smaller playerId the polite peer.
//
// Each join is a new epoch, and a signal names the epoch of the join it
// is for: a signal meant for someone who has since left and come back is
// refused rather than applied to their new connection.

/// The voice envelope on the room stream: exactly one action.
structure VoiceCommand {
    @required
    action: VoiceAction
}

union VoiceAction {
    join: JoinVoice
    leave: LeaveVoice
    signal: SendSignal
}

/// The voice envelope on the event stream: exactly one update.
structure VoiceEvent {
    @required
    update: VoiceUpdate
}

union VoiceUpdate {
    roster: VoiceRoster
    joined: VoiceJoined
    left: VoiceLeft
    signal: ReceivedSignal
}

/// Enter the room's voice. Refused outside a room, while already in
/// voice, or when voice is full; answered with a roster, and everyone
/// already in voice hears joined.
structure JoinVoice {}

/// Leave the room's voice; everyone still in it hears left. Refused
/// when not in voice.
structure LeaveVoice {}

/// One negotiation message for one peer: exactly one of a description
/// or a candidate. Relayed only when sender and peer are both in the
/// same room's voice and `toEpoch` is the peer's current join; the
/// refusal is the same whether or not the peer exists.
structure SendSignal {
    @required
    to: String

    @required
    toEpoch: Long

    description: SessionDescription

    candidate: IceCandidate
}

/// A SendSignal as its peer receives it, naming the sender.
structure ReceivedSignal {
    @required
    from: String

    description: SessionDescription

    candidate: IceCandidate
}

/// An RTCSessionDescription: `type` is "offer" or "answer", and `sdp`
/// is at most 16 KiB.
structure SessionDescription {
    @required
    type: String

    @required
    sdp: String
}

/// An RTCIceCandidate, as its toJSON() spells it; a null reads as absent.
/// `candidate` is at most 1 KiB; empty marks the end of a peer's
/// candidates.
structure IceCandidate {
    @required
    candidate: String

    sdpMid: String

    sdpMLineIndex: Integer

    usernameFragment: String
}

/// The joiner's view of voice: everyone already in it, each of whom the
/// joiner offers a connection to, and the ICE servers to reach them by.
structure VoiceRoster {
    /// The joiner's own join, as its peers address it.
    @required
    epoch: Long

    @required
    members: VoiceMembers

    @required
    iceServers: IceServers
}

list VoiceMembers {
    member: VoiceMember
}

/// Someone in voice, and the join a signal to them names.
structure VoiceMember {
    @required
    playerId: String

    @required
    epoch: Long
}

structure VoiceJoined {
    @required
    playerId: String

    @required
    epoch: Long
}

/// A member left voice: deliberately, by leaving the room, or by the
/// socket closing.
structure VoiceLeft {
    @required
    playerId: String
}

/// An RTCIceServer. Credentials are absent for STUN.
structure IceServer {
    @required
    urls: IceServerUrls

    username: String

    credential: String
}

list IceServers {
    member: IceServer
}

list IceServerUrls {
    member: String
}
