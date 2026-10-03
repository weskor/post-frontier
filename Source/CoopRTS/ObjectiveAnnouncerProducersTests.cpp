#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ObjectiveAnnouncerFixture.h"
#include "Commands/CommandService.h"

bool FObjectiveEventsScenario::Produce(UWorld* World)
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
	return ProduceHQ(World, Owner, OtherOwner, Friendly, Enemy, Teammate, OrphanNumber);
}

bool FObjectiveEventsScenario::ProduceHQ(UWorld* World, ACommandPlayerState* Owner, ACommandPlayerState* OtherOwner,
	AArmyGroup* Friendly, AArmyGroup* Enemy, AArmyGroup* Teammate, int32 OrphanNumber)
{
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
	if (!FirstHitPriority(World, OwnHQ, EnemyHit)
		|| !InterleavedForces(World, EnemyHQ, Friendly, Teammate, HumanHit, OrphanNumber)
		|| !Capture(Owner, OtherOwner, Friendly, Enemy, Teammate, OrphanNumber))
		return false;
	return Rigs(World, HumanHit, EnemyHit, DeadHit);
}

bool FObjectiveEventsScenario::FirstHitPriority(UWorld* World, AHeadquarters* OwnHQ, AArmyUnit* EnemyHit)
{
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
	return true;
}

bool FObjectiveEventsScenario::InterleavedForces(UWorld* World, AHeadquarters* EnemyHQ, AArmyGroup* Friendly,
	AArmyGroup* Teammate, AArmyUnit* HumanHit, int32 OrphanNumber)
{
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
	return true;
}

bool FObjectiveEventsScenario::Capture(ACommandPlayerState* Owner, ACommandPlayerState* OtherOwner, AArmyGroup* Friendly,
	AArmyGroup* Enemy, AArmyGroup* Teammate, int32 OrphanNumber)
{
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
	return true;
}

bool FObjectiveEventsScenario::Rigs(UWorld* World, AArmyUnit* HumanHit, AArmyUnit* EnemyHit, AArmyUnit* DeadHit)
{
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

#endif
