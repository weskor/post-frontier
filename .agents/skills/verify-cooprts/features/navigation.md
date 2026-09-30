# Force recruitment navigation and fixture crossing

`regression --scenario movement` (`CoopRTS.Movement.TwoGroups`) checks two controlled six-member fixtures crossing the central obstacle, order replacement/Hold, rejected destinations and per-member successful arrival/proximity/low velocity. Its existing internal 90-second stage guard is not permission for a command timeout. No friendly player starts with these fixtures.

`regression --scenario production` covers actual fixed forces: paid individual exits, capacity including travellers, recruitment that retargets a moving formation, physical join, missing-slot correction, killed recruits and complete-wipe rebuild. Travellers do not bias the joined center. Their rendezvous uses the moving anchor recovered from joined positions and composition offsets, not that biased center plus a slot offset. A front replacement validates joined paths atomically and preserves intent on rejection; empty producer-owned forces retain a complete route to their front.

Keep desired rendezvous separate from its accepted navigation projection: retargeting compares old/new desired formation positions; physical arrival compares the body with the accepted path endpoint. A projection offset near geometry is not force motion and must not permanently prevent a physically arrived recruit from joining.

For the last capsule-diameter-plus-35 units of a recruit's accepted route, predictive velocity avoidance is disabled so it does not predict continuing past the finite goal into members behind that slot. Physical capsule collision and the validated navigation corridor remain enabled; predictive avoidance resumes on a farther retarget or after joining. The 35-unit physical arrival requirement is unchanged.

Keep unrelated stationary forces outside the recruitment fixture's corridor. Paid replacement and scoped fronts are separate from escaping a solid friendly crowd grid; standing members can still physically obstruct a route. The blocked-exit fixture separately checks no-charge retry rather than teleporting through obstruction.

Choose `movement` for changed fixture path/crossing behavior and `production` for changed force/recruitment paths. Neither standalone run proves replication or native input. `network.py --scenario construction` observes real peer movement, casualty replacement and arrival; offscreen HUD proof provides the visible joined/travelling surface.

For native proof, complete a barracks, lock/start its type and assign a front beyond an obstacle on navigable floor. Compare bodies to landmarks and inspect actual arrival, not just accepted markers. Two fronts require two barracks, paid units and independent fronts. Observe a casualty replacement following a changed front without teleporting. Obstacle tops and HUD panels are not ground targets. A rejected front must preserve movement and accepted intent. Historical N-refill probes are not the current input path.

