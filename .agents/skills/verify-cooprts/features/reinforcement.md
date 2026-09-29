# Paid casualty recovery and group rebuilding

## Sub-features

N requests paid restoration of missing fixed-composition roles for the selected owned army. The quote reflects every missing role; a full six-member army cannot purchase a seventh. A wiped army remains selectable via 1/2 and rebuilds its six roles at its base. A living army buys within 450 units of its base or a team-owned forward reinforcement site; its restored member spawns at the source and joins its accepted order. An empty army may rebuild at base only. Rejection leaves wallet, members and accepted order intact. Source: `ArmyGroup.cpp`, `CommandPlayerController.cpp`, `ArmyEconomyTests.cpp`, `ArmyMovementTests.cpp`.

## Primary proof and selection

Run `"$V" --run "$LOGIC_RUN" regression --scenario economy` for quote/payment, exact roles, insufficient funds, source restrictions, atomic rejection and rebuild. Run `movement` for actual purchased newcomer travel, all-member arrival, immediate per-unit Hold, crossing and order replacement. Each uses a fresh Boot world; do not share an evidence directory or count one scenario's success as the other's.

## Targeted desktop proof

Inspect the selected army's starting six roles, wallet, quote and source status near home; N at full composition rejects without purchasing an extra member. After an observed combat casualty, return within the base purchase radius, inspect missing-role quote and READY/source feedback, press N and compare before/after wallet, roster and order. Observe the purchased unit travelling from the source to the commanded formation; accepting N alone does not prove successful joining. Repeated N when full must reject without changing the wallet or order. Away from base and uncontrolled forward sites, N must reject with invalid-source feedback, even if the player has sufficient funds.

For a wiped group, select the empty owned army with 1/2; the HUD must still show its full rebuild quote and base-only restriction. Press N at its base and inspect the complete restored role roster and wallet deduction. A wipe may not occur in a short packaged session; record rebuild as unverified on that surface if it does not, rather than faking a casualty or inferring behavior from a quote. The live economy scenario supplies deterministic authoritative rebuild assertions.

## Gotchas

The old free seventh-unit control and seven-member cap are removed. Movement retains the joining-path invariant by buying a real casualty replacement during a short Move while the group's center remains inside the 450-unit source radius; the long obstacle crossing then runs with the restored six. The original distant-mid-crossing spawn from home is no longer valid under location-restricted purchases. Rejection due to occupied source slot or an invalid joining path must preserve the prior roster/order/payment, not quietly spawn elsewhere. A held-army joining path is distinct from the moving-path scenario and must be exercised separately if changed. The standalone world does not validate a hostile client's RPC.
