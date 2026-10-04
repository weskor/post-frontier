# Local match telemetry [Built]

The host writes one JSON per match in the game's saved folder, under `Telemetry/match-<match_id>.json`. Standalone games count as host authority. Clients do not write match telemetry; nothing is uploaded. A terminal result writes immediately; leaving, travel or shutdown while still ongoing writes an abandoned record from EndPlay. Seamless restart creates a new match identity and retains earlier files.

Authority automation, PIE and `./x verify` worlds also produce terminal or `Abandoned` records in the developer's `Saved/Telemetry`; these files accumulate. The telemetry world test deletes only its own records.

## Schema [Built]

`schema_version` is **2**. Root fields: `match_id`, `map`, `battle_seconds`, `result`, `players`, `ending`. `map` is the original map identity, captured at BeginPlay before seamless travel can rename the old world.

- `battle_seconds`: simulation time from the match component's BeginPlay to terminal result or abandonment; paused wall time is excluded. `result` is `Victory`, `Defeat` or `Abandoned`, from the friendly team's perspective.
- Each `players` entry contains a random match-local `player_id`, `commander_index` (zero-based; index 0 is HUD Commander 1), `disconnected`, `joined_seconds`, `left_seconds`, `participation_seconds`, exact `orders`, `builds`, `pings`, and their per-minute rates plus `decisions_per_minute`. No network ID or player name is written.
- One accepted force-verb command counts as one order, even with several selected forces. One accepted building placement counts as one build. One accepted team ping counts as one ping. Rejected requests, JEV commands, cancellation, production/research, rally and retreat-threshold configuration do not count in these three categories.
- `joined_seconds` is the first join's battle-second. `left_seconds` is the most recent explicit logout's battle-second, or null when no logout occurred; it remains recorded after a reconnect. `participation_seconds` sums connected simulation-time intervals through the match ending, excluding prejoin time and disconnected gaps.
- Every rate uses **that player's participation**, not the full battle: `count × 60 / participation_seconds`. `decisions_per_minute` sums the three categories. Zero participation gives zero rates.
- Zero-activity humans are included. Departed humans retain their counters, pseudonym and last commander slot. A valid network identity may merge a reconnect **in memory only**; the serialized pseudonym is unrelated to it and changes in a fresh match.
- `ending` contains `cause`, `region_index`, `region_name`, `location`. An HQ ending records the map region and exact `{x,y,z}` location of the HQ that was lost: an HQ at 0 HP is only offline, and it is lost when the attackers complete the hold on its main. Causes are `enemy_headquarters_destroyed`, `friendly_headquarters_destroyed` or `both_headquarters_destroyed`; the name says "destroyed" but means lost after a completed hold. An explicit terminal result without a lost HQ records `match_result_set`, unknown region index **-1**, empty region name and null location.
- Abandonment records the EndPlay cause: `level_transition`, `removed_from_world`, `end_play_in_editor`, `application_quit`, `owner_destroyed` or `unknown_end_play`; location is null and region unknown rather than invented.

## Privacy and failure limits [Built]

Telemetry contains only commander slots and random match-local pseudonyms for player attribution, not network IDs, hostnames or player names. Logs and crash reports are separate and can contain identifying information; inspect them before sharing. The Development package's `PLAYTEST.txt` identifies the game log and feedback files. Telemetry remains product code in both Development and Shipping.

Terminal or abandonment flush attempts the write once. A filesystem or serialization failure cannot change the match result; it emits a warning where logging is available. An abrupt process crash or unwritable saved folder can still prevent output.

The [step 1a questions](1a-questions.md) accompany these measurements. Card and unit pick/win rates remain outside this slice; the wider plan lives in [build order](../Design/build-order.md).
