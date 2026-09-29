# First doctrine choice

## Prototype contract

The replicated `ACommandPlayerState::Doctrine` begins `None`. An owning `ACommandPlayerController::ServerChooseDoctrine(EArmyDoctrine)` requests exactly one free, irreversible choice during an ongoing match; `TryChooseDoctrine` rejects `None`, invalid enum values, duplicate/respec requests and terminal matches. Fresh world restart restores `None` and permits another choice. An army resolves its controller's PlayerState dynamically, so both owned groups and purchased replacements inherit the choice; other players and the enemy do not. Unit definitions remain shared and unmodified.

- **F1 Siege Optics:** only siege has 1.25x effective weapon range and 0.75x outgoing weapon damage, including against the enemy HQ. Damage uses integer `base * 3 / 4`, truncated toward zero. Observe an actual hit in the extension band and reduced HP loss, not only a getter or range circle.
- **F2 Field Repairs:** living units gain 5 HP/sec only after five uninterrupted seconds stationary without firing or taking damage; movement, firing or damage resets the quiet interval and fractional healing. Clamp to max HP; neither dead nor terminal units heal. Observe changing HP after the quiet interval and unchanged HP following each interruption.
- **F3 Entrenched Frontline:** only frontline members on Hold *and actually stationary* take `incoming * 3 / 4` damage, truncated toward zero. Moving and Retreating frontlines do not get protection. Observe position change under actual navigation and actual hostile weapon HP loss in each state.

These replace the tentative Mobile Siege (Move already fires) and Rapid Reinforcement (no production timer yet), not the broader roster/draft/doctrine inventory. Whether a player can explain how the selection changes **where or when they fight** is a human playtest criterion, not an automatic test result.

## Live assertions: separate fresh Boot worlds

After building the current editor target, run from repository root. Regression scenarios use `Automation RunTests ...; SoftQuit` without `-seconds`, command timeout or test-runner subprocess deadline. The wrapper requires process exit 0, the exact named test's `Test Completed. Result={Success}`, and `**** TEST COMPLETE. EXIT CODE: 0 ****`. Do not start these while a build, another verification world or a game using the same package is active.

```bash
V=.agents/skills/verify-cooprts/scripts/verify.py
"$V" --run Saved/Verification/m7-siege-unique regression --scenario doctrine-siege
"$V" --run Saved/Verification/m7-repairs-unique regression --scenario doctrine-repairs
"$V" --run Saved/Verification/m7-frontline-unique regression --scenario doctrine-frontline
"$V" --run Saved/Verification/m7-restart-unique regression --scenario doctrine-restart
```

`CoopRTS.Doctrine.SiegeOptics` probes before/after actual siege unit hits inside and beyond original range, an enemy HQ hit, both owned armies, independent second controller/PlayerState and hostile siege, shared asset, once-only/invalid selections, and paid newcomer firing. `CoopRTS.Doctrine.FieldRepairs` observes real hostile damage, five-second delay, two owned groups, damage/firing/navigation interruption, delayed recovery, max cap and death. `CoopRTS.Doctrine.EntrenchedFrontline` observes actual incoming damage for stationary Hold in both armies versus position-changing Move and Retreat, then paid replacement. `CoopRTS.Doctrine.Restart` uses an actual siege shot to destroy the enemy HQ, checks terminal choice rejection, restarts, checks `None` and selects another doctrine. Test setup may position actors or adjust initial health, but no test writes the outcome or simulates a hit by editing the post-shot health.

These are authoritative **standalone** world assertions. A second real controller/PlayerState/group in the same process checks dynamic ownership, not replication to a network client or a hostile-client RPC. The changed feature also affects combat, order movement, paid replacement and terminal restart; run `combat`, `orders`, `movement`, `economy`, `strategy`, `match-win` and `match-loss` once for the complete milestone baseline, preserving old assertions.

## Native input and HUD: one choice per fresh game

Use a freshly packaged build and the skill's `launch`, `doctor`, `focus`, `capture`, `key`, `point`, `click`, and `stop` commands. Never adopt or stop the user's existing game. Run doctor before every drive, explicitly focus only the owned mapped window, and keep the viewport in focus; `key f1` / `key f2` / `key f3` emits native `F1` / `F2` / `F3` through `wtype` under the existing ownership/focus guards. At 1600x900 inspect `baseline` for the right-hand "DOCTRINE | choose one (free)" panel with three cards, F1/F2/F3 labels, costs/deadline statement, and current player wallet. The panel is an input surface: optional click testing must use observed window geometry and a card center from the current capture, not hard-coded fractions. Keep ground orders outside the panel; clicks on it should not clear army selection.

For a targeted packaged probe, select 1 / Space; capture before the choice, press `key f1`, capture immediately afterward, and check the panel now says `DOCTRINE | Siege Optics`, both-army/newcomer text and no-respec text. Press `key f2` afterward: choice stays Siege Optics and rejection feedback appears, with no wallet cost. Choose an actual live hostile or HQ from the current screenshot, point, press Q, and observe accepted Attack, weapon effects/HP loss or repositioning at extended reach; this presentation is **not** numeric range/damage proof. New separate fresh sessions are required to inspect F2's HP bar refill after safely waiting at Hold (and reset after real movement/damage) and F3's Hold frontline during incoming fire versus moving/retreating exposure. Arrange an enemy encounter or HQ outcome before claiming those visible effects; selection text alone proves only input-to-state. For terminal/restart, observe the finished match closing the panel, press Enter, and check a fresh unchosen panel and a different choice. Native input and captures are optional only if the changed path cannot affect input/HUD; not optional for this new doctrine UI.

Record commands, exit codes, exact test paths/results, action/log and inspected image paths in the evidence directory `RESULTS.md`; identify missing paths. Four green standalone tests and a selected panel do not prove multiplayer replication or the human playtest acceptance judgement. Never call milestone 7 verified until the packaged selection and affected surface have been inspected on a fresh package.
