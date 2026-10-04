#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "EnemyCommander.h"
#include "MatchTelemetry.h"
#include "PlanningFixture.h"

// Planning freezes the world, spends no pause, ends exactly once when both humans are Ready, and starts the
// battle clock at 0:00.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningPhaseWorldTest, "CoopRTS.Planning.Phase",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PlanningPhaseTests
{
class FScenario final : public PlanningFixture::FScenario
{
public:
	using PlanningFixture::FScenario::FScenario;

private:
	bool Step() override
	{
		switch (Stage)
		{
		case 0:
			return Open();
		case 1:
			return Frozen();
		default:
			return Resumed();
		}
	}

	AEnemyCommander* Jev() const
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			if (It->TeamIndex == 5)
				return *It;
		return nullptr;
	}

	bool Open()
	{
		ACommandPlayerState* Guest = GuestPtr.Get();
		if (!JevKitStands(2) || State->Planning.Kits.Num() != 2)
			return false;
		FVector Spot;
		if (!Check(Host->Resources == 200 && Guest->Resources == 200 && Host->Data == 0 && Guest->Data == 0
					&& State->EnemyCommander->Resources == 200,
				TEXT("Every commander opens with 200 Power and 0 Data"))
			|| !Check(World->IsPaused() && !State->IsActivePaused(), TEXT("Planning freezes the world without the shared pause"))
			|| !Check(!FCommandService::Pause(PC).IsAccepted() && !State->IsActivePaused() && State->GetPauseSecondsRemaining() == 0.f,
				TEXT("P is rejected during planning and starts no pause budget"))
			|| !Check(FCommandService::Pause(PC).Message == TEXT("Nothing runs during planning."), TEXT("The rejection says nothing runs during planning"))
			|| !Check(BarracksSpot(0, Spot), TEXT("A legal Barracks spot exists")))
			return Done();
		const FCommandResult Placed = FPlanningCommands::PlaceKit(Host, EBuildingKind::Barracks, Spot);
		if (!Check(Placed.IsAccepted() && Placed.Building && Placed.Building->IsComplete() && Placed.Building->OwningPlayerState == Host
					&& Host->Resources == 200,
				TEXT("The kit Barracks is free and stands finished")))
			return Done();
		// A running producer and two adjacent hostile forces would work and fight if anything ran.
		FVector Arena = ArmyTestSetup::FromFriendlyHQ(State, 1700.f, 600.f, 100.f);
		for (const AMapRegion* Region : State->Regions)
			if (IsValid(Region) && Region->RegionRole != ERegionRole::Main && State->GetRegionController(Region->RegionIndex) == INDEX_NONE)
			{
				Arena = State->GetRegionAnchor(Region->RegionIndex) + FVector(0.f, 0.f, 100.f);
				break;
			}
		AArmyGroup* Friendly = ArmyTestSetup::SpawnGroup(World, PC, 0, Arena - FVector(125.f, 0.f, 0.f));
		AArmyGroup* Hostile = ArmyTestSetup::SpawnGroup(World, nullptr, -1, Arena + FVector(125.f, 0.f, 0.f));
		if (!CommandsLocked(Friendly, Hostile, Spot, Placed.Building))
			return Done();
		// Fixture: a producer already running, as before planning existed, so the frozen world has work to skip.
		State->Planning.bActive = false;
		const bool bConfigured = FCommandService::ConfigureProduction(Host, Placed.Building, EUnitRole::Frontline, true).IsAccepted();
		State->Planning.bActive = true;
		if (!Check(bConfigured && IsValid(Placed.Building->ForceGroup), TEXT("The fixture producer starts")))
			return Done();
		Producer = Placed.Building;
		CombatUnits.Reset();
		for (AArmyUnit* Unit : Friendly->GetUnits())
			CombatUnits.Add(Unit);
		for (AArmyUnit* Unit : Hostile->GetUnits())
			CombatUnits.Add(Unit);
		Time0 = World->GetTimeSeconds();
		Power0 = Host->Resources;
		GuestPower0 = Guest->Resources;
		JevPower0 = State->EnemyCommander->Resources;
		Battle0 = State->MatchTelemetry->GetBattleSeconds();
		Enter(1);
		return false;
	}

	// Every command but planning's own and pings is refused while the phase runs, naming the planning reason.
	bool CommandsLocked(AArmyGroup* Friendly, AArmyGroup* Hostile, const FVector& Spot, ACommandBuilding* Barracks)
	{
		ACommandPlayerState* Guest = GuestPtr.Get();
		const int32 HomeRegion = ArmyTestSetup::RegionAt(State, State->FriendlyHeadquarters->GetActorLocation());
		AMapRegion* Home = nullptr;
		for (AMapRegion* Region : State->Regions)
			if (IsValid(Region) && Region->RegionIndex == HomeRegion)
				Home = Region;
		const TCHAR* Locked = TEXT("Nothing runs during planning.");
		const auto Refused = [Locked](const FCommandResult& Result) { return !Result.IsAccepted() && Result.Message == Locked; };
		return Check(Friendly && Hostile, TEXT("Combat fixtures spawn while frozen"))
			&& Check(Refused(FCommandService::ConfigureProduction(Host, Barracks, EUnitRole::Ranged, true))
					&& Refused(FCommandService::PlaceBuilding(Host, ArmyTestSetup::BarracksIndex, Spot))
					&& Refused(FCommandService::CancelBuilding(Host, Barracks))
					&& Refused(FCommandService::IssueForceOrder(Host, Friendly, EForceVerb::MoveHold, HomeRegion))
					&& Refused(FCommandService::SetRetreatThreshold(Host, Friendly, ERetreatThreshold::Never))
					&& Refused(FCommandService::SetRallyPoint(Host, Barracks, HomeRegion))
					&& Home && Refused(FCommandService::CastFortify(Host, Home))
					&& Refused(FCommandService::Research(Host, Barracks, EArmyDoctrine::FieldRepairs))
					&& Refused(FCommandService::Gift(Host, Guest, EEconomyResource::Power, 10))
					&& Refused(FCommandService::Resume(PC)),
				TEXT("Every command except planning and pings is refused during planning, with the planning reason"))
			&& Check(!Barracks->bForceConfigured && Host->Resources == 200 && Guest->Resources == 200,
				TEXT("A refused production command changes neither the Barracks nor a wallet"))
			&& Check(FCommandService::Ping(PC, ArmyTestSetup::FromFriendlyHQ(State, 800.f, 0.f, 5.f)).IsAccepted(), TEXT("Pings still work during planning"));
	}

	bool Frozen()
	{
		if (StageSeconds() < 4.)
			return false;
		ACommandPlayerState* Guest = GuestPtr.Get();
		AEnemyCommander* Planner = Jev();
		bool bFullHealth = true;
		for (const TWeakObjectPtr<AArmyUnit>& Unit : CombatUnits)
			bFullHealth &= Unit.IsValid() && Unit->GetHealth() == Unit->MaxHealth();
		bool bJevIdle = true;
		for (const FPlanningKit& Kit : State->Planning.JevKits)
			bJevIdle &= IsValid(Kit.Barracks) && IsValid(Kit.Barracks->ForceGroup) && Kit.Barracks->ForceGroup->GetAliveCount() == 0;
		if (!Check(World->GetTimeSeconds() == Time0, TEXT("Game time stands still during planning"))
			|| !Check(Host->Resources == Power0 && Guest->Resources == GuestPower0 && State->EnemyCommander->Resources == JevPower0,
				TEXT("No income reaches any wallet"))
			|| !Check(Producer->ForceGroup->GetAliveCount() == 0 && bFullHealth, TEXT("Production and combat do not run"))
			|| !Check(Planner && Planner->GetMatchSeconds() == 0.f && bJevIdle && State->EnemyPlans.Num() == State->Planning.JevKits.Num(),
				TEXT("JEV's clock reads 0, it fields no unit yet and plans only its kit forces"))
			|| !Check(State->MatchTelemetry->GetBattleSeconds() == Battle0, TEXT("The battle duration does not count planning"))
			|| !Check(State->Planning.SecondsRemaining < 57.f && State->Planning.SecondsRemaining > 0.f,
				TEXT("The countdown runs in real time")))
			return Done();
		return ReadyUp(Guest);
	}

	bool ReadyUp(ACommandPlayerState* Guest)
	{
		if (!Check(FPlanningCommands::SetReady(Host, true).IsAccepted() && State->IsPlanning(), TEXT("One Ready of two keeps planning going"))
			|| !Check(!FPlanningCommands::PlaceKit(Host, EBuildingKind::Barracks, Producer->GetActorLocation()).IsAccepted()
					&& !FPlanningCommands::SetUnitType(Host, EUnitRole::Ranged).IsAccepted(),
				TEXT("Ready locks kit and unit-type edits"))
			|| !Check(FPlanningCommands::SetReady(Host, false).IsAccepted()
					&& FPlanningCommands::SetUnitType(Guest, EUnitRole::Ranged).IsAccepted(),
				TEXT("Un-Ready unlocks edits"))
			|| !Check(FPlanningCommands::SetReady(Host, true).IsAccepted() && State->GetPlanningEndCount() == 0,
				TEXT("Ready again still waits for the guest")))
			return Done();
		if (!Check(FPlanningCommands::SetReady(Guest, true).IsAccepted(), TEXT("The last Ready is accepted"))
			|| !Check(!State->IsPlanning() && State->GetPlanningEndCount() == 1 && State->GetPlanningEnd() == EPlanningEnd::AllReady
					&& !World->IsPaused(),
				TEXT("All Ready ends planning exactly once and unfreezes the world"))
			|| !Check(!FPlanningCommands::SetReady(Guest, false).IsAccepted() && State->GetPlanningEndCount() == 1,
				TEXT("Planning cannot be re-opened by un-Ready"))
			|| !Check(Host->Resources == 200 && Guest->Resources == 200,
				FString::Printf(TEXT("The kit cost nothing: both wallets still hold 200 (host %d, guest %d)"), Host->Resources, Guest->Resources)))
			return Done();
		bool bKits = true;
		for (const ACommandPlayerState* Commander : { Host, Guest })
		{
			int32 Barracks = 0, Rigs = 0;
			for (const ACommandBuilding* Building : State->Buildings)
				if (IsValid(Building) && Building->OwningPlayerState == Commander && Building->IsComplete())
				{
					Barracks += Building->Kind == EBuildingKind::Barracks;
					Rigs += Building->Kind == EBuildingKind::Extractor;
				}
			bKits &= Barracks == 1 && Rigs == 1;
		}
		ACommandBuilding* GuestBarracks = nullptr;
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->OwningPlayerState == Guest && Building->Kind == EBuildingKind::Barracks)
				GuestBarracks = Building;
		if (!Check(bKits, TEXT("Each commander has one finished Barracks and one finished Drill Rig"))
			|| !Check(GuestBarracks && GuestBarracks->bForceConfigured && GuestBarracks->bProductionEnabled
					&& GuestBarracks->ProductionRole == EUnitRole::Ranged,
				TEXT("At 0:00 production starts with the picked unit type"))
			|| !Check(Jev() && Jev()->GetMatchSeconds() >= 0.f && Jev()->GetMatchSeconds() < .2f && State->GetBattleClockStartServerTime() >= 0.f,
				TEXT("The battle clock starts at 0 when planning ends")))
			return Done();
		Enter(2);
		return false;
	}

	bool Resumed()
	{
		bool bDamaged = false;
		for (const TWeakObjectPtr<AArmyUnit>& Unit : CombatUnits)
			bDamaged |= Unit.IsValid() && Unit->GetHealth() < Unit->MaxHealth();
		bool bJevProducing = false;
		for (const ACommandBuilding* Building : State->Buildings)
			bJevProducing |= IsValid(Building) && Building->TeamIndex == 5 && Building->bForceConfigured;
		const bool bAll = Host->Resources > Power0 && Producer->ForceGroup->GetAliveCount() > 0 && bDamaged && bJevProducing;
		if (!bAll && StageGameSeconds() < 20.)
			return false;
		const float Match = Jev() ? Jev()->GetMatchSeconds() : -1.f;
		Check(Host->Resources > Power0, TEXT("Income resumes at 0:00"));
		Check(Producer->ForceGroup->GetAliveCount() > 0, TEXT("Production resumes at 0:00"));
		Check(bDamaged, TEXT("Combat resumes at 0:00"));
		Check(bJevProducing, TEXT("JEV's kit Barracks takes up production at 0:00"));
		Check(FMath::Abs(Match - StageGameSeconds()) < .5 && FMath::Abs(State->MatchTelemetry->GetBattleSeconds() - StageGameSeconds()) < .5,
			TEXT("JEV's clock and the battle duration count from 0:00"));
		LateKit();
		const FCommandResult Pause = FCommandService::Pause(PC);
		Check(Pause.IsAccepted() && State->IsActivePaused(), TEXT("The shared pause is still available after planning"));
		FCommandService::Resume(PC);
		return Done();
	}

	// A commander who joins after 0:00 (ACommandGameMode calls GrantLateKit at login) gets 200 Power and a finished
	// kit at default spots; with no deposit left the Rig is paid back instead.
	void LateKit()
	{
		ACommandPlayerState* Late = SpawnCommander();
		if (!Check(Late != nullptr, TEXT("A late commander can join")))
			return;
		const int32 Before = Late->Resources;
		State->GrantLateKit(Late);
		ACommandBuilding* Barracks = nullptr;
		bool bRig = false;
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->OwningPlayerState == Late && Building->IsComplete())
			{
				Barracks = Building->Kind == EBuildingKind::Barracks ? Building : Barracks;
				bRig |= Building->Kind == EBuildingKind::Extractor;
			}
		const UBuildingDefinition* RigDefinition = State->Content->Building(ArmyTestSetup::ExtractorIndex);
		Check(Before == 200 && Barracks && Barracks->bForceConfigured && Barracks->bProductionEnabled
				&& Late->Resources == (bRig ? 200 : 200 + RigDefinition->BuildCost),
			TEXT("A commander who joins after 0:00 gets 200 Power and a finished kit with production started"));
		RemoveCommander(Late);
	}

	ACommandBuilding* Producer = nullptr;
	TArray<TWeakObjectPtr<AArmyUnit>> CombatUnits;
	double Time0 = 0., Battle0 = 0.;
	int32 Power0 = 0, GuestPower0 = 0, JevPower0 = 0;
};
}

bool FPlanningPhaseWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PlanningPhaseTests::FScenario(this));
	return true;
}

#endif
