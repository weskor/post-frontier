#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "DepositSite.h"
#include "MapRegion.h"
#include "CommandGameMode.h"
#include "Content/BuildingDefinition.h"
#include "Headquarters.h"
#include "NavigationSystem.h"
#include "Rules/PlacementPolicy.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructionLifecycleTest, "CoopRTS.Construction.Lifecycle",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructionProductionTest, "CoopRTS.Construction.Production",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Named, not anonymous: a using-directive inside an anonymous namespace leaks into
// every later file of a unity translation unit.
namespace ConstructionScenarioTests
{
using namespace ArmyTestSetup;
bool FindPlacement(ACommandGameState* State, int32 BuildingIndex, const FVector& Center, FVector& Result)
{
	const UBuildingDefinition* Definition = State->Content->Building(BuildingIndex);
	if (!Definition)
		return false;
	if (Definition->bRequiresDeposit)
	{
		for (ADepositSite* Deposit : State->Deposits)
		{
			if (!IsValid(Deposit) || IsValid(Deposit->Extractor))
				continue;
			const AMapRegion* Region = State->FindRegionAt(Deposit->GetActorLocation());
			const AMapRegion* RequestedRegion = State->FindRegionAt(Center);
			if (!Region || Region != RequestedRegion)
				continue;
			const FVector Point = State->ResolveBuildingLocation(BuildingIndex, Deposit->GetActorLocation());
			FString Reason;
			if (State->ValidateBuildingPlacement(BuildingIndex, 0, Point, Reason))
			{
				Result = Point;
				return true;
			}
		}
		return false;
	}
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			Point.Z = 5.f;
			Point = State->ResolveBuildingLocation(BuildingIndex, Point);
			if (State->FindRegionAt(Point) != State->FindRegionAt(Center))
				continue;
			FString Reason;
			if (State->ValidateBuildingPlacement(BuildingIndex, 0, Point, Reason))
			{
				Result = Point;
				return true;
			}
		}
	return false;
}
bool AssignNavigableFront(ACommandPlayerController* PC, ACommandBuilding* Producer,
	EFrontOrder Order, const FVector& Requested)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Producer->GetWorld());
	if (!Navigation)
		return false;
	// Projection alone may pick a disconnected island. The owning command also validates the complete route.
	for (int32 Ring = 0; Ring <= 6; ++Ring)
		for (int32 Direction = 0; Direction < (Ring == 0 ? 1 : 12); ++Direction)
		{
			const float Angle = Direction * PI / 6.f;
			const FVector Candidate = Requested + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (Ring * 200.f);
			FNavLocation Ground;
			if (!Navigation->ProjectPointToNavigation(Candidate, Ground, FVector(75.f, 75.f, 200.f))
				|| !AArenaBounds::IsTravelLocation(Producer->GetWorld(), Ground.Location))
				continue;
			bool bClearFormation = true;
			for (int32 X = -4; X <= 4 && bClearFormation; ++X)
				for (int32 Y = -4; Y <= 4; ++Y)
				{
					const FVector Sample = Ground.Location + FVector(X * 50.f, Y * 50.f, 0.f);
					FNavLocation Clear;
					if (!Navigation->ProjectPointToNavigation(Sample, Clear, FVector(15.f, 15.f, 100.f))
						|| FVector::DistSquared2D(Sample, Clear.Location) > FMath::Square(15.f))
					{
						bClearFormation = false;
						break;
					}
				}
			if (!bClearFormation)
				continue;
			FCommandService::AssignFront(PC->GetPlayerState<ACommandPlayerState>(), Producer, Order, Ground.Location);
			if (Producer->FrontOrder == Order && Producer->HasConfiguredFront()
				&& Producer->FrontLocation.Equals(Ground.Location, 1.f))
				return true;
		}
	return false;
}
class FConstructionScenario : public IAutomationLatentCommand
{
public:
	FConstructionScenario(FAutomationTestBase* InTest, bool bInProduction) : Test(InTest), bProduction(bInProduction) {}
	bool Update() override
	{
		UWorld* World = ArmyTestSetup::World();
		if (!World || World->GetTimeSeconds() < 3.f)
			return false;
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!PC || !State || !Wallet || Wallet->CommanderIndex < 0 || !MapReady(State) || !State->Content)
			return false;
		if (Stage == 0)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->TeamIndex == 5)
					It->Destroy();
			State->bVerificationIncomePaused = true;
			Wallet->Resources = 4000; // Budget fixture; configuration and every recruit use real paid authority paths.
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->GetTeamIndex() == 0)
					return Fail(TEXT("Normal new match must not spawn fixed friendly armies"));
			const UBuildingDefinition* Barracks = State->Content->Building(BarracksIndex);
			if (!Barracks)
				return Fail(TEXT("Match content lacks the barracks definition"));
			FVector Location;
			if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
				return Fail(TEXT("No valid barracks footprint in HQ construction territory"));
			const FVector Snapped = Location;
			Location += FVector(7.f, -11.f, 0.f);
			if (!Check(Location.X != Snapped.X && Location.Y != Snapped.Y,
					TEXT("Server placement fixture requests explicitly off-grid XY")))
				return true;
			FString RequestedReason, SnappedReason;
			const bool bRequestedValid = State->ValidateBuildingPlacement(BarracksIndex, 0, Location, RequestedReason);
			const bool bSnappedValid = State->ValidateBuildingPlacement(BarracksIndex, 0, Snapped, SnappedReason);
			if (!Check(bRequestedValid && bSnappedValid && RequestedReason == SnappedReason,
					TEXT("Off-grid and snapped valid placements have the same verdict and reason")))
				return true;
			if (!Check(State->IsInBuildTerritory(BarracksIndex, 0, Location)
						&& State->IsInBuildTerritory(BarracksIndex, 0, Snapped),
					TEXT("Off-grid and snapped valid footprints share build territory")))
				return true;
			UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation Ground;
			if (!Check(Navigation && Navigation->ProjectPointToNavigation(Snapped, Ground, FVector(45.f, 45.f, 200.f)),
					TEXT("Server placement fixture has navigable snapped ground")))
				return true;
			const int32 Before = Wallet->Resources;
			FString PreviewReason;
			if (!Check(PC->CanPlaceBuildingAt(BarracksIndex, Location, PreviewReason),
					TEXT("Construction preview permits a valid affordable footprint regardless of explanatory text")))
				return true;
			Wallet->Resources = Barracks->BuildCost - 1;
			if (!Check(!PC->CanPlaceBuildingAt(BarracksIndex, Location, PreviewReason),
					TEXT("Construction preview rejects a valid footprint when one resource short")))
				return true;
			Wallet->Resources = Before;
			Building = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
			if (!Check(Building.IsValid() && Wallet->Resources == Before - Barracks->BuildCost,
					TEXT("Owned placement creates one paid barracks")))
				return true;
			const FVector BuiltLocation = Building->GetActorLocation();
			if (!Check(BuiltLocation.X == Snapped.X && BuiltLocation.Y == Snapped.Y,
					TEXT("Off-grid server request builds at exact snapped XY, not navigation-projected XY")))
				return true;
			if (!Check(FMath::IsNearlyEqual(BuiltLocation.Z, Ground.Location.Z + 65.f, .01),
					TEXT("Server placement uses navigation ground height plus 65")))
				return true;
			const bool bRequestedOverlap = State->ValidateBuildingPlacement(BarracksIndex, 0, Location, RequestedReason);
			const bool bSnappedOverlap = State->ValidateBuildingPlacement(BarracksIndex, 0, Snapped, SnappedReason);
			if (!Check(!bRequestedOverlap && !bSnappedOverlap && !RequestedReason.IsEmpty() && RequestedReason == SnappedReason,
					TEXT("Off-grid and snapped overlapping placements have the same rejection and reason")))
				return true;
			const FVector OutsideSnapped = PlacementPolicy::SnapToBuildGrid(OutsideArena(State),
				ACommandBuilding::GetFootprintRadius(*Barracks));
			const FVector OutsideRequest = OutsideSnapped + FVector(7.f, -11.f, 0.f);
			if (!Check(!State->IsInBuildTerritory(BarracksIndex, 0, OutsideRequest)
						&& !State->IsInBuildTerritory(BarracksIndex, 0, OutsideSnapped),
					TEXT("Off-grid and snapped outside footprints both lack build territory")))
				return true;
			if (!Check(!Building->IsComplete(), TEXT("Placement begins construction instead of instantly completing")))
				return true;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, true);
			if (!Check(!Building->bProductionEnabled, TEXT("Unfinished production rejects activation")))
				return true;
			const int32 After = Wallet->Resources;
			FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location);
			FCommandService::PlaceBuilding(Wallet, BarracksIndex, OutsideArena(State));
			FCommandService::PlaceBuilding(Wallet, 255, Location + FVector(400.f, 0.f, 0.f)); // No such definition index.
			if (!Check(Wallet->Resources == After, TEXT("Overlap, outside territory and invalid definition cannot debit wallet")))
				return true;
			Building->Tick(60.f);
			if (!Check(Building->IsComplete() && Building->Health > 0, TEXT("Game-time construction completes a living building")))
				return true;
			if (!bProduction)
				return Lifecycle(World, PC, State, Wallet);
			Stage = 1;
			return false; // Let the real dynamic navmesh incorporate the new blocker before deployment.
		}
		if (Stage < 8 && !Building.IsValid())
			return Fail(TEXT("Production building disappeared"));
		if (Stage == 1)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress())
				return false;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FVector Location;
				if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
					return Fail(TEXT("No footprint for independent force producer"));
				ACommandBuilding* Added = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
				if (!Added)
					return Fail(TEXT("Independent producer placement rejected"));
				Added->Tick(60.f);
				Producers.Add(Added);
			}
			Producers.Insert(Building.Get(), 0);
			UnrelatedWallet = World->SpawnActor<ACommandPlayerState>();
			if (!UnrelatedWallet.IsValid())
				return Fail(TEXT("Independent commander wallet fixture could not spawn"));
			UnrelatedWallet->CommanderIndex = 1;
			UnrelatedWallet->Resources = 777;
			State->AddPlayerState(UnrelatedWallet.Get());
			Attacker = SpawnGroup(World, nullptr, -1, HostileStaging(State));
			if (!Attacker.IsValid())
				return Fail(TEXT("Hostile damage fixture failed"));
			FCommandService::IssueOrder(State->EnemyCommander, Attacker.Get(), EArmyOrder::Hold, Attacker->GetCenter());
			Attacker->SetActorTickEnabled(false);
			for (AArmyUnit* Unit : Attacker->GetUnits())
				Unit->SetActorTickEnabled(false);
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress())
				return false;
			const EUnitRole Roles[] = { EUnitRole::Ranged, EUnitRole::Frontline, EUnitRole::Siege };
			for (int32 Index = 0; Index < Producers.Num(); ++Index)
			{
				ACommandBuilding* Producer = Producers[Index].Get();
				const int32 Before = Wallet->Resources;
				FCommandService::ConfigureProduction(Wallet, Producer, static_cast<EUnitRole>(255), true);
				if (!Check(!Producer->bForceConfigured && !Producer->bProductionEnabled
							&& !IsValid(Producer->ForceGroup) && Wallet->Resources == Before,
						TEXT("Invalid first Start rejects atomically without allocating a force or charging")))
					return true;
				if (Roles[Index] == EUnitRole::Siege)
				{
					Wallet->Resources = 179;
					FCommandService::ConfigureProduction(Wallet, Producer, EUnitRole::Siege, true);
					if (!Check(!Producer->bForceConfigured && Wallet->Resources == 179,
							TEXT("Unaffordable siege configuration does not lock type or debit")))
						return true;
					Wallet->Resources = Before;
				}
				FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], false);
				if (!Check(!Producer->bForceConfigured, TEXT("Choosing a type before Start does not lock it")))
					return true;
				FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], true);
				if (!Check(Producer->bForceConfigured && IsValid(Producer->ForceGroup) && Alive(Producer) == 0
							&& Wallet->Resources == Before - (Roles[Index] == EUnitRole::Siege ? 180 : 0),
						TEXT("First Start creates a permanent typed force and charges siege configuration exactly 180")))
					return true;
				const float ExpectedDuration = Roles[Index] == EUnitRole::Frontline ? 10.f / 3.f
					: Roles[Index] == EUnitRole::Ranged                             ? 13.f / 3.f
																					: 20.f / 3.f;
				const int32 BeforeWork = Wallet->Resources;
				Producer->TickProduction(ExpectedDuration - .25f);
				if (!Check(Alive(Producer) == 0 && Wallet->Resources == BeforeWork,
						TEXT("Work short of one unit duration cannot spawn or debit, for any force type")))
					return true;
				FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], false);
				const int32 ConfiguredBalance = Wallet->Resources;
				const float Progress = Producer->ProductionProgressSeconds;
				AArmyGroup* Identity = Producer->ForceGroup;
				FCommandService::ConfigureProduction(Wallet, Producer, static_cast<EUnitRole>(255), true);
				FCommandService::ConfigureProduction(Wallet, Producer, Roles[(Index + 1) % 3], true);
				if (!Check(Producer->ProductionRole == Roles[Index] && !Producer->bProductionEnabled
							&& Producer->ForceGroup == Identity && Producer->ProductionProgressSeconds == Progress
							&& Wallet->Resources == ConfiguredBalance,
						TEXT("Invalid and changed roles after Start reject atomically even while paused")))
					return true;
				FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], true);
				if (!Check(Wallet->Resources == ConfiguredBalance, TEXT("Resume never repeats configuration charge")))
					return true;
				FCommandService::ConfigureProduction(Wallet, Producer, Roles[Index], false);
				Forces.Add(Identity);
			}
			if (!Check(Forces[0] != Forces[1] && Forces[1] != Forces[2] && Forces[0] != Forces[2],
					TEXT("Every producer owns a distinct stable force")))
				return true;
			Wallet->Resources = 0;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
			const float StarvedProgress = Building->ProductionProgressSeconds;
			Building->TickProduction(60.f);
			if (!Check(Alive(Building.Get()) == 0 && Building->ProductionProgressSeconds == StarvedProgress
						&& Wallet->Resources == 0,
					TEXT("Starvation cannot emit recruits or advance paid work")))
				return true;
			Wallet->Resources = 2000;
			Building->TickProduction(Building->GetProductionDuration());
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
			Squad = Building->ForceGroup;
			if (!Check(Alive(Building.Get()) == 1 && Wallet->Resources == 1970,
					TEXT("One completed ranged slot produces one unit, not a batch, for exactly 30")))
				return true;
			Recruit = Squad->GetUnits()[0];
			if (!Check(Recruit->GetUnitRole() == EUnitRole::Ranged && Recruit->GetCommanderIndex() == Wallet->CommanderIndex
						&& Recruit->GetGroup() == Squad.Get() && FVector::Dist2D(Recruit->GetActorLocation(), Building->GetActorLocation()) > State->Content->Building(BarracksIndex)->FootprintRadius,
					TEXT("Paid recruit physically starts outside its owning producer")))
				return true;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
			Building->TickProduction(Building->GetProductionDuration() * .25f);
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
			const float PausedProgress = Building->ProductionProgressSeconds;
			const int32 PausedBalance = Wallet->Resources;
			Building->TickProduction(60.f);
			if (!Check(PausedProgress > 0.f && Building->ProductionProgressSeconds == PausedProgress
						&& Wallet->Resources == PausedBalance && Alive(Building.Get()) == 1,
					TEXT("Pause preserves partially completed unit work and wallet")))
				return true;
			FillBalance = Wallet->Resources;
			for (int32 Index = 0; Index < Producers.Num(); ++Index)
			{
				ACommandBuilding* Producer = Producers[Index].Get();
				// Fronts are HQ-relative: the first force east-south of the HQ, the others
				// further north, out of the recruit's later travel corridor; this scenario
				// proves ownership/refill, not crowd-grid escape.
				const FVector RequestedFront = Index == 0 ? FromFriendlyHQ(State, 1700.f, -1100.f, 5.f)
														  : FromFriendlyHQ(State, 1000.f, 1900.f + Index * 400.f, 5.f);
				if (!AssignNavigableFront(PC, Producer, EFrontOrder::Defend, RequestedFront))
					return Fail(*FString::Printf(TEXT("Independent force %d has no reachable front near %s"), Index, *RequestedFront.ToString()));
				FCommandService::ConfigureProduction(Wallet, Producer, State->Content->Unit(Producer->ProductionUnitIndex)->Role, true);
			}
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			const int32 Capacities[] = { 4, 6, 2 };
			const int32 Costs[] = { 30, 20, 50 };
			int32 ExpectedDebit = 0;
			bool bFull = true;
			const bool bReportProgress = World->GetTimeSeconds() - LastProgressReport >= 5.f;
			if (bReportProgress)
				LastProgressReport = World->GetTimeSeconds();
			for (int32 Index = 0; Index < Producers.Num(); ++Index)
			{
				int32 Joined, Travelling;
				Producers[Index]->GetForceCounts(Joined, Travelling);
				if (!Check(Joined + Travelling <= Capacities[Index], TEXT("Alive travellers consume their producer capacity")))
					return true;
				if (!Check(ValidMembers(Producers[Index].Get(), Capacities[Index]), TEXT("Force roles, ownership and unique slots remain valid")))
					return true;
				ExpectedDebit += (Joined + Travelling - (Index == 0 ? 1 : 0)) * Costs[Index];
				bFull &= Joined == Capacities[Index] && Travelling == 0;
				if (bReportProgress)
					UE_LOG(LogTemp, Display, TEXT("Production fixture filling force=%d joined=%d travelling=%d state=%d front=%s"),
						Index, Joined, Travelling, static_cast<int32>(Producers[Index]->GetProductionState()),
						*Producers[Index]->FrontLocation.ToString());
			}
			if (!Check(Wallet->Resources == FillBalance - ExpectedDebit, TEXT("Each produced unit charges only its own role price")))
				return true;
			if (!bFull)
				return false;
			for (const auto& Producer : Producers)
			{
				const float Progress = Producer->ProductionProgressSeconds;
				Producer->TickProduction(60.f);
				if (!Check(Producer->ProductionProgressSeconds == Progress && Producer->GetProductionState() == EProductionState::ForceComplete,
						TEXT("Enabled full forces stop work and report automatic capacity waiting")))
					return true;
				FCommandService::ConfigureProduction(Wallet, Producer.Get(), State->Content->Unit(Producer->ProductionUnitIndex)->Role, false);
				if (!Check(Producer->GetProductionState() == EProductionState::Paused,
						TEXT("Explicit pause takes presentation priority even on a full force")))
					return true;
			}
			if (!Check(Wallet->Resources == FillBalance - 3 * 30 - 6 * 20 - 2 * 50,
					TEXT("Twelve living units fill independent 4/6/2 forces without a shared cap")))
				return true;
			OtherFront = Forces[1]->FrontLocation;
			OtherOrder = Forces[1]->FrontOrder;
			FCommandService::AssignFront(Wallet, Building.Get(), EFrontOrder::Secure, FromFriendlyHQ(State, 1700.f, -1100.f, 5.f));
			if (!Check(Forces[1]->FrontLocation == OtherFront && Forces[1]->FrontOrder == OtherOrder,
					TEXT("An owning commander's front change affects only the selected producer")))
				return true;
			AArmyUnit* Victim = Squad->GetUnits()[0];
			Victim->ReceiveAttack(Victim->GetHealth(), Attacker->GetUnits()[0]);
			if (!Check(!Victim->IsAlive() && Alive(Building.Get()) == 3, TEXT("Real lethal damage opens exactly one vacancy")))
				return true;
			ReplacementBalance = Wallet->Resources;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
			Stage = 4;
			return false;
		}
		if (Stage == 4)
		{
			for (AArmyUnit* Unit : Squad->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive() && Unit->IsReinforcing())
					Recruit = Unit;
			if (!Recruit.IsValid() || !Recruit->IsReinforcing())
				return false;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
			if (!Check(Wallet->Resources == ReplacementBalance - 30 && UnrelatedWallet->Resources == 777
						&& Alive(Building.Get()) == 4 && Alive(Producers[1].Get()) == 6,
					TEXT("Replacement debits only its own commander, without taking another producer's capacity")))
				return true;
			RecruitStart = Recruit->GetActorLocation();
			JoinedStart = Squad->GetCenter();
			if (!Check(FVector::Dist2D(RecruitStart, Building->GetActorLocation()) < 1200.f
						&& FVector::Dist2D(RecruitStart, JoinedStart) > 500.f && JoinedCenterMatches(Squad.Get()),
					TEXT("Replacement leaves producer rather than spawning at force; center excludes travellers")))
				return true;
			if (!AssignNavigableFront(PC, Building.Get(), EFrontOrder::Defend, FromFriendlyHQ(State, 1700.f, 3100.f, 5.f)))
				return Fail(TEXT("Replacement front needs a complete navigable route"));
			Stage = 5;
			return false;
		}
		if (Stage == 5)
		{
			if (!Check(Recruit.IsValid() && Recruit->IsAlive() && JoinedCenterMatches(Squad.Get()),
					TEXT("Moving force center remains independent of travelling recruit")))
				return true;
			bRecruitMoved |= FVector::Dist2D(RecruitStart, Recruit->GetActorLocation()) > 200.f;
			bJoinedMoved |= FVector::Dist2D(JoinedStart, Squad->GetCenter()) > 200.f;
			if (Recruit->IsReinforcing() || !bRecruitMoved || !bJoinedMoved)
				return false;
			if (!Check(FVector::Dist2D(Recruit->GetActorLocation(), Squad->GetCenter()) < 500.f
						&& Forces[1]->FrontLocation == OtherFront && Forces[1]->FrontOrder == OtherOrder,
					TEXT("Recruit follows moving force to physical arrival without altering the other front")))
				return true;
			while (!Squad->GetUnits().IsEmpty())
			{
				AArmyUnit* Unit = Squad->GetUnits().Last();
				Unit->ReceiveAttack(Unit->GetHealth(), Attacker->GetUnits()[0]);
				if (!Check(!Unit->IsAlive() && !Squad->GetUnits().Contains(Unit), TEXT("Each lethal hit removes its force member")))
					return true;
			}
			if (!Check(IsValid(Building->ForceGroup) && Building->ForceGroup == Squad.Get() && Alive(Building.Get()) == 0,
					TEXT("Complete wipe retains the same empty force identity")))
				return true;
			RememberedFront = Building->FrontLocation;
			ReplacementBalance = Wallet->Resources;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
			Stage = 6;
			return false;
		}
		if (Stage == 6)
		{
			if (Alive(Building.Get()) == 0)
				return false;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
			Recruit = Squad->GetUnits()[0];
			if (!Check(Recruit->IsReinforcing() && Building->ForceGroup == Squad.Get()
						&& Squad->FrontLocation == RememberedFront && Wallet->Resources == ReplacementBalance - 30,
					TEXT("Wiped force refills its remembered front under the original identity")))
				return true;
			Recruit->ReceiveAttack(Recruit->GetHealth(), Attacker->GetUnits()[0]);
			if (!Check(!Recruit->IsAlive() && Alive(Building.Get()) == 0,
					TEXT("Killing a travelling recruit reopens its paid vacancy")))
				return true;
			ReplacementBalance = Wallet->Resources;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
			Stage = 7;
			return false;
		}
		if (Stage == 7)
		{
			if (Alive(Building.Get()) == 0)
				return false;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
			if (!Check(Wallet->Resources == ReplacementBalance - 30, TEXT("Dead traveller replacement charges again")))
				return true;
			Building->ReceiveAttack(Building->Health, Attacker->GetUnits()[0]);
			if (!Check(Squad.IsValid() && !IsValid(Squad->GetProductionBuilding()) && Squad->FrontLocation == RememberedFront,
					TEXT("Destroyed producer leaves survivors on their last front with no producer transfer")))
				return true;
			SurvivorCount = Squad->GetUnits().Num();
			ReplacementBalance = Wallet->Resources;
			Blocker = World->SpawnActor<AActor>();
			if (!Blocker.IsValid())
				return Fail(TEXT("Deployment obstruction fixture could not spawn"));
			UBoxComponent* Box = NewObject<UBoxComponent>(Blocker.Get());
			Blocker->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(1400.f, 1400.f, 300.f));
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Box->RegisterComponent();
			Blocker->SetActorLocation(Producers[2]->GetActorLocation());
			while (!Forces[2]->GetUnits().IsEmpty())
			{
				AArmyUnit* Unit = Forces[2]->GetUnits().Last();
				Unit->ReceiveAttack(Unit->GetHealth(), Attacker->GetUnits()[0]);
				if (!Check(!Unit->IsAlive() && !Forces[2]->GetUnits().Contains(Unit), TEXT("Blocked-producer casualty is real lethal damage")))
					return true;
			}
			FCommandService::ConfigureProduction(Wallet, Producers[2].Get(), EUnitRole::Siege, true);
			Stage = 8;
			return false;
		}
		if (Stage == 8)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress())
				return false;
			Producers[2]->TickProduction(60.f);
			if (!Check(Alive(Producers[2].Get()) == 0 && Wallet->Resources == ReplacementBalance,
					TEXT("Physically blocked deployment never charges or emits a recruit")))
				return true;
			Blocker->Destroy();
			FCommandService::ConfigureProduction(Wallet, Producers[2].Get(), EUnitRole::Siege, false);
			Stage = 9;
			return false;
		}
		if (Stage == 9)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress())
				return false;
			FVector Location;
			if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
				return Fail(TEXT("No legal footprint for a replacement producer"));
			NewProducer = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
			if (!NewProducer.IsValid())
				return Fail(TEXT("Replacement producer placement rejected"));
			NewProducer->Tick(60.f);
			Stage = 10;
			return false;
		}
		if (Stage == 10)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress())
				return false;
			if (!Check(FCommandService::ConfigureProduction(Wallet, NewProducer.Get(), EUnitRole::Frontline, true).IsAccepted()
						&& IsValid(NewProducer->ForceGroup) && NewProducer->ForceGroup != Squad.Get()
						&& Alive(NewProducer.Get()) == 0 && Squad->GetUnits().Num() == SurvivorCount
						&& !IsValid(Squad->GetProductionBuilding()),
					TEXT("A new producer creates its own empty force instead of adopting orphan survivors")))
				return true;
			FCommandService::ConfigureProduction(Wallet, NewProducer.Get(), EUnitRole::Frontline, false);
			ReplacementBalance = Wallet->Resources;
			State->SetMatchResult(EMatchResult::Victory); // Terminal guard fixture, not outcome proof.
			const float Progress = Producers[2]->ProductionProgressSeconds;
			FCommandService::ConfigureProduction(Wallet, Producers[2].Get(), EUnitRole::Siege, true);
			FCommandService::AssignFront(Wallet, Producers[1].Get(), EFrontOrder::FallBack, FromFriendlyHQ(State, 1700.f, 600.f, 5.f));
			Producers[2]->TickProduction(60.f);
			if (!Check(!Producers[2]->bProductionEnabled && Producers[2]->ProductionProgressSeconds == Progress
						&& Wallet->Resources == ReplacementBalance && Forces[1]->FrontLocation == OtherFront
						&& Squad->GetUnits().Num() == SurvivorCount && !IsValid(Squad->GetProductionBuilding()),
					TEXT("Terminal freezes production/front commands; orphan survivors receive no free refill or transfer")))
				return true;
			Test->AddInfo(TEXT("Fixed-force proof: one paid unit, permanent role, siege charge, 4/6/2 independent capacities, travel/arrival, casualty and traveller replacement, wipe identity, pause/starve/block/terminal and producer destruction."));
			return true;
		}
		return false;
	}
private:
	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
			Test->AddError(Message);
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	static int32 Alive(const ACommandBuilding* Producer)
	{
		int32 Joined, Travelling;
		Producer->GetForceCounts(Joined, Travelling);
		return Joined + Travelling;
	}
	static bool JoinedCenterMatches(const AArmyGroup* Force)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
			{
				Sum += Unit->GetActorLocation();
				++Count;
			}
		return Count == 0 || FVector::Dist2D(Sum / Count, Force->GetCenter()) < 1.f;
	}
	static bool ValidMembers(const ACommandBuilding* Producer, int32 Capacity)
	{
		uint32 Slots = 0;
		for (const AArmyUnit* Unit : Producer->ForceGroup->GetUnits())
		{
			if (!IsValid(Unit) || !Unit->IsAlive())
				continue;
			if (Unit->GetUnitRole() != Producer->ProductionRole || Unit->GetGroup() != Producer->ForceGroup
				|| Unit->GetCommanderIndex() != Producer->OwningPlayerState->CommanderIndex
				|| Unit->GetCompositionSlot() < 0 || Unit->GetCompositionSlot() >= Capacity
				|| (Slots & (1u << Unit->GetCompositionSlot())))
				return false;
			Slots |= 1u << Unit->GetCompositionSlot();
		}
		return true;
	}
	bool Lifecycle(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Wallet)
	{
		ACommandPlayerController* Other = World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* OtherWallet = World->SpawnActor<ACommandPlayerState>();
		if (!Other || !OtherWallet)
			return Fail(TEXT("Other owner fixture could not spawn"));
		Other->SetPlayerState(OtherWallet);
		OtherWallet->CommanderIndex = 1;
		OtherWallet->Resources = 777;
		State->AddPlayerState(OtherWallet);
		const int32 Before = Wallet->Resources;
		FCommandService::ConfigureProduction(OtherWallet, Building.Get(), EUnitRole::Ranged, true);
		FCommandService::CancelBuilding(OtherWallet, Building.Get());
		FCommandService::AssignFront(OtherWallet, Building.Get(), EFrontOrder::FallBack, FromFriendlyHQ(State, 1700.f, 600.f, 5.f));
		if (!Check(Building.IsValid() && !Building->bForceConfigured && !Building->bProductionEnabled
					&& !Building->HasConfiguredFront() && Wallet->Resources == Before && OtherWallet->Resources == 777,
				TEXT("Same-team foreign role/front/cancel commands change neither building nor either wallet")))
			return true;
		FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, true);
		FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Ranged, false);
		AArmyGroup* ConfiguredForce = Building->ForceGroup;
		FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Siege, true);
		if (!Check(Building->bForceConfigured && Building->ProductionRole == EUnitRole::Ranged
					&& !Building->bProductionEnabled && Building->ForceGroup == ConfiguredForce && Wallet->Resources == Before,
				TEXT("Lifecycle retains first-Start configuration and rejects paused type changes without an upgrade path")))
			return true;
		FVector Location;
		if (!FindPlacement(State, WorkshopIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
			return Fail(TEXT("No workshop footprint in HQ territory"));
		const FVector Snapped = Location;
		Location += FVector(-9.f, 13.f, 0.f);
		if (!Check(Location.X != Snapped.X && Location.Y != Snapped.Y,
				TEXT("Direct placement fixture requests explicitly off-grid XY")))
			return true;
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FNavLocation Ground;
		if (!Check(Navigation && Navigation->ProjectPointToNavigation(Snapped, Ground, FVector(45.f, 45.f, 200.f)),
				TEXT("Direct placement fixture has navigable snapped ground")))
			return true;
		ACommandBuilding* Workshop = FCommandService::PlaceBuilding(Wallet, WorkshopIndex, Location).Building;
		if (!Check(Workshop != nullptr, TEXT("Workshop construction accepted")))
			return true;
		const FVector BuiltLocation = Workshop->GetActorLocation();
		if (!Check(BuiltLocation.X == Snapped.X && BuiltLocation.Y == Snapped.Y,
				TEXT("Off-grid placement request builds the even-cell workshop at exact snapped XY")))
			return true;
		if (!Check(FMath::IsNearlyEqual(BuiltLocation.Z, Ground.Location.Z + 65.f, .01),
				TEXT("Direct placement uses navigation ground height plus 65")))
			return true;
		Workshop->Tick(60.f);
		const int32 ResearchBalance = Wallet->Resources;
		FCommandService::Research(Wallet, Workshop, EArmyDoctrine::SiegeOptics);
		FCommandService::Research(Wallet, Workshop, EArmyDoctrine::FieldRepairs);
		if (!Check(Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Wallet->Resources == ResearchBalance - ACommandBuilding::ResearchCost
					&& OtherWallet->Doctrine == EArmyDoctrine::None,
				TEXT("Research is paid exactly once and scoped to owning commander")))
			return true;
		if (!FindPlacement(State, BarracksIndex, State->FriendlyHeadquarters->GetActorLocation(), Location))
			return Fail(TEXT("No cancellation-test footprint"));
		const int32 CancelBalance = Wallet->Resources;
		ACommandBuilding* Cancelled = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
		if (!Check(Cancelled && FCommandService::CancelBuilding(Wallet, Cancelled).IsAccepted() && Wallet->Resources == CancelBalance,
				TEXT("Immediate cancellation refunds unbuilt construction exactly once")))
			return true;
		const int32 AfterCancel = Wallet->Resources;
		FCommandService::CancelBuilding(Wallet, Cancelled);
		if (!Check(Wallet->Resources == AfterCancel, TEXT("Repeated cancellation cannot mint resources")))
			return true;
		ADepositSite* Deposit = nullptr;
		ACapturePoint* Site = nullptr;
		for (ADepositSite* Candidate : State->Deposits)
		{
			if (!IsValid(Candidate) || IsValid(Candidate->Extractor))
				continue;
			for (AMapRegion* Region : State->Regions)
				if (IsValid(Region) && Region->RegionIndex == Candidate->RegionIndex && IsValid(Region->Anchor))
				{
					Deposit = Candidate;
					Site = Region->Anchor;
					break;
				}
			if (Site)
				break;
		}
		if (!Site || !Deposit)
			return Fail(TEXT("No anchored region with a free deposit in map"));
		AArmyGroup* Occupiers = ArmyTestSetup::SpawnGroup(World, PC, 10, Site->GetActorLocation() + FVector(0.f, 0.f, 100.f));
		if (!Check(Occupiers != nullptr, TEXT("Real capture occupants spawn")))
			return true;
		Occupiers->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Occupiers->GetUnits())
			Unit->SetActorTickEnabled(false);
		Site->AdvanceCapture(20.f);
		if (!Check(Site->ControllingTeam == 0 && State->GetRegionController(Deposit->RegionIndex) == 0
					&& State->GetIncomePerSecond(Wallet) == 2 && State->GetIncomePerSecond(OtherWallet) == 2,
				TEXT("Anchor capture grants polygon rights but no shared income")))
			return true;
		for (AArmyUnit* Unit : Occupiers->GetUnits())
			Unit->SetActorLocation(FromFriendlyHQ(State, 0.f, 0.f, 100.f));
		Site->AdvanceCapture(20.f); // Capture rights persist; clear the deposit footprint before construction.
		if (!FindPlacement(State, BarracksIndex, Site->GetActorLocation(), Location))
			return Fail(TEXT("Captured region without an extractor must permit a barracks footprint"));
		const int32 RegionBuildBalance = Wallet->Resources;
		ACommandBuilding* RegionBarracks = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Location).Building;
		if (!Check(RegionBarracks && State->IsInBuildTerritory(BarracksIndex, 0, Location)
					&& Wallet->Resources == RegionBuildBalance - State->Content->Building(BarracksIndex)->BuildCost
					&& State->GetIncomePerSecond(Wallet) == 2,
				TEXT("Bare capture grants paid barracks placement but only baseline income")))
			return true;
		if (!Check(FCommandService::CancelBuilding(Wallet, RegionBarracks).IsAccepted() && Wallet->Resources == RegionBuildBalance,
				TEXT("Captured-region barracks cancellation refunds its unbuilt cost")))
			return true;
		if (!FindPlacement(State, ExtractorIndex, Deposit->GetActorLocation(), Location))
			return Fail(TEXT("No free controlled-region deposit placement"));
		const FVector Requested = Location + FVector(71.f, -63.f, 0.f);
		ACommandBuilding* Extractor = FCommandService::PlaceBuilding(Wallet, ExtractorIndex, Requested).Building;
		if (!Check(Extractor && Extractor->Kind == EBuildingKind::Extractor && IsValid(Extractor->Deposit)
					&& Extractor->Deposit->Extractor == Extractor
					&& Extractor->GetActorLocation().X == Extractor->Deposit->GetActorLocation().X
					&& Extractor->GetActorLocation().Y == Extractor->Deposit->GetActorLocation().Y,
				TEXT("Off-deposit request snaps exact XY and reserves a free deposit")))
			return true;
		Deposit = Extractor->Deposit;
		if (!Check(State->GetIncomePerSecond(Wallet) == 2,
				TEXT("Unfinished extractor reserves deposit without paying income")))
			return true;
		const int32 OccupiedBalance = Wallet->Resources;
		if (!Check(!FCommandService::PlaceBuilding(Wallet, ExtractorIndex, Deposit->GetActorLocation())
					&& Wallet->Resources == OccupiedBalance,
				TEXT("Occupied deposit rejects duplicate placement without debit")))
			return true;
		Extractor->Tick(60.f);
		for (AArmyUnit* Unit : Occupiers->GetUnits())
			Unit->SetActorLocation(FromFriendlyHQ(State, 0.f, 0.f, 100.f));
		Site->AdvanceCapture(20.f);
		if (!Check(Site->ControllingTeam == 0 && !Site->bFriendlyPresent
					&& State->GetRegionController(Deposit->RegionIndex) == 0,
				TEXT("Controlled polygon retains rights after real force departure")))
			return true;
		const int32 Rate = Deposit->RatePerSecond();
		if (!Check(Rate == (Deposit->bRich ? 6 : 4) && Deposit->Remaining == (Deposit->bRich ? 3000 : 2400),
				TEXT("Deposit kind selects exact finite total and rate")))
			return true;
		const int32 IncomeBefore = Wallet->Resources, OtherBefore = OtherWallet->Resources;
		const int32 RemainingBefore = Deposit->Remaining;
		State->bVerificationIncomePaused = false;
		State->Tick(2.f);
		State->bVerificationIncomePaused = true;
		if (!Check(State->GetIncomePerSecond(Wallet) == 2 + Rate && State->GetIncomePerSecond(OtherWallet) == 2
					&& Wallet->Resources == IncomeBefore + (2 + Rate) * 2 && OtherWallet->Resources == OtherBefore + 4
					&& Deposit->Remaining == RemainingBefore - Rate * 2,
				TEXT("Completed extractor pays only builder, baseline pays teammate, deposit drains exactly")))
			return true;
		AArmyGroup* Enemy = SpawnGroup(World, nullptr, -1, HostileStaging(State));
		if (!Enemy)
			return Fail(TEXT("Hostile destruction fixture failed"));
		Enemy->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Enemy->GetUnits())
			Unit->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Enemy->GetUnits())
			Unit->SetActorLocation(Site->GetActorLocation() + FVector(0.f, 0.f, 100.f));
		Site->AdvanceCapture(40.f);
		if (!Check(Extractor->IsAlive() && State->GetRegionController(Deposit->RegionIndex) == 5
					&& !State->IsInBuildTerritory(BarracksIndex, 0, Site->GetActorLocation()),
				TEXT("Living extractor cannot lock anchor capture or preserve former owner's polygon rights")))
			return true;
		for (AArmyUnit* Unit : Enemy->GetUnits())
			Unit->SetActorLocation(HostileStaging(State));
		for (AArmyUnit* Unit : Occupiers->GetUnits())
			Unit->SetActorLocation(Site->GetActorLocation() + FVector(0.f, 0.f, 100.f));
		Site->AdvanceCapture(40.f);
		for (AArmyUnit* Unit : Occupiers->GetUnits())
			Unit->SetActorLocation(FromFriendlyHQ(State, 0.f, 0.f, 100.f));
		Site->AdvanceCapture(20.f);
		Deposit->Remaining = 3; // Isolate final partial payment, not claimed natural depletion duration.
		const int32 FinalBefore = Wallet->Resources, FinalOther = OtherWallet->Resources;
		State->bVerificationIncomePaused = false;
		State->Tick(2.f);
		State->Tick(2.f);
		State->bVerificationIncomePaused = true;
		if (!Check(Deposit->Remaining == 0 && State->GetIncomePerSecond(Wallet) == 2
					&& Wallet->Resources == FinalBefore + 8 + 3 && OtherWallet->Resources == FinalOther + 8,
				TEXT("Final partial payment cannot overdraw deposit; next tick pays baseline only")))
			return true;
		Extractor->ReceiveAttack(Extractor->Health, Enemy->GetUnits()[0]);
		if (!Check(!IsValid(Deposit->Extractor) && Building.IsValid() && Building->OwningPlayerState == Wallet
					&& Site->ControllingTeam == 0 && State->GetIncomePerSecond(Wallet) == 2
					&& State->IsInBuildTerritory(BarracksIndex, 0, Site->GetActorLocation()),
				TEXT("Destroyed extractor frees deposit without changing capture rights or ownership")))
			return true;
		const AMapRegion* Region = State->FindRegionAt(Deposit->GetActorLocation());
		FVector ContestLocation = Deposit->GetActorLocation();
		for (const FVector2D& Vertex : Region->Polygon)
		{
			const FVector Candidate = Deposit->GetActorLocation() * .2f + FVector(Vertex.X, Vertex.Y, 100.f) * .8f;
			if (Region->Contains(Candidate) && FVector::Dist2D(Candidate, Site->GetActorLocation()) > ACapturePoint::CaptureRadius + 100.f)
			{
				ContestLocation = Candidate;
				break;
			}
		}
		if (!Check(FVector::Dist2D(ContestLocation, Site->GetActorLocation()) > ACapturePoint::CaptureRadius,
				TEXT("Contest fixture stands inside polygon but outside capture circle")))
			return true;
		Enemy->GetUnits()[0]->SetActorLocation(ContestLocation);
		if (!Check(State->IsRegionContested(Deposit->RegionIndex, 0)
					&& !State->IsInBuildTerritory(ExtractorIndex, 0, Deposit->GetActorLocation()),
				TEXT("Enemy anywhere in region denies construction even outside anchor radius")))
			return true;
		Test->AddInfo(TEXT("Construction proof: paid snapped placement, owner isolation, research, cancellation, polygon capture/contest, builder-only finite extractor payment, depletion and freeing."));
		return true;
	}
	FAutomationTestBase* Test;
	bool bProduction;
	int32 Stage = 0;
	float LastProgressReport = 0.f;
	TWeakObjectPtr<ACommandBuilding> Building;
	TWeakObjectPtr<AArmyGroup> Squad;
	TArray<TWeakObjectPtr<ACommandBuilding>> Producers;
	TWeakObjectPtr<ACommandBuilding> NewProducer;
	TArray<TWeakObjectPtr<AArmyGroup>> Forces;
	TWeakObjectPtr<AArmyGroup> Attacker;
	TWeakObjectPtr<AArmyUnit> Recruit;
	TWeakObjectPtr<AActor> Blocker;
	TWeakObjectPtr<ACommandPlayerState> UnrelatedWallet;
	FVector RecruitStart, JoinedStart, OtherFront, RememberedFront;
	EFrontOrder OtherOrder = EFrontOrder::Defend;
	int32 FillBalance = 0, ReplacementBalance = 0, SurvivorCount = 0;
	bool bRecruitMoved = false, bJoinedMoved = false;
};
}
bool FConstructionLifecycleTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ConstructionScenarioTests::FConstructionScenario(this, false));
	return true;
}
bool FConstructionProductionTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ConstructionScenarioTests::FConstructionScenario(this, true));
	return true;
}
#endif
