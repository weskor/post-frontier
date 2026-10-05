#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Commands/CommandService.h"
#include "FailoverNode.h"
#include "Headquarters.h"
#include "JevReleaseWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChainStructureAttackTest, "CoopRTS.Enemy.Chain.StructureAttack",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

constexpr double AttackSeconds = 60.;

// A free JEV force given an explicit Attack on a human Failover Node, from a start 900 cm out, must walk into range and
// fire on it. A force that stands off and never engages (the stall behind phase 1's idle waves) leaves it untouched.
class FStructureScenario : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		Kit.Planner->SetActorTickEnabled(false);
		for (const TWeakObjectPtr<AFailoverNode>& Node : Kit.State->FriendlyHeadquarters->GetNodes())
			if (Node.IsValid() && Node->IsAlive() && !Victim)
				Victim = Node.Get();
		if (!Check(Victim != nullptr, TEXT("The humans have a standing Failover Node")))
			return true;
		StartHealth = Victim->Health;
		ACommandPlayerState* Jev = Kit.State->EnemyCommander;
		const int32 Region = ArmyTestSetup::RegionAt(Kit.State, Victim->GetActorLocation());
		const int32 Brawler = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Frontline);
		const int32 Rifle = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Ranged);
		const TArray<int32> Members = { Brawler, Brawler, Brawler, Rifle, Rifle, Rifle };
		const FVector Start = Victim->GetActorLocation() + FVector(900.f, 0.f, 0.f);
		AArmyGroup* Force = AArmyGroup::SpawnFreeForce(*Kit.World, *Jev, Start, Members, 4, 1.f);
		if (!Check(Force != nullptr && Region != INDEX_NONE, TEXT("The attacking force spawned beside the building")))
			return true;
		FCommandService::SetRetreatThreshold(Jev, Force, ERetreatThreshold::Never);
		if (!Check(FCommandService::IssueForceOrder(Jev, Force, EForceVerb::Attack, Region, Victim).IsAccepted(),
				TEXT("The force accepted an Attack on the building")))
			return true;
		Enter(1);
		return false;
	}

	bool Step() override
	{
		if (!IsValid(Victim) || !Victim->IsAlive())
			return true;
		if (InStage() < AttackSeconds)
			return false;
		Check(Victim->Health < StartHealth,
			*FString::Printf(TEXT("A force ordered to attack a human Failover Node fired on it within %.0f s (health %d of %d)"), AttackSeconds,
				Victim->Health, StartHealth));
		return true;
	}

	AFailoverNode* Victim = nullptr;
	int32 StartHealth = 0;
};
}

bool FJevChainStructureAttackTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStructureScenario(this));
	return true;
}

#endif
