#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "Content/BuildingDefinition.h"
#include "ForceGoals.h"
#include "MapRegion.h"
#include "NavigationSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructionGoalOrdersTest, "CoopRTS.Construction.GoalOrders",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace GoalOrderScenarioTests
{
using namespace ArmyTestSetup;

// Independent reference traversal of the actual map actors, not the driver's bit graph.
AMapRegion* Region(const ACommandGameState* State, int32 Index)
{
	for (AMapRegion* Candidate : State->Regions)
		if (IsValid(Candidate) && Candidate->RegionIndex == Index)
			return Candidate;
	return nullptr;
}
void FindPaths(const ACommandGameState* State, int32 Start, int32* Distance, int32* First)
{
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
		Distance[Index] = First[Index] = INDEX_NONE;
	int32 Queue[ForceGoals::MaxRegions], Read = 0, Write = 0;
	Distance[Start] = 0;
	First[Start] = Start;
	Queue[Write++] = Start;
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		const AMapRegion* CurrentRegion = Region(State, Current);
		for (int32 Next = 0; Next < ForceGoals::MaxRegions; ++Next)
		{
			if (!Region(State, Next) || !CurrentRegion->Neighbours.Contains(Next) || Distance[Next] != INDEX_NONE)
				continue;
			Distance[Next] = Distance[Current] + 1;
			First[Next] = Current == Start ? Next : First[Current];
			Queue[Write++] = Next;
		}
	}
}

class FGoalOrdersScenario : public IAutomationLatentCommand
{
public:
	explicit FGoalOrdersScenario(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		UWorld* World = ArmyTestSetup::World();
		if (!World || World->GetTimeSeconds() < 3.f)
			return false;
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!PC || !State || !Wallet || Wallet->CommanderIndex < 0 || !MapReady(State)
			|| !State->Content || !IsValid(State->EnemyCommander) || State->Regions.IsEmpty())
			return false;
		if (Stage == 0)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->TeamIndex == 5)
					It->Destroy();
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
			{
				if (It->GetTeamIndex() == 0)
					return Fail(TEXT("Fresh goal scenario must begin without friendly armies"));
				It->Destroy();
			}
			State->bVerificationIncomePaused = true;
			Wallet->Resources = 4000; // Budget/isolation only; buildings, recruits and captures use real authority paths.
			ForeignWallet = World->SpawnActor<ACommandPlayerState>();
			if (!ForeignWallet.IsValid())
				return Fail(TEXT("Foreign goal-owner wallet fixture could not spawn"));
			ForeignWallet->CommanderIndex = Wallet->CommanderIndex == 0 ? 1 : 0;
			ForeignWallet->Resources = 4000;
			State->AddPlayerState(ForeignWallet.Get());
			Building = Place(State, Wallet);
			Foreign = Place(State, ForeignWallet.Get());
			if (!Building.IsValid() || !Foreign.IsValid())
				return true;
			const AMapRegion* Own = State->FindRegionAt(Building->GetActorLocation());
			if (!Check(Own && Own->RegionIndex >= 0 && Own->RegionIndex < ForceGoals::MaxRegions,
					TEXT("Real placed barracks stands in a generated region")))
				return true;
			Home = Own->RegionIndex;
			if (!Check(Building->ForceGoal == EForceGoal::Hold && Building->GoalRegionIndex == Home,
					TEXT("New producer defaults to Hold its own region before force configuration")))
				return true;
			for (AMapRegion* Candidate : State->Regions)
			{
				if (!Check(IsValid(Candidate) && Candidate->RegionIndex >= 0 && Candidate->RegionIndex < ForceGoals::MaxRegions,
						TEXT("Generated region indices fit the supported goal graph")))
					return true;
				if (Candidate->HomeTeam == 5)
					EnemyMain = Candidate->RegionIndex;
			}
			int32 Distance[ForceGoals::MaxRegions], First[ForceGoals::MaxRegions];
			FindPaths(State, Home, Distance, First);
			for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
			{
				const AMapRegion* Candidate = Region(State, Index);
				const AMapRegion* Via = Candidate ? Region(State, First[Index]) : nullptr;
				if (Candidate && Via && Distance[Index] == 2 && Candidate->HomeTeam < 0 && Via->HomeTeam < 0
					&& IsValid(Candidate->Anchor) && IsValid(Via->Anchor)
					&& State->GetRegionController(Index) == -1 && State->GetRegionController(Via->RegionIndex) == -1)
				{
					Target = Index;
					Intermediate = Via->RegionIndex;
					break;
				}
			}
			if (!Check(Target != INDEX_NONE && EnemyMain != INDEX_NONE && Distance[EnemyMain] != INDEX_NONE,
					TEXT("Generated map provides a neutral two-step expansion and a reachable enemy main")))
				return true;
			// Casualty source stays inside the excluded enemy main, never contesting an expansion waypoint.
			const AMapRegion* EnemyHome = Region(State, EnemyMain);
			Attacker = SpawnGroup(World, nullptr, -1, State->GetRegionAnchor(EnemyMain) + FVector(0.f, 0.f, 100.f));
			if (!Attacker.IsValid())
				return Fail(TEXT("Lethal-damage fixture could not spawn"));
			FCommandService::IssueOrder(State->EnemyCommander, Attacker.Get(), EArmyOrder::Hold, Attacker->GetCenter());
			Attacker->SetActorTickEnabled(false);
			for (AArmyUnit* Unit : Attacker->GetUnits())
			{
				if (!Check(IsValid(Unit) && EnemyHome->Contains(Unit->GetActorLocation()),
						TEXT("Every casualty fixture unit stays inside the enemy main, outside the expansion path")))
					return true;
				Unit->SetActorTickEnabled(false);
			}
			Stage = 1;
			return false; // Wait for the real dynamic navmesh to incorporate paid building blockers.
		}
		if (!Building.IsValid() || !Foreign.IsValid() || !Attacker.IsValid())
			return Fail(TEXT("Isolated goal fixture disappeared"));
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav || Nav->IsNavigationBuildInProgress())
			return false;
		if (Stage == 1)
		{
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, true);
			if (!Check(Building->bForceConfigured && IsValid(Building->ForceGroup)
						&& Building->GetProductionDefinition()->Capacity == 6,
					TEXT("Owning Start configures a real six-slot frontline force")))
				return true;
			if (!Check(FCommandService::ConfigureProduction(ForeignWallet.Get(), Foreign.Get(), EUnitRole::Frontline, true).IsAccepted(),
					TEXT("Foreign producer configures its independent force")))
				return true;
			FCommandService::ConfigureProduction(ForeignWallet.Get(), Foreign.Get(), State->Content->Unit(Foreign->ProductionUnitIndex)->Role, false);
			Building->TickGoal();
			Foreign->TickGoal();
			if (!Check(AtFront(State, Building.Get(), Home, EFrontOrder::Defend),
					TEXT("Default Hold drives Defend at the barracks region anchor")))
				return true;
			if (!RejectsAtomically(PC, Building.Get(), EForceGoal::Hold, INDEX_NONE)
				|| !RejectsAtomically(PC, Building.Get(), EForceGoal::Expand, ForceGoals::MaxRegions + 7)
				|| !RejectsAtomically(PC, Building.Get(), static_cast<EForceGoal>(255), Home)
				|| !RejectsAtomically(PC, Building.Get(), EForceGoal::Hold, EnemyMain)
				|| !RejectsAtomically(PC, Building.Get(), EForceGoal::Expand, EnemyMain)
				|| !RejectsAtomically(PC, Building.Get(), EForceGoal::Assault, Home)
				|| !RejectsAtomically(PC, Building.Get(), EForceGoal::FallBack, Home)
				|| !RejectsAtomically(PC, Foreign.Get(), EForceGoal::Expand, Target))
				return true;
			FillBalance = Wallet->Resources;
			Stage = 2;
			return false;
		}
		int32 Joined, Travelling;
		Building->GetForceCounts(Joined, Travelling);
		if (Stage == 2)
		{
			if (Joined != 6 || Travelling != 0)
				return false;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, false);
			if (!Check(Wallet->Resources == FillBalance - 6 * Building->GetProductionCost(),
					TEXT("Six naturally deployed and joined members each pay the real unit price")))
				return true;
			FCommandService::AssignGoal(Wallet, Building.Get(), EForceGoal::Expand, Target);
			Building->TickGoal();
			if (!Check(Building->ForceGoal == EForceGoal::Expand && Building->GoalRegionIndex == Target
						&& AtFront(State, Building.Get(), Intermediate, EFrontOrder::Secure),
					TEXT("Two-step Expand secures the intermediate waypoint, not the final anchor")))
				return true;
			Stage = 3;
			Test->AddInfo(TEXT("GoalOrders: paid force full; walking to intermediate expansion region."));
			return false;
		}
		if (Stage == 3)
		{
			bVisitedIntermediate |= Occupies(State, Intermediate);
			if (State->GetRegionController(Intermediate) != 0)
				return false;
			Building->TickGoal();
			if (Building->GetGoalWaypointRegionIndex() != Target)
				return false;
			if (!Check(bVisitedIntermediate && State->GetRegionController(Target) == -1
						&& Building->ForceGoal == EForceGoal::Expand && Building->GoalRegionIndex == Target
						&& AtFront(State, Building.Get(), Target, EFrontOrder::Secure),
					TEXT("Physical intermediate visit and real capture advance Expand to the still-neutral target")))
				return true;
			Stage = 4;
			Test->AddInfo(TEXT("GoalOrders: intermediate captured; walking to final expansion region."));
			return false;
		}
		if (Stage == 4)
		{
			bVisitedTarget |= Occupies(State, Target);
			if (State->GetRegionController(Target) != 0)
				return false;
			Building->TickGoal();
			const AMapRegion* Current = State->FindRegionAt(Building->ForceGroup->GetCenter());
			if (!Current || Current->RegionIndex != Target)
				return false;
			if (!Check(bVisitedTarget && State->GetRegionController(Intermediate) == 0
						&& Building->ForceGoal == EForceGoal::Hold && Building->GoalRegionIndex == Target
						&& AtFront(State, Building.Get(), Target, EFrontOrder::Defend),
					TEXT("Expand physically captures both path regions and finishes as Hold at its target")))
				return true;
			Stage = 5;
			return false;
		}
		if (Stage == 5)
		{
			const AMapRegion* Current = State->FindRegionAt(Building->ForceGroup->GetCenter());
			if (!Current || Current->RegionIndex != Target)
				return false;
			int32 Distance[ForceGoals::MaxRegions], First[ForceGoals::MaxRegions];
			FindPaths(State, Target, Distance, First);
			AssaultWaypoint = First[EnemyMain];
			if (!Check(AssaultWaypoint != INDEX_NONE && AssaultWaypoint != Target,
					TEXT("Captured region has a nontrivial shortest path to the enemy main")))
				return true;
			FCommandService::AssignGoal(Wallet, Building.Get(), EForceGoal::Assault, INDEX_NONE);
			Building->TickGoal();
			if (!Check(Building->ForceGoal == EForceGoal::Assault && Building->GoalRegionIndex == EnemyMain
						&& AtFront(State, Building.Get(), AssaultWaypoint, EFrontOrder::Secure),
					TEXT("Assault resolves the enemy main server-side and secures its shortest-path next region")))
				return true;
			if (!KillTo(3))
				return true;
			Building->TickGoal();
			if (!Check(!Building->IsGoalRefilling() && AtFront(State, Building.Get(), AssaultWaypoint, EFrontOrder::Secure),
					TEXT("Three of six members (50 percent) continue Assault")))
				return true;
			if (!KillTo(2))
				return true;
			Building->TickGoal();
			if (!Check(Building->IsGoalRefilling() && Building->ForceGoal == EForceGoal::Assault
						&& Building->GoalRegionIndex == EnemyMain && AtFront(State, Building.Get(), Target, EFrontOrder::Defend),
					TEXT("Two of six members (below 40 percent) Hold the last controlled path region without losing Assault")))
				return true;
			RefillBalance = Wallet->Resources;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, true);
			Stage = 6;
			Test->AddInfo(TEXT("GoalOrders: casualties triggered last-controlled-region Hold; awaiting paid refill."));
			return false;
		}
		if (Stage == 6)
		{
			if (Joined + Travelling < 5)
				return false;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, false);
			Building->TickGoal();
			if (!Check(Joined + Travelling == 5 && Building->IsGoalRefilling()
						&& AtFront(State, Building.Get(), Target, EFrontOrder::Defend)
						&& Wallet->Resources == RefillBalance - 3 * Building->GetProductionCost(),
					TEXT("Five of six paid living members still Hold; recovering above 40 percent is not a full refill")))
				return true;
			FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, true);
			Stage = 7;
			return false;
		}
		if (Joined + Travelling != 6)
			return false;
		FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, false);
		Building->TickGoal();
		if (!Check(!Building->IsGoalRefilling() && Building->ForceGoal == EForceGoal::Assault
					&& Building->GoalRegionIndex == EnemyMain && AtFront(State, Building.Get(), AssaultWaypoint, EFrontOrder::Secure)
					&& Wallet->Resources == RefillBalance - 4 * Building->GetProductionCost(),
				TEXT("Full paid refill resumes the retained Assault along its next shortest-path waypoint")))
			return true;
		FCommandService::AssignGoal(Wallet, Building.Get(), EForceGoal::FallBack, INDEX_NONE);
		Building->TickGoal();
		if (!Check(Building->ForceGoal == EForceGoal::FallBack && Building->GoalRegionIndex == Home
					&& Building->FrontOrder == EFrontOrder::FallBack && Building->ForceGroup->FrontOrder == EFrontOrder::FallBack,
				TEXT("Fall Back retains existing producer-scoped regroup semantics")))
			return true;
		Test->AddInfo(TEXT("GoalOrders proof: default own-region Hold, paid two-step physical capture, enemy-main shortest-path Assault, below-40-percent Hold/full-refill resume, atomic invalid/enemy-main/foreign-building rejection and Fall Back."));
		return true;
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
	ACommandBuilding* Place(ACommandGameState* State, ACommandPlayerState* Wallet)
	{
		const FVector Center = State->FriendlyHeadquarters->GetActorLocation();
		for (int32 Ring = 0; Ring < 5; ++Ring)
			for (int32 Direction = 0; Direction < 16; ++Direction)
			{
				const float Angle = Direction * PI / 8.f;
				FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 110.f);
				Point.Z = 5.f;
				FString Reason;
				if (!State->ValidateBuildingPlacement(BarracksIndex, 0, Point, Reason))
					continue;
				const int32 Before = Wallet->Resources;
				ACommandBuilding* Producer = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Point).Building;
				const UBuildingDefinition* Definition = State->Content->Building(BarracksIndex);
				if (!Check(IsValid(Producer) && Definition && Wallet->Resources == Before - Definition->BuildCost,
						TEXT("Goal scenario barracks placement pays its real building price")))
					return nullptr;
				Producer->Tick(60.f);
				if (!Check(Producer->IsComplete() && Producer->IsAlive(), TEXT("Paid goal producer completes before configuration")))
					return nullptr;
				return Producer;
			}
		Fail(TEXT("No valid barracks footprint in real friendly construction territory"));
		return nullptr;
	}
	bool AtFront(const ACommandGameState* State, const ACommandBuilding* Producer, int32 Index, EFrontOrder Order) const
	{
		const FVector Anchor = State->GetRegionAnchor(Index);
		return Producer->GetGoalWaypointRegionIndex() == Index && Producer->FrontOrder == Order
			&& IsValid(Producer->ForceGroup) && Producer->ForceGroup->FrontOrder == Order
			&& FVector::Dist2D(Producer->FrontLocation, Anchor) <= 75.f
			&& Producer->ForceGroup->FrontLocation == Producer->FrontLocation;
	}
	bool Occupies(const ACommandGameState* State, int32 Index) const
	{
		const AMapRegion* At = Region(State, Index);
		for (const AArmyUnit* Unit : Building->ForceGroup->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing() && At->Contains(Unit->GetActorLocation())
				&& FVector::Dist2D(Unit->GetActorLocation(), State->GetRegionAnchor(Index)) <= ACapturePoint::CaptureRadius)
				return true;
		return false;
	}
	bool RejectsAtomically(ACommandPlayerController* PC, ACommandBuilding* Producer, EForceGoal Goal, int32 Index)
	{
		const EForceGoal BeforeGoal = Producer->ForceGoal;
		const int32 BeforeTarget = Producer->GoalRegionIndex, BeforeWaypoint = Producer->GetGoalWaypointRegionIndex();
		const EFrontOrder BeforeOrder = Producer->FrontOrder;
		const FVector BeforeFront = Producer->FrontLocation;
		AArmyGroup* BeforeForce = Producer->ForceGroup;
		const uint32 BeforeSerial = BeforeForce->OrderSerial;
		const bool bBeforeRefill = Producer->IsGoalRefilling();
		const int32 OwnerBalance = Producer->OwningPlayerState->Resources;
		const int32 CallerBalance = PC->GetPlayerState<ACommandPlayerState>()->Resources;
		const float BeforeProgress = Producer->ProductionProgressSeconds;
		const bool bBeforeProduction = Producer->bProductionEnabled;
		FCommandService::AssignGoal(PC->GetPlayerState<ACommandPlayerState>(), Producer, Goal, Index);
		return Check(Producer->ForceGoal == BeforeGoal && Producer->GoalRegionIndex == BeforeTarget
				&& Producer->GetGoalWaypointRegionIndex() == BeforeWaypoint && Producer->IsGoalRefilling() == bBeforeRefill
				&& Producer->FrontOrder == BeforeOrder && Producer->FrontLocation == BeforeFront
				&& Producer->ForceGroup == BeforeForce && BeforeForce->FrontOrder == BeforeOrder
				&& BeforeForce->FrontLocation == BeforeFront && BeforeForce->OrderSerial == BeforeSerial
				&& Producer->OwningPlayerState->Resources == OwnerBalance
				&& PC->GetPlayerState<ACommandPlayerState>()->Resources == CallerBalance
				&& Producer->ProductionProgressSeconds == BeforeProgress && Producer->bProductionEnabled == bBeforeProduction,
			TEXT("Invalid region/goal, enemy-main or foreign-building RPC rejects without mutating force, goal, production or wallets"));
	}
	bool KillTo(int32 Remaining)
	{
		int32 Joined, Travelling;
		Building->GetForceCounts(Joined, Travelling);
		while (Joined + Travelling > Remaining)
		{
			AArmyUnit* Victim = nullptr;
			for (AArmyUnit* Unit : Building->ForceGroup->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive())
				{
					Victim = Unit;
					break;
				}
			if (!Check(Victim != nullptr, TEXT("Real casualty fixture finds a living produced member")))
				return false;
			Victim->ReceiveAttack(Victim->GetHealth(), Attacker->GetUnits()[0]);
			if (!Check(!Victim->IsAlive(), TEXT("Real lethal damage removes a produced member")))
				return false;
			Building->GetForceCounts(Joined, Travelling);
		}
		return Check(Joined + Travelling == Remaining, TEXT("Casualties leave the intended living force strength"));
	}
	FAutomationTestBase* Test;
	int32 Stage = 0, Home = INDEX_NONE, Target = INDEX_NONE, Intermediate = INDEX_NONE;
	int32 EnemyMain = INDEX_NONE, AssaultWaypoint = INDEX_NONE, FillBalance = 0, RefillBalance = 0;
	bool bVisitedIntermediate = false, bVisitedTarget = false;
	TWeakObjectPtr<ACommandPlayerState> ForeignWallet;
	TWeakObjectPtr<ACommandBuilding> Building, Foreign;
	TWeakObjectPtr<AArmyGroup> Attacker;
};
}

bool FConstructionGoalOrdersTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(GoalOrderScenarioTests::FGoalOrdersScenario(this));
	return true;
}
#endif
