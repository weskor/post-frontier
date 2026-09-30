# Fixed forces and physical casualty replacement

Each completed barracks configures one persistent Frontline/Ranged/Siege force on first Start, permanently locking type. Capacity is 6/4/2, price 20/30/50 per unit and build time 10/3, 13/3, 20/3 seconds. Siege first Start also charges 180 once. Production creates one recruit for an empty slot, charging only after successful physical deployment. Joined members and travelling recruits both occupy capacity; no shared squad race, three-unit batch, level upgrade or manual N purchase remains.

Casualties automatically reopen that producer's slots. Enabled production waits for funds or an unblocked exit; full capacity stops timer/spending. Pause retains partial work and the locked type. Recruits leave the barracks and path to their force's moving formation anchor, corrected for missing slots so depleted formations do not target occupied members. Travellers do not fire or receive stationary-Defend mitigation; they can be attacked and counted as capture occupants. A traveller killed en route needs another paid recruit.

Wipe retains force identity/front for paid rebuilding. Producer destruction stops replacement, detaches its survivors and leaves them following the last front, without transfer/adoption. The selected building's Secure/Defend/Fall Back controls only its own force. See [construction](construction.md), `CommandBuildingProduction.cpp`, `ArmyGroup.cpp` and `ArmyUnit.cpp`.

## Proof selection

`regression --scenario production` (`CoopRTS.Construction.Production`) exercises permanent configuration, exact individual payment/timing, all three independent capacities including travellers, pause/starvation/full/blocked guards, front isolation, moving physical recruitment and join, recruit death, complete-wipe refill, producer destruction/no adoption and terminal freeze. `construction` covers placement/completion/ownership; `movement` covers legacy fixture crossings, not force recruitment. The remote `network.py --scenario construction --mode editor --clients 1` observes locked Siege configuration, one paid recruit and arrival, independent Ranged producer/front and causal moving casualty replacement in separate worlds.

For surface proof, complete a barracks, choose type and Start & Lock. Observe individual timer/wallet debit, travelling count and bodies leaving the building, then actual joined strength. Pause preserves work; type controls must remain disabled. Compare enabled full force with full explicit pause. After a casualty, observe a vacancy followed by one paid traveller and physical arrival while changing the building front. More force capacity requires another barracks, not another squad batch or N.

