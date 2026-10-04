#pragma once
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandCamera.h"
#include "MapRegion.h"
#include "DepositSite.h"
#include "ObjectiveAnnouncer.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "HAL/PlatformTime.h"

// A fresh standalone world owns the map; explicit fixtures own every attack and
// occupancy transition. JEV and paid production cannot contribute extra events.
class FObjectiveEventsScenario : public IAutomationLatentCommand
{
public:
	explicit FObjectiveEventsScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override;

private:
	bool Stage0(UWorld* World, double GameNow);
	bool Stage1(double GameNow);
	bool Stage2(double GameNow);
	bool Stage3(double GameNow);
	bool Stage4();
	bool FinishNavigation(int32 OlderSequence);
	bool Produce(UWorld* World);
	bool ProduceHQ(UWorld* World, ACommandPlayerState* Owner, ACommandPlayerState* OtherOwner,
		AArmyGroup* Friendly, AArmyGroup* Enemy, AArmyGroup* Teammate, int32 OrphanNumber);
	bool FirstHitPriority(UWorld* World, AHeadquarters* OwnHQ, AArmyUnit* EnemyHit);
	bool InterleavedForces(UWorld* World, AHeadquarters* EnemyHQ, AArmyGroup* Friendly,
		AArmyGroup* Teammate, AArmyUnit* HumanHit, int32 OrphanNumber);
	bool Capture(ACommandPlayerState* Owner, ACommandPlayerState* OtherOwner, AArmyGroup* Friendly,
		AArmyGroup* Enemy, AArmyGroup* Teammate, int32 OrphanNumber);
	bool Rigs(UWorld* World, AArmyUnit* HumanHit, AArmyUnit* EnemyHit, AArmyUnit* DeadHit);
	bool Check(bool Value, const TCHAR* Message) { return Test->TestTrue(Message, Value); }
	void SetStage(int32 Value, double GameNow)
	{
		Stage = Value;
		StageStarted = GameNow;
	}
	void Press(FKey Key, bool bPressed)
	{
		FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
		Controller->InputKey(FInputKeyEventArgs(Viewport, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),
			Key, bPressed ? IE_Pressed : IE_Released, FPlatformTime::Cycles64()));
	}
	bool At(FVector Target, const TCHAR* Message)
	{
		Target.X = FMath::Clamp(Target.X, -State->Arena->HalfExtent.X, State->Arena->HalfExtent.X);
		Target.Y = FMath::Clamp(Target.Y, -State->Arena->HalfExtent.Y, State->Arena->HalfExtent.Y);
		Target.Z = 0.;
		return Check(Camera->GetActorLocation().Equals(Target, 1.f), Message);
	}
	int32 Count(FName Id) const
	{
		int32 Found = 0;
		for (const FObjectiveEvent& Event : Announcer->GetEvents())
			if (Event.Id == Id)
				++Found;
		return Found;
	}
	ACommandBuilding* Building(UWorld* World, int32 Team, int32 Index, const FVector& Location, float Progress = 1.f)
	{
		const FTransform Transform(Location);
		ACommandBuilding* Result = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			Team == 0 ? Controller.Get() : nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Result)
		{
			Result->BuildingIndex = Index;
			Result->TeamIndex = Team;
			Result->OwningPlayerState = Team == 0 ? Controller->GetPlayerState<ACommandPlayerState>() : State->EnemyCommander.Get();
			Result->ConstructionProgress = Progress;
			Result->FinishSpawning(Transform);
			Result->SetActorTickEnabled(false);
		}
		return Result;
	}
	void Freeze(AArmyGroup* Group)
	{
		Group->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Group->GetUnits())
		{
			Unit->SetActorTickEnabled(false);
			if (AController* AI = Unit->GetController())
				AI->SetActorTickEnabled(false);
		}
	}
	void Place(AArmyGroup* Group, const FVector& Location)
	{
		for (AArmyUnit* Unit : Group->GetUnits())
			Unit->SetActorLocation(Location + FVector(0.f, Unit->GetCompositionSlot() * 20.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	bool Attribution(const FObjectiveEvent& Event, const AArmyUnit* Attacker, const FVector& Location)
	{
		const AArmyGroup* Group = Attacker->GetGroup();
		const ACommandPlayerState* Owner = Group ? Group->GetOwningPlayerState() : nullptr;
		const AMapRegion* Region = State->FindRegionAt(Location);
		return Check(Owner && Event.Forces.Num() == 1 && Event.Forces[0].TeamIndex == Attacker->GetTeamIndex()
					   && Event.Forces[0].CommanderIndex == Owner->CommanderIndex && Event.Forces[0].ForceNumber == Group->ForceNumber
					   && Event.Forces[0].PlayerName == (Attacker->GetTeamIndex() == 5 ? TEXT("JEV") : Owner->GetPlayerName())
					   && Event.Forces[0].UnitIndex == Attacker->GetUnitIndex(),
				   TEXT("Attack event attributes the actual impacting player's stable force and unit"))
			&& Check(Region && Event.RegionIndex == Region->RegionIndex && Event.RegionName == Region->DisplayName.ToString()
					&& Event.Location.Equals(Location),
				TEXT("Attack event carries the real map-derived region and position"));
	}

	bool Hit(AHeadquarters* HQ, AArmyUnit* Attacker, int32 Damage, FName Id = NAME_None, int32 Tier = 0)
	{
		const int32 Before = Announcer->GetEvents().Num();
		const int32 Health = HQ->Health;
		// The first time a match HQ goes offline it also announces its emergency force, right after.
		const bool bOffline = Id == TEXT("own_hq_offline") || Id == TEXT("enemy_hq_offline");
		const bool bMatchHQ = State->FriendlyHeadquarters == HQ || State->EnemyHeadquarters == HQ;
		const int32 Raised = Id.IsNone() ? 0 : bOffline && bMatchHQ ? 2 : 1;
		HQ->ReceiveAttack(Damage, Attacker);
		if (!Check(HQ->Health == FMath::Max(0, Health - Damage) && Announcer->GetEvents().Num() == Before + Raised,
				TEXT("A valid HQ hit applies damage and emits only its most urgent event, or suppresses the known force")))
			return false;
		if (Id.IsNone())
			return true;
		const FObjectiveEvent& Event = Announcer->GetEvents()[Before];
		if (Raised == 2 && !Check(Announcer->GetEvents().Last().Id == (HQ->TeamIndex == 0 ? TEXT("own_emergency") : TEXT("enemy_emergency"))
							&& Announcer->GetEvents().Last().AffectedTeam == HQ->TeamIndex,
				TEXT("The first offline transition announces its emergency force to all")))
			return false;
		return Check(Event.Id == Id && Event.DamageTier == Tier && Event.AffectedTeam == HQ->TeamIndex,
				   TEXT("HQ event preserves resulting damage tier and affected team with the expected priority"))
			&& Attribution(Event, Attacker, HQ->GetActorLocation());
	}
	bool SequentialHQ(AHeadquarters* HQ, AArmyUnit* Attacker)
	{
		const bool bOwn = HQ->TeamIndex == 0;
		return Hit(HQ, Attacker, 1, bOwn ? TEXT("own_hq_under_attack") : TEXT("enemy_hq_under_attack"))
			&& Hit(HQ, Attacker, 1)
			&& Hit(HQ, Attacker, HQ->Health - HQ->MaxHealth() / 2 - 1)
			&& Hit(HQ, Attacker, 1, bOwn ? TEXT("own_hq_half") : TEXT("enemy_hq_half"), 1)
			&& Hit(HQ, Attacker, 1)
			&& Hit(HQ, Attacker, HQ->Health - HQ->MaxHealth() / 4 - 1)
			&& Hit(HQ, Attacker, 1, bOwn ? TEXT("own_hq_critical") : TEXT("enemy_hq_critical"), 2)
			&& Hit(HQ, Attacker, 1)
			&& Hit(HQ, Attacker, HQ->Health, bOwn ? TEXT("own_hq_offline") : TEXT("enemy_hq_offline"), 2)
			&& Hit(HQ, Attacker, 1);
	}

	FAutomationTestBase* Test;
	double Started;
	double StageStarted = 0.;
	int32 Stage = 0;
	bool bIsolated = false;
	TWeakObjectPtr<ACommandGameState> State;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<ACommandCamera> Camera;
	TWeakObjectPtr<UObjectiveAnnouncer> Announcer;
	TWeakObjectPtr<ACommandBuilding> Selected;
};
#endif
