#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "Commands/CommandService.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandCamera.h"
#include "MapRegion.h"
#include "DepositSite.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/AnnouncerPolicy.h"
#include "Sound/SoundWave.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveEventsTest, "CoopRTS.Objectives.Events",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveAssetsTest, "CoopRTS.Objectives.Assets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FObjectiveAssetsTest::RunTest(const FString& Parameters)
{
	for (const AnnouncerPolicy::FDefinition& Definition : AnnouncerPolicy::Definitions())
	{
		const FString Path = FString::Printf(TEXT("/Game/Audio/Announcer/VO_%s.VO_%s"), Definition.Id, Definition.Id);
		USoundWave* Voice = LoadObject<USoundWave>(nullptr, *Path);
		if (TestNotNull(FString::Printf(TEXT("Voice exists for %s"), Definition.Id), Voice))
			TestTrue(FString::Printf(TEXT("Voice for %s has playable duration"), Definition.Id), Voice->GetDuration() > 0.f);
	}
	return true;
}

// A fresh standalone world owns the map; explicit fixtures own every attack and
// occupancy transition. JEV and paid production cannot contribute extra events.
class FObjectiveEventsScenario : public IAutomationLatentCommand
{
public:
	explicit FObjectiveEventsScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		UWorld* World = ArmyTestSetup::World();
		const double Now = FPlatformTime::Seconds();
		if (World && !bIsolated)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				It->bProductionEnabled = false;
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<ACapturePoint> It(World); It; ++It)
				It->SetActorTickEnabled(false);
			bIsolated = true;
		}
		if (Now - Started > 35.)
		{
			Test->AddError(FString::Printf(TEXT("Objective scenario timed out at stage %d"), Stage));
			return true;
		}
		if (Stage == 0)
		{
			if (!World || Now - Started < 3.)
				return false;
			State = World->GetGameState<ACommandGameState>();
			Controller = ArmyTestSetup::Controller(World);
			Camera = Controller.IsValid() ? Cast<ACommandCamera>(Controller->GetPawn()) : nullptr;
			if (!ArmyTestSetup::MapReady(State.Get()) || !Camera.IsValid() || !Controller->GetPlayerState<ACommandPlayerState>()
				|| Controller->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0 || State->CaptureSites.IsEmpty())
				return false;
			Announcer = UObjectiveAnnouncer::Get(State.Get());
			if (!Check(Announcer.IsValid(), TEXT("Map exposes the replicated objective history")))
				return true;
			const FVector BeforeEvents = Camera->GetActorLocation();
			if (!Produce(World) || !Check(Camera->GetActorLocation().Equals(BeforeEvents), TEXT("Receiving all objective events never moves the camera")))
				return true;
			Selected = Building(World, 0, ArmyTestSetup::BarracksIndex,
				ArmyTestSetup::FromFriendlyHQ(State.Get(), -600.f, -600.f, 5.f));
			if (!Check(Selected.IsValid(), TEXT("Owned selected-building fixture exists")))
				return true;
			Camera->FocusOn(State->EnemyHeadquarters->GetActorLocation());
			const FVector BeforeSelection = Camera->GetActorLocation();
			Controller->SelectActor(Selected.Get());
			if (!Check(Controller->GetSelectedBuilding() == Selected.Get() && Camera->GetActorLocation().Equals(BeforeSelection),
					TEXT("Selecting a building changes selection without camera movement")))
				return true;
			Press(EKeys::F, true);
			SetStage(1, Now);
			return false;
		}
		if (Now - StageStarted < .15)
			return false; // Real Enhanced Input must process the pressed key in a world frame.
		if (Stage == 1)
		{
			Press(EKeys::F, false);
			if (!At(Selected->GetActorLocation(), TEXT("Real F input focuses the selected building")))
				return true;
			Press(EKeys::SpaceBar, true);
			SetStage(2, Now);
			return false;
		}
		if (Stage == 2)
		{
			Press(EKeys::SpaceBar, false);
			const FObjectiveEvent& Latest = Announcer->GetEvents().Last();
			if (!Check(Controller->GetFocusedAlertSequence() == Latest.Sequence, TEXT("Real Space input focuses newest objective"))
				|| !At(Latest.Location, TEXT("Space camera reaches latest objective location")))
				return true;
			SetStage(3, Now); // Release receives its own frame before the next press.
			return false;
		}
		if (Stage == 3)
		{
			Press(EKeys::SpaceBar, true);
			SetStage(4, Now);
			return false;
		}
		if (Stage == 4)
		{
			Press(EKeys::SpaceBar, false);
			const auto Events = Announcer->GetEvents();
			const FObjectiveEvent& Older = Events[Events.Num() - 2];
			if (!Check(Controller->GetFocusedAlertSequence() == Older.Sequence, TEXT("Second real Space input steps back one objective"))
				|| !At(Older.Location, TEXT("Older objective has its own camera location")))
				return true;
			if (!Check(Controller->FocusAlertSequence(Events[0].Sequence), TEXT("Exact-sequence navigation accepts retained history")))
				return true;
			Controller->FocusAlert();
			Controller->FocusAlert();
			if (!Check(Controller->GetFocusedAlertSequence() == Events[0].Sequence, TEXT("Navigation clamps at oldest retained objective"))
				|| !At(Events[0].Location, TEXT("Clamped navigation stays at oldest location")))
				return true;
			const FVector BeforeInvalid = Camera->GetActorLocation();
			if (!Check(!Controller->FocusAlertSequence(-1) && Camera->GetActorLocation().Equals(BeforeInvalid),
					TEXT("Missing sequence cannot move the camera")))
				return true;
			const int32 OlderSequence = Older.Sequence;
			if (!Check(Controller->FocusAlertSequence(OlderSequence), TEXT("Exact-sequence navigation accepts an older retained alert"))
				|| !At(Older.Location, TEXT("Exact-sequence camera navigation reaches the older alert")))
				return true;
			Controller->FocusAlert();
			if (!Check(Controller->GetFocusedAlertSequence() == Events[Events.Num() - 3].Sequence, TEXT("Navigation continues older from an exact-sequence cursor")))
				return true;
			const FVector BeforeNew = Camera->GetActorLocation();
			Announcer->Raise(TEXT("region_captured"), 0, State->FriendlyHeadquarters->GetActorLocation(), {});
			if (!Check(Camera->GetActorLocation().Equals(BeforeNew), TEXT("New event does not auto-focus from an older cursor")))
				return true;
			Controller->FocusAlert();
			if (!Check(Controller->GetFocusedAlertSequence() == Announcer->GetEvents().Last().Sequence, TEXT("New objective resets navigation to newest"))
				|| !At(Announcer->GetEvents().Last().Location, TEXT("Reset reaches new objective")))
				return true;
			const int32 FirstRetained = Announcer->GetEvents()[0].Sequence;
			const int32 FillCount = UObjectiveAnnouncer::HistoryLimit - Announcer->GetEvents().Num();
			for (int32 Index = 0; Index < FillCount; ++Index)
				Announcer->Raise(TEXT("region_captured"), 0, State->FriendlyHeadquarters->GetActorLocation(), {});
			if (!Check(Announcer->GetEvents().Num() == UObjectiveAnnouncer::HistoryLimit
						&& Announcer->GetEvents()[0].Sequence == FirstRetained,
					TEXT("Filling the ring does not evict its oldest event")))
				return true;
			const int32 Next = Announcer->GetEvents().Last().Sequence + 1;
			const int32 Added = 2 * UObjectiveAnnouncer::HistoryLimit + 3;
			for (int32 Index = 0; Index < Added; ++Index)
				Announcer->Raise(TEXT("region_captured"), 0, State->FriendlyHeadquarters->GetActorLocation(), {});
			const auto Bounded = Announcer->GetEvents();
			if (!Check(Bounded.Num() == UObjectiveAnnouncer::HistoryLimit
						&& Bounded[0].Sequence == Next + Added - UObjectiveAnnouncer::HistoryLimit,
					TEXT("Repeated ring wrap discards only oldest events while retaining bounded history")))
				return true;
			for (int32 Index = 1; Index < Bounded.Num(); ++Index)
				if (!Check(Bounded[Index].Sequence == Bounded[Index - 1].Sequence + 1, TEXT("Retained history sequences remain monotonic and contiguous")))
					return true;
			if (!Check(!Controller->FocusAlertSequence(OlderSequence), TEXT("Evicted sequence is no longer navigable")))
				return true;
			Test->AddInfo(TEXT("All 11 objective producers passed attribution/region/transition checks; real F/Space, selection stability, navigation and bounded history passed. Rendered feed clicks are covered by HUD verification."));
			return true;
		}
		return false;
	}

private:
	bool Check(bool Value, const TCHAR* Message) { return Test->TestTrue(Message, Value); }
	void SetStage(int32 Value, double Now)
	{
		Stage = Value;
		StageStarted = Now;
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
		HQ->ReceiveAttack(Damage, Attacker);
		if (!Check(HQ->Health == FMath::Max(0, Health - Damage)
					&& Announcer->GetEvents().Num() == Before + (Id.IsNone() ? 0 : 1),
				TEXT("A valid HQ hit applies damage and emits only its most urgent event, or suppresses the known force")))
			return false;
		if (Id.IsNone())
			return true;
		const FObjectiveEvent& Event = Announcer->GetEvents().Last();
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
	bool Produce(UWorld* World)
	{
		ACommandPlayerState* Owner = Controller->GetPlayerState<ACommandPlayerState>();
		const FVector Home = ArmyTestSetup::FromFriendlyHQ(State.Get(), 1200.f, 600.f, 100.f);
		ACommandBuilding* Producer = Building(World, 0, ArmyTestSetup::BarracksIndex, Home);
		if (!Check(Producer != nullptr, TEXT("Orphan attribution fixture has a real producer")))
			return false;
		const FTransform Transform(Home);
		AArmyGroup* Friendly = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			Controller.Get(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Friendly != nullptr, TEXT("Friendly force fixture spawns")))
			return false;
		Friendly->Initialize({ 0, Owner, 0, Producer, Home });
		Friendly->FinishSpawning(Transform);
		Producer->ForceGroup = Friendly;
		if (!Check(Friendly->SpawnUnits(), TEXT("Explicit friendly mixed-role members spawn")))
			return false;
		const int32 OrphanNumber = Friendly->ForceNumber;
		Producer->Destroy();
		if (!Check(!Friendly->GetProductionBuilding() && OrphanNumber > 0 && Friendly->ForceNumber == OrphanNumber,
				TEXT("Producer destruction leaves survivor force identity intact")))
			return false;
		AArmyGroup* Enemy = ArmyTestSetup::SpawnGroup(World, nullptr, -1, ArmyTestSetup::HostileStaging(State.Get()));
		ACommandPlayerState* OtherOwner = World->SpawnActor<ACommandPlayerState>();
		if (!Check(Enemy && OtherOwner, TEXT("Enemy and second player's fixtures exist")))
			return false;
		OtherOwner->CommanderIndex = (Owner->CommanderIndex + 1) % 5;
		OtherOwner->SetPlayerName(TEXT("Objective teammate"));
		OtherOwner->TeamIndex = 0;
		const FTransform OtherTransform(Home + FVector(0.f, 800.f, 0.f));
		AArmyGroup* Teammate = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), OtherTransform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Teammate != nullptr, TEXT("Second player's force fixture spawns")))
			return false;
		Teammate->Initialize({ 0, OtherOwner, 1, nullptr, OtherTransform.GetLocation() });
		Teammate->FinishSpawning(OtherTransform);
		if (!Check(Teammate->SpawnUnits(), TEXT("Second player's force members spawn")))
			return false;
		Teammate->ForceNumber = OrphanNumber; // Same number, different owner: two participating forces.
		Enemy->ForceNumber = 9;
		Freeze(Friendly);
		Freeze(Enemy);
		Freeze(Teammate);
		AArmyUnit* HumanHit = Friendly->GetUnits()[4];
		AArmyUnit* EnemyHit = Enemy->GetUnits()[4];
		AArmyUnit* DeadHit = Enemy->GetUnits().Last();
		DeadHit->ReceiveAttack(DeadHit->GetHealth(), HumanHit);
		AHeadquarters* OwnHQ = World->SpawnActor<AHeadquarters>(State->FriendlyHeadquarters->GetActorLocation(), FRotator::ZeroRotator);
		AHeadquarters* EnemyHQ = World->SpawnActorDeferred<AHeadquarters>(AHeadquarters::StaticClass(),
			FTransform(State->EnemyHeadquarters->GetActorLocation()), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(OwnHQ && EnemyHQ, TEXT("HQ fixtures use real map headquarters locations")))
			return false;
		EnemyHQ->TeamIndex = 5;
		EnemyHQ->FinishSpawning(FTransform(State->EnemyHeadquarters->GetActorLocation()));
		const int32 Initial = Announcer->GetEvents().Num();
		OwnHQ->ReceiveAttack(1, nullptr);
		OwnHQ->ReceiveAttack(1, HumanHit);
		OwnHQ->ReceiveAttack(1, DeadHit);
		OwnHQ->ReceiveAttack(0, EnemyHit);
		OwnHQ->ReceiveAttack(-1, EnemyHit);
		State->MatchResult = EMatchResult::Victory;
		OwnHQ->ReceiveAttack(1, EnemyHit);
		State->MatchResult = EMatchResult::Ongoing;
		if (!Check(Announcer->GetEvents().Num() == Initial && OwnHQ->Health == OwnHQ->MaxHealth(), TEXT("Invalid HQ attacks change neither health nor events")))
			return false;
		if (!SequentialHQ(OwnHQ, EnemyHit) || !SequentialHQ(EnemyHQ, HumanHit))
			return false;
		const TCHAR* HQIds[] = { TEXT("own_hq_under_attack"), TEXT("own_hq_half"), TEXT("own_hq_critical"), TEXT("own_hq_offline"),
			TEXT("enemy_hq_under_attack"), TEXT("enemy_hq_half"), TEXT("enemy_hq_critical"), TEXT("enemy_hq_offline") };
		for (const TCHAR* Id : HQIds)
			if (!Check(Count(Id) == 1, TEXT("Sequential HQ damage covers every own/enemy ID exactly once without tier-driven attack repeats")))
				return false;
		for (const int32 FirstTier : { 1, 2, 3 })
		{
			AHeadquarters* FreshHQ = World->SpawnActor<AHeadquarters>(OwnHQ->GetActorLocation(), FRotator::ZeroRotator);
			if (!Check(FreshHQ != nullptr, TEXT("First-hit priority fixture is a distinct HQ at the original location")))
				return false;
			const int32 Remaining = FirstTier == 1 ? FreshHQ->MaxHealth() / 2 : FirstTier == 2 ? FreshHQ->MaxHealth() / 4
																							   : 0;
			const FName Id = FirstTier == 1 ? TEXT("own_hq_half") : FirstTier == 2 ? TEXT("own_hq_critical")
																				   : TEXT("own_hq_offline");
			const int32 Damage = FreshHQ->Health - Remaining + (FirstTier == 3 ? 1 : 0);
			if (!Hit(FreshHQ, EnemyHit, Damage, Id, FMath::Min(FirstTier, 2))
				|| !Hit(FreshHQ, EnemyHit, 1))
				return false; // A threshold-only first hit records its force; lethal remains terminal.
			FreshHQ->Destroy();
		}
		AHeadquarters* OtherHQ = World->SpawnActorDeferred<AHeadquarters>(AHeadquarters::StaticClass(),
			FTransform(EnemyHQ->GetActorLocation()), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(OtherHQ != nullptr, TEXT("Interleaved-force fixture is a distinct enemy HQ at the original location")))
			return false;
		OtherHQ->TeamIndex = 5;
		OtherHQ->FinishSpawning(FTransform(EnemyHQ->GetActorLocation()));
		AArmyUnit* TeammateHit = Teammate->GetUnits()[4];
		if (!Hit(OtherHQ, HumanHit, 1, TEXT("enemy_hq_under_attack"))
			|| !Hit(OtherHQ, TeammateHit, 1, TEXT("enemy_hq_under_attack"))
			|| !Hit(OtherHQ, HumanHit, 1) || !Hit(OtherHQ, TeammateHit, 1))
			return false;
		++Teammate->ForceNumber; // A genuinely new force lands the threshold instead of an extra under_attack.
		if (!Hit(OtherHQ, TeammateHit, OtherHQ->Health - OtherHQ->MaxHealth() / 2, TEXT("enemy_hq_half"), 1)
			|| !Hit(OtherHQ, TeammateHit, 1) || !Hit(OtherHQ, HumanHit, 1))
			return false;
		++Friendly->ForceNumber;
		if (!Hit(OtherHQ, HumanHit, 1, TEXT("enemy_hq_under_attack"), 1) || !Hit(OtherHQ, HumanHit, 1))
			return false;
		Friendly->ForceNumber = OrphanNumber;
		OtherHQ->Destroy();
		Teammate->ForceNumber = OrphanNumber;
		ACapturePoint* Site = State->CaptureSites[0];
		const FVector FriendlyHQ = State->FriendlyHeadquarters->GetActorLocation();
		const FVector HostileHQ = State->EnemyHeadquarters->GetActorLocation();
		const FVector Away = FVector::DistSquared2D(FriendlyHQ, Site->GetActorLocation()) > FVector::DistSquared2D(HostileHQ, Site->GetActorLocation())
			? FriendlyHQ
			: HostileHQ;
		if (!Check(FVector::Dist2D(Away, Site->GetActorLocation()) > ACapturePoint::CaptureRadius + 200.f,
				TEXT("Map-derived staging lies safely outside capture radius")))
			return false;
		Site->ControllingTeam = -1;
		Site->CaptureProgress = 0.f;
		Place(Friendly, Site->GetActorLocation());
		Place(Teammate, Site->GetActorLocation());
		Place(Enemy, Site->GetActorLocation());
		Site->AdvanceCapture(8.f);
		if (!Check(Count(TEXT("region_captured")) == 0 && Site->CaptureProgress == 0.f, TEXT("Contested occupancy never captures or attributes an event")))
			return false;
		Place(Enemy, Away);
		Site->AdvanceCapture(8.f);
		Site->AdvanceCapture(8.f);
		const FObjectiveEvent Captured = Announcer->GetEvents().Last();
		const AMapRegion* Region = State->FindRegionAt(Site->GetActorLocation());
		if (!Check(Count(TEXT("region_captured")) == 1 && Captured.Id == TEXT("region_captured") && Captured.Forces.Num() == 2,
				TEXT("Uncontested capture emits once with all unique player forces, not one row per unit"))
			|| !Check(Region && Captured.RegionIndex == Region->RegionIndex && Captured.RegionName == Region->DisplayName.ToString(), TEXT("Capture identifies its map region")))
			return false;
		const int32 LowCommander = FMath::Min(Owner->CommanderIndex, OtherOwner->CommanderIndex);
		const int32 HighCommander = FMath::Max(Owner->CommanderIndex, OtherOwner->CommanderIndex);
		if (!Check(Captured.Forces[0].CommanderIndex == LowCommander && Captured.Forces[1].CommanderIndex == HighCommander
					&& Captured.Forces[0].ForceNumber == OrphanNumber && Captured.Forces[1].ForceNumber == OrphanNumber,
				TEXT("Capture contributors retain independent owners and deterministic force ordering")))
			return false;
		for (const FObjectiveForce& Force : Captured.Forces)
		{
			const ACommandPlayerState* Contributor = Force.CommanderIndex == Owner->CommanderIndex ? Owner : OtherOwner;
			if (!Check(Force.TeamIndex == 0 && Force.PlayerName == Contributor->GetPlayerName(),
					TEXT("Capture reports each participating player's identity")))
				return false;
		}
		Place(Friendly, Away);
		Place(Teammate, Away);
		Place(Enemy, Site->GetActorLocation());
		Site->AdvanceCapture(8.f);
		if (!Check(Site->ControllingTeam == -1 && Count(TEXT("region_lost")) == 1, TEXT("Losing friendly control emits at neutralization, not enemy completion"))
			|| !Attribution(Announcer->GetEvents().Last(), Enemy->GetUnits().Last(), Site->GetActorLocation()))
			return false;
		Site->AdvanceCapture(8.f);
		Site->AdvanceCapture(8.f);
		if (!Check(Site->ControllingTeam == 5 && Count(TEXT("region_lost")) == 1, TEXT("Enemy completion and occupied steady state do not repeat region loss")))
			return false;
		Place(Enemy, Away);
		Place(Friendly, Site->GetActorLocation());
		Site->AdvanceCapture(16.f);
		if (!Check(Count(TEXT("region_captured")) == 2 && Announcer->GetEvents().Last().Forces.Num() == 1, TEXT("A later recapture is a new transition with current contributors only")))
			return false;
		for (const int32 Team : { 0, 5 })
		{
			const FVector Location = Team == 0 ? State->Deposits[0]->GetActorLocation() : State->EnemyHeadquarters->GetActorLocation();
			ACommandBuilding* Rig = Building(World, Team, ArmyTestSetup::ExtractorIndex, Location, .5f);
			if (!Check(Rig && Rig->Kind == EBuildingKind::Extractor, TEXT("Drill Rig fixture resolves the live extractor definition")))
				return false;
			AArmyUnit* Attacker = Team == 0 ? EnemyHit : HumanHit;
			const int32 Before = Announcer->GetEvents().Num();
			Rig->ReceiveAttack(1, nullptr);
			Rig->ReceiveAttack(1, Team == 0 ? HumanHit : EnemyHit);
			Rig->ReceiveAttack(1, DeadHit);
			Rig->ReceiveAttack(0, Attacker);
			Rig->ReceiveAttack(-1, Attacker);
			State->MatchResult = EMatchResult::Defeat;
			Rig->ReceiveAttack(1, Attacker);
			State->MatchResult = EMatchResult::Ongoing;
			if (!Check(Announcer->GetEvents().Num() == Before && Rig->Health == Rig->MaxHealth(), TEXT("Invalid Drill Rig attacks emit nothing")))
				return false;
			Rig->ReceiveAttack(Rig->Health - 1, Attacker);
			if (!Check(Announcer->GetEvents().Num() == Before, TEXT("Nonlethal Drill Rig attack is not a loss")))
				return false;
			Rig->ReceiveAttack(1, Attacker);
			if (!Check(!Rig->IsAlive() && Rig->IsActorBeingDestroyed(), TEXT("Either team's lethal Drill Rig attack destroys the structure")))
				return false;
			if (Team == 0)
			{
				if (!Check(Announcer->GetEvents().Num() == Before + 1 && Announcer->GetEvents().Last().Id == TEXT("drill_rig_lost")
							&& Announcer->GetEvents().Last().AffectedTeam == 0,
						TEXT("Friendly lethal Drill Rig loss emits exactly once"))
					|| !Attribution(Announcer->GetEvents().Last(), Attacker, Location))
					return false;
			}
			else if (!Check(Announcer->GetEvents().Num() == Before, TEXT("JEV Drill Rig destruction raises no loss event")))
				return false;
			const int32 AfterRigDeath = Announcer->GetEvents().Num();
			Rig->ReceiveAttack(1, Attacker);
			if (!Check(Announcer->GetEvents().Num() == AfterRigDeath, TEXT("A destroyed Drill Rig cannot repeat its terminal transition")))
				return false;
			ACommandBuilding* Cancelled = Building(World, Team, ArmyTestSetup::ExtractorIndex, Location, .5f);
			ACommandBuilding* Cleaned = Building(World, Team, ArmyTestSetup::ExtractorIndex, Location);
			const int32 BeforeRemoval = Announcer->GetEvents().Num();
			if (!Check(Cancelled && Cleaned
						&& FCommandService::CancelBuilding(Cancelled->OwningPlayerState, Cancelled).IsAccepted(),
					TEXT("Incomplete Drill Rig can be cancelled")))
				return false;
			Cleaned->Destroy();
			if (!Check(Announcer->GetEvents().Num() == BeforeRemoval, TEXT("Cancellation and EndPlay cleanup are not Drill Rig combat losses")))
				return false;
		}
		return Check(Count(TEXT("drill_rig_lost")) == 1, TEXT("Only the friendly lethal Drill Rig transition remains in history"));
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

bool FObjectiveEventsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FObjectiveEventsScenario(this));
	return true;
}
#endif
