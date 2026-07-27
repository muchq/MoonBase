# Per-Room Chat Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist the latest 100 room-chat messages and deliver ordered, deduplicated chat across golf-hub instances, then expose it through the Golf web UI.

**Architecture:** A composed `ChatStore` owns authoritative append and cursor reads without growing the existing room/game `HubStore`. PostgreSQL appends, prunes, and notifies atomically; handlers immediately deliver committed local rows and use a dedicated chat wake plus message-ID cursor for remote catch-up. Join/resume sends a bounded history event, and the browser merges history and live rows by ID.

**Tech Stack:** C++20, Bazel, GoogleTest, Smithy, libpq/PostgreSQL, React, TypeScript, Vitest.

## Global Constraints

- Retain at most the latest 100 committed messages per room.
- Delete chat only with its room; deleting a member preserves prior messages.
- Keep messages immutable plain text and reject empty/whitespace-only, more than 500 bytes, ill-formed UTF-8, or an embedded NUL (libpq sends text parameters as C strings, so a NUL would silently truncate). The limit counts bytes, and text that would split a character is rejected rather than truncated. Handlers reject so clients see a protocol error; stores re-check through `ValidateChatText` so retention stays bounded regardless of the caller.
- Appends are authorized against room membership in the store, not only the handler, so a membership row that vanishes mid-send rejects the message. `PgChatStore` reads `room_members` inside the transaction; `MemoryChatStore` takes an injected `MemberCheck` and relies on the handler's re-check under `mu_` for the remaining window.
- Use server-assigned monotonic IDs and server time; delivery is at-least-once and consumers deduplicate by ID. `message_id` is the only ordering key — `sent_at_unix_millis` is wall-clock and display-only.
- Retention can prune rows a lagging cursor never read. `LoadAfter` returns the oldest row still retained above the cursor and does not report the gap, so catch-up is bounded by the 100-row window, not by the cursor. A consumer more than 100 messages behind loses the difference by design.
- `DropRoom` means different things per implementation: `MemoryChatStore` reclaims memory only through it, while PostgreSQL cascades from the room row and no-ops. Any handler path that deletes a room must call it, and that call needs its own coverage since PostgreSQL tests cannot catch a missing one.
- Never log or emit metrics containing message text.
- Every blocking concurrency test must have a bounded deadline.
- Do not commit or push implementation checkpoints unless the user explicitly requests it.

---

### Task 1: Define the composed `ChatStore`

**Files:**
- Create: `domains/games/apis/golf_hub/chat_store.h`
- Create: `domains/games/apis/golf_hub/chat_store.cc`
- Create: `domains/games/apis/golf_hub/chat_store_test.cc`
- Modify: `domains/games/apis/golf_hub/BUILD.bazel`

**Interfaces:**
- Produces:
  - `ChatRow { int64_t message_id; std::string room_id; std::string player_id; std::string text; int64_t sent_at_unix_millis; }`
  - `ChatStore::Append(room_id, player_id, text, notify_payload) -> StatusOr<ChatRow>`
  - `ChatStore::LoadRecent(room_id, limit) -> StatusOr<vector<ChatRow>>`
  - `ChatStore::LoadAfter(room_id, after_message_id, limit) -> StatusOr<vector<ChatRow>>`
  - `ChatStore::DropRoom(room_id)`
  - `ValidateChatText(text) -> Status`, shared by handlers and every store
  - `NotAMemberError()`, the one rejection both stores return for a non-member or missing room
  - `MemberCheck`, the membership predicate `MemoryChatStore` is constructed with

- [x] **Step 1: Write failing memory-store tests**

Add tests that append rows, assert ascending reads, retain exactly 100 of 101 rows, bound recent/cursor reads, and erase history through `DropRoom`.

```cpp
auto first = store.Append("R1", "alice", "one", "ignored");
ASSERT_TRUE(first.ok());
EXPECT_EQ(first->message_id, 1);

auto recent = store.LoadRecent("R1", 100);
ASSERT_TRUE(recent.ok());
ASSERT_EQ(recent->size(), 1u);
EXPECT_EQ(recent->front().text, "one");
```

- [x] **Step 2: Verify RED**

Run:

```bash
bazel test //domains/games/apis/golf_hub:chat_store_test
```

Observed: Bazel analysis failed because the new `chat_store` target did not exist.

- [x] **Step 3: Implement the minimal memory store**

Use one mutex-protected global `next_chat_id_` and `std::map<std::string, std::deque<ChatRow>> chats_`. The handler remains responsible for in-process authorization; timestamp with `system_clock` milliseconds, append, and pop from the front while size exceeds 100.

- [x] **Step 4: Verify GREEN**

Run:

```bash
bazel test //domains/games/apis/golf_hub:chat_store_test
```

Observed: `chat_store_test` and the unchanged `hub_store_test` pass.

### Task 2: Add the durable schema and composed PostgreSQL chat store

**Files:**
- Modify: `domains/platform/libs/pg/pg.h`
- Modify: `domains/platform/libs/pg/pg.cc`
- Modify: `domains/platform/libs/pg/pg_test.cc`
- Modify: `domains/games/apis/golf_hub/migrations.cc`
- Create: `domains/games/apis/golf_hub/pg_chat_store.h`
- Create: `domains/games/apis/golf_hub/pg_chat_store.cc`
- Create: `domains/games/apis/golf_hub/pg_chat_store_test.cc`
- Modify: `domains/games/apis/golf_hub/BUILD.bazel`

**Interfaces:**
- Consumes: Task 1 `ChatStore` interface.
- Produces: a `pg::Client` transaction surface that holds the connection mutex from `BEGIN` through `COMMIT`/`ROLLBACK`, and an independent `PgChatStore`.

- [x] **Step 1: Write failing PostgreSQL tests**

Cover migration idempotence, append/notify, ascending recent/after reads, missing member, member deletion preserving history, room cascade, 101-to-100 pruning, two-store concurrent appends, and rollback emitting neither row nor notify.

- [x] **Step 2: Verify RED against the local PostgreSQL test database**

Run:

```bash
GOLF_HUB_TEST_DB_URL='postgresql://moonbase_test:moonbase_test@127.0.0.1:55432/moonbase_test' \
  bazel test //domains/games/apis/golf_hub:pg_chat_store_test --test_output=errors
```

Expected: compile/schema failures for missing chat support.

- [x] **Step 3: Add transaction ownership to `pg::Client`**

Expose a callback transaction API whose transaction object calls `ExecLocked`, rolls back on callback failure, and does not reconnect/retry after `BEGIN`. Add unit/integration coverage proving another `Client` caller cannot interleave statements on the same connection.

Observed: a CTE-chained single statement was tried first and is **not** an adequate substitute, despite the issue leaving that door open. In READ COMMITTED a statement takes its snapshot before it blocks on `FOR UPDATE`, so a waiting append prunes against a view of the room from before the lock holder committed and leaves 101 rows. Verified directly: two concurrent appends to a room already holding 100 messages ended at 101. Separate statements issued *after* the lock is held take fresh snapshots and see the prior commit, which is what makes retention hold — the reason the transaction is load-bearing rather than stylistic. `PgTransactionTest.StatementsAfterALockSeeConcurrentCommits` pins the property.

- [x] **Step 4: Add the schema**

Create `room_chat_messages(message_id bigint GENERATED ALWAYS AS IDENTITY, room_id text REFERENCES rooms ON DELETE CASCADE, player_id text, body text, sent_at timestamptz)` with byte-length checks and `(room_id, message_id)` index.

- [x] **Step 5: Implement atomic append**

Within one transaction: lock and verify the room/member, insert and return ID/time, prune rows older than the newest 100, notify `ChatChannel(room_id)`, then commit. Return `FailedPrecondition` when membership vanished and a failed status for database errors.

- [x] **Step 6: Implement bounded reads and verify GREEN**

Recent reads select newest `LIMIT n` in a subquery and return ascending; cursor reads select `message_id > $2 ORDER BY message_id LIMIT $3`.

Run the focused PostgreSQL test command from Step 2. Expected: pass.

### Task 3: Extend the Smithy chat protocol

**Files:**
- Modify: `domains/games/apis/golf_hub/model/games.smithy`
- Modify: `domains/games/apis/golf_hub/model/golf_hub.smithy`
- Modify: `domains/games/apis/golf_hub/hub_e2e_test.cc`

**Interfaces:**
- Produces:
  - `ChatMessage { messageId: Long, playerId: String, text: String, sentAtUnixMillis: Long }`
  - `ChatMessages` list
  - `ChatHistory { messages: ChatMessages }`
  - `GolfEvents.roomChatHistory`

- [ ] **Step 1: Update the e2e test to require enriched live chat and join history**

Assert IDs are positive, timestamps are positive, history is ascending, and an existing member does not receive another member's history replay.

- [ ] **Step 2: Verify RED**

Run:

```bash
bazel test //domains/games/apis/golf_hub:hub_e2e_test
```

Expected: generated types lack the new members/event.

- [ ] **Step 3: Update Smithy shapes**

Add required ID/time fields to `ChatMessage` and the separate `roomChatHistory` event. Document that history/live overlap is legal and clients deduplicate by ID.

- [ ] **Step 4: Build generated client/server code**

Run:

```bash
bazel build //domains/games/apis/golf_hub:golf_hub_smithy_client \
  //domains/games/apis/golf_hub:golf_hub_smithy_server
```

Expected: both targets build.

### Task 4: Persist commands and replay history in `HubHandler`

**Files:**
- Modify: `domains/games/apis/golf_hub/hub_handler.h`
- Modify: `domains/games/apis/golf_hub/hub_handler.cc`
- Modify: `domains/games/apis/golf_hub/hub_e2e_test.cc`

**Interfaces:**
- Consumes: Tasks 1-3 chat-store/protocol types.
- Produces: persisted local chat delivery and join/resume history replay.

- [ ] **Step 1: Add failing e2e tests**

Test whitespace rejection, durable append before echo, latest-100 history on join, history on resume, no replay to existing members, and store failure producing `commandRejected` without `roomChat`.

- [ ] **Step 2: Verify RED**

Run the focused `hub_e2e_test`; expected failures show the current local-only fan-out and absent history.

- [ ] **Step 3: Replace local-only chat**

Resolve room ID under `mu_`, release the lock for `ChatStore::Append`, reacquire to ensure the sender still belongs to that room, stage the returned row to current local members, advance the room cursor, then deliver outside the lock. Inject `std::shared_ptr<ChatStore>` separately from `HubStore`, defaulting to `MemoryChatStore`.

- [ ] **Step 4: Replay history**

Load recent rows outside `mu_` during join/resume and send one `roomChatHistory` only to the admitted stream after `roomState`. Convert every row through one `ChatEvent` helper shared with live delivery.

- [ ] **Step 5: Verify GREEN**

Run:

```bash
bazel test //domains/games/apis/golf_hub:hub_e2e_test \
  //domains/games/apis/golf_hub:hub_store_test
```

Expected: pass.

### Task 5: Add subscription catch-up and cross-instance fan-out

**Files:**
- Modify: `domains/platform/libs/pg/listener.h`
- Modify: `domains/platform/libs/pg/listener.cc`
- Modify: `domains/platform/libs/pg/listener_test.cc`
- Modify: `domains/games/apis/golf_hub/hub_handler.h`
- Modify: `domains/games/apis/golf_hub/hub_handler.cc`
- Create: `domains/games/apis/golf_hub/hub_chat_race_test.cc`
- Modify: `domains/games/apis/golf_hub/pg_hub_e2e_test.cc`
- Modify: `domains/games/apis/golf_hub/BUILD.bazel`

**Interfaces:**
- Produces: `ChatChannel(room_id)`, listener active/re-subscribed callback, per-room highest delivered ID, and paged catch-up.

- [ ] **Step 1: Write failing listener synchronization test**

Subscribe once, wait on a condition variable for an active-channel callback, then send exactly one notification. Terminate the backend, assert a second active callback after re-LISTEN, and send exactly one post-reconnect notification.

- [ ] **Step 2: Implement listener active signaling**

Make `LISTEN` report success, update `active_` only for successful statements, and invoke the optional callback after a channel first becomes active on a connection. Never hold the listener mutex while invoking owner code.

- [ ] **Step 3: Write deterministic no-PostgreSQL handler race tests**

Use two handlers, a shared gated store, condition variables, and bounded receives. Cover delayed/coalesced/duplicate/own wakes, paging, room drop, and history/live overlap.

- [ ] **Step 4: Implement cursor catch-up**

Subscribe `ChatChannel` with `RoomChannel`; on chat wake or active callback, load pages after the cursor in ascending order and deliver only rows whose IDs exceed it. Remove cursor state when the room drops.

`ChatChannel` yields `chat_<room_id>` to match `RoomChannel`'s `room_<room_id>`. `LISTEN` cannot take a bind parameter, so quote the channel name as an identifier there and confirm room IDs stay inside PostgreSQL's 63-byte identifier limit; `NOTIFY` goes through `pg_notify($1, $2)`.

- [ ] **Step 5: Add PostgreSQL two-instance tests**

Split clients across instances, exchange messages both ways, kill/reconnect a listener during a send, join a third client for history, and verify room deletion cascades.

- [ ] **Step 6: Verify GREEN and stress**

Run:

```bash
bazel test //domains/platform/libs/pg:listener_test \
  //domains/games/apis/golf_hub:hub_chat_race_test \
  //domains/games/apis/golf_hub:pg_hub_e2e_test --test_output=errors
```

Then repeat the deterministic race target 100 times. Expected: no duplicates, loss, or timeout.

### Task 6: Observability, formatting, and backend regression

**Files:**
- Modify: `domains/games/apis/golf_hub/hub_handler.cc`
- Modify: comments in `domains/games/apis/golf_hub/hub_handler.h`
- Modify: comments in `domains/games/apis/golf_hub/chat_store.h`

- [ ] **Step 1: Add metric assertions**

Assert append success/failure, catch-up rows, and history-load failures are counted without room IDs or message text.

- [ ] **Step 2: Implement counters and update contracts**

Document retention, ordering, at-least-once delivery, listener recovery, and the process-local limitation of `MemoryChatStore`.

- [ ] **Step 3: Run the backend regression suite**

```bash
bazel test //domains/games/apis/golf_hub/... //domains/platform/libs/pg/...
```

Expected: all tests pass (database-gated tests run when their URLs are inherited).

- [ ] **Step 4: Format and inspect**

Run `clang-format --dry-run --Werror` over every changed C++ file, then inspect `git diff --check` and `git status`.

### Task 7: Implement the web UI in `muchq/muchq.github.io`

**Repository:** `https://github.com/muchq/muchq.github.io`

**Files:**
- Modify: `src/types/golfAdapter.ts`
- Modify: `src/utils/golfV2Adapter.ts`
- Modify: `src/hooks/useGolfGame.ts`
- Create: `src/apps/golf/components/RoomChat.tsx`
- Create: `src/apps/golf/components/RoomChat.module.css`
- Create: `src/apps/golf/components/__tests__/RoomChat.test.tsx`
- Modify: `src/apps/golf/components/GolfGame.tsx`
- Modify: `src/apps/golf/components/GolfGame.module.css`
- Add focused adapter and hook tests under existing `src/**/__tests__` locations.

- [ ] **Step 1: Create a separate issue branch in the UI repository after the backend event shape is stable**

- [ ] **Step 2: Write failing adapter tests**

Require exact chat command JSON, enriched live/history parsing, ID deduplication, ordering, and reconnect merge.

- [ ] **Step 3: Add typed chat state and actions**

Expose `ChatMessage[]`, `sendChat(text)`, history/live callbacks, room-scoped clearing, a 100-row cap, and unread state.

- [ ] **Step 4: Write failing component tests**

Cover empty state, literal HTML-looking text, byte limit, Enter/Shift+Enter, offline disabled state, draft preservation, unread behavior, focus management, and restrained `aria-live`.

- [ ] **Step 5: Build the responsive component**

Render a desktop side panel and mobile drawer without covering game controls. Preserve scroll unless near the bottom and provide a new-message affordance otherwise.

- [ ] **Step 6: Integrate lobby and game views**

Keep one room-scoped chat instance alive across lobby/game transitions and clear it on room leave/switch.

- [ ] **Step 7: Verify**

Run repository lint, typecheck, Vitest, and production build commands from `package.json`; then review desktop, tablet, and narrow-phone screenshots.
