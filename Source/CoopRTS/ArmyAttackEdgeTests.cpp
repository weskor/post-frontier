#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "AIController.h"
#include "CombatTarget.h"
#include "FailoverNode.h"
#include "JevReleaseWorldFixture.h"
#include "Navigation/PathFollowingComponent.h"
#include "Rules/CombatPolicy.h"
#include "Rules/HqHoldPolicy.h"

// Attack orders against structures: units reach the edge of the footprint, stop inside weapon range and fire.
// Before the edge rule, range was measured to the structure's origin while the footprint's navigation cutout
// kept units about 100 cm further out, so a melee squad (175 cm) put one unit in range of a Drill Rig and the
// rest hovered idle at 175-240 cm; against a Workshop (145 cm half size) no melee unit could ever fire.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackEdgeRigTest, "CoopRTS.Combat.AttackEdge.BrawlersDrillRig",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackEdgeWorkshopTest, "CoopRTS.Combat.AttackEdge.BrawlersWorkshop",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackEdgeNodeTest, "CoopRTS.Combat.AttackEdge.BrawlersFailoverNode",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackEdgeRangedTest, "CoopRTS.Combat.AttackEdge.RangedStandoff",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttackEdgeRangedNodeTest, "CoopRTS.Combat.AttackEdge.RangedStandoffFailoverNode",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

enum class ETarget : uint8
{
	DrillRig,
	Workshop,
	Node,
};

// Time every unit has, from the order, to put its first shot into the structure.
constexpr double FirstShotSeconds = 30.;

class FEdgeScenario : public FScenario
{
public:
	FEdgeScenario(FAutomationTestBase* InTest, ETarget InTarget, EUnitRole InRole) : FScenario(InTest), Kind(InTarget), Role(InRole) {}

private:
	bool Prepare() override
	{
		Kit.Planner->SetActorTickEnabled(false);
		// The nodes' plating (10% damage) lasts to 240 s of battle time.
		SkipTo(HqHoldPolicy::PlatingEndSeconds + 1.f);
		if (!SpawnTarget())
			return true;
		const FVector At = TargetActor()->GetActorLocation();
		const FVector Approach = (Kit.State->FriendlyHeadquarters->GetActorLocation() - At).GetSafeNormal2D();
		TArray<int32> Members;
		Members.Init(ArmyTestSetup::UnitIndex(Kit.State, Role), 6);
		Force = AArmyGroup::SpawnFreeForce(*Kit.World, *Kit.Wallet, At + Approach * 1500.f, Members, 4, 1.f);
		if (!Check(Force != nullptr && Force->GetUnits().Num() == 6, TEXT("The six-unit squad spawns on the navmesh")))
			return true;
		Check(FCommandService::SetRetreatThreshold(Kit.Wallet, Force, ERetreatThreshold::Never).IsAccepted(), TEXT("No casualty withdrawal"));
		Check(FCommandService::IssueForceOrder(Kit.Wallet, Force, EForceVerb::Attack, INDEX_NONE, TargetActor()).IsAccepted(),
			TEXT("The structure Attack is accepted"));
		Attacker = Force->GetUnits()[0];
		Range = Attacker->WeaponRange();
		Enter(1);
		return false;
	}

	bool Step() override
	{
		if (!TargetStanding())
		{
			Check(FiredCount() == Force->GetUnits().Num(), TEXT("Every unit fired before the structure fell"));
			Test->AddInfo(FString::Printf(TEXT("Six melee units destroyed a %d HP structure (target kind %d) %.1f game seconds after the order"), StartHealth, static_cast<int32>(Kind), InStage()));
			return true;
		}
		if (Role == EUnitRole::Ranged)
			return StepRanged();
		return StepMelee();
	}

	// Melee: everyone fires soon, and the structure falls in about the time the squad's damage needs.
	bool StepMelee()
	{
		const double T = InStage();
		if (T > FirstShotSeconds && FiredCount() < Force->GetUnits().Num())
			return Fail(*FString::Printf(TEXT("Only %d of %d units fired within %.0f s: the rest hover out of range"), FiredCount(), Force->GetUnits().Num(), FirstShotSeconds));
		const int32 Damage = CombatPolicy::Damage(Attacker->GetDefinition()->AttackDamage, Attacker->GetDamageType(), EArmorClass::Structure);
		const double Bound = FirstShotSeconds + 1.5 * StartHealth * Attacker->AttackInterval() / (Damage * Force->GetUnits().Num());
		if (T > Bound)
			return Fail(*FString::Printf(TEXT("The structure survived %.0f s of a six-unit melee squad (%d HP left)"), Bound, CurrentHealth()));
		return false;
	}

	// Ranged: units stop near 0.9 x range from the edge and keep firing.
	bool StepRanged()
	{
		const double T = InStage();
		constexpr double SettleSeconds = 25., WindowSeconds = 10.;
		if (T < SettleSeconds)
			return false;
		if (WindowStart < 0.)
		{
			WindowStart = T;
			for (const AArmyUnit* Unit : Force->GetUnits())
				ShotsAtWindowStart.Add(Unit->AttackCount);
			double Mean = 0.;
			for (const AArmyUnit* Unit : Force->GetUnits())
			{
				const double Edge = CombatTarget::EdgeDistance(*Unit, TargetActor());
				const AAIController* AI = Cast<AAIController>(Unit->GetController());
				Check(Edge <= Range && Edge >= .8 * Range,
					*FString::Printf(TEXT("A ranged unit stands %.0f cm from the edge: inside .8-1.0 x its %.0f cm range"), Edge, Range));
				Check(AI && AI->GetMoveStatus() == EPathFollowingStatus::Idle, TEXT("A ranged unit in position has stopped walking"));
				Mean += Edge / Force->GetUnits().Num();
			}
			Check(FMath::Abs(Mean - .9 * Range) <= .1 * Range,
				*FString::Printf(TEXT("The squad stands %.0f cm from the edge on average, near 0.9 x %.0f"), Mean, Range));
			return false;
		}
		if (T < WindowStart + WindowSeconds)
			return false;
		for (int32 Index = 0; Index < Force->GetUnits().Num(); ++Index)
		{
			const double Shots = Force->GetUnits()[Index]->AttackCount - ShotsAtWindowStart[Index];
			Check(Shots >= .9 * WindowSeconds / Force->GetUnits()[Index]->AttackInterval(),
				*FString::Printf(TEXT("Ranged unit %d fired %.0f shots in %.0f s: it fires continuously"), Index, Shots, WindowSeconds));
		}
		return true;
	}

	bool SpawnTarget()
	{
		const FVector Place = ArmyTestSetup::HostileStaging(Kit.State);
		if (Kind == ETarget::Node)
		{
			// The default map places none; a node guards the HQ of its team wherever it stands.
			const FTransform NodeTransform(Place + FVector(0.f, 0.f, 50.f));
			Node = Kit.World->SpawnActorDeferred<AFailoverNode>(AFailoverNode::StaticClass(), NodeTransform, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Check(Node != nullptr, TEXT("The hostile Failover Node spawns")))
				return false;
			Node->TeamIndex = 5;
			Node->FinishSpawning(NodeTransform);
			Node->Health = StartHealth = Role == EUnitRole::Ranged ? 100000 : 1500;
			return true;
		}
		const FTransform Transform(Place + FVector(0.f, 0.f, -35.f));
		Building = Kit.World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Building != nullptr, TEXT("The hostile structure spawns")))
			return false;
		Building->BuildingIndex = Kind == ETarget::Workshop ? ArmyTestSetup::WorkshopIndex : ArmyTestSetup::ExtractorIndex;
		Building->TeamIndex = 5;
		Building->OwningPlayerState = Kit.State->EnemyCommander;
		Building->ConstructionProgress = 1.f;
		Building->FinishSpawning(Transform);
		// The ranged fixture must still stand after its measurement window.
		Building->Health = StartHealth = Role == EUnitRole::Ranged ? 100000 : 1500;
		return true;
	}

	AActor* TargetActor() const { return Kind == ETarget::Node ? static_cast<AActor*>(Node) : static_cast<AActor*>(Building); }
	int32 CurrentHealth() const { return Kind == ETarget::Node ? Node->Health : Building->Health; }

	int32 FiredCount() const
	{
		int32 Fired = 0;
		for (const AArmyUnit* Unit : Force->GetUnits())
			Fired += Unit->AttackCount > 0;
		return Fired;
	}

	bool TargetStanding() const
	{
		return Kind == ETarget::Node ? IsValid(Node) && Node->IsAlive() : IsValid(Building) && Building->IsAlive();
	}

	ETarget Kind;
	EUnitRole Role;
	ACommandBuilding* Building = nullptr;
	AFailoverNode* Node = nullptr;
	AArmyGroup* Force = nullptr;
	AArmyUnit* Attacker = nullptr;
	int32 StartHealth = 0;
	float Range = 0.f;
	double WindowStart = -1.;
	TArray<uint32> ShotsAtWindowStart;
};
}

bool FAttackEdgeRigTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEdgeScenario(this, ETarget::DrillRig, EUnitRole::Frontline));
	return true;
}

bool FAttackEdgeWorkshopTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEdgeScenario(this, ETarget::Workshop, EUnitRole::Frontline));
	return true;
}

bool FAttackEdgeNodeTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEdgeScenario(this, ETarget::Node, EUnitRole::Frontline));
	return true;
}

bool FAttackEdgeRangedTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEdgeScenario(this, ETarget::Workshop, EUnitRole::Ranged));
	return true;
}

bool FAttackEdgeRangedNodeTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEdgeScenario(this, ETarget::Node, EUnitRole::Ranged));
	return true;
}

#endif
