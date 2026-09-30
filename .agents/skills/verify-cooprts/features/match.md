# Enemy construction strategy and match loop

The match starts with two damageable HQs, three neutral sectors and no fixed friendly army. The enemy spends its wallet on barracks, locked fixed forces, individual casualty replacements and outposts; building fronts control its forces. There is no dual-site shield, Assault unlock or objective HUD. Victory requires enemy HQ health zero; friendly HQ destruction causes Defeat (same-frame double lethal is Defeat). Terminal matches reject orders, construction/research and production, and stop income. Enter requests fresh seamless-travel Boot, preserving commander identity but resetting wallets, research, HQs, territory, buildings, configured forces and recruits.

## Live proof selection

- `regression --scenario strategy` → `CoopRTS.Enemy.ConstructionEconomy`: paid enemy construction and one-at-a-time recruits, capture/outpost income, reactive defense, real-damage Fall Back on only the injured producer, retained force backlink, natural repair and strategic-front release. More barracks create independently configured force types rather than detaching an injured squad. No all-plans or human-balance claim follows.
- `regression --scenario match-win` and `match-loss` → `CoopRTS.Match.VictoryRestart` / `CoopRTS.Match.DefeatRestart`: controlled fixture armies, actual HQ weapon shots after low-health setup, terminal command/economy rejection and fresh-world reset. Each needs a fresh run; these fixtures do not prove player construction or native front assignment.
- `regression --scenario construction`/`production` prove paid building/fixed-force rules and terminal guards. `network.py --scenario construction --mode editor --clients 1` checks one real-socket construction→forces/replacements→victory→restart arc with independent peer convergence and preserved net-driver identities. Budget/lethal fixtures are explicit; natural force movement secures the sector. This is not repeat delayed-peer recovery or an unaided native match.

Require exact test name, zero exit and SoftQuit as in `../SKILL.md`. Current enemy proof is `Saved/Verification/force-strategy-a-20260930/strategy.log`. Earlier construction-cutover HQ/restart and network records are historical; do not claim them as fixed-force acceptance. Current socket/packaged evidence must identify its fresh artifact and remaining native/five-player/fault limits.

## Packaged encounter, only after current package

Inspect a fresh package baseline: empty friendly base, wallet, HQ health and construction choices. Build/complete barracks, lock/start a force type and assign a front; observe individual paid units and physical reinforcement arrival. Pan the minimap to inspect enemy construction, force movement and sector state. Space focuses the selected building or friendly HQ. Mark strategic states unverified when the session does not reach them.

For offense, assign an owned barracks Secure front near the hostile HQ and observe automatic weapon hits and falling HQ HP. The HQ has no unlock prerequisite. A front acceptance alone does not prove damage. After Victory/Defeat, inspect the overlay and terminal rejection, press Enter and observe an empty fresh construction base with intact HQs, neutral sites and reset wallets/research. Images prove only inspected transitions; live assertions supply exact authoritative invariants.

## Historical boundary

The former power-site shield/Assault objective recipes and `objective`/`objective-defeat` regression scenarios were removed. Old objective runs remain historical evidence for a retired mechanic, not commands to repeat or proof of the present construction loop. The old forced-delay three-cycle seamless-restart result is also historical; see [multiplayer](multiplayer.md). Do not report the current one-cycle construction socket scenario as a rerun of that acceptance.
