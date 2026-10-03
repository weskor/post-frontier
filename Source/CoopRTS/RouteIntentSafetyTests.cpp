#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "AIController.h"
#include "CapturePoint.h"
#include "Commands/OrderGraph.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRouteIntentSafetyTest, "CoopRTS.Forces.RouteIntent.Safety",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Explicit encounter and inactive producer, not a paid-production test. Frozen
// members let the executor's accepted navigation endpoints drive arrival checks.
class FRouteIntentSafetyScenario : public IAutomationLatentCommand
{
public:
	explicit FRouteIntentSafetyScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	~FRouteIntentSafetyScenario()
	{
		for (const auto& Saved : Captures)
			if (Saved.Key.IsValid())
				Saved.Key->ControllingTeam = Saved.Value;
		if (Force.IsValid())
			Force->Destroy();
		if (Producer.IsValid())
			Producer->Destroy();
		if (Target.IsValid())
			Target->Destroy();
	}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 35.)
		{
			Test->AddError(TEXT("Safety route fixture did not become navigation-ready"));
			return true;
		}
		if (!Force.IsValid())
			return Prepare();
		if (!ArmyTestSetup::NavigationReady(Force->GetWorld()))
			return false;
		CheckRetreat();
		CheckQueue();
		CheckWithdrawal();
		return true;
	}
private:
	ACommandBuilding* Building(int32 Team, int32 Region)
	{
		const FTransform Transform(State->GetRegionAnchor(Region) + FVector(600., 0., 5.));
		ACommandBuilding* Result = State->GetWorld()->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Result)
			return nullptr;
		Result->BuildingIndex = ArmyTestSetup::BarracksIndex;
		Result->TeamIndex = Team;
		Result->OwningPlayerState = Team == 0 ? Wallet : State->EnemyCommander.Get();
		Result->ConstructionProgress = 1.f;
		Result->FinishSpawning(Transform);
		Result->SetActorTickEnabled(false);
		return Result;
	}
	bool Prepare()
	{
		UWorld* World = ArmyTestSetup::World();
		if (!World || !ArmyTestSetup::CombatActors(World) || !ArmyTestSetup::NavigationReady(World))
			return false;
		State = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		Wallet = PC->GetPlayerState<ACommandPlayerState>();
		Home = ForceOrderGraph::TeamMain(*State, 0);
		const AMapRegion* Main = ForceOrderGraph::Region(*State, Home);
		for (const AMapRegion* Region : State->Regions)
			if (Region->HomeTeam < 0 && Region->Anchor)
			{
				Captures.Emplace(Region->Anchor.Get(), Region->Anchor->ControllingTeam);
				bool bOccupied = false;
				for (TActorIterator<AArmyUnit> It(World); It; ++It)
					if (It->GetTeamIndex() == 5 && It->IsAlive() && Region->Contains(It->GetActorLocation()))
					{
						bOccupied = true;
						break;
					}
				if (bOccupied)
					continue;
				if (!Main->Neighbours.Contains(Region->RegionIndex))
					Far = Region->RegionIndex;
				else
					Near = Region->RegionIndex;
			}
		if (!Test->TestTrue(TEXT("Real map offers a neutral non-adjacent safety trip"), Far != INDEX_NONE && Near != INDEX_NONE))
			return true;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (It->GetTeamIndex() == 5 && !It->GetUnits().IsEmpty())
			{
				Shooter = It->GetUnits()[0];
				It->TickOrders();
				Test->TestTrue(TEXT("JEV does not publish human intent routes"), It->GetIntentRoutes().IsEmpty());
			}
		Producer = Building(0, Home);
		Target = Building(5, ForceOrderGraph::TeamMain(*State, 5));
		if (!Test->TestTrue(TEXT("Inactive real producer and hostile structure fixtures spawn"), Producer.IsValid() && Target.IsValid() && Shooter.IsValid()))
			return true;
		const FTransform Transform(State->GetRegionAnchor(Far) + FVector(0., 0., 100.));
		Force = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			PC, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Test->TestTrue(TEXT("Safety encounter force spawns"), Force.IsValid()))
			return true;
		Force->Initialize({ 0, Wallet, -1, Producer.Get(), Transform.GetLocation() });
		Force->FinishSpawning(Transform);
		Force->SetActorTickEnabled(false);
		if (!Test->TestTrue(TEXT("Safety encounter has six registered members"), Force->SpawnUnits() && Force->GetJoinedCount() == 6))
			return true;
		for (AArmyUnit* Unit : Force->GetUnits())
		{
			Unit->SetActorTickEnabled(false);
			Unit->GetCharacterMovement()->SetComponentTickEnabled(false);
		}
		return false;
	}
	void Control(int32 Team)
	{
		for (const auto& Saved : Captures)
			if (Saved.Key.IsValid())
				Saved.Key->ControllingTeam = Team;
	}
	bool Issue(EForceVerb Verb, int32 Region = INDEX_NONE, AActor* Structure = nullptr, bool bQueue = false)
	{
		return Test->TestTrue(TEXT("Owner command accepted in safety fixture"),
			FCommandService::IssueForceOrder(Wallet, Force.Get(), Verb, Region, Structure, bQueue).IsAccepted());
	}
	void Position(int32 Region)
	{
		const FVector Shift = State->GetRegionAnchor(Region) + FVector(0., 0., 100.) - Force->GetCenter();
		for (AArmyUnit* Unit : Force->GetUnits())
			Unit->SetActorLocation(Unit->GetActorLocation() + Shift, false, nullptr, ETeleportType::TeleportPhysics);
	}
	bool Arrive()
	{
		for (AArmyUnit* Unit : Force->GetUnits())
		{
			AAIController* AI = Cast<AAIController>(Unit->GetController());
			const FNavPathSharedPtr Path = AI ? AI->GetPathFollowingComponent()->GetPath() : nullptr;
			if (!Test->TestTrue(TEXT("Executor accepted a complete member path"), Path.IsValid() && !Path->GetPathPoints().IsEmpty()))
				return false;
			Unit->SetActorLocation(Path->GetPathPoints().Last().Location, false, nullptr, ETeleportType::TeleportPhysics);
		}
		Force->TickOrders();
		return true;
	}
	void DirectLeg(int32 Source, int32 Destination)
	{
		const auto& Routes = Force->GetIntentRoutes();
		if (!Test->TestTrue(TEXT("Safety executor publishes an active leg"), !Routes.IsEmpty()))
			return;
		const auto& Regions = Routes[0].Regions;
		if (!Test->TestEqual(TEXT("Non-adjacent safety travel has only its two direct endpoints"), Regions.Num(), 2))
			return;
		Test->TestEqual(TEXT("Safety line starts at physical source"), Regions[0], Source);
		Test->TestEqual(TEXT("Safety line ends at actual executor waypoint"), Regions[1], Force->WaypointRegionIndex);
		Test->TestEqual(TEXT("Executor travels directly to selected safe region"), Force->WaypointRegionIndex, Destination);
	}
	void CheckRetreat()
	{
		Control(-1);
		Force->RetreatThreshold = ERetreatThreshold::Never;
		if (!Issue(EForceVerb::Retreat))
			return;
		DirectLeg(Far, Home);
	}
	void CheckQueue()
	{
		Control(0);
		if (!Issue(EForceVerb::MoveHold, Far) || !Arrive())
			return;
		Test->TestEqual(TEXT("Executor seeds last-held region by reaching MoveHold"), Force->Status, EForceStatus::Holding);
		if (!Issue(EForceVerb::MoveHold, Near) || !Issue(EForceVerb::Retreat, INDEX_NONE, nullptr, true))
			return;
		const auto& Routes = Force->GetIntentRoutes();
		if (!Test->TestEqual(TEXT("MoveHold and queued Retreat publish separate legs"), Routes.Num(), 2))
			return;
		Test->TestEqual(TEXT("Queued Retreat predicts the newly held region, not old LastHeld"), Routes[1].Regions.Num(), 1);
		Test->TestEqual(TEXT("Queued Retreat prediction ends at preceding MoveHold target"), Routes[1].Regions.Last(), Near);
		if (!Arrive())
			return;
		Test->TestEqual(TEXT("Executor advances to queued Retreat"), Force->Verb, EForceVerb::Retreat);
		Test->TestEqual(TEXT("Actual queued Retreat waypoint equals predicted new LastHeld"), Force->WaypointRegionIndex, Near);
	}
	void CheckWithdrawal()
	{
		Control(-1);
		Position(Far);
		Force->RetreatThreshold = ERetreatThreshold::Percent40;
		if (!Issue(EForceVerb::Attack, INDEX_NONE, Target.Get()) || !Issue(EForceVerb::MoveHold, Far, nullptr, true))
			return;
		while (Force->GetAliveCount() > 2)
		{
			AArmyUnit* Victim = Force->GetUnits()[0];
			Victim->ReceiveAttack(Victim->GetHealth(), Shooter.Get());
		}
		Force->TickOrders();
		Test->TestEqual(TEXT("Casualties enter real executor withdrawal"), Force->Status, EForceStatus::Withdrawing);
		DirectLeg(Far, Home);
		const auto& Recovery = Force->GetIntentRoutes();
		if (!Test->TestEqual(TEXT("Recovery publishes safety, retained Attack and queued order"), Recovery.Num(), 3))
			return;
		Test->TestEqual(TEXT("Retained Attack keeps its active-order index"), Recovery[1].OrderIndex, 0);
		Test->TestEqual(TEXT("Retained Attack resumes from safety"), Recovery[1].Regions[0], Force->WaypointRegionIndex);
		Test->TestEqual(TEXT("Retained Attack reaches the live executor target"), Recovery[1].Regions.Last(), Force->TargetRegionIndex);
		Target->ReceiveAttack(Target->Health, Force->GetUnits()[0]);
		Force->TickOrders();
		CheckDroppedAttack(TEXT("Completed target omits dead Attack recovery"));
		if (!Arrive())
			return;
		Test->TestEqual(TEXT("Completed Attack advances at safety to queued MoveHold"), Force->Verb, EForceVerb::MoveHold);
		Position(Far);
		if (!Issue(EForceVerb::Attack, INDEX_NONE, State->EnemyHeadquarters) || !Issue(EForceVerb::MoveHold, Far, nullptr, true))
			return;
		Force->DetachProducer();
		Force->TickOrders();
		CheckDroppedAttack(TEXT("Orphan omits Attack it cannot resume"));
		if (Arrive())
			Test->TestEqual(TEXT("Orphan advances at safety to queued MoveHold"), Force->Verb, EForceVerb::MoveHold);
	}
	void CheckDroppedAttack(const TCHAR* Message)
	{
		const auto& Routes = Force->GetIntentRoutes();
		if (!Test->TestEqual(Message, Routes.Num(), 2))
			return;
		DirectLeg(Far, Home);
		Test->TestEqual(TEXT("Queued order replaces dropped recovery leg"), Routes[1].OrderIndex, 1);
		Test->TestEqual(TEXT("Queued leg starts at safety after dropped Attack"), Routes[1].Regions[0], Force->WaypointRegionIndex);
		Test->TestEqual(TEXT("Queued leg retains its own target"), Routes[1].Regions.Last(), Far);
	}
	FAutomationTestBase* Test;
	double Started;
	ACommandGameState* State = nullptr;
	ACommandPlayerState* Wallet = nullptr;
	TWeakObjectPtr<AArmyGroup> Force;
	TWeakObjectPtr<AArmyUnit> Shooter;
	TWeakObjectPtr<ACommandBuilding> Producer, Target;
	TArray<TPair<TWeakObjectPtr<ACapturePoint>, int32>> Captures;
	int32 Home = INDEX_NONE, Far = INDEX_NONE, Near = INDEX_NONE;
};

bool FRouteIntentSafetyTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRouteIntentSafetyScenario(this));
	return true;
}
#endif
