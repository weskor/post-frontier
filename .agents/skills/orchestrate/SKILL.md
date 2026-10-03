---
name: orchestrate
description: Split a task across parallel agents in herdr tabs. Opus 5.5 orchestrates, up to nine Sonnet 5.5 workers implement isolated slices in their own worktrees, up to three Opus 5.5 reviewers review, then each slice lands. Use when splitting work across agents, or when you are a worker or reviewer started under this workflow.
---

# Orchestrate parallel agents

One orchestrator splits a task into slices. Workers build the slices in parallel, each in its own pane and git worktree. Idle reviewers check every slice before it lands. Agents live in separate `workers` and `reviewers` tabs, not beside the orchestrator.

Workers and reviewers read only their own section ([Worker rules](#worker-rules), [Reviewer rules](#reviewer-rules)).

## Roles

| Role | Agent name | Model | Fallback |
|---|---|---|---|
| Orchestrator | the session running this skill | `anthropic/claude-opus-5-5` | `openai-codex/gpt-6.1-sol --thinking xhigh` |
| Worker | `worker-1` … `worker-9` | `anthropic/claude-sonnet-5-5 --thinking high` | `openai-codex/gpt-6.1-sol` |
| Reviewer | `reviewer-1` … `reviewer-3` | `anthropic/claude-opus-5-5 --thinking medium` | `openai-codex/gpt-6-astra` |

- **Starting the orchestrator:** the user runs `omp --config /home/wes/workspace/game/.agents/omp-agents.yml --model anthropic/claude-opus-5-5` in a herdr pane at the repo root. Fallback: `omp --config /home/wes/workspace/game/.agents/omp-agents.yml --model openai-codex/gpt-6.1-sol --thinking xhigh`.
- **Model providers: OpenAI (`openai-codex`) and Anthropic only.** No OpenRouter or other model providers. Every start uses the [agent config overlay](../../omp-agents.yml), which pins all model roles and disables `find`. Agents have no `find` tool; briefs must never tell them to use it.
- **Worker model trial (owner decision 2026-10-03):** workers moved from Sol 6.1 to Sonnet 5.5 because Sol generated about 14 tokens/s and workers spent 76–95% of their time waiting on it. A running worker keeps its model until its slot is restarted: switch a slot when it starts its next slice (`/exit`, then `agent start` with the new model), never mid-slice.
- **The orchestrator plans, briefs, waits, lands and talks to the user.** It doesn't implement slices itself.
- **At most 9 workers and 3 reviewers at a time.** `./x` shares Unreal through its headless pool and exclusive lock; the limits are review throughput and the orchestrator's attention.

## Orchestrator procedure

Check `test "$HERDR_ENV" = 1` first; outside herdr, say so and stop.

### 1. Split

- **Slices touch disjoint files, except shared configuration files with disjoint entries.** Files such as `Tools/x/scopes.toml` and `Tools/x/lint.toml` may appear in two in-flight slices only when their entries are disjoint and both briefs describe ownership and landing conflict resolution. All other files remain disjoint.
- **A shared interface change is its own slice.** It lands first; slices that depend on it start after, branching from the new main.
- **One slice is one session's work,** with one observable acceptance check and the smallest verification that proves it.
- Name each slice with a short slug, e.g. `hold-region-alarm`. The branch is `task/<slug>`.

### 2. Brief

Write `/tmp/cooprts-work/tasks/<slug>/brief.md`. The worker has none of your conversation, so the brief is self-contained:

```markdown
# <slug>
Worktree: /home/wes/workspace/game-wt/<worker-n>   Branch: task/<slug>
## Goal
## Files you may change
## Contract (interfaces other slices rely on)
## Prerequisites (landed interface/runner commits required for implementation or proof)
## Acceptance (named observable requirements, including required repeat counts)
## Verification (smallest proof for each requirement; whether Unreal runs are allowed)
## Out of scope
```

List **every file the slice may need**, including shared files such as `Tools/x/scopes.toml` and `Tools/x/lint.toml` when its checks or lint policy need changes. Narrow file lists caused most blocked rounds; do not leave predictable dependencies for the worker to discover after dispatch.

For each shared file, state the exact entries the slice owns and how to resolve rebase conflicts at landing: preserve already-landed independent entries and apply the slice's entries, never take an entire side blindly. If edits compete for the same entry, serialize those slices instead.

**Amend the brief before sending a changed instruction.** Update its authoritative file list, contract, acceptance or verification section, then append a dated `Addendum (orchestrator, <date/time>)` naming the approval and which requirement it supersedes. Keep the effective rule in its section, not in conflicting copies. Scope extensions must pass the same ownership check as initial dispatch. Workers and cleared reviewers read the same amended brief; conversation-only permission is not the review contract.

### 3. Prepare slots

A slot is one pane, one agent and one worktree. The worktree outlives the pane so its build stays warm. Create the two agent tabs once per orchestration session; reuse their panes for later slices.

Read tab IDs from `.result.tab.tab_id`, root pane IDs from `.result.root_pane.pane_id`, and split pane IDs from `.result.pane.pane_id`. The commands below use `jq` to retain those IDs and record them in Bash-sourceable files under `/tmp/cooprts-work/tasks/`; never infer IDs from tab or pane numbers.

```bash
# workers: three columns, then three rows in each column
workers_json=$(herdr tab create --workspace "$HERDR_WORKSPACE_ID" --label workers --no-focus)
workers_tab=$(printf '%s\n' "$workers_json" | jq -r '.result.tab.tab_id')
w1=$(printf '%s\n' "$workers_json" | jq -r '.result.root_pane.pane_id')
w4=$(herdr pane split --pane "$w1" --direction right --ratio 0.333333 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
w7=$(herdr pane split --pane "$w4" --direction right --ratio 0.5 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
w2=$(herdr pane split --pane "$w1" --direction down --ratio 0.333333 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
w3=$(herdr pane split --pane "$w2" --direction down --ratio 0.5 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
w5=$(herdr pane split --pane "$w4" --direction down --ratio 0.333333 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
w6=$(herdr pane split --pane "$w5" --direction down --ratio 0.5 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
w8=$(herdr pane split --pane "$w7" --direction down --ratio 0.333333 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
w9=$(herdr pane split --pane "$w8" --direction down --ratio 0.5 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
declare -p workers_tab w1 w2 w3 w4 w5 w6 w7 w8 w9 > /tmp/cooprts-work/tasks/worker-panes.txt

# reviewers: three columns
reviewers_json=$(herdr tab create --workspace "$HERDR_WORKSPACE_ID" --label reviewers --no-focus)
reviewers_tab=$(printf '%s\n' "$reviewers_json" | jq -r '.result.tab.tab_id')
r1=$(printf '%s\n' "$reviewers_json" | jq -r '.result.root_pane.pane_id')
r2=$(herdr pane split --pane "$r1" --direction right --ratio 0.333333 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
r3=$(herdr pane split --pane "$r2" --direction right --ratio 0.5 --cwd /home/wes/workspace/game --no-focus | jq -r '.result.pane.pane_id')
declare -p reviewers_tab r1 r2 r3 > /tmp/cooprts-work/tasks/reviewer-panes.txt
```

Record each tab and its pane IDs before dispatch. If the orchestrator's shell restarts or a later call uses a fresh Bash shell, restore the variables instead of creating duplicate tabs:

```bash
source /tmp/cooprts-work/tasks/worker-panes.txt
source /tmp/cooprts-work/tasks/reviewer-panes.txt
```

`--ratio` is the fraction retained by the original pane (left for `right`, top for `down`), not the new pane's fraction. First retain one third, then split the remaining two thirds in half. This was verified in a throwaway tab with `herdr pane layout`: a 273-column area became three 91-column panes, and 70 rows became 23/24/23; the throwaway tab was closed. Thus `w1` … `w9` map to workers in column-major order, and `r1` … `r3` map to reviewers. Inspect both layouts with `herdr pane layout --pane "$w1"` and `herdr pane layout --pane "$r1"`.

For each worker slot:

```bash
# worktree: create if missing, otherwise reuse (it must be clean)
git -C /home/wes/workspace/game worktree add ../game-wt/<worker-n> -b task/<slug> main
git -C /home/wes/workspace/game-wt/<worker-n> switch -c task/<slug> main   # when the worktree exists

# pane: use the corresponding w<n> ID from the workers tab
herdr pane run <pane-id> "cd /home/wes/workspace/game-wt/<worker-n> && pwd"
herdr pane wait-output <pane-id> --regex '^/home/wes/workspace/game-wt/<worker-n>$' --source recent-unwrapped --timeout 10000
herdr pane get <pane-id>
herdr agent start <worker-n> --kind omp --pane <pane-id> --timeout 90000 -- --config /home/wes/workspace/game/.agents/omp-agents.yml --model anthropic/claude-sonnet-5-5 --thinking high
```

Before starting, confirm `.result.pane.foreground_cwd` from `pane get` is the worktree path and the pane is at its shell prompt. For each reviewer, use its `r<n>` pane, set and confirm its shell cwd the same way:

```bash
herdr pane run <id> "cd /home/wes/workspace/game && pwd"
herdr pane wait-output <id> --regex '^/home/wes/workspace/game$' --source recent-unwrapped --timeout 10000
herdr pane get <id>
herdr agent start reviewer-<n> --kind omp --pane <id> --timeout 90000 -- --config /home/wes/workspace/game/.agents/omp-agents.yml --model anthropic/claude-opus-5-5 --thinking medium
```

### 4. Dispatch

Before prompting a worker, check the candidate's authorized paths against **every active slice's brief**, including amended paths and files reserved for integration. Expand existing glob matches and compare prospective new paths too. Reserve a non-configuration file for only one slice; shared configuration is allowed only for the disjoint entries recorded in both briefs. On overlap, split out the shared interface or defer the dependent slice; a promise to resolve conflicts at landing does not satisfy ownership.

Check each named prerequisite against landed main and the worker's baseline. Dispatch only the independent portion if it has disjoint files and its own acceptance; defer integration and expensive proof until the required interface or runner is present in that worktree. A tooling fix on main does not update an already-running branch. Recheck ownership and prerequisites when amending a brief or resuming a blocked slice.

```bash
herdr agent prompt <worker-n> "Read /tmp/cooprts-work/tasks/<slug>/brief.md and do it. Follow the Worker rules in .agents/skills/orchestrate/SKILL.md." --wait --until working --timeout 30000
```

This returns once the worker is working. Dispatch every ready slice before waiting on any.

### 5. Wait

Wait on **all working workers and reviewers at once**, not in a serial loop. Start one async bash job per working agent, each with its own tool timeout greater than herdr's `--timeout` (greater than 1,800 seconds for the wait below, e.g. 1,860 seconds):

```bash
herdr agent wait <name> --timeout 1800000
```

Handle whichever job settles first while the others keep waiting; process every result as it arrives:
- **`idle` or `done`:** read `report.md` (worker) or `review.md` (reviewer) in the task folder.
- **`blocked`:** `herdr agent read <name> --source recent-unwrapped --lines 80`. Answer only if the answer is inside the brief. Otherwise ask the user.
- **Timeout:** read the screen the same way. If it is progressing, wait again. If it shows the same screen as at the last timeout, send `herdr agent send-keys <name> esc` and prompt it to write its report and reply `BLOCKED`.

### 6. Review

Choose any idle `reviewer-<n>` and reserve it for this slice until its review finishes. Before **every** review, clear that reviewer: `herdr agent prompt reviewer-<n> "/new"`. Don't add `--wait`: `/new` starts no turn, so the wait would fail with `agent_prompt_stalled`. Then:

Before requesting review, ensure the brief contains every approval and the report maps every current acceptance requirement to evidence. Keep the previous review available for re-review. An unexplained failed requirement remains open even if a later run passes.

```bash
herdr agent prompt reviewer-<n> "Review task <slug>: brief, report and review file in /tmp/cooprts-work/tasks/<slug>/, code in /home/wes/workspace/game-wt/<worker-n>. Follow the Reviewer rules in .agents/skills/orchestrate/SKILL.md." --wait --until working --timeout 30000
```

- **Must-fix items:** prompt the same worker, without `/new`: "Fix the must-fix items in /tmp/cooprts-work/tasks/<slug>/review.md, commit, update report.md, reply DONE." Then review again with a cleared reviewer.
- **Must-fix items still open after the second review:** stop and ask the user.
- **Should-fix items** inside the brief's files go to the worker in the same round. Others go in your summary for the user.

### 7. Land

Land one slice at a time.

Authorize `./x land` only after review accepts every current requirement and any relevant integration changes. Prompt the worker: "Run `./x land`, reply DONE." The hook rejects other updates to main.

After the strict-landing hook cutover, every already-in-flight task branch must run `git rebase main` **before** invoking `./x land`. Landing code is imported before land's internal rebase, so an old runner cannot authorize the new hook even if that internal rebase succeeds. Resolve behavior-changing rebase conflicts through re-review before authorizing landing.

If landing fails, the worker follows the landing-failure rule in [Worker rules](#worker-rules). `./x land` aborts conflicting rebases and prints their paths; the worker resolves them by rebasing its task branch onto main using the brief's entry-ownership instructions. If resolving a conflict changes behaviour, the worker updates its report and replies `BLOCKED` without retrying `./x land`; send the slice for re-review through an idle reviewer cleared with `/new`, then explicitly authorize another landing attempt after review passes. Otherwise the worker reruns `./x check`, commits any fixes and retries `./x land`.

If `./x check` or `./x land` reports `main audit failed` or `hook integrity failed`, stop and ask the owner; do not repair main, rewrite audit evidence or retry landing to bypass the refusal. Main's worktree must be clean before landing.

Then free the slot:

```bash
git -C /home/wes/workspace/game-wt/<worker-n> switch --detach main
git -C /home/wes/workspace/game branch -d task/<slug>
herdr agent prompt <worker-n> "/new"
```

Every finished task ends with `/new`, so the next task starts with an empty context. A blocked task the user drops ends the same way, with `git branch -D`.

### 8. Close

When a slot has no further slice, close its pane: `herdr pane close <pane-id>`. This ends the agent; its transcript stays on disk. Close all reviewer panes when no slice is left to review, and close any unused grid panes. Close only panes and tabs you created, and keep the worktrees. Once a tab has no remaining slots, close it with `herdr tab close "$workers_tab"` or `herdr tab close "$reviewers_tab"`.

Finish with a summary for the user: landed commits, blocked slices with their reasons, and should-fix items left open.

### Fallback

Switch to the fallback model when `agent start` fails, or a turn ends on a provider error (credits, quota, a rate limit that persists after omp's retries, model unavailable). Poor work isn't a reason to switch; that goes through review.

1. In the same pane, `herdr agent prompt <name> "/exit"`.
2. Run `herdr agent start <name> --kind omp --pane <pane-id> --timeout 90000 -- --config /home/wes/workspace/game/.agents/omp-agents.yml --model <fallback>`.
3. Resend the last prompt.

## Worker rules

1. **Stay in your worktree and on your task branch.** Commit there. Never touch main, other branches or other worktrees.
2. **Start with `git status`.** If an earlier attempt left changes on the branch, continue from them.
3. **Change only the amended brief's authorized files.** If you need another file, stop, explain why in the report and reply `BLOCKED`. Resume from the persisted approval, not just a conversational grant.
4. **Don't ask questions.** When a decision is missing, write it in the report and reply `BLOCKED`.
5. **Unreal only through `./x` commands, which take their own locks and stop stalled runs,** and only if the brief allows it. Never wrap `./x` (or anything else) in `flock`: an outer lock deadlocks against the runner's lock. Never kill a process you didn't start. Never write into `Builds/`.
6. **`./x check` enforces the [lint policy](../../../Docs/Engineering/Setup.md):** inline suppressions, `TODO`/`FIXME`/`HACK`, stubs and disabled tests fail `./x check`. Exceptions only through `Tools/x/lint-exceptions.toml` with a reason.
7. **Verify with `./x check`; the command chooses the scopes.** Fix failures with `./x test <failed scopes>` and run the full `./x check` once at the end, not after every fix (`./x help check`). Run `./x verify` only when the brief names it. Keep exploratory/dirty runs as diagnostic evidence. Commit formatting and implementation changes before final acceptance, or establish equivalent tested content as described by `./x help runs`. **Never rerun `./x check` on content a passed check already covered:** after committing, run `./x runs <check-id> --compare-current`; exit 0 keeps that check as the evidence, and `./x land` reuses it when the rebase changes nothing. Rerun only after content changed (formatting, review fixes, a rebase onto a moved main). After a commit or rebase, carry forward evidence only for demonstrably unchanged tested inputs, command and fixture; record the reason and refresh only affected requirements. `--all` expands lint, not test selection. Never weaken a test to make it pass.
8. **Land only after review and explicit instruction.** If `./x land` aborts a conflicting rebase, resolve it by rebasing your task branch onto main using the brief's entry-ownership instructions. If resolving a conflict changes behaviour, update the report and reply `BLOCKED`; do not retry `./x land` until re-review passes and the orchestrator explicitly authorizes it. Otherwise fix the landing failure, rerun the failed scopes with `./x test`, commit any fixes and retry `./x land`; it reruns the scoped check itself because the content changed.
9. **Write `/tmp/cooprts-work/tasks/<slug>/report.md`,** at most 45 lines:
   1. Files and functions changed.
   2. Behaviour now, with exact constants.
   3. Acceptance table: requirement, run IDs, tested source/equivalence, observed proof and limits. Include actual selected scopes and required repetitions; a green check does not substitute for named simulation, rendered HUD or network acceptance.
   4. Failed/interrupted runs: assertion or failure fingerprint, command/fixture, run IDs and diagnosis or comparable baseline. A later pass does not resolve an unexplained failure. Use existing equivalent baseline records first; any new baseline run must be authorized by the brief.
   5. Risks, remaining requirements and what the orchestrator must check. Mark superseded evidence as historical; distinguish headless state, rendered surface, loopback replication and native/Steam proof.
10. **Reply with one line:** `DONE` or `BLOCKED`.

## Reviewer rules

1. **Read-only.** Don't edit files, build, launch Unreal or run git write commands.
2. **Inputs:** the amended brief, report acceptance table, retained run evidence and the diff `git -C <worktree> diff main...HEAD`. On a re-review, also read the previous `review.md`.
3. **Check every claim against the code and evidence.** Inspect the exact acceptance records and captures supporting visual claims. Follow `./x help runs` to inspect source provenance; matching content is not proof that a requirement passed. Label unavailable legacy provenance and any unverified claim as inference.
4. **Look for:** correctness against the effective acceptance, files changed outside the amended brief, shared-interface ownership/contract breaks, missing or weakened verification, and lint-policy violations. Require a diagnosis or comparable baseline for an unexplained failure; a passing retry or the word "intermittent" is insufficient. Compare the same failure fingerprint, command/fixture and relevant source, not unrelated failures in the same scope. Do not demand new proof for unchanged inputs or tiers the brief does not require.
5. **Write `/tmp/cooprts-work/tasks/<slug>/review.md`,** at most about 700 words, most important first. End with three lists: **Must fix**, **Should fix**, **Fine as is**.
6. **Reply with one line:** `DONE`.
