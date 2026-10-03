#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "AIController.h"
#include "HAL/PlatformTime.h"
#include "Navigation/PathFollowingComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Components/CapsuleComponent.h"

struct FArmyMovementTestAccess
{
	static bool Travel(AArmyGroup& Force, const FVector& Destination, bool bApply = true)
	{
		return Force.IssueTravel(EArmyOrder::Move, Destination, bApply);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyTwoGroupsTest, "CoopRTS.Movement.TwoGroups",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Real character paths between generated polygon anchors, not arbitrary ground orders.
class FArmyTwoGroupsScenario : public IAutomationLatentCommand
{
public:
	explicit FArmyTwoGroupsScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 90.)
		{
			for (const TWeakObjectPtr<AArmyGroup>& Force : Groups)
				if (Force.IsValid())
					for (const AArmyUnit* Unit : Force->GetUnits())
					{
						const UPathFollowingComponent* Path = Following(Unit);
						Test->AddInfo(FString::Printf(TEXT("stage=%d force=%s serial=%u status=%d destination=%s unit=%s position=%s path_status=%d"),
							Stage, *Force->GetName(), Force->OrderSerial, static_cast<int32>(Force->Status),
							*Force->Destination.ToString(), *Unit->GetName(), *Unit->GetActorLocation().ToString(),
							Path ? static_cast<int32>(Path->GetStatus()) : -1));
					}
			return Fail(TEXT("TwoGroups timed out before physical region arrival"));
		}
		if (!Groups[0].IsValid())
		{
			UWorld* World = ArmyTestSetup::World();
			ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
			Controller = World ? ArmyTestSetup::Controller(World) : nullptr;
			if (!ArmyTestSetup::MapReady(State) || !Controller.IsValid() || ArmyTestSetup::GameSeconds(World) < 3.
				|| !ArmyTestSetup::NavigationReady(World))
				return false;
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->IsOpposingArmy())
					It->Destroy();
			for (ACommandBuilding* Building : State->Buildings)
				if (IsValid(Building) && Building->TeamIndex == 5 && Building->IsProducer())
					FCommandService::ConfigureProduction(State->EnemyCommander, Building,
						Building->bForceConfigured ? Building->ProductionRole : static_cast<EUnitRole>(255), false);
			Home = ArmyTestSetup::RegionAt(State, State->FriendlyHeadquarters->GetActorLocation());
			const FVector Anchor = State->GetRegionAnchor(Home);
			Groups[0] = ArmyTestSetup::SpawnGroup(World, Controller.Get(), 0, Anchor + FVector(0.f, -350.f, 100.f));
			Groups[1] = ArmyTestSetup::SpawnGroup(World, Controller.Get(), 1, Anchor + FVector(0.f, 350.f, 100.f));
			if (!Groups[0].IsValid() || !Groups[1].IsValid())
				return Fail(TEXT("Both independently owned six-member fixtures must spawn"));
			Target = ArmyTestSetup::TravelRegion(Groups[0].Get(), State->EnemyHeadquarters->GetActorLocation());
			if (Target == INDEX_NONE)
				return Fail(TEXT("Generated home polygon needs a reachable non-enemy-main neighbour"));
			StageStarted = ArmyTestSetup::GameSeconds(World);
			return false;
		}
		if (!Groups[1].IsValid() || !Controller.IsValid())
			return Fail(TEXT("Both independent forces and their commander must survive"));
		const double Now = ArmyTestSetup::GameSeconds(Groups[0]->GetWorld());
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (Stage == 0)
		{
			if (Now - StageStarted < .5)
				return false; // Spawned AI controllers and dynamic navigation must settle.
			const ACommandGameState* State = Groups[0]->GetWorld()->GetGameState<ACommandGameState>();
			if (!FindObstacleCircuit(*State))
				return false; // The bounded deadline reports unavailable map/navigation fixtures.
			const EForceVerb OtherVerb = Groups[1]->Verb;
			const int32 OtherTarget = Groups[1]->TargetRegionIndex;
			if (!FCommandService::IssueForceOrder(Wallet, Groups[0].Get(), EForceVerb::MoveHold, Target))
				return false; // Navigation initialization can reject the first route.
			if (Groups[1]->Verb != OtherVerb || Groups[1]->TargetRegionIndex != OtherTarget)
				return Fail(TEXT("Ordering force zero must not change force one's intent"));
			if (!FCommandService::IssueForceOrder(Wallet, Groups[1].Get(), EForceVerb::MoveHold, Target))
				return false;
			Stage = 1;
			StageStarted = Now;
		}
		else if (Stage == 1 && Now - StageStarted >= .12)
		{
			const int32 Replacement = (++Replacements % 2) ? Home : Target;
			for (const TWeakObjectPtr<AArmyGroup>& Force : Groups)
				if (!FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, Replacement))
					return Fail(TEXT("Reachable region replacement must be accepted"));
			StageStarted = Now;
			if (Replacements == 8)
				Stage = 2;
		}
		else if (Stage == 2 && Now - StageStarted >= .5)
		{
			for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
			{
				AArmyGroup* Force = Groups[GroupIndex].Get();
				const uint32 Serial = Force->OrderSerial;
				const FVector Destination = Force->Destination;
				for (AArmyUnit* Unit : Force->GetUnits())
				{
					const UPathFollowingComponent* Path = Following(Unit);
					if (!Path || Path->GetStatus() != EPathFollowingStatus::Moving)
						return Fail(TEXT("Every member must have an active navigation request before rejection"));
					Requests[GroupIndex].Add(Path->GetCurrentRequestId());
					Positions[GroupIndex].Add(Unit->GetActorLocation());
				}
				if (FCommandService::IssueForceOrder(Wallet, Force, EForceVerb::MoveHold, ForceOrders::MaxRegions + 1)
					|| Force->OrderSerial != Serial || Force->TargetRegionIndex != Target || Force->Destination != Destination)
					return Fail(TEXT("Invalid region rejects atomically without replacing accepted travel"));
				if (!RejectObstacleDestinations(*Force))
					return true;
				for (int32 Index = 0; Index < Force->GetUnits().Num(); ++Index)
					if (Following(Force->GetUnits()[Index])->GetCurrentRequestId() != Requests[GroupIndex][Index])
						return Fail(TEXT("Rejected order must preserve every active character path request"));
			}
			Stage = 3;
			StageStarted = Now;
		}
		else if (Stage == 3 && Now - StageStarted >= .6)
		{
			for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
				for (int32 Index = 0; Index < Groups[GroupIndex]->GetUnits().Num(); ++Index)
					if (FVector::Dist2D(Groups[GroupIndex]->GetUnits()[Index]->GetActorLocation(), Positions[GroupIndex][Index]) < 20.f)
						return Fail(TEXT("Rejected intent must not stop any travelling character"));
			Stage = 4;
		}
		else if (Stage == 4)
		{
			const ACommandGameState* State = Groups[0]->GetWorld()->GetGameState<ACommandGameState>();
			for (const TWeakObjectPtr<AArmyGroup>& Force : Groups)
			{
				if (Force->GetUnits().Num() != 6)
					return Fail(TEXT("Navigation must preserve every member's identity"));
				if (!Force->IsHoldingRegion() || Force->TargetRegionIndex != Target
					|| Force->HoldRegionIndex != Target || Force->HoldPostIndex == INDEX_NONE || Force->bHoldResponding
					|| ArmyTestSetup::CurrentRegion(Force.Get()) != Target)
					return false;
				for (const AArmyUnit* Unit : Force->GetUnits())
				{
					if (Unit->GetGroup() != Force.Get())
						return Fail(TEXT("Independent forces cannot exchange members"));
					if (Unit->GetVelocity().Size2D() > 5.f || FVector::Dist2D(Unit->GetActorLocation(), Force->HoldPostLocation) > 500.f)
						return false;
				}
			}
			if (!PlaceCrossingGroups(*State))
				return Fail(TEXT("Obstacle crossing needs separate, navigable, collision-free starting formations"));
			for (const TWeakObjectPtr<AArmyGroup>& Force : Groups)
			{
				if (!FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, CrossingTarget))
					return Fail(TEXT("Obstacle crossing accepts a real region verb for each independent force"));
			}
			Stage = 5;
			StageStarted = Now;
		}
		else if (Stage == 5)
		{
			bool bArrived = true;
			for (const TWeakObjectPtr<AArmyGroup>& Force : Groups)
			{
				bArrived &= Force->IsHoldingRegion() && Force->TargetRegionIndex == CrossingTarget
					&& Force->HoldRegionIndex == CrossingTarget && Force->HoldPostIndex != INDEX_NONE && !Force->bHoldResponding
					&& ArmyTestSetup::CurrentRegion(Force.Get()) == CrossingTarget;
				for (AArmyUnit* Unit : Force->GetUnits())
				{
					const FVector Relative = Unit->GetActorLocation() - Obstacle.GetCenter();
					if (FMath::Abs(FVector::DotProduct(Relative, TravelAxis)) < TravelExtent - 50.f
						&& FMath::Abs(FVector::DotProduct(Relative, SideAxis)) > SideExtent)
						WentAroundObstacle.Add(Unit);
					const UPathFollowingComponent* Path = Following(Unit);
					bArrived &= Path && Path->GetStatus() == EPathFollowingStatus::Idle
						&& Unit->GetVelocity().Size2D() < 5.f
						&& FVector::Dist2D(Unit->GetActorLocation(), Force->HoldPostLocation) < 450.f;
				}
			}
			if (!bArrived)
				return false;
			for (const TWeakObjectPtr<AArmyGroup>& Force : Groups)
				for (AArmyUnit* Unit : Force->GetUnits())
					if (!WentAroundObstacle.Contains(Unit))
						return Fail(TEXT("Every real character must physically cross around the map obstacle, not only record a curved path"));
			Test->AddInfo(TEXT("TwoGroups: independent verbs, rapid replacement, near-wall/top rejection with uninterrupted paths, twelve region arrivals and physical obstacle crossings."));
			return true;
		}
		return false;
	}
private:
	static const UPathFollowingComponent* Following(const AArmyUnit* Unit)
	{
		const AAIController* AI = Cast<AAIController>(Unit->GetController());
		return AI ? AI->GetPathFollowingComponent() : nullptr;
	}
	bool PlaceCrossingGroups(const ACommandGameState& State)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State.GetWorld());
		if (!Navigation)
			return false;
		FVector Starts[2][6];
		for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
			for (int32 Index = 0; Index < Groups[GroupIndex]->GetUnits().Num(); ++Index)
			{
				const AArmyUnit* Unit = Groups[GroupIndex]->GetUnits()[Index];
				const FNavAgentProperties& Agent = Unit->GetNavAgentPropertiesRef();
				const ANavigationData* Data = Navigation->GetNavDataForProps(Agent, Unit->GetNavAgentLocation());
				const int32 Slot = Unit->GetCompositionSlot();
				const FVector Candidate = State.GetRegionAnchor(CrossingHome)
					+ FVector((1 - Slot / 2) * 220.f, (Slot % 2 ? 1.f : -1.f) * 140.f, 0.f)
					+ SideAxis * (GroupIndex == 0 ? -350.f : 350.f);
				FNavLocation Ground;
				if (!Data || !Navigation->ProjectPointToNavigation(Candidate, Ground, FVector(35.f, 35.f, 200.f), Data)
					|| FVector::Dist2D(Candidate, Ground.Location) > 35.f)
					return false;
				Starts[GroupIndex][Index] = Ground.Location
					+ FVector(0.f, 0.f, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f);
				if (State.GetWorld()->OverlapBlockingTestByChannel(Starts[GroupIndex][Index], FQuat::Identity, ECC_Pawn,
						FCollisionShape::MakeCapsule(Unit->GetSimpleCollisionRadius(), Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())))
					return false;
			}
		// Unswept teleports to identical slots stack opposing capsules above the
		// floor. Separate the forces and ground every start before issuing paths.
		for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
			for (int32 Index = 0; Index < Groups[GroupIndex]->GetUnits().Num(); ++Index)
				Groups[GroupIndex]->GetUnits()[Index]->SetActorLocation(Starts[GroupIndex][Index],
					false, nullptr, ETeleportType::TeleportPhysics);
		return true;
	}
	bool FindObstacleCircuit(const ACommandGameState& State)
	{
		for (TActorIterator<AStaticMeshActor> It(State.GetWorld()); It; ++It)
		{
			const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent();
			if (!Mesh || Mesh->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block
				|| Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
				continue;
			const FBox Box = Mesh->Bounds.GetBox();
			const FVector Extent = Box.GetExtent();
			if (Extent.X < 200.f || Extent.Y < 200.f || Extent.X > 2000.f || Extent.Y > 2000.f || Extent.Z < 150.f)
				continue;
			for (const AMapRegion* Source : State.Regions)
				for (const AMapRegion* End : State.Regions)
				{
					if (!IsValid(Source) || !IsValid(End) || !Source->Neighbours.Contains(End->RegionIndex)
						|| (Source->RegionRole == ERegionRole::Main && Source->HomeTeam != 0)
						|| (End->RegionRole == ERegionRole::Main && End->HomeTeam != 0))
						continue;
					const FVector Start = State.GetRegionAnchor(Source->RegionIndex) - Box.GetCenter();
					const FVector Finish = State.GetRegionAnchor(End->RegionIndex) - Box.GetCenter();
					for (int32 Axis = 0; Axis < 2; ++Axis)
					{
						const double From = Axis == 0 ? Start.X : Start.Y;
						const double To = Axis == 0 ? Finish.X : Finish.Y;
						const double AlongExtent = Axis == 0 ? Extent.X : Extent.Y;
						const double AcrossExtent = Axis == 0 ? Extent.Y : Extent.X;
						if (From >= -AlongExtent - 450.f || To <= AlongExtent + 450.f)
							continue;
						const FVector Intersection = FMath::Lerp(Start, Finish, -From / (To - From));
						const double Across = Axis == 0 ? Intersection.Y : Intersection.X;
						if (FMath::Abs(Across) > AcrossExtent - 150.f
							|| !FArmyMovementTestAccess::Travel(*Groups[0].Get(), State.GetRegionAnchor(Source->RegionIndex), false)
							|| !FArmyMovementTestAccess::Travel(*Groups[0].Get(), State.GetRegionAnchor(End->RegionIndex), false))
							continue;
						Obstacle = Box;
						TravelAxis = Axis == 0 ? FVector::ForwardVector : FVector::RightVector;
						SideAxis = Axis == 0 ? FVector::RightVector : FVector::ForwardVector;
						TravelExtent = AlongExtent;
						SideExtent = AcrossExtent;
						CrossingHome = Source->RegionIndex;
						CrossingTarget = End->RegionIndex;
						if (FindWallProbe(State))
							return true;
					}
				}
		}
		return false;
	}
	bool FindWallProbe(const ACommandGameState& State)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State.GetWorld());
		const AArmyUnit* Unit = Groups[0]->GetUnits()[0];
		const FNavAgentProperties& Agent = Unit->GetNavAgentPropertiesRef();
		const ANavigationData* Data = Navigation ? Navigation->GetNavDataForProps(Agent, Unit->GetNavAgentLocation()) : nullptr;
		if (!Data)
			return false;
		for (double Along = -TravelExtent - 100.f; Along < 0.f; Along += 50.f)
		{
			FVector Candidate = Obstacle.GetCenter() + TravelAxis * Along;
			Candidate.Z = State.GetRegionAnchor(CrossingHome).Z;
			FNavLocation Ground;
			if (!Navigation->ProjectPointToNavigation(Candidate, Ground, FVector(35.f, 35.f, 200.f), Data)
				|| FVector::Dist2D(Candidate, Ground.Location) > 35.f)
				continue;
			FPathFindingQuery Query(nullptr, *Data, Unit->GetNavAgentLocation(), Ground.Location);
			Query.SetAllowPartialPaths(false);
			const FPathFindingResult CenterPath = Navigation->FindPathSync(Agent, Query);
			if (!CenterPath.IsSuccessful() || !CenterPath.Path.IsValid() || CenterPath.Path->IsPartial()
				|| FArmyMovementTestAccess::Travel(*Groups[0].Get(), Ground.Location, false))
				continue;
			WallProbe = Ground.Location;
			return true;
		}
		return false;
	}
	bool RejectObstacleDestinations(AArmyGroup& Force)
	{
		FVector Top = Obstacle.GetCenter();
		Top.Z = Obstacle.Max.Z;
		for (const FVector Destination : { WallProbe, Top })
		{
			const uint32 Serial = Force.OrderSerial;
			const FVector Accepted = Force.Destination;
			const EArmyOrder Phase = Force.Order;
			TArray<FAIRequestID, TInlineAllocator<6>> Active;
			for (const AArmyUnit* Unit : Force.GetUnits())
				Active.Add(Following(Unit)->GetCurrentRequestId());
			if (FArmyMovementTestAccess::Travel(Force, Destination)
				|| Force.OrderSerial != Serial || Force.Destination != Accepted || Force.Order != Phase)
			{
				Fail(TEXT("Reachable-center near-wall and unreachable-top formations reject without mutating accepted travel"));
				return false;
			}
			for (int32 Index = 0; Index < Force.GetUnits().Num(); ++Index)
			{
				const UPathFollowingComponent* Path = Following(Force.GetUnits()[Index]);
				if (!Path || Path->GetCurrentRequestId() != Active[Index] || Path->GetStatus() != EPathFollowingStatus::Moving)
				{
					Fail(TEXT("Near-wall/top rejection preserves every active character path"));
					return false;
				}
			}
		}
		return true;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Groups[2];
	TArray<FVector> Positions[2];
	TArray<FAIRequestID> Requests[2];
	TSet<AArmyUnit*> WentAroundObstacle;
	FBox Obstacle = FBox(ForceInit);
	FVector TravelAxis = FVector::ForwardVector, SideAxis = FVector::RightVector, WallProbe = FVector::ZeroVector;
	double TravelExtent = 0., SideExtent = 0.;
	int32 CrossingHome = INDEX_NONE, CrossingTarget = INDEX_NONE;
	int32 Home = INDEX_NONE, Target = INDEX_NONE, Stage = 0, Replacements = 0;
	double Started, StageStarted = 0.;
};

bool FArmyTwoGroupsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyTwoGroupsScenario(this));
	return true;
}
#endif
