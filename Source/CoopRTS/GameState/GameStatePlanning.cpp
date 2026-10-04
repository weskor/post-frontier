#include "GameState/GameStatePlanning.h"

#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameMode.h"
#include "CommandGameState.h"
#include "Commands/CommandService.h"
#include "Content/MatchContent.h"
#include "CommandPlayerController.h"
#include "DepositSite.h"
#include "EnemyCommander.h"
#include "EnemyCommanderTurn.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameState/GameStateRegistry.h"
#include "Headquarters.h"
#include "NavigationSystem.h"
#include "Rules/PlanningPolicy.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SimulationSettings.h"
#endif

// Planning phase (battle.md "Opening"): a frozen world, a kit per human commander and JEV's matching start.
// Commands are Commands/PlanningCommands.cpp; the pure rules are Rules/PlanningPolicy.cpp.

namespace
{
int32 FirstBuildingWith(const UMatchContent& Content, bool UBuildingDefinition::* Capability)
{
	for (int32 Index = 0; Index < Content.Buildings.Num(); ++Index)
		if (const UBuildingDefinition* Building = Content.Building(Index); Building && Building->*Capability)
			return Index;
	return INDEX_NONE;
}

void CollectRigSites(const ACommandGameState& State, int32 Team, TArray<PlanningPolicy::FRigSite>& Out)
{
	for (const ADepositSite* Deposit : State.Deposits)
	{
		PlanningPolicy::FRigSite& Site = Out.AddDefaulted_GetRef();
		if (!IsValid(Deposit))
			continue;
		Site.Position = Deposit->GetActorLocation();
		Site.bFree = !IsValid(Deposit->Extractor) && Deposit->Remaining > 0;
		Site.bOwnTerritory = State.GetRegionController(Deposit->RegionIndex) == Team
			&& !State.IsRegionContested(Deposit->RegionIndex, Team);
	}
}

// A free deposit within Clearance of Location in the plane.
bool CrowdsFreeDeposit(const ACommandGameState& State, const FVector& Location, float Clearance)
{
	for (const ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && Deposit->Remaining > 0 && !IsValid(Deposit->Extractor)
			&& FVector::DistSquared2D(Location, Deposit->GetActorLocation()) < FMath::Square(Clearance))
			return true;
	return false;
}

int32 SlotOf(const FPlanningKit& Kit)
{
	return IsValid(Kit.Commander) ? Kit.Commander->CommanderIndex : INDEX_NONE;
}
}

bool GameStatePlanning::NavigationReady(UWorld* World)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	return !Navigation || !Navigation->GetDefaultNavDataInstance(FNavigationSystem::DontCreate)
		|| !Navigation->IsNavigationBuildInProgress();
}

// Kits stand on the navmesh, which only builds and updates while its system ticks; a paused world stops that
// unless the system is told otherwise. The flag is protected, so it is set by reflection.
static void SetNavigationTicksWhilePaused(UWorld* World, bool bTicks)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (const FBoolProperty* Flag = Navigation ? FindFProperty<FBoolProperty>(UNavigationSystemV1::StaticClass(), TEXT("bTickWhilePaused")) : nullptr)
		Flag->SetPropertyValue_InContainer(Navigation, bTicks);
}

float ACommandGameState::GetBattleClockStartServerTime() const
{
	if (Planning.bActive)
		return GetServerWorldTimeSeconds();
	return BattleClockStartServerTime >= 0.f ? BattleClockStartServerTime : GetAudioLiveStartServerTime();
}

const FPlanningKit* ACommandGameState::FindKit(const ACommandPlayerState* Commander) const
{
	return Planning.Kits.FindByPredicate([Commander](const FPlanningKit& Kit) { return Kit.Commander == Commander; });
}

FPlanningKit* ACommandGameState::FindKit(const ACommandPlayerState* Commander)
{
	return Planning.Kits.FindByPredicate([Commander](const FPlanningKit& Kit) { return Kit.Commander == Commander; });
}

void ACommandGameState::BeginPlanning()
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || MatchResult != EMatchResult::Ongoing || !IsValid(Content) || !IsValid(EnemyCommander))
		return;
	Planning = FPlanningState();
	Planning.bActive = true;
	Planning.SecondsRemaining = static_cast<float>(PlanningPolicy::PlanningSeconds);
	PlanningStartedReal = World->GetRealTimeSeconds();
	PlanningDeadline = PlanningStartedReal + PlanningPolicy::PlanningSeconds;
	PlanningEndReason = EPlanningEnd::None;
	JevKitRetryAt = 0.;
	BattleClockStartServerTime = -1.f;
	for (ACommandPlayerState* Commander : FGameStateEconomy::Roster(*this))
		Commander->ResetForNewMatch();
	EnemyCommander->ResetForNewMatch();
	ReconcilePlanningRoster();
	SetNavigationTicksWhilePaused(World, true);
	SyncWorldPause(nullptr);
	ForceNetUpdate();
}

void ACommandGameState::SyncWorldPause(ACommandPlayerController* Controller)
{
	ACommandGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ACommandGameMode>() : nullptr;
	if (!Mode)
		return;
	Mode->ApplyMatchPause(Controller, Planning.bActive || PauseBudget.bPaused || bSoloMenuPaused);
	GetWorld()->GetWorldSettings()->ForceNetUpdate();
}

void ACommandGameState::ReconcilePlanningRoster()
{
	const TArray<ACommandPlayerState*> Roster = FGameStateEconomy::Roster(*this);
	TArray<int32, TInlineAllocator<5>> RosterSlots, KitSlots;
	for (const ACommandPlayerState* Commander : Roster)
		RosterSlots.Add(Commander->CommanderIndex);
	for (const FPlanningKit& Kit : Planning.Kits)
		KitSlots.Add(SlotOf(Kit));
	const PlanningPolicy::FRosterChange Change = PlanningPolicy::Reconcile(KitSlots, RosterSlots);
	for (int32 Slot : Change.Leave)
		for (int32 Index = Planning.Kits.Num() - 1; Index >= 0; --Index)
			if (SlotOf(Planning.Kits[Index]) == Slot)
			{
				DestroyKit(Planning.Kits[Index]);
				Planning.Kits.RemoveAt(Index);
			}
	for (int32 Slot : Change.Join)
		for (ACommandPlayerState* Commander : Roster)
			if (Commander->CommanderIndex == Slot)
				Planning.Kits.AddDefaulted_GetRef().Commander = Commander;
	if (!Change.Leave.IsEmpty() || !Change.Join.IsEmpty())
		ForceNetUpdate();
}

void ACommandGameState::DestroyKit(FPlanningKit& Kit)
{
	if (IsValid(Kit.Barracks))
		Kit.Barracks->Destroy();
	if (IsValid(Kit.Rig))
		Kit.Rig->Destroy();
	Kit.Barracks = nullptr;
	Kit.Rig = nullptr;
}

bool ACommandGameState::PlaceKitPiece(FPlanningKit& Kit, bool bRig, const FVector& Location, FString& OutReason)
{
	TObjectPtr<ACommandBuilding>& Piece = bRig ? Kit.Rig : Kit.Barracks;
	const int32 Index = IsValid(Content) ? FirstBuildingWith(*Content, bRig ? &UBuildingDefinition::bRequiresDeposit : &UBuildingDefinition::bProducesForces)
										 : INDEX_NONE;
	if (Index == INDEX_NONE || !IsValid(Kit.Commander))
	{
		OutReason = TEXT("Kit unavailable");
		return false;
	}
	const bool bHadPiece = IsValid(Piece);
	const FVector Previous = bHadPiece ? Piece->GetActorLocation() : FVector::ZeroVector;
	if (bHadPiece)
		Piece->Destroy();
	ACommandBuilding* Placed = ApplyKitPlacement(Index, Location, Kit.Commander, Kit.Commander->TeamIndex, OutReason);
	if (!Placed && bHadPiece)
	{
		FString Ignored;
		Placed = ApplyKitPlacement(Index, Previous, Kit.Commander, Kit.Commander->TeamIndex, Ignored);
		Piece = Placed;
		ForceNetUpdate();
		return false;
	}
	Piece = Placed;
	ForceNetUpdate();
	return Placed != nullptr;
}

bool ACommandGameState::PlaceDefaultBarracks(FPlanningKit& Kit)
{
	const int32 Team = Kit.Commander->TeamIndex;
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(*this, Team);
	if (!Home)
		return false;
	// Rings around the headquarters, mirrored with the side; the first legal spot wins. A spot that would crowd a
	// free deposit is skipped, so a later Drill Rig still fits (as JEV's own placement does).
	const float Orientation = Team == 5 ? -1.f : 1.f;
	const UBuildingDefinition* Barracks = Content->Building(FirstBuildingWith(*Content, &UBuildingDefinition::bProducesForces));
	const float Clearance = Barracks ? Barracks->FootprintRadius * UE_SQRT_2 + 200.f : 0.f;
	FString Reason;
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Location = Home->GetActorLocation()
				+ FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * ((380.f + Ring * 160.f) * Orientation);
			Location.Z = 5.f;
			const bool bCrowds = CrowdsFreeDeposit(*this, Location, Clearance);
			if (!bCrowds && PlaceKitPiece(Kit, false, Location, Reason))
				return true;
		}
	return false;
}

bool ACommandGameState::PlaceDefaultRig(FPlanningKit& Kit)
{
	const int32 Team = Kit.Commander->TeamIndex;
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(*this, Team);
	if (!Home)
		return false;
	TArray<PlanningPolicy::FRigSite> Sites;
	CollectRigSites(*this, Team, Sites);
	FString Reason;
	for (int32 Best = PlanningPolicy::NearestRigSite(Sites, Home->GetActorLocation()); Best != INDEX_NONE;
		Best = PlanningPolicy::NearestRigSite(Sites, Home->GetActorLocation()))
	{
		if (PlaceKitPiece(Kit, true, Sites[Best].Position, Reason))
			return true;
		Sites[Best].bFree = false;
	}
	return false;
}

namespace
{
// JEV's kit forces are made by the game state, not commanded, so the player lock (commands refuse while planning)
// lifts for that one call. The role follows JEV's own producer rule; no human has a unit during planning.
void ConfigureJevKitForces(ACommandGameState& State, TArray<FPlanningKit>& Kits, bool& bChanged)
{
	static const EDamageType NoSlotDamage[JevExecution::RoleSlots] = {};
	int32 Roles[JevExecution::RoleSlots] = {};
	for (const FPlanningKit& Kit : Kits)
		if (IsValid(Kit.Barracks) && Kit.Barracks->bForceConfigured)
			++Roles[RoleSlot(Kit.Barracks->ProductionRole)];
	for (FPlanningKit& Kit : Kits)
	{
		if (!IsValid(Kit.Barracks) || !Kit.Barracks->IsComplete() || Kit.Barracks->bForceConfigured)
			continue;
		const int32 Slot = JevExecution::NextRoleSlot(Roles, JevRelease::FArmorCounts(), NoSlotDamage);
		TGuardValue<bool> Lift(State.Planning.bActive, false);
		if (FCommandService::ConfigureProduction(State.EnemyCommander, Kit.Barracks, SlotRole(Slot), true).IsAccepted())
		{
			++Roles[Slot];
			bChanged = true;
		}
	}
}

// A removed JEV kit gives back what its force cost to configure.
void RefundJevKitForce(ACommandPlayerState& Commander, const FPlanningKit& Kit)
{
	const UArmyUnitDefinition* Unit = IsValid(Kit.Barracks) && Kit.Barracks->bForceConfigured ? Kit.Barracks->GetProductionDefinition() : nullptr;
	if (Unit)
		Commander.AddResources(ACommandBuilding::GetConfigurationCost(*Unit));
}

// JEV's first plans: its kit forces are planned as if at 0:00, on the frozen world, whenever its kit changes.
void PlanJevKitForces(UWorld& World)
{
	for (TActorIterator<AEnemyCommander> It(&World); It; ++It)
		if (It->TeamIndex == 5)
			It->EvaluatePlan();
}
}

void ACommandGameState::PlaceJevKit(bool bForce)
{
	bool bChanged = false;
	while (Planning.JevKits.Num() > Planning.Kits.Num())
	{
		RefundJevKitForce(*EnemyCommander, Planning.JevKits.Last());
		DestroyKit(Planning.JevKits.Last());
		Planning.JevKits.Pop();
		bChanged = true;
	}
	while (Planning.JevKits.Num() < Planning.Kits.Num())
	{
		FPlanningKit& Kit = Planning.JevKits.AddDefaulted_GetRef();
		Kit.Commander = EnemyCommander;
		Kit.bReady = true;
	}
	const double Now = GetWorld()->GetRealTimeSeconds();
	if ((!bForce && Now < JevKitRetryAt) || !GameStatePlanning::NavigationReady(GetWorld()))
	{
		if (bChanged)
			PlanJevKitForces(*GetWorld());
		return;
	}
	JevKitRetryAt = Now + .5;
	for (FPlanningKit& Kit : Planning.JevKits)
	{
		if (!IsValid(Kit.Barracks))
			bChanged |= PlaceDefaultBarracks(Kit);
		if (!IsValid(Kit.Rig))
			bChanged |= PlaceDefaultRig(Kit);
	}
	ConfigureJevKitForces(*this, Planning.JevKits, bChanged);
	if (bChanged)
		PlanJevKitForces(*GetWorld());
	ForceNetUpdate();
}

// One commander's kit at the end of planning: what is missing is placed at default spots, and a Rig with no free
// deposit is paid back in Power. Production and orders are the caller's.
void ACommandGameState::FillKit(FPlanningKit& Kit)
{
	if (!IsValid(Kit.Commander))
		return;
	TArray<PlanningPolicy::FRigSite> Sites;
	CollectRigSites(*this, Kit.Commander->TeamIndex, Sites);
	const AHeadquarters* Home = GameStateRegistry::HomeHeadquarters(*this, Kit.Commander->TeamIndex);
	const bool bSite = Home && PlanningPolicy::NearestRigSite(Sites, Home->GetActorLocation()) != INDEX_NONE;
	const PlanningPolicy::FKitFill Need = PlanningPolicy::Fill(IsValid(Kit.Barracks), IsValid(Kit.Rig), bSite);
	if (Need.bPlaceBarracks && !PlaceDefaultBarracks(Kit))
		UE_LOG(LogTemp, Warning, TEXT("Planning: no room for commander %d's Barracks"), Kit.Commander->CommanderIndex);
	const UBuildingDefinition* Rig = Content->Building(FirstBuildingWith(*Content, &UBuildingDefinition::bRequiresDeposit));
	if ((Need.bRefundRig || (Need.bPlaceRig && !PlaceDefaultRig(Kit))) && Rig)
		Kit.Commander->AddResources(ACommandBuilding::GetBuildCost(*Rig));
}

void ACommandGameState::FillKits()
{
	for (FPlanningKit& Kit : Planning.Kits)
		FillKit(Kit);
	PlaceJevKit(true);
}

void ACommandGameState::GrantLateKit(ACommandPlayerState* Commander)
{
	if (!HasAuthority() || Planning.bActive || PlanningEndCount == 0 || PlanningEndReason == EPlanningEnd::Fixture
		|| MatchResult != EMatchResult::Ongoing || !IsValid(Content) || !IsValid(Commander) || Commander->TeamIndex != 0)
		return;
	FPlanningKit Kit;
	Kit.Commander = Commander;
	FillKit(Kit);
	if (IsValid(Kit.Barracks))
		FCommandService::ConfigureProduction(Commander, Kit.Barracks, Kit.UnitRole, true);
}

void ACommandGameState::TickPlanning()
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	const FSimulationSettings& Simulation = FSimulationSettings::ForWorld(GetWorld());
	FString VerificationDirectory;
	// The planning probe keeps the real phase; every other network probe skips it.
	const bool bVerification = FParse::Value(FCommandLine::Get(), TEXT("CoopRTSNetVerifyDir="), VerificationDirectory)
		&& FPlatformMisc::GetEnvironmentVariable(TEXT("COOPRTS_NET_KEEP_PLANNING")).IsEmpty();
	if (Simulation.bEnabled || bVerification || (GIsAutomationTesting && !bPlanningHeldByTest))
	{
		// A simulation plays the real opening with default kits; duels, network probes and automation
		// fixtures skip it.
		ReconcilePlanningRoster();
		CompletePlanningForHarness(Simulation.bEnabled && !Simulation.bDuel);
		return;
	}
#endif
	ReconcilePlanningRoster();
	PlaceJevKit(false);
	Planning.SecondsRemaining = static_cast<float>(PlanningPolicy::Remaining(GetWorld()->GetRealTimeSeconds(), PlanningDeadline));
	// A frozen world slows normal replication scheduling; the countdown is published every tick, as the pause's is.
	ForceNetUpdate();
	EvaluatePlanningEnd();
}

void ACommandGameState::EvaluatePlanningEnd()
{
	if (!Planning.bActive)
		return;
	int32 Ready = 0;
	for (const FPlanningKit& Kit : Planning.Kits)
		Ready += Kit.bReady ? 1 : 0;
	const double Now = GetWorld()->GetRealTimeSeconds();
	const PlanningPolicy::EEnd End = PlanningPolicy::Evaluate(Planning.Kits.Num(), Ready, Now, PlanningDeadline);
	if (End == PlanningPolicy::EEnd::Continue)
		return;
	const bool bNavigationReady = GameStatePlanning::NavigationReady(GetWorld());
	if (!PlanningPolicy::MayEnd(bNavigationReady, Now, PlanningDeadline))
		return;
	if (!bNavigationReady)
		UE_LOG(LogTemp, Warning, TEXT("Planning ends %.1f s after its deadline with navigation still not ready"), Now - PlanningDeadline);
	EndPlanning(End == PlanningPolicy::EEnd::AllReady ? EPlanningEnd::AllReady : EPlanningEnd::Expired);
}

void ACommandGameState::EndPlanning(EPlanningEnd Reason)
{
	if (!Planning.bActive)
		return;
	if (Reason == EPlanningEnd::Fixture)
	{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
		for (FPlanningKit& Kit : Planning.Kits)
			DestroyKit(Kit);
		for (FPlanningKit& Kit : Planning.JevKits)
			DestroyKit(Kit);
		for (ACommandPlayerState* Commander : FGameStateEconomy::Roster(*this))
			Commander->Resources = GameStatePlanning::FixtureStartingResources;
		if (IsValid(EnemyCommander))
			EnemyCommander->Resources = GameStatePlanning::FixtureStartingResources;
#endif
	}
	else
		FillKits();
	Planning.bActive = false;
	Planning.SecondsRemaining = 0.f;
	PlanningSeconds = GetWorld()->GetRealTimeSeconds() - PlanningStartedReal;
	PlanningEndReason = Reason;
	++PlanningEndCount;
	if (Reason != EPlanningEnd::Fixture)
		BattleClockStartServerTime = GetServerWorldTimeSeconds();
	SetNavigationTicksWhilePaused(GetWorld(), false);
	SyncWorldPause(nullptr);
	if (Reason != EPlanningEnd::Fixture)
		StartKitForces();
	UE_LOG(LogTemp, Display, TEXT("Planning ended reason=%d after %.1f real seconds"), static_cast<int32>(Reason), PlanningSeconds);
	Planning.Kits.Reset();
	Planning.JevKits.Reset();
	ForceNetUpdate();
}

// At 0:00 production starts: each Barracks takes its chosen unit type and the first orders, and JEV plans.
void ACommandGameState::StartKitForces()
{
	for (const FPlanningKit& Kit : Planning.Kits)
	{
		if (!IsValid(Kit.Commander) || !IsValid(Kit.Barracks))
			continue;
		if (!FCommandService::ConfigureProduction(Kit.Commander, Kit.Barracks, Kit.UnitRole, true))
		{
			UE_LOG(LogTemp, Warning, TEXT("Planning: commander %d's Barracks could not start production"), Kit.Commander->CommanderIndex);
			continue;
		}
		bool bQueue = false;
		for (const FPlanningOrder& Order : Kit.Orders)
		{
			FCommandService::IssueForceOrder(Kit.Commander, Kit.Barracks->ForceGroup, Order.Verb, Order.RegionIndex,
				Order.Structure, bQueue);
			bQueue = true;
		}
	}
	for (TActorIterator<AEnemyCommander> It(GetWorld()); It; ++It)
		if (It->TeamIndex == 5)
			It->EvaluatePlan();
}

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
bool ACommandGameState::bPlanningHeldByTest = false;

void ACommandGameState::CompletePlanningForHarness(bool bWithKits)
{
	if (!Planning.bActive || bPlanningHeldByTest)
		return;
	if (!bWithKits)
	{
		EndPlanning(EPlanningEnd::Fixture);
		return;
	}
	for (FPlanningKit& Kit : Planning.Kits)
		Kit.bReady = true;
	if (PlanningPolicy::MayEnd(GameStatePlanning::NavigationReady(GetWorld()), GetWorld()->GetRealTimeSeconds(), PlanningDeadline))
		EndPlanning(EPlanningEnd::Harness);
}
#endif
