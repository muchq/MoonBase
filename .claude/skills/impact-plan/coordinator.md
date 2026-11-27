# Coordinator protocol

Every node in an impact-plan run follows this file. You were spawned with a
**contract**; read it first. It is the only definition of your job.

## Contract

```
Node:       <id, e.g. root / 2 / 2.3>
Role:       coordinator | executor
Brief:      <path to the brief, on branch X>
Parent:     <session id or "user">
Goal:       <one sentence>
Check:      <command/test/query that passes only when the goal is met>
Guardrails: <inherited from the parent, plus any it added>
Depends on: <sibling ids whose output you need | none>
Budget:     <wall time / max depth remaining / spawn cap>
Branch:     <git branch you work on>
```

**Goal and Check are fixed.** You don't reword them, relax them, swap in an
easier check, or decide a different goal would be better. If they are wrong,
unreachable, or in conflict, that is a blocker: escalate it (below). Only the
node that wrote a contract can change it, and the root's contract (the brief)
only changes when the user says so. Guardrails can be tightened, never loosened.

## Executor

One task, one check. Cheaper model.

Loop: do the work → run the Check → read the failure → fix → repeat. Commit
to your branch as you go. Finish when the Check passes and the guardrails
hold. Report to the parent with the branch, head commit, and the Check output.

Don't spawn anything. If the task turns out to need decomposition, that's a
blocker (`needs-split`) and goes to the parent.

## Coordinator

Turns its goal into ≤5 children. Smart model.

1. **Decompose.** At most 5 children. Prefer fewer; a node with one child
   is a sign it should have been an executor. Every child gets a full contract
   whose Check, together with its siblings' Checks, implies yours. If you
   can't write that implication down, the split is wrong.
2. **Pick roles.** Make a child an **executor** when it is one item with one
   Check that a single session can finish. Make it a **coordinator** only when
   it genuinely has more than one independent part or needs its own planning,
   and only if depth budget remains. Default to the shallowest tree that works:
   every layer costs a smart model and a round trip.
3. **Order.** Write the sibling dependencies down (`3 depends on 1`). Start
   everything with no unmet dependency at once; start the rest as their
   dependencies report done. No cycles.
4. **Record** the tree in `plans/<slug>/tree.md` on your branch: id, role,
   goal, status (`pending | running | done | blocked`), branch, and the last
   report. This file is what the user reads when they come back.
5. **Wait on events**, not a timer: children's reports wake you. Set one
   long fallback wake (≈2h) only to catch a child that died silently; on it,
   check each running child, and treat one with no progress as stuck.
6. **Integrate.** Merge done children's branches into yours, run your own
   Check, fix integration breakage yourself (or with one new executor).
   Done when your Check passes → report up.
7. **Re-plan within your contract.** If a child fails or a split was wrong,
   you can rewrite *that child's* contract, or replace children. Your own
   Goal and Check stay as they are.

## Flat mode

When nodes can't spawn their own children, the root does every node's
planning and spawning. The tree, contracts, roles and rules are the same;
only who spawns changes.

1. **Plan the whole tree up front.** Decompose recursively yourself, ≤5
   children per node, down to executors, within the depth budget. Write every
   node's contract into `tree.md` before spawning anything.
2. **Executors**: spawn each as a subagent (cheaper model) once its
   dependencies are done. Independent ones run in parallel.
3. **Coordinator nodes**: when all of a node's children are done, spawn one
   subagent for that node (smart model) to merge its children's branches, run
   the node's Check, and fix integration breakage. It doesn't spawn anything;
   it reports to you. Work bottom-up to the root.
4. **Re-planning and blocks.** A failed or blocked child is handled as its
   own coordinator would handle it: you apply that node's contract, not the
   root's, when deciding whether to re-split, rewrite the child's contract, or
   escalate one level up. Same rules: a node's Goal and Check change only with
   the agreement of whoever wrote them, and the root's only with the user's.
5. **Lost depth**, not lost structure: if the planned tree can't be known up
   front (a node's split depends on what an earlier node finds), plan that
   subtree when its prerequisite reports done, and update `tree.md`.

## Stuck

Progress means your Check output changed for the better, or a child finished.
You are **stuck** when there's been no progress for ~3 hours of wall time
(`date` at each loop, kept in your notes) or 5 attempts in a row failed the
same way.

When stuck, first try once to unblock within your contract (different
approach, smaller step, ask a sibling's output). Then write
`plans/<slug>/blocked/<node-id>.md`:

```markdown
# Blocked: <node id> — <goal>
Kind: decision | access | missing-info | contract-wrong | needs-split
Since: <timestamp>
Tried: <each approach, one line, with the evidence it failed>
Need: <the one thing that would unblock this, stated as a question>
Options: <2–3, with a recommendation>
Meanwhile: <what keeps running; what's paused because of this>
```

Commit it, report it to the parent, and stop working on that goal. Don't
guess past the block and don't redefine the goal to get around it.

**A parent receiving a block** resolves it if the answer is within its own
contract (re-split, provide info, re-order). Otherwise it adds its own context
and escalates further, while siblings with no dependency on the blocked node
keep running. The root collects unresolved blocks into
`plans/<slug>/BLOCKED.md` (most impactful first: what each blocks, measured
against the brief's Impact table) for the user.

## Reporting

Report results against your Check and the guardrails, not against effort.
"Check passes; p95 unchanged at 41ms" — not "implemented the cache".
Never report done with a failing or skipped Check.
