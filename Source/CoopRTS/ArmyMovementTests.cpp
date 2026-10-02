#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "AIController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "CommandPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyTwoGroupsTest, "CoopRTS.Movement.TwoGroups",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Run alone in a fresh standalone game. The placed map supplies the obstacle and
// fixture positions; arrivals observe real AI paths and every character.
class FArmyTwoGroupsScenario : public IAutomationLatentCommand
{
public:
	explicit FArmyTwoGroupsScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (!bIsolated)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
				if (UWorld* World = Context.World())
					if (World->IsGameWorld() && World->GetNetMode() == NM_Standalone)
					{
						for (TActorIterator<AEnemyCommander> It(World); It; ++It)
							It->Destroy();
						bIsolated = true;
						break;
					}
		}
		const double Now = FPlatformTime::Seconds();
		if (bFailed)
			return true;
		if (Now - Started > 90.)
		{
			Test->AddError(FString::Printf(TEXT("TwoGroups exceeded 90 seconds in stage %d"), static_cast<int32>(Stage)));
			if (Stage == EStage::FindGroups && Controller.IsValid())
			{
				UWorld* World = Controller->GetWorld();
				const ACommandGameState* State = World->GetGameState<ACommandGameState>();
				UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				Test->AddInfo(FString::Printf(TEXT("Circuit setup mapReady=%d navigation=%d building=%d eligible=%d clear=%d"),
					ArmyTestSetup::MapReady(State), Navigation != nullptr,
					Navigation && Navigation->IsNavigationBuildInProgress(), EligibleObstacles, ClearCircuits));
			}
			for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
			{
				if (!Groups[GroupIndex].IsValid())
					continue;
				for (int32 Index = 0; Index < Groups[GroupIndex]->GetUnits().Num(); ++Index)
				{
					const AArmyUnit* Unit = Groups[GroupIndex]->GetUnits()[Index];
					if (!IsValid(Unit))
						continue;
					const UPathFollowingComponent* Following = GetFollowing(Unit);
					Test->AddInfo(FString::Printf(TEXT("Army %d unit %d position=%s goal=%s speed=%.1f status=%d reached=%d"),
						GroupIndex, Index, *Unit->GetActorLocation().ToCompactString(),
						Goals[GroupIndex].IsValidIndex(Index) ? *Goals[GroupIndex][Index].ToCompactString() : TEXT("unset"),
						Unit->GetVelocity().Size2D(), Following ? static_cast<int32>(Following->GetStatus()) : -1,
						Following && Following->DidMoveReachGoal()));
				}
			}
			return true;
		}

		if (Stage == EStage::FindGroups)
			return Start(Now);
		if (!Check(Controller.IsValid() && Groups[0].IsValid() && Groups[1].IsValid() && Enemy.IsValid(),
				TEXT("Both groups, their controller and the opposing army survive")))
			return true;
		for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
		{
			if (!Check(Groups[GroupIndex]->GetUnits().Num() == 6, TEXT("No units disappear or appear unexpectedly")))
				return true;
			for (AArmyUnit* Unit : Groups[GroupIndex]->GetUnits())
			{
				if (!Check(IsValid(Unit) && Unit->GetGroup() == Groups[GroupIndex].Get(), TEXT("Every unit retains its own group")))
					return true;
				if (Stage >= EStage::Crossing)
				{
					const FVector Location = Unit->GetActorLocation();
					// Observe each character inside the obstacle's longitudinal strip
					// but outside its lateral footprint, not merely a bent AI path.
					const FVector Relative = Location - Obstacle.GetCenter();
					if (FMath::Abs(FVector::DotProduct(Relative, TravelAxis)) < TravelExtent - 50.
						&& FMath::Abs(FVector::DotProduct(Relative, SideAxis)) > SideExtent)
						WentAroundObstacle.Add(Unit);
				}
			}
		}

		switch (Stage)
		{
		case EStage::Exchange:
			if (AllArrived())
			{
				Test->AddInfo(TEXT("Both six-unit groups exchanged home positions without stragglers."));
				SetStage(EStage::RapidReplacement, Now);
			}
			break;
		case EStage::RapidReplacement:
			if (Now - StageStarted >= .12)
			{
				const double Offset = (ReplacementCount % 2 == 0) ? 250. : -250.;
				if (!Move(0, CircuitPoint(-TravelExtent - 2200. + Offset, SideExtent + 900.))
					|| !Move(1, CircuitPoint(-TravelExtent - 1200. - Offset, -SideExtent - 900.)))
					return true;
				StageStarted = Now;
				if (++ReplacementCount == 8)
					SetStage(EStage::BeforeRejection, Now);
			}
			break;
		case EStage::BeforeRejection:
			if (Now - StageStarted >= .6)
			{
				UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld());
				FNavLocation Projected;
				const FNavAgentProperties& Agent = Groups[0]->GetUnits()[0]->GetNavAgentPropertiesRef();
				if (!Check(Navigation && Navigation->ProjectPointToNavigation(WallProbe, Projected, FVector(75., 75., 200.), &Agent),
						TEXT("Near-wall rejection probe has a navigable center")))
					return true;
				for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
				{
					for (AArmyUnit* Unit : Groups[GroupIndex]->GetUnits())
						if (!Check(Unit->GetVelocity().Size2D() > 25., TEXT("Every unit is moving before rejected requests")))
							return true;
					// The raised obstacle has no complete ground route to its top;
					// the wall probe fits a center but not all six formation slots.
					const ACommandGameState* State = Controller->GetWorld()->GetGameState<ACommandGameState>();
					FVector Top = Obstacle.GetCenter();
					Top.Z = Obstacle.Max.Z;
					if (!RejectMove(GroupIndex, ArmyTestSetup::OutsideArena(State), TEXT("Out-of-bounds"))
						|| !RejectMove(GroupIndex, Top, TEXT("Unreachable obstacle top"))
						|| !RejectMove(GroupIndex, WallProbe, TEXT("Near-wall formation")))
						return true;
					RememberPositions(GroupIndex);
				}
				SetStage(EStage::AfterRejection, Now);
			}
			break;
		case EStage::AfterRejection:
			if (Now - StageStarted >= .8)
			{
				for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
					for (int32 Index = 0; Index < Groups[GroupIndex]->GetUnits().Num(); ++Index)
					{
						const FVector Position = Groups[GroupIndex]->GetUnits()[Index]->GetActorLocation();
						if (!Check(FVector::Dist2D(Position, Positions[GroupIndex][Index]) > 30., TEXT("Rejected requests do not stop any unit"))
							|| !Check(FVector::Dist2D(Position, Goals[GroupIndex][Index]) + 30. < FVector::Dist2D(Positions[GroupIndex][Index], Goals[GroupIndex][Index]),
								TEXT("Every unit continues toward its accepted replacement")))
							return true;
					}
				if (!Move(0, CircuitPoint(TravelExtent + 1600., 400.))
					|| !Move(1, CircuitPoint(TravelExtent + 1600., -400.)))
					return true;
				SetStage(EStage::Crossing, Now);
			}
			break;
		case EStage::Crossing:
			if (Now - StageStarted >= 2.)
			{
				if (!Move(0, CircuitPoint(TravelExtent + 2200., 650.)))
					return true;
				for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
				{
					const uint32 Serial = Groups[GroupIndex]->OrderSerial;
					Controller->ServerIssueOrder(Groups[GroupIndex].Get(), EArmyOrder::Hold, FVector::ZeroVector);
					if (!Check(Groups[GroupIndex]->Order == EArmyOrder::Hold && Groups[GroupIndex]->OrderSerial > Serial,
							TEXT("Immediate Hold replaces travel for every member")))
						return true;
					RememberPositions(GroupIndex);
				}
				SetStage(EStage::Held, Now);
			}
			break;
		case EStage::Held:
			for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
				for (int32 Index = 0; Index < Groups[GroupIndex]->GetUnits().Num(); ++Index)
				{
					AArmyUnit* Unit = Groups[GroupIndex]->GetUnits()[Index];
					UPathFollowingComponent* Following = GetFollowing(Unit);
					if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Positions[GroupIndex][Index]) < 5.
								&& Unit->GetVelocity().Size2D() < 1. && Following && Following->GetStatus() == EPathFollowingStatus::Idle,
							FString::Printf(TEXT("Army %d unit %d stays at its immediate Hold position"), GroupIndex, Index)))
						return true;
				}
			if (Now - StageStarted >= 1.)
			{
				if (!Move(0, CircuitPoint(TravelExtent + 1700., 400.))
					|| !Move(1, CircuitPoint(TravelExtent + 1700., -400.)))
					return true;
				SetStage(EStage::FinalArrival, Now);
			}
			break;
		case EStage::FinalArrival:
			if (AllArrived())
				SetStage(EStage::Settled, Now);
			break;
		case EStage::Settled:
			if (!Check(AllArrived(), TEXT("No stale order resumes after all units reach the latest destination")))
				return true;
			if (Now - StageStarted >= 1.)
			{
				for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
					for (AArmyUnit* Unit : Group->GetUnits())
						if (!Check(WentAroundObstacle.Contains(Unit), FString::Printf(TEXT("%s crossed around the map obstacle"), *Unit->GetName())))
							return true;
				Test->AddInfo(TEXT("TwoGroups passed: independent armies, every-unit exchange and obstacle crossing, eight rapid replacements, atomic invalid orders with continued motion, immediate per-unit Hold, and all twelve units at their latest goals."));
				return true;
			}
			break;
		default:
			break;
		}
		return bFailed;
	}

private:
	enum class EStage : uint8
	{
		FindGroups,
		Exchange,
		RapidReplacement,
		BeforeRejection,
		AfterRejection,
		Crossing,
		Held,
		FinalArrival,
		Settled
	};

	bool Check(bool bCondition, const FString& Message)
	{
		if (!bCondition)
		{
			Test->AddError(Message);
			bFailed = true;
		}
		return bCondition;
	}

	static UPathFollowingComponent* GetFollowing(const AArmyUnit* Unit)
	{
		const AAIController* AI = Unit ? Cast<AAIController>(Unit->GetController()) : nullptr;
		return AI ? AI->GetPathFollowingComponent() : nullptr;
	}

	FVector CircuitPoint(double Along, double Across) const
	{
		FVector Point = Obstacle.GetCenter() + TravelAxis * Along + SideAxis * Across;
		Point.Z = GroundHeight;
		return Point;
	}

	bool ClearFormation(UNavigationSystemV1& Navigation, const FVector& Center, const FVector& Start) const
	{
		const FNavAgentProperties& Agent = GetDefault<AArmyUnit>()->GetNavAgentPropertiesRef();
		const ANavigationData* NavData = Navigation.GetNavDataForProps(Agent, Start);
		if (!NavData)
			return false;
		for (int32 Slot = -1; Slot < 6; ++Slot)
		{
			const FVector Offset = Slot < 0 ? FVector::ZeroVector
											: FVector((1 - Slot / 2) * 220., (Slot % 2 ? 1. : -1.) * 140., 0.);
			const FVector Target = Center + Offset;
			FNavLocation Ground;
			if (!AArenaBounds::IsTravelLocation(Navigation.GetWorld(), Target)
				|| !Navigation.ProjectPointToNavigation(Target, Ground, FVector(35., 35., 200.), NavData)
				|| FVector::Dist2D(Target, Ground.Location) > 35.)
				return false;
			FPathFindingQuery Query(nullptr, *NavData, Start, Ground.Location);
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult Path = Navigation.FindPathSync(Agent, Query);
			if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial())
				return false;
		}
		return true;
	}

	bool FindCircuit(ACommandGameState* State)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->GetWorld());
		if (!Navigation || Navigation->IsNavigationBuildInProgress())
			return false;
		const FVector Midpoint = (State->FriendlyHeadquarters->GetActorLocation()
									 + State->EnemyHeadquarters->GetActorLocation())
			* .5;
		double BestDistance = TNumericLimits<double>::Max();
		FBox BestObstacle(ForceInit);
		FVector BestAxis = FVector::ZeroVector;
		FVector BestWall = FVector::ZeroVector;
		EligibleObstacles = 0;
		ClearCircuits = 0;
		for (TActorIterator<AStaticMeshActor> It(State->GetWorld()); It; ++It)
		{
			const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
			if (!Mesh || Mesh->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block
				|| Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
				continue;
			Obstacle = Mesh->Bounds.GetBox();
			const FVector Extent = Obstacle.GetExtent();
			FVector Top = Obstacle.GetCenter();
			Top.Z = Obstacle.Max.Z;
			if (Extent.X < 200. || Extent.Y < 200. || Extent.X > 2000. || Extent.Y > 2000. || Extent.Z < 150.
				|| !AArenaBounds::IsTravelLocation(State->GetWorld(), Top))
				continue;
			++EligibleObstacles;
			const double Distance = FVector::DistSquared2D(Obstacle.GetCenter(), Midpoint);
			if (Distance >= BestDistance)
				continue;
			GroundHeight = Obstacle.Min.Z;
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				TravelAxis = Axis == 0 ? FVector::ForwardVector : FVector::RightVector;
				SideAxis = Axis == 0 ? FVector::RightVector : FVector::ForwardVector;
				TravelExtent = Axis == 0 ? Extent.X : Extent.Y;
				SideExtent = Axis == 0 ? Extent.Y : Extent.X;
				const FVector Home = CircuitPoint(-TravelExtent - 1200., 0.);
				const FVector Targets[] = {
					Home, CircuitPoint(-TravelExtent - 2200., 0.),
					CircuitPoint(-TravelExtent - 2450., SideExtent + 900.),
					CircuitPoint(-TravelExtent - 1950., SideExtent + 900.),
					CircuitPoint(-TravelExtent - 1450., -SideExtent - 900.),
					CircuitPoint(-TravelExtent - 950., -SideExtent - 900.),
					CircuitPoint(TravelExtent + 1600., 400.), CircuitPoint(TravelExtent + 1600., -400.),
					CircuitPoint(TravelExtent + 2200., 650.),
					CircuitPoint(TravelExtent + 1700., 400.), CircuitPoint(TravelExtent + 1700., -400.)
				};
				bool bClear = true;
				for (const FVector& Target : Targets)
					if (!ClearFormation(*Navigation, Target, Home))
					{
						bClear = false;
						break;
					}
				if (!bClear)
					continue;
				++ClearCircuits;
				const FNavAgentProperties& Agent = GetDefault<AArmyUnit>()->GetNavAgentPropertiesRef();
				const ANavigationData* NavData = Navigation->GetNavDataForProps(Agent, Home);
				FNavLocation Probe;
				bool bFoundWall = false;
				// Rotated mesh bounds are not their collision face. Search inward
				// for a reachable ground center whose formation cannot fit.
				for (double Along = -TravelExtent - 100.; Along < 0.; Along += 50.)
				{
					const FVector Wall = CircuitPoint(Along, 0.);
					if (!Navigation->ProjectPointToNavigation(Wall, Probe, FVector(35., 35., 200.), NavData)
						|| FVector::Dist2D(Wall, Probe.Location) > 35.
						|| ClearFormation(*Navigation, Probe.Location, Home))
						continue;
					FPathFindingQuery Query(nullptr, *NavData, Home, Probe.Location);
					Query.SetAllowPartialPaths(false);
					const FPathFindingResult CenterPath = Navigation->FindPathSync(Agent, Query);
					if (!CenterPath.IsSuccessful() || !CenterPath.Path.IsValid() || CenterPath.Path->IsPartial())
						continue;
					bFoundWall = true;
					break;
				}
				if (!bFoundWall)
					continue;
				BestDistance = Distance;
				BestObstacle = Obstacle;
				BestAxis = TravelAxis;
				BestWall = Probe.Location;
				break;
			}
		}
		if (!BestObstacle.IsValid)
			return false;
		Obstacle = BestObstacle;
		TravelAxis = BestAxis;
		SideAxis = FVector(TravelAxis.Y, TravelAxis.X, 0.);
		TravelExtent = FVector::DotProduct(Obstacle.GetExtent(), TravelAxis);
		SideExtent = FVector::DotProduct(Obstacle.GetExtent(), SideAxis);
		GroundHeight = Obstacle.Min.Z;
		WallProbe = BestWall;
		Test->AddInfo(FString::Printf(TEXT("Map circuit obstacle=%s axis=%s wall=%s"),
			*Obstacle.ToString(), *TravelAxis.ToCompactString(), *WallProbe.ToCompactString()));
		return true;
	}

	bool bFixturesReady = false;
	bool Start(double Now)
	{
		if (Now - Started < 3.)
			return false;
		if (!Controller.IsValid())
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || !World->IsGameWorld() || World->GetNetMode() != NM_Standalone)
					continue;
				for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
					if (It->IsLocalController())
					{
						Controller = *It;
						break;
					}
				if (Controller.IsValid())
					break;
			}
		}
		if (!Controller.IsValid())
			return false;
		ACommandGameState* State = Controller->GetWorld()->GetGameState<ACommandGameState>();
		if (!ArmyTestSetup::MapReady(State))
			return false;
		if (!bFixturesReady)
		{
			if (!FindCircuit(State))
				return false;
			Groups[0] = ArmyTestSetup::SpawnGroup(State->GetWorld(), Controller.Get(), 0,
				CircuitPoint(-TravelExtent - 1200., 0.) + FVector(0., 0., 100.));
			Groups[1] = ArmyTestSetup::SpawnGroup(State->GetWorld(), Controller.Get(), 1,
				CircuitPoint(-TravelExtent - 2200., 0.) + FVector(0., 0., 100.));
			Enemy = ArmyTestSetup::SpawnGroup(State->GetWorld(), nullptr, -1, ArmyTestSetup::HostileStaging(State));
			if (!Check(Groups[0].IsValid() && Groups[1].IsValid() && Enemy.IsValid(),
					TEXT("Map-derived fixtures spawn two friendly groups and a static opposing group")))
				return true;
			Enemy->IssueHold();
			bFixturesReady = true;
			return false; // Let every spawned character settle on navigation ground.
		}
		for (TActorIterator<AArmyGroup> It(Controller->GetWorld()); It; ++It)
		{
			if (It->IsOpposingArmy())
				Enemy = *It;
			if (It->GetOwner() == Controller.Get() && It->GetArmyIndex() >= 0 && It->GetArmyIndex() < 2)
			{
				if (!Check(!Groups[It->GetArmyIndex()].IsValid() || Groups[It->GetArmyIndex()].Get() == *It, TEXT("ArmyIndex uniquely identifies each local group")))
					return true;
				Groups[It->GetArmyIndex()] = *It;
			}
		}
		if (!Groups[0].IsValid() || !Groups[1].IsValid() || !Enemy.IsValid() || Enemy->GetUnits().IsEmpty())
			return false;
		TSet<AArmyUnit*> UniqueUnits;
		for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
		{
			if (!Check(Group->OrderSerial == 0 && Group->Order == EArmyOrder::Hold && Group->GetUnits().Num() == 6,
					TEXT("TwoGroups requires fresh fixture groups: six units and no earlier orders")))
				return true;
			for (AArmyUnit* Unit : Group->GetUnits())
			{
				if (!Check(IsValid(Unit) && Unit->GetGroup() == Group.Get()
							&& Unit->GetTeamIndex() == Group->GetTeamIndex() && !UniqueUnits.Contains(Unit)
							&& FVector::Dist2D(Unit->GetActorLocation(), Group->GetHomeLocation()) < 450.,
						TEXT("Each starting member belongs exclusively to its group at home")))
					return true;
				UniqueUnits.Add(Unit);
			}
		}
		if (!Check(Groups[0]->GetTeamIndex() == Groups[1]->GetTeamIndex() && Groups[0]->GetOwner() == Groups[1]->GetOwner(), TEXT("Two independent armies belong to the same player")))
			return true;
		// A rejected first request is safe to retry while dynamic navmesh starts.
		Controller->ServerIssueOrder(Groups[0].Get(), EArmyOrder::Move, Groups[1]->GetHomeLocation());
		if (Groups[0]->OrderSerial == 0)
			return false;
		if (!Check(Groups[1]->OrderSerial == 0 && Groups[1]->Order == EArmyOrder::Hold,
				TEXT("Ordering army zero does not order army one")))
			return true;
		if (!Move(0, Groups[1]->GetHomeLocation()) || !Move(1, Groups[0]->GetHomeLocation()))
			return true;
		SetStage(EStage::Exchange, Now);
		return false;
	}

	bool Move(int32 GroupIndex, const FVector& Target)
	{
		AArmyGroup* Group = Groups[GroupIndex].Get();
		const AArmyGroup* Other = Groups[1 - GroupIndex].Get();
		const uint32 Serial = Group->OrderSerial;
		const uint32 OtherSerial = Other->OrderSerial;
		const EArmyOrder OtherOrder = Other->Order;
		const FVector OtherDestination = Other->Destination;
		Controller->ServerIssueOrder(Group, EArmyOrder::Move, Target);
		return Check(Group->OrderSerial > Serial && Group->Order == EArmyOrder::Move
					   && FVector::Dist2D(Group->Destination, Target) < 100.,
				   TEXT("Valid replacement is accepted at the requested destination"))
			&& Check(Other->OrderSerial == OtherSerial && Other->Order == OtherOrder && Other->Destination.Equals(OtherDestination), TEXT("Ordering one army preserves the other army's accepted order"))
			&& CaptureGoals(GroupIndex);
	}

	bool CaptureGoals(int32 GroupIndex)
	{
		Goals[GroupIndex].Reset();
		for (AArmyUnit* Unit : Groups[GroupIndex]->GetUnits())
		{
			UPathFollowingComponent* Following = GetFollowing(Unit);
			if (!Check(Following && Following->GetPath().IsValid() && Following->GetStatus() == EPathFollowingStatus::Moving,
					TEXT("Every member receives a real active navigation path")))
				return false;
			const FVector Goal = Following->GetPath()->GetEndLocation();
			if (!Check(FVector::Dist2D(Goal, Groups[GroupIndex]->Destination) < 450., TEXT("Every member's path ends in the commanded formation")))
				return false;
			Goals[GroupIndex].Add(Goal);
		}
		return true;
	}

	bool AllArrived() const
	{
		for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
			for (int32 Index = 0; Index < Groups[GroupIndex]->GetUnits().Num(); ++Index)
			{
				AArmyUnit* Unit = Groups[GroupIndex]->GetUnits()[Index];
				const UPathFollowingComponent* Following = GetFollowing(Unit);
				if (!Goals[GroupIndex].IsValidIndex(Index) || !Following || Following->GetStatus() != EPathFollowingStatus::Idle
					|| !Following->DidMoveReachGoal()
					|| Unit->GetVelocity().Size2D() > 5. || FVector::Dist2D(Unit->GetActorLocation(), Goals[GroupIndex][Index]) > 80.)
					return false;
			}
		return true;
	}

	bool RejectMove(int32 GroupIndex, const FVector& Target, const TCHAR* Description)
	{
		AArmyGroup* Group = Groups[GroupIndex].Get();
		const uint32 Serial = Group->OrderSerial;
		const EArmyOrder Order = Group->Order;
		const FVector Destination = Group->Destination;
		TArray<FAIRequestID, TInlineAllocator<6>> Requests;
		for (AArmyUnit* Unit : Group->GetUnits())
			Requests.Add(GetFollowing(Unit)->GetCurrentRequestId());
		if (!Check(!Group->IssueMove(Target), FString::Printf(TEXT("%s request is rejected"), Description))
			|| !Check(Group->OrderSerial == Serial && Group->Order == Order && Group->Destination.Equals(Destination), TEXT("Rejected request preserves accepted order state")))
			return false;
		for (int32 Index = 0; Index < Group->GetUnits().Num(); ++Index)
		{
			UPathFollowingComponent* Following = GetFollowing(Group->GetUnits()[Index]);
			if (!Check(Following && Following->GetCurrentRequestId() == Requests[Index] && Following->GetStatus() == EPathFollowingStatus::Moving,
					TEXT("Rejected request preserves each active AI request")))
				return false;
		}
		return true;
	}

	void RememberPositions(int32 GroupIndex)
	{
		Positions[GroupIndex].Reset();
		for (AArmyUnit* Unit : Groups[GroupIndex]->GetUnits())
			Positions[GroupIndex].Add(Unit->GetActorLocation());
	}

	void SetStage(EStage Next, double Now)
	{
		Stage = Next;
		StageStarted = Now;
	}

	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Groups[2];
	TWeakObjectPtr<AArmyGroup> Enemy;
	TArray<FVector> Goals[2];
	TArray<FVector> Positions[2];
	TSet<AArmyUnit*> WentAroundObstacle;
	FBox Obstacle = FBox(ForceInit);
	FVector TravelAxis = FVector::ForwardVector;
	FVector SideAxis = FVector::RightVector;
	FVector WallProbe = FVector::ZeroVector;
	double TravelExtent = 0.;
	double SideExtent = 0.;
	double GroundHeight = 0.;
	int32 EligibleObstacles = 0;
	int32 ClearCircuits = 0;
	EStage Stage = EStage::FindGroups;
	int32 ReplacementCount = 0;
	bool bFailed = false;
	bool bIsolated = false;
	double Started;
	double StageStarted = 0.;
};

bool FArmyTwoGroupsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyTwoGroupsScenario(this));
	return true;
}

#endif
