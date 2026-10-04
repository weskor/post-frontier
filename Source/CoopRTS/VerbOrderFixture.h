#pragma once

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
	ContestedTransit,
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

inline AMapRegion* Region(const ACommandGameState* State, int32 Index)
{
	for (AMapRegion* Candidate : State->Regions)
		if (IsValid(Candidate) && Candidate->RegionIndex == Index)
			return Candidate;
	return nullptr;
}

// Independent breadth-first traversal of the generated map, not executor rules.
inline void FindPaths(const ACommandGameState* State, int32 Start, TMap<int32, int32>& Distance, TMap<int32, int32>& First)
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

enum class EStepResult : uint8
{
	Continue,
	Waiting,
	Finished
};

// Each registered test runs alone in a fresh standalone world. Only the refill,
// orphan and rally scenarios use paid production; other forces are explicit
// encounter fixtures. JEV, automatic income and unrelated producers are isolated.
class FScenarioBase : public IAutomationLatentCommand
{
public:
	FScenarioBase(FAutomationTestBase* InTest, EScenario InScenario)
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
			const EStepResult Result = Recruit();
			if (Result != EStepResult::Continue)
				return Result == EStepResult::Finished;
		}
		if (!Check(Force.IsValid(), TEXT("Ordered force survives the scenario")))
			return true;
		return RunScenario();
	}

protected:
	virtual bool RunScenario() { return Fail(TEXT("Unknown verb scenario")); }
	bool Prepare();
	bool IsolateWorld();
	bool FindRoute();
	bool PrepareEncounters();
	bool PrepareForce();
	EStepResult Recruit();
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
		StageGameStarted = ArmyTestSetup::GameSeconds(GameWorld);
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
			if (IsValid(Unit) && Unit->IsAlive() && !AtRegion->Contains(Unit->GetActorLocation()))
				return false;
		return true;
	}
	bool Occupies(int32 Index) const
	{
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive()
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

	FAutomationTestBase* Test;
	EScenario Scenario;
	double Started, StageStarted, StageGameStarted = 0.;
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

#define VERB_WORLD_TEST(ClassName, TestName, ScenarioName)                              \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "CoopRTS.Forces.Verbs." TestName,       \
		EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)       \
	bool ClassName::RunTest(const FString&)                                             \
	{                                                                                   \
		ADD_LATENT_AUTOMATION_COMMAND(VerbOrder##ScenarioName##Tests::FScenario(this)); \
		return true;                                                                    \
	}

#endif
