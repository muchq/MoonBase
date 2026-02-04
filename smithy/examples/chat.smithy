$version: "2.0"

namespace com.example.chat

/// A real-time chat service using WebSockets
@smithy.ws#websocket
service ChatService {
    version: "1.0"
    operations: [
        OnConnect
        OnDisconnect
        SendMessage
        JoinRoom
        LeaveRoom
        GetRoomMembers
    ]
}

/// Called when a client connects
@smithy.ws#onConnect
operation OnConnect {
    input: ConnectInput
    output: ConnectOutput
}

/// Called when a client disconnects
@smithy.ws#onDisconnect
operation OnDisconnect {
    input: DisconnectInput
}

/// Send a chat message to a room
@smithy.ws#onMessage(route: "sendMessage")
operation SendMessage {
    input: SendMessageInput
    output: SendMessageOutput
}

/// Join a chat room
@smithy.ws#onMessage(route: "joinRoom")
operation JoinRoom {
    input: JoinRoomInput
    output: JoinRoomOutput
}

/// Leave a chat room
@smithy.ws#onMessage(route: "leaveRoom")
operation LeaveRoom {
    input: LeaveRoomInput
}

/// Get members of a room
@smithy.ws#onMessage(route: "getRoomMembers")
operation GetRoomMembers {
    input: GetRoomMembersInput
    output: GetRoomMembersOutput
}

structure ConnectInput {
    @required
    userId: String

    username: String
}

structure ConnectOutput {
    @required
    sessionId: String

    serverTime: Timestamp
}

structure DisconnectInput {
    reason: String
}

structure SendMessageInput {
    @required
    roomId: String

    @required
    content: String

    messageType: MessageType
}

structure SendMessageOutput {
    @required
    messageId: String

    @required
    timestamp: Timestamp
}

structure JoinRoomInput {
    @required
    roomId: String
}

structure JoinRoomOutput {
    @required
    roomId: String

    members: MemberList
}

structure LeaveRoomInput {
    @required
    roomId: String
}

structure GetRoomMembersInput {
    @required
    roomId: String
}

structure GetRoomMembersOutput {
    @required
    members: MemberList
}

list MemberList {
    member: Member
}

structure Member {
    @required
    userId: String

    @required
    username: String

    status: UserStatus
}

enum MessageType {
    TEXT
    IMAGE
    FILE
    SYSTEM
}

enum UserStatus {
    ONLINE
    AWAY
    DO_NOT_DISTURB
    OFFLINE
}

/// Error when a room is not found
@error("client")
@httpError(404)
structure RoomNotFoundError {
    @required
    message: String

    roomId: String
}

/// Error when user is not authorized
@error("client")
@httpError(403)
structure UnauthorizedError {
    @required
    message: String
}

/// Error when rate limit is exceeded
@error("client")
@httpError(429)
structure RateLimitError {
    @required
    message: String

    retryAfterSeconds: Integer
}
