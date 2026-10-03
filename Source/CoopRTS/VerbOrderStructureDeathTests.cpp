#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderStructureDeathTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::StructureDeath) {}

private:
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			const FTransform Transform(State->GetRegionAnchor(Target) + FVector(600.f, 0.f, 5.f));
			Structure = GameWorld->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Check(Structure.IsValid(), TEXT("Hostile structure fixture spawns")))
				return true;
			Structure->BuildingIndex = BarracksIndex;
			Structure->TeamIndex = 5;
			Structure->OwningPlayerState = EnemyWallet.Get();
			Structure->ConstructionProgress = 1.f;
			Structure->FinishSpawning(Transform);
			const AMapRegion* AtStructure = State->FindRegionAt(Structure->GetActorLocation());
			if (!Check(Structure->IsAlive() && AtStructure, TEXT("Hostile structure has health and a real region")))
				return true;
			if (!Issue(EForceVerb::Attack, AtStructure->RegionIndex, Structure.Get()))
				return true;
			if (!Check(Force->TargetStructure == Structure.Get(), TEXT("Attack records the live hostile structure")))
				return true;
			const AMapRegion* Current = State->FindRegionAt(Force->GetCenter());
			if (!Check(Current != nullptr, TEXT("Attacking force stands in a real end region")))
				return true;
			EndRegion = Current->RegionIndex;
			Structure->ReceiveAttack(Structure->Health, Force->GetUnits()[0]);
			if (!Check(!Structure.IsValid() || !Structure->IsAlive(), TEXT("Authoritative damage destroys the attacked structure")))
				return true;
			TickForce();
			if (!Check(Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == EndRegion
						&& !Force->TargetStructure && Force->Orders.Num() == 1 && Force->Orders[0].Verb == EForceVerb::MoveHold,
					TEXT("Destroyed structure converts unqueued Attack to MoveHold where the force ended")))
				return true;
			SetStage(1);
		}
		if (Holding(EndRegion))
			return true;
		return false;
	}
};
}

VERB_WORLD_TEST(FVerbStructureDeathTest, "StructureDeath", StructureDeath)

#endif
