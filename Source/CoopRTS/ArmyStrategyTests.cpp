#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "DepositSite.h"
#include "MapRegion.h"
#include "Headquarters.h"
#include "HAL/PlatformTime.h"
#include "NavigationSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyConstructionTest, "CoopRTS.Enemy.ConstructionEconomy",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

class FEnemyConstructionScenario : public IAutomationLatentCommand
{
public:
	explicit FEnemyConstructionScenario(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (TimedStage != Stage)
		{
			TimedStage = Stage;
			StageStarted = Now;
		}
		if (Now - Started > 300.0 || Now - StageStarted > 90.0)
			return Fail(*FString::Printf(TEXT("Strategy stage %d exceeded its bounded progress deadline"), Stage));
		UWorld* World = ArmyTestSetup::World();
		if (!World || World->GetTimeSeconds() < 3.f)
			return false;
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		if (!State || !PC || !ArmyTestSetup::MapReady(State))
			return false;
		if (State->MatchResult != EMatchResult::Ongoing)
			return Fail(TEXT("Strategy fixture ended the match before its required economy/recovery states; cannot keep waiting"));
		if (World->GetTimeSeconds() >= NextProgress)
		{
			int32 Joined = 0, Travelling = 0;
			if (Production.IsValid())
				Production->GetForceCounts(Joined, Travelling);
			UE_LOG(LogTemp, Display, TEXT("Strategy progress stage=%d wallet=%d income=%d joined=%d travelling=%d forward=%d"),
				Stage, State->EnemyCommander ? State->EnemyCommander->Resources : -1, State->GetEnemyIncomePerSecond(),
				Joined, Travelling, ForwardProduction.IsValid());
			NextProgress = World->GetTimeSeconds() + 10.f;
		}
		AEnemyCommander* Planner = nullptr;
		for (TActorIterator<AEnemyCommander> It(World); It && !Planner; ++It)
			if (It->TeamIndex == 5)
				Planner = *It;
		if (!Planner)
			return Fail(TEXT("New match must create the economic enemy commander"));
		if (!IsValid(State->EnemyCommander) || State->EnemyCommander->CommanderIndex != -1
			|| State->EnemyCommander->TeamIndex != 5 || State->EnemyCommander->GetOwner()
			|| State->PlayerArray.Contains(State->EnemyCommander))
			return Fail(TEXT("Enemy wallet must be a controllerless team-5 commander outside the human roster"));
		if (Stage == 3)
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!Nav || Nav->IsNavigationBuildInProgress())
				return false;
			Planner->EvaluatePlan();
			const bool bSafe = InSafeRecovery(State);
			if (!bSafe && Recovery->Verb != EForceVerb::Retreat)
				return Fail(TEXT("Below-35-percent joined health must issue Retreat before recovery movement"));
			if (!bSafe)
				return false; // Await the real force tick applying its route after navigation becomes ready.
			SafeRecoveryRegion = Recovery->WaypointRegionIndex;
			if (Recovery->GetProductionBuilding() != Production.Get() || Production->ForceGroup != Recovery.Get())
				return Fail(TEXT("Retreat cannot detach or replace the injured force's owning producer"));
			if (OtherForce->Verb == EForceVerb::Retreat)
				return Fail(TEXT("Damage to the original producer cannot retreat the healthy forward producer"));
			Planner->SetActorTickEnabled(false); // Explicit evaluations below observe natural return and repair ticks.
			Stage = 4;
			return false;
		}
		if (Stage == 4)
		{
			if (!Recovery.IsValid() || !Production.IsValid() || Production->ForceGroup != Recovery.Get()
				|| Recovery->GetProductionBuilding() != Production.Get())
				return Fail(TEXT("Recovery must retain its living producer and stable force backlink"));
			float Health = 0.f;
			int32 Joined = 0;
			for (const TWeakObjectPtr<AArmyUnit>& Member : DamagedUnits)
			{
				const AArmyUnit* Unit = Member.Get();
				if (!IsValid(Unit) || !Unit->IsAlive() || Unit->IsReinforcing() || Unit->GetGroup() != Recovery.Get())
					return Fail(TEXT("Natural recovery must heal the same joined recruits, not replace or transfer them"));
				Health += float(Unit->GetHealth()) / Unit->MaxHealth();
				++Joined;
			}
			if (Recovery->Verb == EForceVerb::MoveHold && Recovery->Status == EForceStatus::Holding
				&& InSafeRecovery(State))
				bObservedSafeHold = true;
			Planner->EvaluatePlan();
			if (!OtherProduction.IsValid() || !OtherForce.IsValid() || OtherProduction->ForceGroup != OtherForce.Get()
				|| OtherForce->GetProductionBuilding() != OtherProduction.Get())
				return Fail(TEXT("Independent producer must retain its own living force during another producer's recovery"));
			if (OtherForce->Verb == EForceVerb::Retreat)
				return Fail(TEXT("Recovering one force cannot retreat an uninjured independent producer"));
			if (!Joined || Health / Joined < .8f)
			{
				if (!InSafeRecovery(State))
					return Fail(TEXT("Injured force must retain its controlled safe waypoint while retreating or physically held below 80 percent joined health"));
				return false;
			}
			if (!bObservedSafeHold || Health / Joined <= InitialDamagedHealth)
				return Fail(TEXT("Recovery must physically reach safe MoveHold and heal the same injured joined roster before resuming"));
			if ((Recovery->Verb != EForceVerb::MoveHold && Recovery->Verb != EForceVerb::Attack)
				|| Recovery->Orders.IsEmpty() || Recovery->WaypointRegionIndex == INDEX_NONE
				|| Recovery->TargetRegionIndex == SafeRecoveryRegion)
				return Fail(TEXT("Naturally healed joined recruits must resume accepted strategic travel beyond their recovery region"));
			Test->AddInfo(TEXT("Enemy proof: exact per-unit paid economy, real polygon capture/extractor, paid forward barracks construction, region expansion/nearest defense orders, JEV-only finite payments/depletion/freeing and producer-scoped natural repair recovery."));
			return true;
		}
		if (Stage == 0)
		{
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->TeamIndex == 5)
					It->Destroy();
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->GetTeamIndex() == 5)
					It->Destroy();
			State->bVerificationIncomePaused = true;
			State->EnemyCommander->Resources = 600;
			HumanBalance = PC->GetPlayerState<ACommandPlayerState>()->Resources;
			// HQ outcome is outside this economy/recovery fixture. Give the passive human
			// base an explicit survival budget without changing combat or AI behavior.
			State->FriendlyHeadquarters->Health = 1000000;
			// Controlled strategy fixture, not an unaided match: passive home guards
			// deny the empty-human strength advantage until forward investment is observed.
			const AMapRegion* HumanHome = State->FindRegionAt(State->FriendlyHeadquarters->GetActorLocation());
			for (int32 Index = 0; Index < 2; ++Index)
			{
				AArmyGroup* Guard = ArmyTestSetup::SpawnGroup(World, PC, 30 + Index,
					ArmyTestSetup::FromFriendlyHQ(State, -250.f, Index ? 350.f : -350.f, 100.f));
				if (!Guard)
					return Fail(TEXT("Passive human home-guard fixture could not spawn"));
				Guard->SetActorTickEnabled(false);
				for (AArmyUnit* Unit : Guard->GetUnits())
				{
					if (!HumanHome || State->FindRegionAt(Unit->GetActorLocation()) != HumanHome)
						return Fail(TEXT("Passive human guards must stay in their own main, outside captured forward regions"));
					Unit->SetActorTickEnabled(false);
				}
			}
			Planner->EvaluatePlan();
			for (ACommandBuilding* Building : State->Buildings)
				if (IsValid(Building) && Building->TeamIndex == 5 && Building->IsProducer())
					Production = Building;
			const int32 BarracksCost = State->Content->FindBuilding(TEXT("barracks"))->BuildCost;
			if (!Production.IsValid() || State->EnemyCommander->Resources != 600 - BarracksCost
				|| Production->OwningPlayerState != State->EnemyCommander || Production->IsComplete())
				return Fail(TEXT("Enemy must pay its own wallet for an unfinished barracks through shared construction"));
			Stage = 1;
			Test->AddInfo(TEXT("Enemy paid construction observed; waiting for normal construction and single-unit production ticks."));
			return false;
		}
		if (Stage == 1)
		{
			AArmyGroup* Produced = Production.IsValid() ? Production->ForceGroup.Get() : nullptr;
			if (!IsValid(Produced) || Produced->GetUnits().IsEmpty())
				return false;
			int32 Joined, Travelling;
			Production->GetForceCounts(Joined, Travelling);
			const int32 Count = Joined + Travelling;
			if (Count != 1 || !Production->bForceConfigured || Produced->GetProductionBuilding() != Production.Get()
				|| Produced->Verb != EForceVerb::MoveHold
				|| Produced->GetOwningPlayerState() != State->EnemyCommander
				|| State->EnemyCommander->Resources != 600 - State->Content->FindBuilding(TEXT("barracks"))->BuildCost - 20
				|| PC->GetPlayerState<ACommandPlayerState>()->Resources != HumanBalance)
				return Fail(TEXT("First enemy production must create one paid infantry unit, not a batch, in its producer force"));
			if (Produced->Verb != EForceVerb::MoveHold
				|| Produced->WaypointRegionIndex == INDEX_NONE
				|| State->GetRegionController(Produced->TargetRegionIndex) == 5)
				return Fail(TEXT("First paid force must expand toward an uncontrolled region through an anchor order waypoint"));
			Recovery = Produced;
			Stage = 2;
			EnemyBudget = 2000 + State->Content->FindBuilding(TEXT("barracks"))->BuildCost + 20;
			State->EnemyCommander->Resources = 2000; // Expansion budget; all spending remains exactly accounted.
			Test->AddInfo(TEXT("Enemy naturally produced one paid recruit; waiting for real region capture and completed extractor."));
			return false;
		}
		if (!Production.IsValid() || !Recovery.IsValid())
			return Fail(TEXT("Enemy lost its producer-owned expansion force"));
		int32 Joined, Travelling;
		Production->GetForceCounts(Joined, Travelling);
		const int32 Count = Joined + Travelling;
		const AMapRegion* HomeRegion = State->FindRegionAt(Production->GetActorLocation());
		if (Joined > 0 && HomeRegion && Recovery->WaypointRegionIndex != HomeRegion->RegionIndex
			&& Recovery->Verb == EForceVerb::MoveHold
			&& FVector::Dist2D(Recovery->Destination,
				   State->GetRegionAnchor(Recovery->WaypointRegionIndex))
				<= 75.f)
			bObservedRegionAdvance = true;
		int32 ConstructionSpend = 0, ProductionSpend = 0;
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->TeamIndex == 5)
			{
				if (Building->OwningPlayerState != State->EnemyCommander)
					return Fail(TEXT("Every naturally constructed enemy building must retain JEV ownership"));
				ConstructionSpend += Building->GetDefinition() ? Building->GetDefinition()->BuildCost : 0;
				const AMapRegion* Region = State->FindRegionAt(Building->GetActorLocation());
				if (Building->IsProducer() && Region && Region->RegionRole != ERegionRole::Main && !ForwardProduction.IsValid())
				{
					if (!IsValid(Region->Anchor) || State->GetRegionController(Region->RegionIndex) != 5
						|| State->IsRegionContested(Region->RegionIndex, 5))
						return Fail(TEXT("Natural forward barracks must be placed in a captured, uncontested region"));
					if (Building->IsComplete())
						return Fail(TEXT("Forward barracks must first appear unfinished through normal paid construction"));
					ForwardProduction = Building;
				}
				if (!Building->IsProducer() || !Building->bForceConfigured)
					continue;
				int32 ForceJoined, ForceTravelling;
				Building->GetForceCounts(ForceJoined, ForceTravelling);
				if (ForceJoined + ForceTravelling > ACommandBuilding::GetForceCapacity(*State->Content->Unit(Building->ProductionUnitIndex)))
					return Fail(TEXT("Each enemy producer's travellers must consume its own capacity"));
				ProductionSpend += (ForceJoined + ForceTravelling) * Building->GetProductionCost()
					+ (Building->ProductionRole == EUnitRole::Siege ? 180 : 0);
			}
		const int32 ResearchSpend = State->EnemyCommander->Doctrine == EArmyDoctrine::None ? 0 : ACommandBuilding::ResearchCost;
		if (Count > 6)
			return Fail(TEXT("Original infantry producer must never exceed its six paid capacity slots"));
		if (Production->ForceGroup != Recovery.Get() || Recovery->GetProductionBuilding() != Production.Get())
			return Fail(TEXT("Natural expansion must preserve the original producer and force identity"));
		if (State->EnemyCommander->Resources != EnemyBudget - ConstructionSpend - ProductionSpend - ResearchSpend)
			return Fail(TEXT("Enemy wallet must exactly account for all natural building, unit, configuration and research spending"));
		if (PC->GetPlayerState<ACommandPlayerState>()->Resources != HumanBalance)
			return Fail(TEXT("Enemy expansion cannot spend the human commander's wallet"));
		ACommandBuilding* CapturedExtractor = nullptr;
		int32 ExtractorRate = 0;
		for (ADepositSite* Deposit : State->Deposits)
		{
			ACommandBuilding* Extractor = IsValid(Deposit) ? Deposit->Extractor.Get() : nullptr;
			if (!IsValid(Extractor) || !Extractor->IsAlive() || !Extractor->IsComplete() || Extractor->TeamIndex != 5)
				continue;
			if (Extractor->OwningPlayerState != State->EnemyCommander || Extractor->Deposit != Deposit)
				return Fail(TEXT("Enemy extractor must retain its owning JEV wallet and deposit backlink"));
			if (Deposit->Remaining > 0)
				ExtractorRate += Deposit->RatePerSecond();
			for (const AMapRegion* Region : State->Regions)
				if (IsValid(Region) && Region->RegionIndex == Deposit->RegionIndex && IsValid(Region->Anchor)
					&& State->GetRegionController(Region->RegionIndex) == 5)
					CapturedExtractor = Extractor;
		}
		if (!CapturedExtractor || !bObservedRegionAdvance || !ForwardProduction.IsValid() || !ForwardProduction->IsComplete()
			|| !ForwardProduction->bForceConfigured || !IsValid(ForwardProduction->ForceGroup))
			return false;
		const AMapRegion* ForwardRegion = State->FindRegionAt(ForwardProduction->GetActorLocation());
		if (!ForwardRegion || State->GetRegionController(ForwardRegion->RegionIndex) != 5)
			return Fail(TEXT("Naturally completed forward barracks must retain captured-region build rights"));
		int32 ForwardJoined, ForwardTravelling;
		ForwardProduction->GetForceCounts(ForwardJoined, ForwardTravelling);
		if (ForwardJoined == 0)
			return false; // Normal paid deployment and physical arrival, not configuration alone.
		// Capture can be performed by travellers. Do not damage an empty joined roster
		// or let newly arriving healthy recruits dilute the recovery threshold.
		if (Joined != ACommandBuilding::GetForceCapacity(*Production->GetProductionDefinition()) || Travelling != 0)
			return false;
		if (State->GetEnemyIncomePerSecond() != 2 + ExtractorRate
			|| State->GetIncomePerSecond(PC->GetPlayerState<ACommandPlayerState>()) != 2)
			return Fail(TEXT("Completed enemy extractors increase only JEV income at exact deposit rates"));
		Planner->SetActorTickEnabled(false);
		TArray<ACommandBuilding*> EnabledProducers;
		for (ACommandBuilding* Producer : State->Buildings)
			if (IsValid(Producer) && Producer->TeamIndex == 5 && Producer->IsProducer() && Producer->bProductionEnabled)
			{
				EnabledProducers.Add(Producer);
				FCommandService::ConfigureProduction(State->EnemyCommander, Producer, State->Content->Unit(Producer->ProductionUnitIndex)->Role, false);
			}
		ADepositSite* TargetDeposit = CapturedExtractor->Deposit;
		const int32 RegionIndex = TargetDeposit->RegionIndex;
		const int32 EnemyBefore = State->EnemyCommander->Resources;
		TArray<int32> Reserves;
		for (const ADepositSite* Deposit : State->Deposits)
			Reserves.Add(IsValid(Deposit) ? Deposit->Remaining : 0);
		State->bVerificationIncomePaused = false;
		State->Tick(2.f);
		State->bVerificationIncomePaused = true;
		if (State->EnemyCommander->Resources != EnemyBefore + (2 + ExtractorRate) * 2
			|| PC->GetPlayerState<ACommandPlayerState>()->Resources != HumanBalance + 4)
			return Fail(TEXT("Normal economy tick pays extractor bonuses only to JEV, human receives baseline"));
		int32 FinalBonus = 0;
		for (int32 Index = 0; Index < State->Deposits.Num(); ++Index)
		{
			ADepositSite* Deposit = State->Deposits[Index];
			if (!IsValid(Deposit))
				continue;
			const ACommandBuilding* Extractor = Deposit->Extractor;
			const bool bPays = IsValid(Extractor) && Extractor->IsAlive() && Extractor->IsComplete() && Extractor->TeamIndex == 5;
			if (Deposit->Remaining != Reserves[Index] - (bPays ? Deposit->RatePerSecond() * 2 : 0))
				return Fail(TEXT("JEV payment drains each paying deposit exactly once"));
			if (bPays)
			{
				Deposit->Remaining = 3;
				FinalBonus += 3;
			}
		}
		const int32 FinalBefore = State->EnemyCommander->Resources;
		State->bVerificationIncomePaused = false;
		State->Tick(2.f);
		State->Tick(2.f);
		State->bVerificationIncomePaused = true;
		if (State->EnemyCommander->Resources != FinalBefore + 8 + FinalBonus || State->GetEnemyIncomePerSecond() != 2
			|| PC->GetPlayerState<ACommandPlayerState>()->Resources != HumanBalance + 12)
			return Fail(TEXT("JEV final finite payment is capped; exhausted extractors stop bonus for every wallet"));
		AArmyGroup* Destroyer = ArmyTestSetup::SpawnGroup(World, PC, 21, ArmyTestSetup::FromFriendlyHQ(State, 0.f, 0.f, 100.f));
		if (!Destroyer)
			return Fail(TEXT("Enemy extractor destruction fixture could not spawn"));
		CapturedExtractor->ReceiveAttack(CapturedExtractor->Health, Destroyer->GetUnits()[0]);
		Destroyer->Destroy();
		if (IsValid(TargetDeposit->Extractor) || State->GetRegionController(RegionIndex) != 5)
			return Fail(TEXT("Destroying enemy extractor must free its deposit without recapturing region"));
		for (ACommandBuilding* Producer : EnabledProducers)
			FCommandService::ConfigureProduction(State->EnemyCommander, Producer, State->Content->Unit(Producer->ProductionUnitIndex)->Role, true);
		const FVector ThreatAnchor = State->GetRegionAnchor(ForwardRegion->RegionIndex);
		ACommandBuilding* NearestDefender = nullptr;
		float NearestDistance = TNumericLimits<float>::Max();
		for (ACommandBuilding* Producer : State->Buildings)
		{
			if (!IsValid(Producer) || Producer->TeamIndex != 5 || !Producer->IsProducer()
				|| !Producer->IsComplete() || !IsValid(Producer->ForceGroup))
				continue;
			const float Distance = FVector::DistSquared2D(Producer->ForceGroup->GetCenter(), ThreatAnchor);
			if (Distance < NearestDistance)
			{
				NearestDistance = Distance;
				NearestDefender = Producer;
			}
		}
		AArmyGroup* Threat = ArmyTestSetup::SpawnGroup(World, PC, 20, ThreatAnchor + FVector(0.f, 0.f, 100.f));
		if (!Threat)
			return Fail(TEXT("Real hostile-pressure fixture could not spawn"));
		if (!State->IsRegionContested(ForwardRegion->RegionIndex, 5))
			return Fail(TEXT("Defense fixture must place real hostile units inside the captured forward region"));
		Planner->EvaluatePlan();
		if (!NearestDefender || NearestDefender->ForceGroup->Verb != EForceVerb::MoveHold
			|| NearestDefender->ForceGroup->TargetRegionIndex != ForwardRegion->RegionIndex)
			return Fail(TEXT("Nearest producer force must receive MoveHold on the actually threatened controlled region"));
		if (NearestDefender->ForceGroup->Orders.IsEmpty())
			return Fail(TEXT("Threatened-region defense must retain an accepted active force order"));
		if (!Production.IsValid() || Production->ForceGroup != Recovery.Get()
			|| Recovery->GetProductionBuilding() != Production.Get())
			return Fail(TEXT("Recovery fixture must use the real producer-owned force"));
		int32 DamageJoined, DamageTravelling;
		Production->GetForceCounts(DamageJoined, DamageTravelling);
		if (DamageJoined == 0 || DamageTravelling != 0)
			return Fail(TEXT("Recovery damage fixture requires naturally joined recruits and no healthy arrivals in flight"));
		float DamagedHealth = 0.f;
		for (AArmyUnit* Unit : Recovery->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
			{
				Unit->ReceiveAttack(Unit->GetHealth() - FMath::Max(1, Unit->MaxHealth() / 4), Threat->GetUnits()[0]);
				DamagedHealth += float(Unit->GetHealth()) / Unit->MaxHealth();
				DamagedUnits.Add(Unit);
			}
		if (DamagedUnits.Num() != DamageJoined || DamagedHealth / DamageJoined >= .35f)
			return Fail(TEXT("Real hostile damage must reduce the observed joined roster below the 35 percent recovery threshold"));
		InitialDamagedHealth = DamagedHealth / DamageJoined;
		Threat->Destroy();
		State->EnemyCommander->Resources = 2000; // Paid repairs setup, not asserted income.
		OtherProduction = ForwardProduction;
		OtherForce = OtherProduction->ForceGroup;
		ACommandBuilding* Workshop = nullptr;
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->TeamIndex == 5 && Building->Kind == EBuildingKind::Workshop)
				Workshop = Building;
		if (!Workshop)
			Workshop = PlaceEnemy(State, TEXT("workshop"), State->EnemyHeadquarters->GetActorLocation());
		if (!Workshop)
			return Fail(TEXT("Recovery research fixture has no legal workshop footprint"));
		Workshop->Tick(60.f);
		const int32 ResearchBefore = State->EnemyCommander->Resources;
		if (State->EnemyCommander->Doctrine == EArmyDoctrine::None)
		{
			if (!FCommandService::Research(State->EnemyCommander, Workshop, EArmyDoctrine::FieldRepairs)
				|| State->EnemyCommander->Resources != ResearchBefore - ACommandBuilding::ResearchCost)
				return Fail(TEXT("Recovery research must pay exactly once from JEV wallet"));
		}
		else if (State->EnemyCommander->Doctrine != EArmyDoctrine::FieldRepairs
			|| FCommandService::Research(State->EnemyCommander, Workshop, EArmyDoctrine::FieldRepairs).IsAccepted() || State->EnemyCommander->Resources != ResearchBefore)
			return Fail(TEXT("Naturally purchased repairs must stay locked and reject a second charge"));
		Stage = 3;
		return false;
	}
private:
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	bool InSafeRecovery(const ACommandGameState* State) const
	{
		if (Recovery->WaypointRegionIndex == INDEX_NONE
			|| State->GetRegionController(Recovery->WaypointRegionIndex) != 5
			|| State->IsRegionContested(Recovery->WaypointRegionIndex, 5)
			|| FVector::Dist2D(Recovery->Destination, State->GetRegionAnchor(Recovery->WaypointRegionIndex)) > 75.f)
			return false;
		if (Recovery->Verb == EForceVerb::Retreat)
			return Recovery->TargetRegionIndex == INDEX_NONE
				&& (Recovery->Status == EForceStatus::Retreating || Recovery->Status == EForceStatus::Refilling);
		return Recovery->Verb == EForceVerb::MoveHold && Recovery->Status == EForceStatus::Holding
			&& Recovery->TargetRegionIndex == Recovery->WaypointRegionIndex
			&& FVector::Dist2D(Recovery->GetCenter(), Recovery->Destination) <= 170.f;
	}
	ACommandBuilding* PlaceEnemy(ACommandGameState* State, FName Id, const FVector& Center)
	{
		for (int32 Ring = 0; Ring < 9; ++Ring)
			for (int32 Direction = 0; Direction < 32; ++Direction)
			{
				const float Angle = Direction * PI / 16.f;
				FVector Location = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
				Location.Z = 5.f;
				Location = State->ResolveBuildingLocation(State->Content->BuildingIndexOf(Id), Location, 5);
				if (State->FindRegionAt(Location) != State->FindRegionAt(Center))
					continue;
				if (ACommandBuilding* Building = FCommandService::PlaceBuilding(State->EnemyCommander,
						State->Content->BuildingIndexOf(Id), Location)
						.Building)
					return Building;
			}
		return nullptr;
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandBuilding> Production;
	TWeakObjectPtr<AArmyGroup> Recovery;
	TWeakObjectPtr<ACommandBuilding> OtherProduction;
	TWeakObjectPtr<ACommandBuilding> ForwardProduction;
	TWeakObjectPtr<AArmyGroup> OtherForce;
	TArray<TWeakObjectPtr<AArmyUnit>> DamagedUnits;
	bool bObservedRegionAdvance = false;
	bool bObservedSafeHold = false;
	int32 SafeRecoveryRegion = INDEX_NONE;
	float InitialDamagedHealth = 0.f;
	int32 Stage = 0;
	int32 HumanBalance = 0;
	int32 EnemyBudget = 600;
	double NextProgress = 0.;
	int32 TimedStage = -1;
	double Started = FPlatformTime::Seconds();
	double StageStarted = Started;
};
bool FEnemyConstructionTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEnemyConstructionScenario(this));
	return true;
}
#endif
