---
name: orchestrate
description: Split a task across parallel agents in herdr panes. Opus 5.5 orchestrates, Sol 6.1 workers implement isolated slices in their own worktrees, Fable 5.1 reviews, then each slice lands. Use when splitting work across agents, or when you are a worker or reviewer started under this workflow.
---

# Orchestrate parallel agents

One orchestrator splits a task into slices that touch different files. Workers build the slices in parallel, each in its own pane and git worktree. A reviewer checks every slice before it lands. Every step below has one command; don't substitute another.

Workers and reviewers read only their own section ([Worker rules](#worker-rules), [Reviewer rules](#reviewer-rules)).

## Roles

| Role | Agent name | Model | Fallback |
|---|---|---|---|
| Orchestrator | the session running this skill | `anthropic/claude-opus-5-5` | `openai-codex/gpt-6.1-sol --thinking xhigh` |
| Worker | `worker-1` … `worker-3` | `openai-codex/gpt-6.1-sol` | none: stop and tell the user |
| Reviewer | `reviewer` | `anthropic/claude-fable-5-1` | `openai-codex/gpt-6-astra` |

- **Starting the orchestrator:** the user runs `omp --model anthropic/claude-opus-5-5` in a herdr pane at the repo root. Fallback: `omp --model openai-codex/gpt-6.1-sol --thinking xhigh`.
- **The orchestrator plans, briefs, waits, lands and talks to the user.** It doesn't implement slices itself.
- **At most 3 workers and 1 reviewer at a time.** Unreal runs one process at a time today, so more workers would mostly queue on the lock.

## Orchestrator procedure

Check `test "$HERDR_ENV" = 1` first; outside herdr, say so and stop.

### 1. Split

- **Slices touch disjoint files.** List each slice's files; no file appears in two slices that are in flight at the same time.
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
## Acceptance (observable result)
## Verification (exact check; whether Unreal runs are allowed)
## Out of scope
```

### 3. Prepare a worker slot

A slot is one pane, one agent and one worktree. The worktree outlives the pane so its build stays warm.

```bash
# worktree: create if missing, otherwise reuse (it must be clean)
git -C /home/wes/workspace/game worktree add ../game-wt/<worker-n> -b task/<slug> main
git -C /home/wes/workspace/game-wt/<worker-n> switch -c task/<slug> main   # when the worktree exists

# pane: first agent splits right of the orchestrator, later ones split down from the last agent pane
herdr pane split --current --direction right --cwd /home/wes/workspace/game-wt/<worker-n> --no-focus
herdr pane split --pane <last-agent-pane> --direction down --cwd /home/wes/workspace/game-wt/<worker-n> --no-focus

herdr agent start <worker-n> --kind omp --pane <pane-id> --timeout 90000 -- --model openai-codex/gpt-6.1-sol
```

Read the pane ID from `.result.pane.pane_id`. The reviewer's pane uses `--cwd /home/wes/workspace/game` and the reviewer model.

### 4. Dispatch

```bash
herdr agent prompt <worker-n> "Read /tmp/cooprts-work/tasks/<slug>/brief.md and do it. Follow the Worker rules in .agents/skills/orchestrate/SKILL.md." --wait --until working --timeout 30000
```

This returns once the worker is working. Dispatch every ready slice before waiting on any.

### 5. Wait

For each working agent, start one async bash job:

```bash
herdr agent wait <name> --timeout 1800000
```

Handle each result as it arrives:
- **`idle` or `done`:** read `report.md` (worker) or `review.md` (reviewer) in the task folder.
- **`blocked`:** `herdr agent read <name> --source recent-unwrapped --lines 80`. Answer only if the answer is inside the brief. Otherwise ask the user.
- **Timeout:** read the screen the same way. If it is progressing, wait again. If it shows the same screen as at the last timeout, send `herdr agent send-keys <name> esc` and prompt it to write its report and reply `BLOCKED`.

### 6. Review

Before every review, clear the reviewer: `herdr agent prompt reviewer "/new"`. Don't add `--wait`: `/new` starts no turn, so the wait would fail with `agent_prompt_stalled`. Then:

```bash
herdr agent prompt reviewer "Review task <slug>: brief, report and review file in /tmp/cooprts-work/tasks/<slug>/, code in /home/wes/workspace/game-wt/<worker-n>. Follow the Reviewer rules in .agents/skills/orchestrate/SKILL.md." --wait --until working --timeout 30000
```

- **Must-fix items:** prompt the same worker, without `/new`: "Fix the must-fix items in /tmp/cooprts-work/tasks/<slug>/review.md, commit, update report.md, reply DONE." Then review again with a cleared reviewer.
- **Must-fix items still open after the second review:** stop and ask the user.
- **Should-fix items** inside the brief's files go to the worker in the same round. Others go in your summary for the user.

### 7. Land

Land one slice at a time.

Prompt the worker: "Run `./x land`, reply DONE." If landing fails, the worker fixes
the failure, commits and reruns `./x land`. The hook rejects other updates to main.

Then free the slot:

```bash
git -C /home/wes/workspace/game-wt/<worker-n> switch --detach main
git -C /home/wes/workspace/game branch -d task/<slug>
herdr agent prompt <worker-n> "/new"
```

Every finished task ends with `/new`, so the next task starts with an empty context. A blocked task the user drops ends the same way, with `git branch -D`.

### 8. Close

When a slot has no further slice, close its pane: `herdr pane close <pane-id>`. This ends the agent; its transcript stays on disk. Close the reviewer pane when no slice is left to review. Close only panes you created, and keep the worktrees.

Finish with a summary for the user: landed commits, blocked slices with their reasons, and should-fix items left open.

### Fallback

Switch to the fallback model when `agent start` fails, or a turn ends on a provider error (credits, quota, a rate limit that persists after omp's retries, model unavailable). Poor work isn't a reason to switch; that goes through review.

1. In the same pane, `herdr agent prompt <name> "/exit"`.
2. Run `herdr agent start <name> --kind omp --pane <pane-id> --timeout 90000 -- --model <fallback>`.
3. Resend the last prompt.

Workers have no fallback: stop and tell the user.

## Worker rules

1. **Stay in your worktree and on your task branch.** Commit there. Never touch main, other branches or other worktrees.
2. **Start with `git status`.** If an earlier attempt left changes on the branch, continue from them.
3. **Change only the files the brief lists.** If you need another file, stop, explain why in the report and reply `BLOCKED`.
4. **Don't ask questions.** When a decision is missing, write it in the report and reply `BLOCKED`.
5. **Unreal only if the brief allows it, and always under the lock:** `flock /tmp/cooprts-work/ue.lock <command>` (until `./x` owns locks). Check the log of every run at least every 3 minutes. Stop the run if gameplay logging stops for 5 minutes, or if the match ends while a test still waits. Never kill a process you didn't start. Never write into `Builds/`.
6. **The [lint policy](../../../Docs/Engineering/Setup.md) applies even before lint enforces it:** no suppressions, no `TODO`/`FIXME`/`HACK` or stubs, no disabled tests.
7. **Verify with the check the brief names,** following [verify-cooprts](../verify-cooprts/SKILL.md) (after phase 1: `./x check`). Never weaken a test to make it pass.
8. **Write `/tmp/cooprts-work/tasks/<slug>/report.md`,** at most 45 lines:
   1. Files and functions changed.
   2. Behaviour now, with exact constants.
   3. Verification run: commands and results, and what wasn't run.
   4. Risks, and what the orchestrator must check.
9. **Reply with one line:** `DONE` or `BLOCKED`.

## Reviewer rules

1. **Read-only.** Don't edit files, build, launch Unreal or run git write commands.
2. **Inputs:** the brief, the report, and the diff `git -C <worktree> diff main...HEAD`. On a re-review, also read the previous `review.md`.
3. **Check every claim against the code.** Label anything you didn't verify as an inference.
4. **Look for:** correctness against the brief's acceptance, files changed outside the brief, contract breaks for other slices, missing or weakened verification, and lint-policy violations.
5. **Write `/tmp/cooprts-work/tasks/<slug>/review.md`,** at most about 700 words, most important first. End with three lists: **Must fix**, **Should fix**, **Fine as is**.
6. **Reply with one line:** `DONE`.
