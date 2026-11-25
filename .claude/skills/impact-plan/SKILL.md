---
name: impact-plan
description: Turn a high-level task into a brief a planning lead / coordinator agent can run with, by first making the user pin down who benefits and how impact is measured, then launch a recursive coordinator/executor agent tree to run it. Use when the user wants to kick off a large or multi-agent task, says "plan this", "set up a run for", "write a brief for the coordinator", or hands over a goal vague enough that success can't yet be checked. Not for small, already-specified changes.
---

# Impact plan

A coordinator agent optimizes whatever it is told success looks like. If that
is "do the task as described", it will finish the task and possibly move
nothing. This skill makes success a measurable change for a named beneficiary
*before* any work is split up, and sizes scope to that change rather than to
the original wording.

Output: one brief file, then (on the user's go) a tree of coordinator and
executor agents that runs it. The protocol every node follows is
[`coordinator.md`](coordinator.md).

## Ground rules

- **Draft, then ask.** Read the cited code, issue, docs, or dashboards first.
  Propose answers and have the user confirm or correct. A user picking between
  concrete options is fast; a user writing from blank is slow and vague.
- **Ask with a recommendation**, not a survey. Batch related questions (≤4 per
  round). Two or three rounds is normal; more means you are asking things you
  could have read.
- **Reject unmeasurable answers once, concretely.** "Better UX", "cleaner
  code", "more scalable" get one push: *which number, measured where, moving
  which way?* Offer two concrete candidates. If the user still can't or won't
  name one, record it as an explicit assumption and move on.
- **"Just do it" is allowed.** Fill the remaining fields with your best guess,
  mark each one `ASSUMED`, and finish. Never block on the interview.

## 1. Restate the ask as an outcome

One sentence: *after this ships, X is different for Y.* If you can't write it,
that is the first question. Also note the request may be a symptom — say if a
different change would dissolve the problem instead of managing it.

## 2. Who benefits — pick one primary

| Beneficiary | What "impact" usually means |
|---|---|
| End users (name the segment) | Behavior or experience metric: task success, latency they feel, errors they hit, retention, conversion |
| Internal developers / operators | Time to change, build/test time, incident count, on-call load, onboarding time |
| The business | Cost (infra, licensing, people-hours), risk, revenue |
| Future change (org or technical) | Cost / risk / duration of a *named* upcoming change: migration, reorg, deprecation, scale event |
| Nobody yet (exploration) | The decision this unblocks, and the question that must be answered |

Exactly one primary. Secondaries are fine but don't steer tradeoffs. If the
user names "everyone", ask which group loses if the others win.

"Code quality" and "complexity" are not beneficiaries. Ask whose work gets
cheaper and how you'd see it: fewer files touched per feature, shorter review
time, lower defect rate in that area, a deleted dependency.

"Preparing for a change" must name the change and roughly when. Prep for an
unnamed future is speculative work; say so.

## 3. How impact is measured

For the primary beneficiary:

- **Primary metric** — name, where it's measured (dashboard, query, benchmark,
  test), current baseline or how to get one, target direction and magnitude,
  when it should show.
- **Leading indicator** if the real metric lags (weeks of retention data,
  next quarter's migration). What can the coordinator check before it's done?
- **Guardrails — what must NOT move.** Ask explicitly; users rarely volunteer
  these. Typical: latency, error rate, cost, other user segments, API
  compatibility, test time, on-call load. Each gets a tolerance ("p95 +≤5%",
  "zero breaking changes").
If none of this is measurable even in principle, the goal is wrong or the
beneficiary is wrong. Go back to step 2 once before accepting it.

## 4. Make the before state clear

Priority is gap × value, and scope follows where the cost sits. Neither is
known until the before state is. For the primary metric **and each
guardrail**, establish:

- **The instrument.** A command, query, benchmark, or dashboard link that
  anyone can re-run unchanged after the work. Before and after use the same
  one, or the comparison means nothing.
- **The current value and its noise.** Run it more than once. If the
  target change is smaller than run-to-run variance, the metric or the target
  is wrong. Fix that now, not after shipping.
- **Where the cost is.** Break the number down: profile, per-component
  counts, the slowest/most-failing/most-expensive N. This sets the ceiling:
  removing a component that is 3% of the total can't win more than 3%.
  It often moves scope more than anything else in the interview does.
- **Cost of the alternative**, for prep work: what the named future change
  costs if you do nothing (files touched, teams involved, estimated weeks).
  That is the baseline the prep work is measured against.

Then sort by what getting each one costs:

| Cost to get the baseline | Do |
|---|---|
| Already exists | Cite it: link and date. Check it measures what the user thinks it does. |
| Minutes (a query, a grep count, one benchmark run, a profile) | Get it **now**, in this skill. Don't hand the coordinator a guess you could have checked. |
| Hours or more (new instrumentation, a data pipeline, a load test) | Make it **phase 0** of the run, with a gate: the coordinator measures, then reports back before committing to the rest. |

Phase 0 gate: if the measured gap is smaller than assumed, or the cost sits
somewhere other than the ask targets, the brief gets revisited (re-scope,
re-prioritize, or drop) before more work starts. Write the threshold down now:
"proceed only if X is above Y."

## 5. Fit scope to the impact

Now compare the original ask against the metric. For each part of the ask:
does it move the primary metric or protect a guardrail? Then check the other
direction: does moving the metric depend on something outside the ask?

- **Shrink**: parts that move nothing measured. Cut them or move them to
  non-goals. Name them.
- **Grow**: work outside the ask that the metric depends on (instrumentation,
  a migration step, a caller that also needs changing, docs that gate
  adoption). Without it the impact won't appear even if the task is "done".
- **Reframe**: a smaller or different change that moves the same metric
  further. Raise it in a sentence with a recommendation.

When minimal and full versions lead to materially different work, ask which.
When they differ cosmetically, pick and say so.

## 6. Stop and kill criteria

- **Done**: metric hits target, or the leading indicator does and the real
  metric is scheduled to be checked (by whom, when).
- **Stop early**: what result would show the approach is wrong? (e.g. the
  first 20% of the migration shows no build-time change.) The coordinator
  needs permission to stop, not just to finish.
- **Budget**: wall time, max tree depth (default 3), and a spawn or cost
  ceiling. Depth 3 at branching 5 is up to 125 executors. Ask the user for a
  ceiling instead of assuming it is fine.

## 7. Write the brief

Write to `plans/<yyyy-mm-dd>-<slug>.md` in the working repo unless the project
has a convention for plans. Show the user the path and the one-sentence
outcome; don't paste the whole brief back.

```markdown
# <title>

**Outcome:** after this ships, <X> is different for <Y>.

## Beneficiary
Primary: <who>. Secondary: <who | none>.
Why them: <one line>.

## Impact
| | Metric | Measured at | Baseline | Target | By |
|---|---|---|---|---|---|
| Primary | | | | | |
| Leading | | | | | |

### Guardrails (must not move)
| Metric | Tolerance | Baseline | How checked |
|---|---|---|---|

## Before state
Instrument(s): <exact command / query / link, re-runnable unchanged>
Noise: <run-to-run variance; target change must exceed it>
Breakdown: <where the cost sits today; ceiling on achievable win>
Phase 0: <none | what must be measured first> — proceed only if <threshold>

## Scope
In: <bullets, each tagged with the metric it moves or guardrail it protects>
Out (non-goals): <bullets, each with why>
Changed from the original ask: <grown / shrunk / reframed, and why>

## Done / stop
Done when: <...>
Stop early if: <...>
Budget: <wall time> · max depth <n> · spawn cap <n>
Models: coordinators <smart model> · executors <cheaper model>

## Assumptions and open questions
- ASSUMED: <...>
- OPEN: <... — who answers it>

## Instructions to the coordinator
Follow `.claude/skills/impact-plan/coordinator.md`. This brief is the root
contract: Goal = Outcome, Check = the Impact table and guardrails. Phase 0
(if any) is the first child, and nothing else starts until its gate passes.
Every node names the metric it moves or the guardrail it protects; work that
does neither is cut. Report against the Impact table, not task completion.
```

Every field is filled or marked `ASSUMED` / `OPEN`. An empty field means the
interview isn't done.

## 8. Launch

Show the user the brief's outcome, budget and models, and get an explicit go:
the run spawns long-lived, paid sessions. Then commit the brief to a branch and
spawn the root coordinator with the root contract from `coordinator.md`.

- **Cloud** (`create_session` available): one session per node, `model` set
  by role. Nodes outlive this conversation, can spawn their own children, and
  report up with `send_message`. Pass each child its contract, the brief path,
  and the parent's session id.
- **Local** (Agent tool only): one subagent per node with `model` set by role.
  Before planning, probe once: spawn a cheap subagent whose only job is to
  spawn one sub-subagent and report whether it could. If it can't, run in
  **flat mode** (`coordinator.md`): the root plans the whole tree itself and is
  the only node that spawns. Tell the user which mode the run is in.

Then tell the user where to look on return: `plans/<slug>/tree.md` for status,
`plans/<slug>/BLOCKED.md` for what needs them.
