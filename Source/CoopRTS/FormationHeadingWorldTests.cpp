#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "HAL/PlatformTime.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Rules/ArmyGroupPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingColumnWorldTest, "CoopRTS.Forces.FormationHeading.Column",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingBoxWorldTest, "CoopRTS.Forces.FormationHeading.Box",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingSwapWorldTest, "CoopRTS.Forces.FormationHeading.Swap",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace FormationHeadingWorld
{
// A six-member fixture force (a Brawler pair, a Rifle pair and an Artillery pair, as ArmyTestSetup spawns them)
// at its home region, JEV gone. A scenario issues one Move & Hold and reads the destinations the order gave
// each member (the destination of its path request), so the plan is observed, not recomputed.
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (bFailed)
			return true;
		if (FPlatformTime::Seconds() - Started > 150.)
			return Fail(FString::Printf(TEXT("Timed out at stage %d"), Stage));
		UWorld* World = ArmyTestSetup::World();
		State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		Controller = World ? ArmyTestSetup::Controller(World) : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !Controller.IsValid() || ArmyTestSetup::GameSeconds(World) < 3.
			|| !ArmyTestSetup::NavigationReady(World))
			return false;
		const double Now = ArmyTestSetup::GameSeconds(World);
		if (Stage == 0)
			return Begin(*World, Now);
		if (Stage == 1)
			return Now - StageStarted >= .5 ? Prepare(Now) : false;
		if (Stage == 2)
			return Now - StageStarted >= .5 ? Issue(Now) : false;
		return Observe(Now);
	}

protected:
	// Stage 1: true once the force is as the scenario needs it (a scenario may remove members and wait for that).
	virtual bool Ready(double Now) { return true; }
	// Stage 2, in the tick of the order: last changes before the order is issued (a scenario may teleport members).
	virtual void BeforeOrder() {}
	// The region to order, from the home region.
	virtual int32 PickTarget(int32 FromRegion) const = 0;
	// Stage 3: the order was accepted; check it.
	virtual bool Check(double Now) = 0;
	virtual bool Observe(double Now) { return Check(Now); }

	bool Fail(const FString& Message)
	{
		Test->AddError(Message);
		bFailed = true;
		return true;
	}

	TArray<FVector> Destinations() const
	{
		TArray<FVector> Result;
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			const AAIController* AI = Cast<AAIController>(Unit->GetController());
			const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
			Result.Add(Path && Path->GetStatus() == EPathFollowingStatus::Moving ? FVector(Path->GetPathDestination()) : FVector::ZeroVector);
		}
		return Result;
	}

	// Graph distance from one region to another along region neighbours.
	int32 HopsFrom(int32 FromRegion, int32 ToRegion) const
	{
		TMap<int32, int32> Distance;
		TArray<int32> Queue{ FromRegion };
		Distance.Add(FromRegion, 0);
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
			for (const AMapRegion* Candidate : State->Regions)
				if (IsValid(Candidate) && Candidate->RegionIndex == Queue[Head])
					for (const int32 Next : Candidate->Neighbours)
						if (!Distance.Contains(Next))
						{
							Distance.Add(Next, Distance[Queue[Head]] + 1);
							Queue.Add(Next);
						}
		return Distance.Contains(ToRegion) ? Distance[ToRegion] : INDEX_NONE;
	}

	const AMapRegion* RegionAt(const FVector& Location) const { return State->FindRegionAt(Location); }

	FAutomationTestBase* Test;
	double Started;
	double StageStarted = 0.;
	int32 Stage = 0, Home = INDEX_NONE, Target = INDEX_NONE;
	ACommandGameState* State = nullptr;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Force;

private:
	bool Begin(UWorld& World, double Now)
	{
		for (TActorIterator<AEnemyCommander> It(&World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(&World); It; ++It)
			if (It->IsOpposingArmy())
				It->Destroy();
		Home = ArmyTestSetup::RegionAt(State, State->FriendlyHeadquarters->GetActorLocation());
		Force = ArmyTestSetup::SpawnGroup(&World, Controller.Get(), 0, State->GetRegionAnchor(Home) + FVector(0.f, -350.f, 100.f));
		if (!Force.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("A six-member fixture force must spawn"));
		Stage = 1;
		StageStarted = Now;
		return false;
	}

	bool Prepare(double Now)
	{
		if (!Ready(Now))
			return bFailed;
		Target = PickTarget(Home);
		if (Target == INDEX_NONE)
			return Fail(TEXT("The map needs the region this scenario orders"));
		Stage = 2;
		StageStarted = Now;
		return false;
	}

	bool Issue(double Now)
	{
		BeforeOrder();
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (!FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, Target).IsAccepted())
			return Fail(TEXT("The Move & Hold order is accepted"));
		Stage = 3;
		StageStarted = Now;
		return Check(Now);
	}

	bool bFailed = false;
};

// Which side of a heading a point lies: coordinates along (u) and across (v) the unit vector from Centre.
struct FAxes
{
	FVector2D Forward, Left;
	FVector Centre;
	double Along(const FVector& Point) const { return FVector2D::DotProduct(FVector2D(Point - Centre), Forward); }
	double Across(const FVector& Point) const { return FVector2D::DotProduct(FVector2D(Point - Centre), Left); }
};

// The first leg of a two-hop march is a column: the destinations lie in a file along the heading, with the melee
// pair ahead of the ranged pair ahead of the artillery pair, and the members stay on the navmesh while marching.
class FColumn : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	int32 PickTarget(int32 HomeRegion) const override
	{
		int32 Best = INDEX_NONE;
		for (const AMapRegion* Candidate : State->Regions)
			if (IsValid(Candidate) && HopsFrom(HomeRegion, Candidate->RegionIndex) == 2
				&& !(Candidate->RegionRole == ERegionRole::Main && Candidate->HomeTeam != 0)
				&& (Best == INDEX_NONE || Candidate->RegionIndex < Best))
				Best = Candidate->RegionIndex;
		return Best;
	}

	bool Check(double Now) override
	{
		if (Stage == 4)
			return Marching(Now);
		if (Force->WaypointRegionIndex == INDEX_NONE || Force->WaypointRegionIndex == Target)
			return Fail(TEXT("Fixture: the first leg must end in an intermediate region"));
		const TArray<FVector> Goals = Destinations();
		FVector Mean = FVector::ZeroVector;
		for (const AArmyUnit* Unit : Force->GetUnits())
			Mean += Unit->GetActorLocation() / 6.;
		const FVector Centre = Force->Destination;
		const FVector2D Heading = FVector2D(Centre - Mean).GetSafeNormal();
		if (FVector::Dist2D(Centre, Mean) < ArmyGroupPolicy::ColumnMinDistance)
			return Fail(TEXT("Fixture: the first leg must be long"));
		const FAxes Axes{ Heading, FVector2D(-Heading.Y, Heading.X), Centre };
		double Spread = 0.;
		TArray<double> Along;
		for (const FVector& Goal : Goals)
		{
			if (Goal.IsZero())
				return Fail(TEXT("Every member has an active path to its slot"));
			Spread = FMath::Max(Spread, FMath::Abs(Axes.Across(Goal)));
			Along.Add(Axes.Along(Goal));
		}
		TArray<double> Sorted = Along;
		Sorted.Sort();
		double LeastGap = TNumericLimits<double>::Max();
		for (int32 Index = 1; Index < Sorted.Num(); ++Index)
			LeastGap = FMath::Min(LeastGap, Sorted[Index] - Sorted[Index - 1]);
		Test->AddInfo(FString::Printf(TEXT("Column leg: lateral spread %.0f cm, least gap %.0f cm, length %.0f cm"),
			Spread, LeastGap, Sorted.Last() - Sorted[0]));
		// Jitter (<= 24) plus navmesh projection (<= 35) either side.
		if (Spread > 2. * ArmyGroupPolicy::JitterRadius + 40.)
			return Fail(TEXT("The destinations lie in a file along the heading"));
		if (LeastGap < 40. || Sorted.Last() - Sorted[0] < 300. || Sorted.Last() - Sorted[0] > 520.)
			return Fail(TEXT("The file is about five spacings long, its members apart"));
		if (!Rows(Goals, Axes))
			return true;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (!Unit->FormationMemory.bPlanned || !Unit->FormationMemory.bColumn)
				return Fail(TEXT("The plan's heading is remembered on the members"));
		Stage = 4;
		StageStarted = ArmyTestSetup::GameSeconds(Force->GetWorld());
		return false;
	}

private:
	// Melee ahead of ranged ahead of artillery, by the mean position along the heading of each class.
	bool Rows(const TArray<FVector>& Goals, const FAxes& Axes)
	{
		double Sum[3] = {}, Count[3] = {};
		for (int32 Index = 0; Index < Goals.Num(); ++Index)
		{
			const EUnitRole Role = Force->GetUnits()[Index]->GetUnitRole();
			const int32 Rank = Role == EUnitRole::Frontline ? 0 : Role == EUnitRole::Siege ? 2
																						   : 1;
			Sum[Rank] += Axes.Along(Goals[Index]);
			Count[Rank] += 1.;
		}
		if (!Count[0] || !Count[1] || !Count[2])
			return !Fail(TEXT("Fixture: the force has a melee, a ranged and an artillery class"));
		const double Front = Sum[0] / Count[0], Middle = Sum[1] / Count[1], Back = Sum[2] / Count[2];
		Test->AddInfo(FString::Printf(TEXT("Class rows along the heading: melee %.0f, ranged %.0f, artillery %.0f"), Front, Middle, Back));
		return Front > Middle && Middle > Back ? true : !Fail(TEXT("The classes take rows: melee, ranged, artillery"));
	}

	bool Marching(double Now)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Force->GetWorld());
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			FNavLocation Ground;
			if (!Navigation || !Navigation->ProjectPointToNavigation(Unit->GetNavAgentLocation(), Ground, FVector(35., 35., 200.))
				|| FVector::Dist2D(Unit->GetNavAgentLocation(), Ground.Location) > 35.)
				return Fail(TEXT("Every member stays on the navmesh while marching"));
		}
		return Now - StageStarted >= 8.;
	}
};

// The last leg into the target region is the box: the destinations are the fitted layout of the force, inside the
// region, whatever the heading.
class FBox : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	int32 PickTarget(int32 HomeRegion) const override
	{
		return ArmyTestSetup::TravelRegion(Force.Get(), State->EnemyHeadquarters->GetActorLocation());
	}

	bool Check(double Now) override
	{
		if (Force->WaypointRegionIndex != Target)
			return Fail(TEXT("Fixture: the first leg ends in the target region"));
		const AMapRegion* Region = RegionAt(Force->Destination);
		if (!Region)
			return Fail(TEXT("The arrival centre is inside a region"));
		const ArmyGroupPolicy::FFormation Formation{ false, 0, false };
		const ArmyGroupPolicy::FFit Fit = ArmyGroupPolicy::FitForce(Formation, Region->Polygon, Force->Destination);
		TArray<FVector> Expected;
		for (int32 Slot = 0; Slot < ArmyGroupPolicy::SlotCount(Formation); ++Slot)
			Expected.Add(ArmyGroupPolicy::FittedSlot(Formation, Fit, Region->Polygon, Slot));
		for (const FVector& Goal : Destinations())
		{
			int32 Nearest = INDEX_NONE;
			for (int32 Index = 0; Index < Expected.Num(); ++Index)
				if (Nearest == INDEX_NONE || FVector::DistSquared2D(Goal, Expected[Index]) < FVector::DistSquared2D(Goal, Expected[Nearest]))
					Nearest = Index;
			if (Goal.IsZero() || Nearest == INDEX_NONE || FVector::Dist2D(Goal, Expected[Nearest]) > 45.)
				return Fail(TEXT("Each member's destination is a slot of the fitted box"));
			if (!Region->Contains(Goal))
				return Fail(TEXT("The box stands inside the region"));
			Expected.RemoveAtSwap(Nearest);
		}
		Test->AddInfo(TEXT("Box arrival: six distinct fitted slots inside the target region"));
		return true;
	}
};

// Two melee members of one class stand on each other's side (swapped along the axis of their slots); the plan
// sends each to the slot on its own side: their destinations are not crossed.
class FSwap : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	bool Ready(double Now) override
	{
		if (!bRemoved)
		{
			// Only the two Brawlers (slots 0 and 1) remain: one class, so rows cannot decide the assignment.
			TArray<AArmyUnit*> Remove;
			for (AArmyUnit* Unit : Force->GetUnits())
				if (Unit->GetCompositionSlot() > 1)
					Remove.Add(Unit);
			for (AArmyUnit* Unit : Remove)
				Unit->Destroy();
			bRemoved = true;
			RemovedAt = Now;
			return false;
		}
		if (Force->GetUnits().Num() == 2)
			return true;
		if (Now - RemovedAt > 5.)
			Fail(TEXT("The four other members are gone"));
		return false;
	}

	int32 PickTarget(int32 HomeRegion) const override
	{
		return ArmyTestSetup::TravelRegion(Force.Get(), State->EnemyHeadquarters->GetActorLocation());
	}

	// Their rigid slots differ by an axis; put A on B's side of the middle and B on A's, 300 cm out each way.
	void BeforeOrder() override
	{
		AArmyUnit* A = Force->GetUnits()[0];
		const AArmyUnit* Other = Force->GetUnits()[1];
		const ArmyGroupPolicy::FFormation Formation{ false, 0, false };
		Axis = (ArmyGroupPolicy::FormationOffset(Formation, A->GetCompositionSlot())
			- ArmyGroupPolicy::FormationOffset(Formation, Other->GetCompositionSlot()))
				   .GetSafeNormal();
		const FVector Middle = (A->GetActorLocation() + Other->GetActorLocation()) / 2.;
		A->SetActorLocation(Middle - Axis * 300., false, nullptr, ETeleportType::TeleportPhysics);
		Force->GetUnits()[1]->SetActorLocation(Middle + Axis * 300., false, nullptr, ETeleportType::TeleportPhysics);
	}

	bool Check(double Now) override
	{
		const TArray<FVector> Goals = Destinations();
		if (Goals.Num() != 2 || Goals[0].IsZero() || Goals[1].IsZero())
			return Fail(TEXT("Both members have an active path"));
		// A stands on the side opposite its own slot (slot A minus slot B points along +Axis, A is at -Axis), so
		// the nearer slot is B's: its destination lies on the -Axis side of B's.
		const double Side = FVector2D::DotProduct(FVector2D(Goals[0] - Goals[1]), FVector2D(Axis));
		Test->AddInfo(FString::Printf(TEXT("Swapped pair: destinations differ by %.0f cm along the slots' axis (negative = no crossing)"), Side));
		if (Side >= -100.)
			return Fail(TEXT("Swapped units take the near slots: their destinations do not cross"));
		bDestinationsChecked = true;
		return false;
	}

	// The walk from the anchor's box to the post's: the pair never changes sides, and rests at the post on the sides
	// the march gave them (the post's slots follow the assignment, not the composition slots).
	bool Observe(double Now) override
	{
		if (!bDestinationsChecked)
			return Check(Now);
		if (!Force.IsValid() || Force->GetUnits().Num() != 2)
			return Fail(TEXT("The pair survives its walk"));
		const AArmyUnit* A = Force->GetUnits()[0];
		const AArmyUnit* B = Force->GetUnits()[1];
		const double Side = FVector2D::DotProduct(FVector2D(A->GetActorLocation() - B->GetActorLocation()), FVector2D(Axis));
		MostCrossed = FMath::Max(MostCrossed, Side);
		if (Side > 40.)
			return Fail(FString::Printf(TEXT("The pair crossed on the way: A is %.0f cm past B along the slots' axis"), Side));
		const bool bAtPost = Force->IsHoldingRegion() && Force->HoldPostIndex != INDEX_NONE && !Force->bHoldResponding
			&& A->GetVelocity().Size2D() < 5.f && B->GetVelocity().Size2D() < 5.f;
		if (!bAtPost)
		{
			SettledSince = -1.;
			return false;
		}
		if (SettledSince < 0.)
			SettledSince = Now;
		if (Now - SettledSince < 2.)
			return false;
		const double Goal = FVector2D::DotProduct(FVector2D(A->FormationTarget - B->FormationTarget), FVector2D(Axis));
		for (const AArmyUnit* Member : { A, B })
			Test->AddInfo(FString::Printf(TEXT("  member slot %d rests at %s on its planned target %s, post %s"), Member->GetCompositionSlot(),
				*Member->GetActorLocation().ToCompactString(), *Member->FormationTarget.ToCompactString(), *Force->HoldPostLocation.ToCompactString()));
		const AMapRegion* Region = State->FindRegionAt(Force->HoldPostLocation);
		if (!Region)
			return Fail(TEXT("The post is inside a region"));
		// The fitted post slots of the two composition slots: each member rests on its own planned target, which is
		// at one of them (or, where the slot is not navigable, the nearest standing point to it), the two apart.
		const ArmyGroupPolicy::FFormation Formation{ false, 0, false };
		const ArmyGroupPolicy::FFit Fit = ArmyGroupPolicy::FitForce(Formation, Region->Polygon, Force->HoldPostLocation);
		const FVector Slots[2] = { ArmyGroupPolicy::FittedSlot(Formation, Fit, Region->Polygon, A->GetCompositionSlot()),
			ArmyGroupPolicy::FittedSlot(Formation, Fit, Region->Polygon, B->GetCompositionSlot()) };
		const double Fitted = FVector::Dist2D(Slots[0], Slots[1]);
		for (const AArmyUnit* Member : { A, B })
			if (Member->FormationTarget.IsZero() || FVector::Dist2D(Member->GetActorLocation(), Member->FormationTarget) > 100.)
				return Fail(TEXT("Each member rests within 100 cm of its own planned post slot"));
		const double Own = FVector::Dist2D(A->FormationTarget, Slots[0]) + FVector::Dist2D(B->FormationTarget, Slots[1]);
		const double Swapped = FVector::Dist2D(A->FormationTarget, Slots[1]) + FVector::Dist2D(B->FormationTarget, Slots[0]);
		const double Off = FMath::Min(Own, Swapped);
		const double Spacing = FVector::Dist2D(A->FormationTarget, B->FormationTarget);
		Test->AddInfo(FString::Printf(TEXT("Planned targets %.0f cm apart, fitted slots %.0f cm apart, targets %.0f cm from their slots in all; A is %.0f cm from B along the axis, targets differ by %.0f cm; most crossed on the way %.0f cm"),
			Spacing, Fitted, Off, Side, Goal, MostCrossed));
		if (Off > 2. * (ArmyGroupPolicy::SlotFallbackRadius + 75.f))
			return Fail(TEXT("The planned post slots are the fitted slots, or the nearest standing points to them"));
		if (Off < 1. && FMath::Abs(Spacing - Fitted) > 1.)
			return Fail(TEXT("On navigable ground the two slots are the fitted spacing apart"));
		if (Spacing < 100.)
			return Fail(TEXT("The two members have distinct slots"));
		return Side < -60. && Goal < -60. ? true : Fail(TEXT("The pair rests on the sides the march gave it"));
	}

private:
	bool bRemoved = false, bDestinationsChecked = false;
	double RemovedAt = 0., SettledSince = -1., MostCrossed = -1.e9;
	FVector Axis = FVector::ZeroVector;
};
}

bool FFormationHeadingColumnWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationHeadingWorld::FColumn(this));
	return true;
}

bool FFormationHeadingBoxWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationHeadingWorld::FBox(this));
	return true;
}

bool FFormationHeadingSwapWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationHeadingWorld::FSwap(this));
	return true;
}

#endif
