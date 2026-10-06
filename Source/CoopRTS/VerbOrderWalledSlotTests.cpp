#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"
#include "VerbOrderProgressSupport.h"

// A lone member whose formation slot is walled in, on a queued two-step route: it never reaches its slot,
// yet the force arrives at the first region, captures it and advances to the queued order.
namespace VerbOrderWalledSlotTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	// Slot 3 sits 140 cm from the formation centre, inside the capture radius even when the wall holds the member off.
	static constexpr int32 SurvivorSlot = 3;

	bool RunScenario() override
	{
		if (Stage == 0)
		{
			// Members leave the force's array as they die.
			const TArray<TObjectPtr<AArmyUnit>> Members = Force->GetUnits();
			for (AArmyUnit* Unit : Members)
			{
				if (Unit->GetCompositionSlot() == SurvivorSlot)
					Survivor = Unit;
				else if (!Check(KillOne(*Unit), TEXT("Authoritative lethal damage removes the other members")))
					return true;
			}
			if (!Check(Survivor.IsValid() && Force->GetAliveCount() == 1, TEXT("Exactly the slot-3 member survives"))
				|| !Issue(EForceVerb::MoveHold, Intermediate) || !Issue(EForceVerb::MoveHold, Target, nullptr, true)
				|| !Check(Force->WaypointRegionIndex == Intermediate && Force->Orders.Num() == 2,
					TEXT("The first region is the active waypoint and the second is queued")))
				return true;
			// The wall goes up after the order is accepted, around the exact slot the force assigned.
			SlotGoal = Force->Destination + ArmyGroupPolicy::FormationOffset({ false, 6, false }, SurvivorSlot);
			SlotGoal.Z = Force->Destination.Z + 60.f; // capsule centre height above the floor
			if (!Check(BuildCage(*GameWorld, SlotGoal, 40.f, 120.f), TEXT("The engine cube builds a wall around the slot")))
				return true;
			SetStage(1);
		}
		if (!Check(Survivor.IsValid(), TEXT("The member survives")))
			return true;
		MinSlotDistance = FMath::Min(MinSlotDistance, FVector::Dist2D(Survivor->GetActorLocation(), SlotGoal));
		bVisitedIntermediate |= Occupies(Intermediate);
		if (Stage == 1 && Force->Orders.Num() == 1)
		{
			if (!Check(Force->TargetRegionIndex == Target && bVisitedIntermediate && State->GetRegionController(Intermediate) == 0,
					TEXT("The force advances to its queued order after capturing the region with its slot walled")))
				return true;
			// The old arrival rule needed the member within 170 cm of its slot, which the wall prevents.
			if (!Check(MinSlotDistance > 170.f, TEXT("The wall kept the member out of arrival range of its slot")))
				return true;
			SetStage(2);
		}
		return Stage == 2 && Holding(Target);
	}

	bool KillOne(AArmyUnit& Unit)
	{
		Unit.ReceiveAttack(Unit.GetHealth(), Hostile->GetUnits()[0]);
		return !Unit.IsAlive();
	}

	TWeakObjectPtr<AArmyUnit> Survivor;
	FVector SlotGoal = FVector::ZeroVector;
	float MinSlotDistance = TNumericLimits<float>::Max();
};
}

VERB_WORLD_TEST(FVerbWalledSlotTest, "WalledSlot", WalledSlot)

#endif
