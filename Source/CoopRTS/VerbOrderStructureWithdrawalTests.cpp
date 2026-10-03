#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderStructureWithdrawalTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::StructureWithdrawal) {}

private:
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			if (!Issue(EForceVerb::MoveHold, Intermediate))
				return true;
			SetStage(1);
		}
		if (Stage == 1 && Holding(Intermediate))
		{
			const FTransform Transform(State->GetRegionAnchor(Target) + FVector(600.f, 0.f, 5.f));
			Structure = GameWorld->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Check(Structure.IsValid(), TEXT("Withdrawal target structure spawns")))
				return true;
			Structure->BuildingIndex = BarracksIndex;
			Structure->TeamIndex = 5;
			Structure->OwningPlayerState = EnemyWallet.Get();
			Structure->ConstructionProgress = 1.f;
			Structure->FinishSpawning(Transform);
			if (!Issue(EForceVerb::Attack, Target, Structure.Get()))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(2);
		}
		if (Stage == 2 && FVector::Dist2D(StartPosition, Force->GetCenter()) >= 700.f)
		{
			if (!KillTo(2))
				return true;
			TickForce();
			SafeRegion = Force->WaypointRegionIndex;
			if (!Check(Force->Status == EForceStatus::Withdrawing, TEXT("Structure attacker begins actual casualty withdrawal")))
				return true;
			Structure->ReceiveAttack(Structure->Health, Force->GetUnits()[0]);
			TickForce();
			if (!Check(Force->Verb == EForceVerb::Attack && Force->Status == EForceStatus::Withdrawing
						&& Force->WaypointRegionIndex == SafeRegion,
					TEXT("Destroyed structure does not abandon the withdrawal in unsafe ground")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(3);
		}
		if (Stage == 3 && Holding(SafeRegion))
			return Check(IsValid(Force->GetProductionBuilding()) && Force->GetJoinedCount() == 2
					&& FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f,
				TEXT("Completed target ends at safe arrival without waiting for paused refill"));
		return false;
	}
};
}

VERB_WORLD_TEST(FVerbStructureWithdrawalTest, "StructureWithdrawal", StructureWithdrawal)

#endif
