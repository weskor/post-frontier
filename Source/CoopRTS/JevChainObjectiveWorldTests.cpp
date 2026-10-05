#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "FailoverNode.h"
#include "Headquarters.h"
#include "JevReleaseWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChainWaveObjectiveTest, "CoopRTS.Enemy.Chain.WaveObjective",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

constexpr double SettleSeconds = 8.;

// The v1.1 wave on Habitable Zone v2 is sent at the humans' main, which is immune while a Failover Node stands. It
// must aim at a standing node (a structure attack in the node's region), and when that node falls it must move on to
// the other node instead of going back to the planner or standing in the main.
class FObjectiveScenario : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		SkipTo(120.8f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		switch (Stage)
		{
		case 1:
			return WaveLaunched(1) ? Launched() : false;
		case 2:
			return Retargeted();
		default:
			return Settled();
		}
	}

	AFailoverNode* NodeAt(const AActor* Structure) const
	{
		for (const TWeakObjectPtr<AFailoverNode>& Node : Kit.State->FriendlyHeadquarters->GetNodes())
			if (Node.Get() == Structure)
				return Node.Get();
		return nullptr;
	}

	bool Launched()
	{
		const int32 HumanMain = ArmyTestSetup::RegionAt(Kit.State, Kit.State->FriendlyHeadquarters->GetActorLocation());
		if (!Check(Release().Waves.Last().TargetRegion == HumanMain, TEXT("The wave is sent at the humans' main")))
			return true;
		Wave = EnemyForces(Kit.World);
		for (const AArmyGroup* Force : Wave)
		{
			Victim = NodeAt(Force->TargetStructure);
			if (!Check(Victim && Victim->IsAlive() && Force->Verb == EForceVerb::Attack
						&& Force->TargetRegionIndex == ArmyTestSetup::RegionAt(Kit.State, Victim->GetActorLocation()),
					TEXT("A wave sent at the guarded main attacks a standing Failover Node in the node's region")))
				return true;
		}
		// The node falls (as if the wave's own fire had killed it); the HQ stays immune while the other stands.
		Victim->Health = 0;
		Enter(2);
		return false;
	}

	bool Retargeted()
	{
		if (InStage() < SettleSeconds)
			return false;
		for (const AArmyGroup* Force : Wave)
		{
			const AFailoverNode* Other = NodeAt(Force->TargetStructure);
			if (!Check(Other && Other != Victim && Other->IsAlive() && Force->Verb == EForceVerb::Attack,
					TEXT("When its node falls a wave force moves on to the other standing node, not to a hold or back to the planner")))
				return true;
		}
		Next = NodeAt(Wave[0]->TargetStructure);
		Next->Health = 0;
		Enter(3);
		return false;
	}

	bool Settled()
	{
		if (InStage() < SettleSeconds)
			return false;
		const int32 HumanMain = ArmyTestSetup::RegionAt(Kit.State, Kit.State->FriendlyHeadquarters->GetActorLocation());
		for (const AArmyGroup* Force : Wave)
			Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == HumanMain && !IsValid(Force->TargetStructure),
				TEXT("With both nodes down the wave goes for the humans' main"));
		return true;
	}

	TArray<AArmyGroup*> Wave;
	AFailoverNode* Victim = nullptr;
	AFailoverNode* Next = nullptr;
};
}

bool FJevChainWaveObjectiveTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FObjectiveScenario(this));
	return true;
}

#endif
