#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyCombatScenario.h"

namespace ArmyCombatScenarioPrivate
{
bool FArmyCombatScenario::Begin()
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetNetMode() == NM_Standalone)
		{
			World = Context.World();
			break;
		}
	if (!World || ArmyTestSetup::GameSeconds(World) < 3. || !ArmyTestSetup::NavigationReady(World))
		return false; // Dynamic navmesh and AI controllers initialize asynchronously.
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	Controller = ArmyTestSetup::Controller(World);
	if (!ArmyTestSetup::MapReady(State) || !Controller.IsValid()
		|| !Controller->GetPlayerState<ACommandPlayerState>()
		|| Controller->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0)
		return false;
	if (!PrepareFixtures(World, State) || !CheckWeapons(State))
		return true;
	FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::MoveHold, ArmyTestSetup::CurrentRegion(Army.Get()));
	if (!CheckCounterAcquisition(State))
		return true;
	return BeginMoveHold(World, State);
}

bool FArmyCombatScenario::PrepareFixtures(UWorld* World, const ACommandGameState* State)
{
	// Stop paid reinforcements and remove pre-existing hostiles before the
	// fixture spawns; Hold alone would still let nearby members fire.
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		if (It->TeamIndex == 5 && It->IsProducer())
			FCommandService::ConfigureProduction(State->EnemyCommander, *It,
				It->bForceConfigured ? It->ProductionRole : static_cast<EUnitRole>(255), false);
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		if (It->GetTeamIndex() == 5)
			It->Destroy();
	// CombatActors treats any produced hostile force as present. Own both
	// fixtures explicitly so a partially assembled paid force cannot replace one.
	Army = ArmyTestSetup::SpawnGroup(World, Controller.Get(), 0,
		ArmyTestSetup::FromFriendlyHQ(State, 1700.f, 600.f, 100.f));
	Enemy = ArmyTestSetup::SpawnGroup(World, nullptr, -1, ArmyTestSetup::HostileStaging(State));
	if (!Check(Army.IsValid() && Enemy.IsValid()
				&& Army->GetUnits().Num() == 6 && Enemy->GetUnits().Num() == 6
				&& Army->GetTeamIndex() != Enemy->GetTeamIndex(),
			TEXT("Explicit combat fixtures have two opposed six-unit armies")))
		return false;
	return true;
}

bool FArmyCombatScenario::BeginMoveHold(UWorld* World, const ACommandGameState* State)
{
	Victim = Enemy->GetUnits()[1];
	const int32 Region = ArmyTestSetup::TravelRegion(Army.Get(), State->EnemyHeadquarters->GetActorLocation());
	if (!Check(FCommandService::IssueForceOrder(Army->GetOwningPlayerState(), Army.Get(), EForceVerb::MoveHold, Region).IsAccepted()
				&& Army->Status == EForceStatus::Marching,
			TEXT("No-chase fixture accepts a real travelling MoveHold")))
		return true;
	const FVector Approach = (Army->Destination - Army->GetCenter()).GetSafeNormal2D();
	const FVector Side(-Approach.Y, Approach.X, 0.f);
	for (AArmyUnit* Unit : Enemy->GetUnits())
	{
		Unit->NextAttackTime = TNumericLimits<float>::Max();
		Unit->SetActorLocation(Army->GetUnits()[4]->GetActorLocation() + Side * 600.f,
			false, nullptr, ETeleportType::TeleportPhysics);
	}
	MoveStart = Army->GetCenter();
	MoveAttacks = TotalAttacks();
	for (AArmyUnit* Unit : Army->GetUnits())
		Unit->NextAttackTime = 0.f;
	SetStage(3, ArmyTestSetup::GameSeconds(World));
	return false;
}
}

#endif
