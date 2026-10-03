#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

namespace HoldAlarmFixture
{
FScenario::EStep FScenario::PrepareSetup(UWorld*& World, ACommandPlayerState*& Human)
{
	World = ArmyTestSetup::World();
	if (!World)
		return EStep::Waiting;
	// Remove planning immediately, before waiting for dynamic navigation.
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->Destroy();
	State = World->GetGameState<ACommandGameState>();
	ACommandPlayerController* Controller = ArmyTestSetup::Controller(World);
	if (!ArmyTestSetup::MapReady(State.Get()) || !Controller || World->GetTimeSeconds() < 3.)
		return EStep::Waiting;
	Human = Controller->GetPlayerState<ACommandPlayerState>();
	if (!Human || Human->CommanderIndex < 0 || !IsValid(State->EnemyCommander))
		return EStep::Waiting;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || Nav->IsNavigationBuildInProgress())
		return EStep::Waiting;
	UnitIndex = ArmyTestSetup::UnitIndex(State.Get(), EUnitRole::Ranged);
	if (!Check(UnitIndex >= 0 && State->Content->Unit(UnitIndex)->UnitCost > 0,
			TEXT("The map catalogue supplies positive-Power ranged fixtures")))
		return EStep::Done;
	FrontlineIndex = ArmyTestSetup::UnitIndex(State.Get(), EUnitRole::Frontline);
	if ((Case == ECase::BuildingEdge || Case == ECase::Timers)
		&& !Check(FrontlineIndex >= 0 && State->Content->Unit(FrontlineIndex)->UnitCost > 0,
			TEXT("The map catalogue supplies positive-Power Frontline fixtures")))
		return EStep::Done;
	if (!FindGeometry())
		return EStep::Waiting;
	return EStep::Continue;
}

bool FScenario::Setup()
{
	UWorld* World = nullptr;
	ACommandPlayerState* Human = nullptr;
	const EStep Preparation = PrepareSetup(World, Human);
	if (Preparation != EStep::Continue)
		return Preparation == EStep::Done;
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		if (It->IsProducer())
			FCommandService::ConfigureProduction(It->OwningPlayerState, *It,
				It->bForceConfigured ? It->ProductionRole : EUnitRole::Unset, false);
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		It->Destroy();
	ACommandPlayerState* HolderWallet = Case == ECase::Jev ? State->EnemyCommander.Get() : Human;
	ACommandPlayerState* ThreatWallet = Case == ECase::Jev ? Human : State->EnemyCommander.Get();
	FVector Anchor;
	if (!SpawnHolders(World, Human, HolderWallet, Anchor) || !SpawnThreats(World, ThreatWallet)
		|| !SpawnBuilding(World, HolderWallet, Anchor))
		return true;
	InitialCenters.SetNum(Holders.Num());
	SetStage(EStage::Posts, World->GetTimeSeconds());
	return false;
}

bool FScenario::SpawnHolders(UWorld* World, ACommandPlayerState* Human, ACommandPlayerState* HolderWallet, FVector& Anchor)
{
	if (Case == ECase::SharedCommanders)
	{
		SecondCommander = World->SpawnActor<ACommandPlayerState>();
		if (!Check(SecondCommander.IsValid(), TEXT("Second independent commander wallet spawns")))
			return false;
		SecondCommander->TeamIndex = 0;
		SecondCommander->CommanderIndex = (Human->CommanderIndex + 1) % 5;
	}
	const int32 Count = Case == ECase::Proportional ? 5 : Case == ECase::SharedCommanders ? 4
		: Case == ECase::Timers                                                           ? 3
																						  : 2;
	Anchor = State->GetRegionAnchor(Region->RegionIndex);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ACommandPlayerState* Wallet = Case == ECase::SharedCommanders && Index % 2 ? SecondCommander.Get() : HolderWallet;
		AArmyGroup* Holder = Spawn(Wallet, Index, Anchor);
		Holders.Add(Holder);
		FVector Ground;
		const FVector SpawnLocation = Case == ECase::BuildingEdge && Index == 0
			? FarOutside
			: Anchor + Side * (Index * 130. - Count * 65.);
		const int32 HolderUnit = Case == ECase::BuildingEdge ? FrontlineIndex : UnitIndex;
		if (!Check(Holder && Project(SpawnLocation, Ground)
					&& Holder->SpawnMember(HolderUnit, Ground, 2),
				TEXT("A real living holder spawns on map navigation")))
			return false;
		Holder->ForceNumber = Index + 1;
		Holder->GetUnits()[0]->NextAttackTime = TNumericLimits<float>::Max();
		if (!Check(FCommandService::IssueForceOrder(Wallet, Holder, EForceVerb::MoveHold, Region->RegionIndex).IsAccepted(),
				TEXT("Real MoveHold accepts travel to the region anchor followed by regional Hold")))
			return false;
	}
	if (Case == ECase::BuildingEdge)
	{
		OutsideEntryStart = Holders[0]->GetCenter();
		if (!Check(!Region->Contains(OutsideEntryStart), TEXT("The incoming holder starts outside its assigned region")))
			return false;
	}
	return true;
}

bool FScenario::SpawnThreats(UWorld* World, ACommandPlayerState* ThreatWallet)
{
	FVector Staging;
	const AHeadquarters* OpposingHQ = Case == ECase::Jev ? State->FriendlyHeadquarters.Get() : State->EnemyHeadquarters.Get();
	if (!Check(Project(OpposingHQ->GetActorLocation() + FVector(Case == ECase::Jev ? 1200. : -1200., 700., 0.), Staging)
				&& !Region->Contains(Staging),
			TEXT("Opposing staging is navigable and outside the held region")))
		return false;
	Enemy = Spawn(ThreatWallet, 40, Staging);
	if (!Check(Enemy.IsValid(), TEXT("Isolated hostile fixture group spawns")))
		return false;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FVector Ground;
		AArmyUnit* Unit = nullptr;
		if (Project(Staging + FVector(0., Index * 130., 0.), Ground))
			Unit = Enemy->SpawnMember(Case == ECase::Timers && Index == 1 ? FrontlineIndex : UnitIndex, Ground, Index);
		if (!Check(Unit != nullptr, TEXT("Hostile fixture member spawns on real navigation")))
			return false;
		Unit->NextAttackTime = TNumericLimits<float>::Max();
		Threats.Add(Unit);
	}
	if (!Check(Enemy->Orders.IsEmpty() && Enemy->HoldRegionIndex == INDEX_NONE,
			TEXT("The scripted idle orphan has no explicit region order or assigned Hold post")))
		return false;
	Enemy->SetActorTickEnabled(false); // Scripted stationary feint/shooter, no unrelated acquisition.
	return true;
}

bool FScenario::SpawnBuilding(UWorld* World, ACommandPlayerState* HolderWallet, const FVector& Anchor)
{
	if (Case == ECase::BuildingEdge || Case == ECase::Border || Case == ECase::Jev)
	{
		const FTransform Transform(BuildingLocation);
		Building = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Building.IsValid(), TEXT("Completed Drill Rig fixture spawns")))
			return false;
		Building->BuildingIndex = ArmyTestSetup::ExtractorIndex;
		Building->OwningPlayerState = HolderWallet;
		Building->ConstructionProgress = 1.f;
		Building->TeamIndex = HolderWallet->TeamIndex;
		Building->FinishSpawning(Transform);
		BuildingInitialHealth = Building->Health;
		if (!Check(Region->Contains(Building->GetActorLocation()) && FVector::Dist2D(Building->GetActorLocation(), Anchor) > 1050.,
				TEXT("The owned Drill Rig is inside the region beyond the former anchor reaction radius")))
			return false;
	}
	return true;
}

AArmyGroup* FScenario::Spawn(ACommandPlayerState* Wallet, int32 Index, const FVector& Home)
{
	const FTransform Transform(Home);
	AArmyGroup* Group = State->GetWorld()->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		Wallet->GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Group)
		return nullptr;
	Group->Initialize({ Wallet->TeamIndex, Wallet, Index, nullptr, Home });
	Group->FinishSpawning(Transform);
	return Group;
}

void FScenario::Teleport(AArmyUnit* Unit, const FVector& Ground)
{
	Unit->SetActorLocation(Ground + FVector(0., 0., 65.), false, nullptr, ETeleportType::TeleportPhysics);
}
}

#endif
