#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "AIController.h"
#include "CapturePoint.h"
#include "Content/BuildingDefinition.h"
#include "ForceOrders.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

namespace VerbOrderTests
{
using namespace ArmyTestSetup;

enum class EScenario : uint8
{
	MoveHold,
	Withdrawal,
	StructureDeath,
	Retreat,
	StructureWithdrawal,
	Orphan,
	Queue,
	MixedSpeed,
	Rally,
	Never,
	NoSafeRegion
};

AMapRegion* Region(const ACommandGameState* State, int32 Index)
{
	for (AMapRegion* Candidate : State->Regions)
		if (IsValid(Candidate) && Candidate->RegionIndex == Index)
			return Candidate;
	return nullptr;
}

// Independent breadth-first traversal of the generated map, not executor rules.
void FindPaths(const ACommandGameState* State, int32 Start, TMap<int32, int32>& Distance, TMap<int32, int32>& First)
{
	TArray<int32> Pending{ Start };
	Distance.Add(Start, 0);
	First.Add(Start, Start);
	for (int32 Cursor = 0; Cursor < Pending.Num(); ++Cursor)
	{
		const int32 Current = Pending[Cursor];
		AMapRegion* At = Region(State, Current);
		if (!At)
			continue;
		TArray<int32> Neighbours = At->Neighbours;
		Neighbours.Sort();
		for (int32 Next : Neighbours)
			if (Region(State, Next) && !Distance.Contains(Next))
			{
				Distance.Add(Next, Distance[Current] + 1);
				First.Add(Next, Current == Start ? Next : First[Current]);
				Pending.Add(Next);
			}
	}
}

// Each registered test runs alone in a fresh standalone world. Only the refill,
// orphan and rally scenarios use paid production; other forces are explicit
// encounter fixtures. JEV, automatic income and unrelated producers are isolated.
class FScenario : public IAutomationLatentCommand
{
public:
	FScenario(FAutomationTestBase* InTest, EScenario InScenario)
		: Test(InTest), Scenario(InScenario), Started(FPlatformTime::Seconds()), StageStarted(Started) {}

	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (bFailed)
			return true;
		if (Now - Started > 180. || Now - StageStarted > 60.)
		{
			Test->AddError(FString::Printf(TEXT("Verb scenario %d timed out at stage %d (alive=%d joined=%d verb=%d status=%d target=%d waypoint=%d)"),
				static_cast<int32>(Scenario), Stage, Force.IsValid() ? Force->GetAliveCount() : -1,
				Force.IsValid() ? Force->GetJoinedCount() : -1, Force.IsValid() ? static_cast<int32>(Force->Verb) : -1,
				Force.IsValid() ? static_cast<int32>(Force->Status) : -1,
				Force.IsValid() ? Force->TargetRegionIndex : -1, Force.IsValid() ? Force->WaypointRegionIndex : -1));
			return true;
		}
		if (!bPrepared)
			return Prepare();
		if (!Check(Hostile.IsValid() && EnemyWallet.IsValid(), TEXT("Isolated hostile fixtures remain alive")))
			return true;
		if (bPaid && !bFilled)
		{
			if (!Check(Producer.IsValid(), TEXT("Paid producer survives initial recruitment")))
				return true;
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GameWorld);
			if (!Nav || Nav->IsNavigationBuildInProgress())
				return false;
			if (!bConfigured)
			{
				if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(),
						TEXT("Paid producer configures frontline force through owner command")))
					return true;
				Force = Producer->ForceGroup;
				if (!Check(Force.IsValid() && Force->GetCapacity() == 6, TEXT("Real frontline definition supplies six slots")))
					return true;
				RecruitBalance = Wallet->Resources;
				bConfigured = true;
			}
			if (!Check(Force.IsValid(), TEXT("Paid force survives initial recruitment")))
				return true;
			if (Scenario == EScenario::Rally)
			{
				if (!Check(Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == Target,
						TEXT("New force inherits producer rally region as MoveHold target")))
					return true;
				bVisitedIntermediate |= Occupies(Intermediate);
			}
			if (Force->GetJoinedCount() != 6)
				return false;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false).IsAccepted()
						&& Wallet->Resources == RecruitBalance - 6 * Producer->GetProductionCost(),
					TEXT("Six naturally joined recruits pay exactly six real unit prices")))
				return true;
			bFilled = true;
			SetStage(0);
		}
		if (!Check(Force.IsValid(), TEXT("Ordered force survives the scenario")))
			return true;
		switch (Scenario)
		{
		case EScenario::MoveHold:
			return MoveHold();
		case EScenario::Withdrawal:
			return Withdrawal();
		case EScenario::StructureDeath:
			return StructureDeath();
		case EScenario::StructureWithdrawal:
			return StructureWithdrawal();
		case EScenario::Retreat:
			return Retreat();
		case EScenario::Orphan:
			return Orphan();
		case EScenario::Queue:
			return Queue();
		case EScenario::MixedSpeed:
			return MixedSpeed();
		case EScenario::Rally:
			return Rally();
		case EScenario::Never:
			return Never();
		case EScenario::NoSafeRegion:
			return NoSafeRegion();
		}
		return Fail(TEXT("Unknown verb scenario"));
	}

private:
	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
		{
			Test->AddError(Message);
			bFailed = true;
		}
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Check(false, Message);
		return true;
	}
	void SetStage(int32 Next)
	{
		Stage = Next;
		StageStarted = FPlatformTime::Seconds();
	}
	bool Issue(EForceVerb Verb, int32 Index = INDEX_NONE, AActor* TargetActor = nullptr, bool bQueue = false)
	{
		return Check(FCommandService::IssueForceOrder(Wallet, Force.Get(), Verb, Index, TargetActor, bQueue).IsAccepted(),
			TEXT("Owned force accepts the requested verb order"));
	}
	bool At(AArmyGroup* Group, int32 Index) const
	{
		const AMapRegion* AtRegion = Region(State, Index);
		if (!AtRegion || Group->GetJoinedCount() == 0 || !AtRegion->Contains(Group->GetCenter()))
			return false;
		for (const AArmyUnit* Unit : Group->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing() && !AtRegion->Contains(Unit->GetActorLocation()))
				return false;
		return true;
	}
	bool Occupies(int32 Index) const
	{
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing()
				&& Region(State, Index)->Contains(Unit->GetActorLocation())
				&& FVector::Dist2D(Unit->GetActorLocation(), State->GetRegionAnchor(Index)) <= ACapturePoint::CaptureRadius)
				return true;
		return false;
	}
	bool Holding(int32 Index) const
	{
		return Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == Index
			&& Force->Status == EForceStatus::Holding && At(Force.Get(), Index)
			&& State->GetRegionController(Index) == 0;
	}
	uint32 Attacks() const
	{
		uint32 Count = 0;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit))
				Count += Unit->AttackCount;
		return Count;
	}
	void TickForce() { static_cast<AActor*>(Force.Get())->Tick(.25f); }
	void PutHostile(const FVector& Point)
	{
		AArmyUnit* Enemy = Hostile->GetUnits()[0];
		Enemy->SetActorLocation(Point, false, nullptr, ETeleportType::TeleportPhysics);
		Enemy->NextAttackTime = TNumericLimits<float>::Max();
	}
	void ParkHostile() { PutHostile(State->GetRegionAnchor(EnemyHome) + FVector(0.f, 0.f, 100.f)); }
	bool KillTo(int32 Count)
	{
		while (Force->GetAliveCount() > Count)
		{
			AArmyUnit* Victim = nullptr;
			for (AArmyUnit* Unit : Force->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive())
				{
					Victim = Unit;
					break;
				}
			if (!Check(Victim != nullptr, TEXT("Casualty fixture finds a live force member")))
				return false;
			Victim->ReceiveAttack(Victim->GetHealth(), Hostile->GetUnits()[0]);
			if (!Check(!Victim->IsAlive(), TEXT("Authoritative lethal damage removes the member")))
				return false;
		}
		return Check(Force->GetAliveCount() == Count, TEXT("Casualties leave the exact requested strength"));
	}
	ACommandBuilding* Place()
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
				ACommandBuilding* Result = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Point).Building;
				if (!Check(IsValid(Result) && Wallet->Resources == Before - State->Content->Building(BarracksIndex)->BuildCost,
						TEXT("Barracks placement pays the real definition price")))
					return nullptr;
				Result->Tick(60.f);
				if (!Check(Result->IsComplete() && Result->IsAlive(), TEXT("Paid barracks completes before recruitment")))
					return nullptr;
				return Result;
			}
		Fail(TEXT("Generated HQ territory offers no valid paid barracks footprint"));
		return nullptr;
	}
	AArmyGroup* EmptyGroup(ACommandPlayerState* Owner, int32 Team, const FVector& Position)
	{
		const FTransform Transform(Position);
		AArmyGroup* Group = GameWorld->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			Owner ? Owner->GetOwner() : nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Group)
		{
			Group->Initialize({ Team, Owner, -1, nullptr, Position });
			Group->FinishSpawning(Transform);
		}
		return Group;
	}
	bool Prepare()
	{
		GameWorld = ArmyTestSetup::World();
		if (!GameWorld || GameWorld->GetTimeSeconds() < 3.f)
			return false;
		PC = ArmyTestSetup::Controller(GameWorld);
		State = GameWorld->GetGameState<ACommandGameState>();
		Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!PC || !Wallet || Wallet->CommanderIndex < 0 || !MapReady(State) || !State->Content)
			return false;
		for (TActorIterator<AEnemyCommander> It(GameWorld); It; ++It)
			It->Destroy();
		for (TActorIterator<ACommandBuilding> It(GameWorld); It; ++It)
			if (It->TeamIndex == 5)
				It->Destroy();
		for (TActorIterator<AArmyGroup> It(GameWorld); It; ++It)
		{
			if (It->GetTeamIndex() == 0)
				return Fail(TEXT("Verb scenario requires a fresh world without friendly armies"));
			It->Destroy();
		}
		State->bVerificationIncomePaused = true;
		Wallet->Resources = 10000;
		for (const AMapRegion* Candidate : State->Regions)
		{
			if (Candidate->HomeTeam == 0)
				Home = Candidate->RegionIndex;
			if (Candidate->HomeTeam == 5)
				EnemyHome = Candidate->RegionIndex;
		}
		if (!Check(Home != INDEX_NONE && EnemyHome != INDEX_NONE, TEXT("Real map supplies both HQ regions")))
			return true;
		TMap<int32, int32> Distance, First;
		FindPaths(State, Home, Distance, First);
		TArray<int32> Indices;
		Distance.GetKeys(Indices);
		Indices.Sort();
		for (int32 Index : Indices)
		{
			AMapRegion* Candidate = Region(State, Index);
			AMapRegion* Via = Region(State, First[Index]);
			if (Distance[Index] == 2 && Candidate->HomeTeam < 0 && Via->HomeTeam < 0
				&& IsValid(Candidate->Anchor) && IsValid(Via->Anchor)
				&& State->GetRegionController(Index) == -1 && State->GetRegionController(Via->RegionIndex) == -1)
			{
				Target = Index;
				Intermediate = Via->RegionIndex;
				break;
			}
		}
		if (!Check(Target != INDEX_NONE, TEXT("Generated map supplies a neutral two-step route")))
			return true;
		EnemyWallet = State->EnemyCommander;
		if (!Check(EnemyWallet.IsValid(), TEXT("Authoritative enemy wallet survives planner isolation")))
			return true;
		Hostile = SpawnGroup(GameWorld, nullptr, -1, FromEnemyHQ(State, -800.f, 500.f, 100.f));
		if (!Check(Hostile.IsValid() && Hostile->GetAliveCount() == 6,
				TEXT("Stationary opposing encounter uses real registered combat members")))
			return true;
		Hostile->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Hostile->GetUnits())
		{
			Unit->SetActorTickEnabled(false);
			Unit->NextAttackTime = TNumericLimits<float>::Max();
		}
		ParkHostile();
		bPaid = Scenario == EScenario::Withdrawal || Scenario == EScenario::Retreat
			|| Scenario == EScenario::Orphan || Scenario == EScenario::Rally
			|| Scenario == EScenario::NoSafeRegion || Scenario == EScenario::StructureWithdrawal;
		if (bPaid)
		{
			Producer = Place();
			if (!Producer.IsValid())
				return true;
			if (!Check(Producer->RallyRegionIndex == Home, TEXT("New producer defaults rally to its own real region")))
				return true;
			if (Scenario == EScenario::Rally && !Check(FCommandService::SetRallyPoint(Wallet, Producer.Get(), Target).IsAccepted() && Producer->RallyRegionIndex == Target, TEXT("Owner sets a rally before force configuration")))
				return true;
		}
		else
		{
			Force = SpawnGroup(GameWorld, PC, 0, FromFriendlyHQ(State, 800.f, 500.f, 100.f));
			if (!Check(Force.IsValid() && Force->GetAliveCount() == 6 && Force->GetJoinedCount() == 6,
					TEXT("Explicit mixed-role encounter starts with six joined members")))
				return true;
		}
		bPrepared = true;
		return false;
	}

	bool MoveHold()
	{
		if (Stage == 0)
		{
			const uint32 Serial = Force->OrderSerial;
			const EForceVerb BeforeVerb = Force->Verb;
			const int32 BeforeTarget = Force->TargetRegionIndex;
			const auto Unchanged = [&] { return Force->OrderSerial == Serial && Force->Verb == BeforeVerb
											 && Force->TargetRegionIndex == BeforeTarget; };
			AArmyGroup* const MixedOwnership[] = { Force.Get(), Hostile.Get() };
			AArmyGroup* const Duplicate[] = { Force.Get(), Force.Get() };
			if (!Check(!FCommandService::IssueForceOrder(Wallet, MixedOwnership, EForceVerb::MoveHold, Target).IsAccepted()
						&& Unchanged(),
					TEXT("Mixed owned/foreign selection rejects atomically before touching owned travel"))
				|| !Check(!FCommandService::IssueForceOrder(Wallet, Duplicate, EForceVerb::MoveHold, Target).IsAccepted()
						&& Unchanged(),
					TEXT("Duplicate selected force rejects without replacing its order")))
				return true;
			const ERetreatThreshold Threshold = Force->RetreatThreshold;
			if (!Check(!FCommandService::SetRetreatThreshold(EnemyWallet.Get(), Force.Get(), ERetreatThreshold::Never).IsAccepted()
						&& Force->RetreatThreshold == Threshold,
					TEXT("Foreign threshold command preserves owned setting"))
				|| !Check(!FCommandService::SetRetreatThreshold(Wallet, Force.Get(), static_cast<ERetreatThreshold>(41)).IsAccepted()
						&& Force->RetreatThreshold == Threshold,
					TEXT("Invalid threshold rejects without changing setting")))
				return true;
			if (!Issue(EForceVerb::MoveHold, Target))
				return true;
			if (!Check(Force->WaypointRegionIndex == Intermediate, TEXT("Two-step MoveHold begins at the adjacent neutral waypoint")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(1);
		}
		bVisitedIntermediate |= Occupies(Intermediate);
		bVisitedTarget |= Occupies(Target);
		if (Stage == 1 && Holding(Target))
		{
			if (!Check(bVisitedIntermediate && bVisitedTarget && State->GetRegionController(Intermediate) == 0
						&& FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f,
					TEXT("MoveHold physically travels through and captures both real map regions")))
				return true;
			SetStage(2);
		}
		if (Stage == 2)
		{
			if (!Check(Holding(Target) && Force->Orders.Num() == 1, TEXT("Unqueued MoveHold persists at its captured region")))
				return true;
			if (FPlatformTime::Seconds() - StageStarted >= 1.)
				return true;
		}
		return false;
	}
	bool Queue()
	{
		if (Stage == 0)
		{
			if (!Issue(EForceVerb::MoveHold, Intermediate) || !Issue(EForceVerb::MoveHold, Home, nullptr, true)
				|| !Issue(EForceVerb::MoveHold, Target, nullptr, true))
				return true;
			const uint32 Serial = Force->OrderSerial;
			const FVector Destination = Force->Destination;
			if (!Check(!FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::Attack, EnemyHome, nullptr, true).IsAccepted()
						&& Force->Orders.Num() == 3 && Force->TargetRegionIndex == Intermediate && Force->OrderSerial == Serial
						&& Force->Destination == Destination && Force->Orders[0].RegionIndex == Intermediate
						&& Force->Orders[1].RegionIndex == Home && Force->Orders[2].RegionIndex == Target,
					TEXT("Queue holds three TOTAL orders and rejects a fourth without replacing or reordering them")))
				return true;
			SetStage(1);
		}
		if (Stage == 1)
		{
			bVisitedIntermediate |= Occupies(Intermediate);
			if (Force->TargetRegionIndex != Intermediate)
			{
				if (!Check(Force->TargetRegionIndex == Home && Force->Orders.Num() == 2
							&& Force->Orders[0].RegionIndex == Home && Force->Orders[1].RegionIndex == Target
							&& bVisitedIntermediate && State->GetRegionController(Intermediate) == 0,
						TEXT("Second order starts only after physical first-region capture")))
					return true;
				SetStage(2);
			}
		}
		if (Stage == 2)
		{
			bVisitedHome |= At(Force.Get(), Home);
			if (Force->TargetRegionIndex != Home)
			{
				if (!Check(Force->TargetRegionIndex == Target && Force->Orders.Num() == 1 && Force->Orders[0].RegionIndex == Target
							&& bVisitedHome,
						TEXT("Third order starts only after the return-home arrival")))
					return true;
				SetStage(3);
			}
		}
		if (Stage == 3 && Holding(Target))
		{
			if (!Check(Force->Orders.Num() == 1, TEXT("Third and last order settles as persistent MoveHold"))
				|| !Issue(EForceVerb::Attack, Home))
				return true;
			if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Home
						&& Force->Status == EForceStatus::Marching,
					TEXT("Attack on an already controlled clear region still begins physical travel")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(4);
		}
		if (Stage == 4 && Holding(Home))
			return Check(FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f
					&& Force->MarchSpeed == 0.f,
				TEXT("Controlled-region Attack completes only after arrival and releases its speed cap"));
		return false;
	}
	bool MixedSpeed()
	{
		if (Stage == 0)
		{
			Second = EmptyGroup(Wallet, 0, FromFriendlyHQ(State, 800.f, -500.f, 100.f));
			if (!Check(Second.IsValid(), TEXT("Second selected force spawns")))
				return true;
			int32 FastestIndex = INDEX_NONE;
			for (int32 Index = 0; Index < State->Content->Units.Num(); ++Index)
				if (const UArmyUnitDefinition* Definition = State->Content->Unit(Index);
					Definition && (FastestIndex == INDEX_NONE || Definition->MoveSpeed > State->Content->Unit(FastestIndex)->MoveSpeed))
					FastestIndex = Index;
			if (!Check(FastestIndex != INDEX_NONE, TEXT("Catalogue supplies a faster comparison unit")))
				return true;
			for (int32 Slot = 0; Slot < 6; ++Slot)
				if (!Check(Second->SpawnMember(FastestIndex,
							   Second->GetHomeLocation() + FVector(0.f, Slot * 70.f, 0.f), Slot)
							!= nullptr,
						TEXT("Homogeneous comparison force has real faster catalogue members")))
					return true;
			ExpectedSpeed = TNumericLimits<float>::Max();
			for (const AArmyGroup* Group : { Force.Get(), Second.Get() })
				for (const AArmyUnit* Unit : Group->GetUnits())
					ExpectedSpeed = FMath::Min(ExpectedSpeed, Unit->GetDefinition()->MoveSpeed);
			if (!Check(Force->GetBaseMarchSpeed() < Second->GetBaseMarchSpeed(), TEXT("Fixture combines genuinely different base force speeds")))
				return true;
			TArray<AArmyGroup*> Selection{ Force.Get(), Second.Get() };
			const FCommandResult Result = FCommandService::IssueForceOrder(Wallet, Selection, EForceVerb::MoveHold, Target);
			if (!Check(Result.IsAccepted(),
					*FString::Printf(TEXT("Bulk owner command orders both selected forces: %s"), *Result.Message)))
				return true;
			StartPosition = Force->GetCenter();
			SecondStart = Second->GetCenter();
			SetStage(1);
		}
		if (Stage == 2)
			return At(Second.Get(), Home) && Second->Status == EForceStatus::Holding
				&& Check(FVector::Dist2D(StartPosition, Second->GetCenter()) > 500.f,
					TEXT("The formerly capped fast force physically completes its next authored-speed march"));
		for (const AArmyGroup* Group : { Force.Get(), Second.Get() })
		{
			const bool bHolding = Group->Status == EForceStatus::Holding;
			const float Speed = bHolding ? Group->GetBaseMarchSpeed() : ExpectedSpeed;
			if (!Check(FMath::IsNearlyEqual(Group->MarchSpeed, bHolding ? 0.f : ExpectedSpeed)
						&& FMath::IsNearlyEqual(Group->GetMarchSpeed(), Speed),
					TEXT("Selection cap applies while marching and ends at completed MoveHold arrival")))
				return true;
			for (const AArmyUnit* Unit : Group->GetUnits())
				if (!Check(FMath::IsNearlyEqual(Unit->GetCharacterMovement()->MaxWalkSpeed, Speed),
						TEXT("Moving characters share cap; completed orders restore authored formation speed")))
					return true;
		}
		if (Holding(Target) && Second->Status == EForceStatus::Holding && At(Second.Get(), Target))
		{
			if (!Check(FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f
						&& FVector::Dist2D(SecondStart, Second->GetCenter()) > 500.f,
					TEXT("Both selected formations physically traverse the route at their common speed")))
				return true;
			Empty = EmptyGroup(Wallet, 0, FromFriendlyHQ(State, 900.f, -600.f, 100.f));
			AArmyGroup* const Selection[] = { Second.Get(), Empty.Get() };
			if (!Check(Empty.IsValid() && Empty->GetBaseMarchSpeed() == 0.f
						&& FCommandService::IssueForceOrder(Wallet, Selection, EForceVerb::MoveHold, Home).IsAccepted()
						&& FMath::IsNearlyEqual(Second->GetMarchSpeed(), Second->GetBaseMarchSpeed()),
					TEXT("A memberless orphan does not erase the authored cap of a real selected force")))
				return true;
			StartPosition = Second->GetCenter();
			SetStage(2);
		}
		return false;
	}
	bool StructureDeath()
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
	bool StructureWithdrawal()
	{
		if (Stage == 0)
		{
			if (!Issue(EForceVerb::MoveHold, Intermediate))
				return true;
			SetStage(1);
		}
		if (Stage == 1 && Holding(Intermediate))
		{
			const FTransform Transform(State->GetRegionAnchor(Target) + FVector(600.f, 0.f, 5.f));
			Structure = GameWorld->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Check(Structure.IsValid(), TEXT("Withdrawal target structure spawns")))
				return true;
			Structure->BuildingIndex = BarracksIndex;
			Structure->TeamIndex = 5;
			Structure->OwningPlayerState = EnemyWallet.Get();
			Structure->ConstructionProgress = 1.f;
			Structure->FinishSpawning(Transform);
			if (!Issue(EForceVerb::Attack, Target, Structure.Get()))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(2);
		}
		if (Stage == 2 && FVector::Dist2D(StartPosition, Force->GetCenter()) >= 700.f)
		{
			if (!KillTo(2))
				return true;
			TickForce();
			SafeRegion = Force->WaypointRegionIndex;
			if (!Check(Force->Status == EForceStatus::Withdrawing, TEXT("Structure attacker begins actual casualty withdrawal")))
				return true;
			Structure->ReceiveAttack(Structure->Health, Force->GetUnits()[0]);
			TickForce();
			if (!Check(Force->Verb == EForceVerb::Attack && Force->Status == EForceStatus::Withdrawing
						&& Force->WaypointRegionIndex == SafeRegion,
					TEXT("Destroyed structure does not abandon the withdrawal in unsafe ground")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(3);
		}
		if (Stage == 3 && Holding(SafeRegion))
			return Check(IsValid(Force->GetProductionBuilding()) && Force->GetJoinedCount() == 2
					&& FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f,
				TEXT("Completed target ends at safe arrival without waiting for paused refill"));
		return false;
	}
	bool Orphan()
	{
		if (Stage == 0)
		{
			const int32 Number = Force->ForceNumber;
			Producer->ReceiveAttack(Producer->Health, Hostile->GetUnits()[0]);
			if (!Check(Force.IsValid() && !Force->GetProductionBuilding() && Force->GetAliveCount() == 6
						&& Force->GetOwningPlayerState() == Wallet && Force->ForceNumber == Number,
					TEXT("Real producer death leaves six owned orphan survivors with their force identity")))
				return true;
			if (!Issue(EForceVerb::MoveHold, Target))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(1);
		}
		if (Stage == 1 && Holding(Target))
		{
			if (!Check(Force->GetAliveCount() == 6 && FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f,
					TEXT("Orphan accepts and physically executes its new order without producer replacement"))
				|| !Issue(EForceVerb::Attack, EnemyHome))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(2);
		}
		if (Stage == 2 && FVector::Dist2D(StartPosition, Force->GetCenter()) >= 700.f)
		{
			if (!KillTo(2))
				return true;
			TickForce();
			if (!Check(Force->Status == EForceStatus::Withdrawing,
					TEXT("Two-of-six orphan starts automatic Attack withdrawal")))
				return true;
			SafeRegion = Force->WaypointRegionIndex;
			StartPosition = Force->GetCenter();
			SetStage(3);
		}
		if (Stage == 3 && Holding(SafeRegion))
			return Check(!Force->GetProductionBuilding() && Force->GetJoinedCount() == 2
					&& FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f,
				TEXT("Orphan Attack withdrawal physically arrives and becomes MoveHold instead of impossible refill"));
		return false;
	}
	bool Rally()
	{
		bVisitedIntermediate |= Occupies(Intermediate);
		if (Stage == 0 && Holding(Target))
		{
			if (!Check(bVisitedIntermediate && Force->GetJoinedCount() == 6 && Force->GetAliveCount() == 6
						&& Producer->RallyRegionIndex == Target && State->GetRegionController(Intermediate) == 0,
					TEXT("Six paid recruits route to non-home rally, join there and secure the route")))
				return true;
			TArray<int32> Saved = MoveTemp(Region(State, Home)->Neighbours);
			const int32 Rally = Producer->RallyRegionIndex;
			const bool bRejected = !FCommandService::SetRallyPoint(Wallet, Producer.Get(), Target).IsAccepted();
			Region(State, Home)->Neighbours = MoveTemp(Saved);
			if (!Check(bRejected && Producer->RallyRegionIndex == Rally,
					TEXT("Unreachable but valid rally region rejects without mutating producer")))
				return true;
			if (!Check(FCommandService::SetRallyPoint(Wallet, Producer.Get(), Home).IsAccepted()
						&& Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == Home,
					TEXT("Rally change retargets an existing idle force")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(1);
		}
		if (Stage == 1 && Holding(Home))
			return Check(FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f,
				TEXT("Idle force physically follows its changed producer rally"));
		return false;
	}
	bool BeginCombatTrip()
	{
		if (Stage == 0)
		{
			if (!Issue(EForceVerb::MoveHold, Intermediate))
				return false;
			SetStage(1);
		}
		if (Stage == 1 && Holding(Intermediate))
		{
			if (!Issue(EForceVerb::Attack, Target))
				return false;
			StartPosition = Force->GetCenter();
			SetStage(2);
		}
		return Stage == 2 && FVector::Dist2D(StartPosition, Force->GetCenter()) >= 700.f;
	}
	bool Withdrawal()
	{
		if (Stage <= 2)
		{
			if (!BeginCombatTrip())
				return bFailed;
			if (!Check(Force->RetreatThreshold == ERetreatThreshold::Percent40, TEXT("Attack defaults to forty-percent retreat threshold")))
				return true;
			if (!KillTo(3))
				return true;
			Force->TickOrders();
			if (!Check(Force->Verb == EForceVerb::Attack && Force->Status != EForceStatus::Withdrawing
						&& Force->Status != EForceStatus::Refilling,
					TEXT("Fifty-percent Attack does not withdraw at forty-percent threshold")))
				return true;
			if (!KillTo(2))
				return true;
			Force->TickOrders();
			if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Target
						&& Force->Status == EForceStatus::Withdrawing && Force->ResumeCount == 5,
					TEXT("Two of six trigger withdrawal without discarding Attack, with ceil eighty-percent resume count")))
				return true;
			SafeRegion = Force->WaypointRegionIndex;
			if (!Check(SafeRegion == Intermediate, TEXT("Withdrawal prefers the last held connected hostile-free region")))
				return true;
			AArmyUnit* Shooter = Force->GetUnits()[0];
			PutHostile(Shooter->GetActorLocation() + FVector(60.f, 0.f, 0.f));
			for (AArmyUnit* Unit : Force->GetUnits())
				Unit->NextAttackTime = 0.f;
			const uint32 Before = Attacks();
			TickForce();
			if (!Check(Attacks() > Before && !Shooter->bPursuing,
					TEXT("Withdrawing Attack actually fires at an in-range hostile without pursuit")))
				return true;
			SafeRegion = Force->WaypointRegionIndex;
			ParkHostile();
			StartPosition = Force->GetCenter();
			SetStage(3);
		}
		if (Stage == 3)
		{
			if (Force->Status != EForceStatus::Refilling)
				return false;
			if (!Check(At(Force.Get(), SafeRegion) && FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f
						&& Force->GetJoinedCount() == 2,
					TEXT("Fighting withdrawal physically returns to safety before refill")))
				return true;
			RefillBalance = Wallet->Resources;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(), TEXT("Owner starts paid withdrawal refill")))
				return true;
			SetStage(4);
		}
		if (Stage == 4)
		{
			if (Force->GetAliveCount() < 5)
				return false;
			if (!Check(Force->GetAliveCount() == 5 && Force->GetJoinedCount() < 5
						&& Force->Status == EForceStatus::Refilling,
					TEXT("Eighty-percent ALIVE including travelling recruits does not resume Attack")))
				return true;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false).IsAccepted(), TEXT("Pause refill at exactly five paid living members")))
				return true;
			SetStage(5);
		}
		if (Stage == 5)
		{
			if (Force->GetJoinedCount() < 5)
				return false;
			Force->TickOrders();
			if (!Check(Force->GetJoinedCount() == 5 && Force->GetAliveCount() == 5 && Force->Verb == EForceVerb::Attack
						&& Force->TargetRegionIndex == Target && Force->Status == EForceStatus::Marching
						&& Wallet->Resources == RefillBalance - 3 * Producer->GetProductionCost(),
					TEXT("Five JOINED of six resume the retained Attack, charging only the three real replacements")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(6);
		}
		if (Stage == 6)
		{
			if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Target
						&& Force->Status == EForceStatus::Marching,
					TEXT("Resumed Attack retains its original target while marching")))
				return true;
			if (FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f)
				return Check(FVector::Dist2D(Force->GetCenter(), Force->Destination) < FVector::Dist2D(StartPosition, Force->Destination),
					TEXT("Eighty-percent joined force physically resumes its forward Attack route"));
		}
		return false;
	}
	bool Retreat()
	{
		if (Stage <= 2)
		{
			if (!BeginCombatTrip())
				return bFailed;
			if (!Holding(Target))
				return false;
			if (!KillTo(3))
				return true;
			AArmyUnit* Shooter = Force->GetUnits()[0];
			PutHostile(Force->GetCenter() + FVector(900.f, 0.f, 0.f));
			TickForce();
			bool bObservedPursuit = false;
			for (const AArmyUnit* Unit : Force->GetUnits())
				if (const AAIController* AI = Cast<AAIController>(Unit->GetController());
					Unit->bPursuing && AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle)
					bObservedPursuit = true;
			if (!Check(bObservedPursuit, TEXT("Retreat replaces a real active combat pursuit, not only an idle formation")))
				return true;
			PutHostile(Shooter->GetActorLocation() + FVector(60.f, 0.f, 0.f));
			if (!Issue(EForceVerb::Retreat))
				return true;
			if (!Check(Force->Verb == EForceVerb::Retreat && Force->Status == EForceStatus::Retreating
						&& FMath::IsNearlyEqual(Force->GetMarchSpeed(), Force->GetBaseMarchSpeed() * 1.25f),
					TEXT("Explicit Retreat sprints at exactly twenty-five percent above base speed")))
				return true;
			SafeRegion = Force->WaypointRegionIndex;
			RetreatAttackCount = Attacks();
			for (AArmyUnit* Unit : Force->GetUnits())
				Unit->NextAttackTime = 0.f;
			TickForce();
			if (!Check(Attacks() == RetreatAttackCount, TEXT("Retreat does not fire at a live in-range hostile")))
				return true;
			for (const AArmyUnit* Unit : Force->GetUnits())
				if (!Check(!Unit->Target && !Unit->bPursuing
							&& FMath::IsNearlyEqual(Unit->GetCharacterMovement()->MaxWalkSpeed, Force->GetBaseMarchSpeed() * 1.25f),
						TEXT("Every retreating member clears combat and receives sprint movement speed")))
					return true;
			ParkHostile();
			StartPosition = Force->GetCenter();
			SetStage(3);
		}
		if (Stage == 3)
		{
			if (!Check(Attacks() == RetreatAttackCount, TEXT("Retreat route remains weapon-silent")))
				return true;
			if (Force->Status != EForceStatus::Refilling)
				return false;
			if (!Check(At(Force.Get(), SafeRegion) && FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f,
					TEXT("Retreat physically reaches its safe region before refill")))
				return true;
			RefillBalance = Wallet->Resources;
			// Sprint is over. Refilling may fight even with production paused.
			AArmyUnit* Shooter = Force->GetUnits()[0];
			AArmyUnit* Victim = Hostile->GetUnits()[0];
			PutHostile(Shooter->GetActorLocation() + FVector(70.f, 0.f, 0.f));
			const int32 BeforeHealth = Victim->GetHealth();
			Shooter->NextAttackTime = 0.f;
			Shooter->FireAt(Victim);
			if (!Check(Victim->GetHealth() < BeforeHealth && Force->Status == EForceStatus::Refilling,
					TEXT("Retreat refilling permits actual in-range damage after weapon-silent sprint")))
				return true;
			ParkHostile();
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(), TEXT("Owner starts paid Retreat refill")))
				return true;
			SetStage(4);
		}
		if (Stage == 4)
		{
			if (Force->GetAliveCount() < 5)
				return false;
			if (!Check(Force->GetAliveCount() == 5
						&& FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false).IsAccepted(),
					TEXT("Pause Retreat refill at exactly five paid living members")))
				return true;
			SetStage(5);
		}
		if (Stage == 5)
		{
			if (Force->GetJoinedCount() < 5)
				return false;
			if (!Check(Force->GetJoinedCount() == 5 && Force->Verb == EForceVerb::Retreat && Force->Status == EForceStatus::Refilling,
					TEXT("Retreat requires full joined capacity rather than Attack's eighty-percent resume")))
				return true;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(),
					TEXT("Owner recruits the final Retreat refill member")))
				return true;
			SetStage(6);
		}
		if (Stage == 6)
		{
			if (Force->GetJoinedCount() < 6)
				return false;
			if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false).IsAccepted(), TEXT("Owner pauses completed Retreat refill")))
				return true;
			Force->TickOrders();
			if (!Check(Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == Producer->RallyRegionIndex
						&& Force->GetJoinedCount() == 6 && Wallet->Resources == RefillBalance - 3 * Producer->GetProductionCost(),
					TEXT("Full paid joined refill completes Retreat into idle MoveHold at the producer rally")))
				return true;
			StartPosition = Force->GetCenter();
			SetStage(7);
		}
		if (Stage == 7 && Holding(Producer->RallyRegionIndex))
			return Check(FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f,
				TEXT("Completed Retreat physically returns from safety to its distinct default rally"));
		return false;
	}
	bool Never()
	{
		if (Stage == 0)
		{
			if (!Check(FCommandService::SetRetreatThreshold(Wallet, Force.Get(), ERetreatThreshold::Never).IsAccepted()
						&& Force->RetreatThreshold == ERetreatThreshold::Never,
					TEXT("Owner selects Never retreat threshold")))
				return true;
			if (!Issue(EForceVerb::Attack, EnemyHome) || !KillTo(1))
				return true;
			Force->TickOrders();
			StartPosition = Force->GetCenter();
			SetStage(1);
		}
		if (!Check(Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == EnemyHome
					&& Force->Status == EForceStatus::Marching && Force->GetAliveCount() == 1,
				TEXT("Never permits a one-of-six Attack to continue instead of withdrawing")))
			return true;
		if (FVector::Dist2D(StartPosition, Force->GetCenter()) > 100.f)
			return Check(FVector::Dist2D(Force->GetCenter(), Force->Destination) < FVector::Dist2D(StartPosition, Force->Destination),
				TEXT("One-of-six Never force physically advances toward its Attack waypoint"));
		return false;
	}
	bool NoSafeRegion()
	{
		if (Stage == 0)
		{
			for (const AMapRegion* Candidate : State->Regions)
				if (!Check(State->GetRegionController(Candidate->RegionIndex) != 0 || Candidate->RegionIndex == Home,
						TEXT("No-safe fixture has only its HQ controlled")))
					return true;
			if (!KillTo(3))
				return true;
			PutHostile(State->GetRegionAnchor(Home) + FVector(0.f, 0.f, 100.f));
			if (!Issue(EForceVerb::Retreat))
				return true;
			if (!Check(Force->Verb == EForceVerb::Retreat && Force->WaypointRegionIndex == Home,
					TEXT("No-safe Retreat chooses the hostile HQ fallback")))
				return true;
			SetStage(1);
		}
		if (Force->Status != EForceStatus::Refilling)
			return false;
		AArmyUnit* Shooter = Force->GetUnits()[0];
		AArmyUnit* Victim = Hostile->GetUnits()[0];
		PutHostile(Shooter->GetActorLocation() + FVector(70.f, 0.f, 0.f));
		const int32 Before = Victim->GetHealth();
		Shooter->NextAttackTime = 0.f;
		TickForce();
		Shooter->Tick(.25f);
		return Check(Force->GetJoinedCount() == 3 && !Producer->bProductionEnabled
				&& Victim->GetHealth() < Before && Force->Verb == EForceVerb::Retreat,
			TEXT("Paused Retreat refill at unsafe HQ fallback actually acquires and fires on hostiles"));
	}

	FAutomationTestBase* Test;
	EScenario Scenario;
	double Started, StageStarted;
	int32 Stage = 0, Home = INDEX_NONE, EnemyHome = INDEX_NONE, Intermediate = INDEX_NONE, Target = INDEX_NONE;
	int32 EndRegion = INDEX_NONE, SafeRegion = INDEX_NONE, RecruitBalance = 0, RefillBalance = 0;
	uint32 RetreatAttackCount = 0;
	float ExpectedSpeed = 0.f;
	bool bFailed = false, bPrepared = false, bPaid = false, bConfigured = false, bFilled = false;
	bool bVisitedIntermediate = false, bVisitedTarget = false, bVisitedHome = false;
	FVector StartPosition, SecondStart;
	UWorld* GameWorld = nullptr;
	ACommandPlayerController* PC = nullptr;
	ACommandGameState* State = nullptr;
	ACommandPlayerState* Wallet = nullptr;
	TWeakObjectPtr<ACommandPlayerState> EnemyWallet;
	TWeakObjectPtr<ACommandBuilding> Producer, Structure;
	TWeakObjectPtr<AArmyGroup> Force, Hostile, Second, Empty;
};
}

#define VERB_WORLD_TEST(ClassName, TestName, ScenarioName)                                                       \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "CoopRTS.Forces.Verbs." TestName,                                \
		EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)                                \
	bool ClassName::RunTest(const FString&)                                                                      \
	{                                                                                                            \
		ADD_LATENT_AUTOMATION_COMMAND(VerbOrderTests::FScenario(this, VerbOrderTests::EScenario::ScenarioName)); \
		return true;                                                                                             \
	}

VERB_WORLD_TEST(FVerbMoveHoldTest, "MoveHold", MoveHold)
VERB_WORLD_TEST(FVerbWithdrawalTest, "AttackWithdrawal", Withdrawal)
VERB_WORLD_TEST(FVerbStructureDeathTest, "StructureDeath", StructureDeath)
VERB_WORLD_TEST(FVerbStructureWithdrawalTest, "StructureDeath.Withdrawal", StructureWithdrawal)
VERB_WORLD_TEST(FVerbRetreatTest, "Retreat", Retreat)
VERB_WORLD_TEST(FVerbOrphanTest, "Orphan", Orphan)
VERB_WORLD_TEST(FVerbQueueTest, "Queue", Queue)
VERB_WORLD_TEST(FVerbMixedSpeedTest, "MixedSelectionSpeed", MixedSpeed)
VERB_WORLD_TEST(FVerbRallyTest, "Rally", Rally)
VERB_WORLD_TEST(FVerbNeverTest, "ThresholdNever", Never)
VERB_WORLD_TEST(FVerbNoSafeRegionTest, "NoSafeHQFallback", NoSafeRegion)

#undef VERB_WORLD_TEST
#endif
