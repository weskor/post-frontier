# Local match telemetry [Built]

The host writes one JSON per completed match in the game's saved folder, under `Telemetry/match-<match_id>.json`. Standalone games count as host authority. Clients do not write match telemetry; nothing is uploaded. Leaving an ongoing match does not manufacture a result. Seamless restart creates a new match identity and retains earlier files.

## Schema [Built]

`schema_version` is **1**. Root fields: `match_id`, `map`, `battle_seconds`, `result`, `players`, `ending`.

- `battle_seconds`: simulation time from the match component's BeginPlay to the first terminal result; paused wall time is excluded. `result` is `Victory` or `Defeat`, from the friendly team's perspective.
- Each `players` entry contains `player_id`, `player_name`, `commander_index`, `disconnected`, exact `orders`, `builds`, `pings`, and `orders_per_minute`, `builds_per_minute`, `pings_per_minute`, `decisions_per_minute`.
- One accepted force-verb command counts as one order, even with several selected forces. One accepted building placement counts as one build. One accepted team ping counts as one ping. Rejected requests, JEV commands, cancellation, production/research, rally and retreat-threshold configuration do not count in these three categories.
- Every rate uses the **full battle duration**, including for late joiners and leavers: `count × 60 / battle_seconds`. `decisions_per_minute` sums the three categories. Zero-duration matches have zero rates.
- Zero-activity humans are included. Disconnected humans retain their counters and last name/commander slot. Online identity merges a reconnect into the same participant; offline identities are match-local UUIDs, not reusable commander slots.
- `ending` contains `cause`, `region_index`, `region_name`, `location`. An HQ ending records the destroyed HQ's map region and exact `{x,y,z}` location. Causes are `enemy_headquarters_destroyed`, `friendly_headquarters_destroyed` or `both_headquarters_destroyed`. An explicit terminal result without a destroyed HQ records `match_result_set`, unknown region index **-1**, empty region name and null location.

## Privacy and failure limits [Built]

JSON includes online identifiers and player display names. Keep it local unless the group agrees to share it; inspect logs and crash reports before sending them. The package's `PLAYTEST.txt` explains collection and feedback delivery. Ordinary Shipping engine logs are compiled out; telemetry does not depend on them.

Terminal flush attempts the write once. A filesystem or serialization failure cannot change the match result; it emits a warning where logging is available. No file is guaranteed if the process crashes before match end or the saved folder is unwritable.

The [step 1a questions](1a-questions.md) accompany these measurements. Card and unit pick/win rates remain outside this slice; the wider plan lives in [build order](../Design/build-order.md).
