#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Commands/CommandService.h"
#include "JevReleaseWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyCrowdMovesTest, "CoopRTS.Enemy.Chain.CrowdMoves",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

constexpr int32 ForceCount = 16;
constexpr int32 UnitsPerForce = 6;
// A force that walks at all covers thousands of units in 20 s; a force whose units cannot path-follow stays put.
constexpr double WalkSeconds = 20.;
constexpr double MinimumTravel = 1500.;

// Detour crowd steering caps the units that can path-follow at UCrowdManager::MaxAgents (50 by default); units
// beyond it stand still while their force reissues the same order. The project raises the cap
// (Config/DefaultEngine.ini) above the largest army the game allows, so 96 units must all march.
class FCrowdScenario : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		Kit.Planner->SetActorTickEnabled(false);
		ACommandPlayerState* Jev = Kit.State->EnemyCommander;
		const FVector Home = Kit.State->EnemyHeadquarters->GetActorLocation();
		const FVector Forward = (Kit.State->FriendlyHeadquarters->GetActorLocation() - Home).GetSafeNormal2D();
		const FVector Side(-Forward.Y, Forward.X, 0.f);
		const int32 HumanMain = ArmyTestSetup::RegionAt(Kit.State, Kit.State->FriendlyHeadquarters->GetActorLocation());
		TArray<int32> Members;
		Members.Init(ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Frontline), UnitsPerForce);
		for (int32 Index = 0; Index < ForceCount; ++Index)
		{
			const FVector Anchor = Home + Forward * (900.f + (Index / 4) * 500.f) + Side * (((Index % 4) - 1.5f) * 700.f);
			AArmyGroup* Force = AArmyGroup::SpawnFreeForce(*Kit.World, *Jev, Anchor, Members, 4 + Index, 1.f);
			if (!Check(Force != nullptr, TEXT("The fixture force spawned")))
				return true;
			FCommandService::SetRetreatThreshold(Jev, Force, ERetreatThreshold::Never);
			if (!Check(FCommandService::IssueForceOrder(Jev, Force, EForceVerb::Attack, HumanMain).IsAccepted(),
					TEXT("The fixture force accepted its Attack")))
				return true;
			Forces.Add(Force);
			Start.Add(Force->GetCenter());
		}
		Enter(1);
		return false;
	}

	bool Step() override
	{
		if (InStage() < WalkSeconds)
			return false;
		for (int32 Index = 0; Index < Forces.Num(); ++Index)
			Check(FVector::Dist2D(Start[Index], Forces[Index]->GetCenter()) > MinimumTravel,
				*FString::Printf(TEXT("Force %d of %d (units %d of %d) marched: the crowd cap must not stop later units"), Index + 1,
					ForceCount, (Index + 1) * UnitsPerForce, ForceCount * UnitsPerForce));
		return true;
	}

	TArray<AArmyGroup*> Forces;
	TArray<FVector> Start;
};
}

bool FArmyCrowdMovesTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCrowdScenario(this));
	return true;
}

#endif
