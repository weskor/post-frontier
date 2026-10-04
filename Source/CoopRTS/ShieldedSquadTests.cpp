#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "CombatTraitFixture.h"
#include "Commands/CommandService.h"
#include "CoopAudioSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShieldedSquadWorldTest, "CoopRTS.Content.ShieldedSquad.ProduceAndFight",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace CombatTraitFixture;

constexpr int32 LancerHealth = 36, LancerShield = 60;

// The Lancer and Scrambler from the real catalogue, produced at a Barracks of each faction (team 0 Human, team 5
// Machine) with the real production tick: a full squad each, configured stats on every member, the Lancer's
// shield, and a Scrambler pulse that strips a hostile Lancer's shield without touching its HP.
class FSquadScenario : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	bool Setup() override;
	bool Step(double Now) override;

private:
	struct FCase
	{
		EUnitRole Role;
		bool bMachine;
	};
	static constexpr FCase Cases[] = { { EUnitRole::Assault, false }, { EUnitRole::Support, false },
		{ EUnitRole::Assault, true }, { EUnitRole::Support, true } };

	bool Produce(const FCase& Case);
	bool PlaceBarracks(ACommandPlayerState* Commander, const FVector& Center);
	bool CheckSquad(const FCase& Case, const UArmyUnitDefinition& Definition);
	bool StartPulse(const FCase& Case);

	ACommandPlayerState* WalletOf(const FCase& Case) const { return Case.bMachine ? Arena.State->EnemyCommander.Get() : Arena.Wallet.Get(); }

	TWeakObjectPtr<ACommandBuilding> Barracks;
	TWeakObjectPtr<AArmyUnit> Caster, Victim;
	FVector Ground = FVector::ZeroVector;
	int32 LancerIndex = INDEX_NONE, ScramblerIndex = INDEX_NONE, CaseIndex = 0;
	int32 PulseCues = 0, BreakCues = 0;
};

bool FSquadScenario::PlaceBarracks(ACommandPlayerState* Commander, const FVector& Center)
{
	ACommandGameState* State = Arena.State.Get();
	Commander->Resources = 5000;
	const int32 Team = Commander == State->EnemyCommander ? 5 : 0;
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			Point.Z = 5.f;
			Point = State->ResolveBuildingLocation(ArmyTestSetup::BarracksIndex, Point, Team);
			FString Reason;
			if (State->FindRegionAt(Point) != State->FindRegionAt(Center)
				|| !State->ValidateBuildingPlacement(ArmyTestSetup::BarracksIndex, Team, Point, Reason))
				continue;
			Barracks = FCommandService::PlaceBuilding(Commander, ArmyTestSetup::BarracksIndex, Point).Building;
			if (Barracks.IsValid())
				return true;
		}
	return false;
}

bool FSquadScenario::Setup()
{
	const ACommandGameState* State = Arena.State.Get();
	LancerIndex = ArmyTestSetup::UnitIndex(State, EUnitRole::Assault);
	ScramblerIndex = ArmyTestSetup::UnitIndex(State, EUnitRole::Support);
	const UArmyUnitDefinition* Lancer = State->Content->Unit(LancerIndex);
	const UArmyUnitDefinition* Scrambler = State->Content->Unit(ScramblerIndex);
	AMapRegion *First = nullptr, *Second = nullptr;
	if (!Check(Lancer && Scrambler && Lancer->Id == TEXT("lancer") && Scrambler->Id == TEXT("scrambler"),
			TEXT("The catalogue lists the Lancer and the Scrambler")))
		return false;
	if (!Check(Arena.PickRegions(First, Second) && Arena.Ground(*First, Ground), TEXT("A roomy navigable region hosts the pulse fixture")))
		return false;
	// Nothing else may be within a pulse radius of the fixture ground.
	return Check(FVector::Dist2D(Ground, State->FriendlyHeadquarters->GetActorLocation()) > 1500.f
			&& FVector::Dist2D(Ground, State->EnemyHeadquarters->GetActorLocation()) > 1500.f,
		TEXT("The pulse fixture is far from both headquarters"));
}

bool FSquadScenario::Produce(const FCase& Case)
{
	ACommandGameState* State = Arena.State.Get();
	ACommandPlayerState* Commander = WalletOf(Case);
	const AHeadquarters* Hq = Case.bMachine ? State->EnemyHeadquarters.Get() : State->FriendlyHeadquarters.Get();
	if (!Check(PlaceBarracks(Commander, Hq->GetActorLocation()), TEXT("A Barracks site exists beside the headquarters")))
		return false;
	Barracks->Tick(60.f);
	const int32 Fee = State->Content->Unit(Case.Role == EUnitRole::Assault ? LancerIndex : ScramblerIndex)->ConfigurationCost;
	const int32 Before = Commander->Resources;
	FCommandService::ConfigureProduction(Commander, Barracks.Get(), Case.Role, true);
	if (!Check(Barracks->IsComplete() && Barracks->bForceConfigured && Barracks->bProductionEnabled && Barracks->ProductionRole == Case.Role
				&& Commander->Resources == Before - Fee,
			TEXT("A completed Barracks locks the new unit type and charges its configuration fee")))
		return false;
	const UArmyUnitDefinition* Definition = Barracks->GetProductionDefinition();
	if (!Check(Definition && Definition->Role == Case.Role, TEXT("The Barracks produces the catalogue unit of that role")))
		return false;
	const int32 BeforeUnits = Commander->Resources;
	for (int32 Pass = 0; Pass < 40; ++Pass)
		Barracks->TickProduction(Definition->UnitDuration + .3f);
	int32 Joined = 0, Travelling = 0;
	Barracks->GetForceCounts(Joined, Travelling);
	return Check(Joined + Travelling == Definition->Capacity && Definition->Capacity == 3
				   && Commander->Resources == BeforeUnits - 3 * Definition->UnitCost,
			   TEXT("Production fills exactly one squad of 3 and charges 3 unit costs, then holds at capacity"))
		&& CheckSquad(Case, *Definition);
}

bool FSquadScenario::CheckSquad(const FCase& Case, const UArmyUnitDefinition& Definition)
{
	const ACommandGameState* State = Arena.State.Get();
	const UBuildingDefinition* Producer = State->Content->Building(ArmyTestSetup::BarracksIndex);
	const TArray<TSoftObjectPtr<UStaticMesh>>& Variants = Case.bMachine ? Producer->MachineRoleMeshes : Producer->HumanRoleMeshes;
	const int32 UnitIndex = Barracks->ProductionUnitIndex;
	if (!Check((Case.bMachine ? Definition.MachineMesh : Definition.HumanMesh).LoadSynchronous()
				&& Variants.IsValidIndex(UnitIndex) && Variants[UnitIndex].LoadSynchronous(),
			TEXT("The unit mesh and the Barracks variant of the faction are imported")))
		return false;
	int32 Members = 0;
	for (TActorIterator<AArmyUnit> It(Arena.World.Get()); It; ++It)
	{
		const AArmyUnit* Unit = *It;
		if (!Unit->IsAlive() || Unit->GetGroup() != Barracks->ForceGroup)
			continue;
		++Members;
		const bool bLancer = Case.Role == EUnitRole::Assault;
		const int32 Shield = bLancer ? LancerShield : 0;
		const int32 Health = bLancer ? LancerHealth : 80;
		const bool bPulse = !bLancer && Definition.PulseInterval == 10.f && Definition.PulseRadius == 600.f && Definition.PulseBuildingStunSeconds == 3.f;
		if (!Check(Unit->GetUnitRole() == Case.Role && Unit->GetHealth() == Health && Unit->GetShield() == Shield
					&& Unit->MaxShield() == Shield && Definition.AttackDamage == (bLancer ? 24 : 12) && Definition.UnitCost == 24
					&& Definition.Range == 550.f && Definition.Interval == 1.f && Definition.MoveSpeed == (bLancer ? 420.f : 480.f)
					&& (bLancer || bPulse),
				TEXT("Every member carries the configured stats; a Lancer has 60 shield, a Scrambler pulses at 600 cm")))
			return false;
	}
	return Check(Members == 3, TEXT("Three living members belong to the squad's force"));
}

bool FSquadScenario::StartPulse(const FCase& Case)
{
	AArmyUnit* Squad = nullptr;
	for (AArmyUnit* Unit : Barracks->ForceGroup->GetUnits())
		if (IsValid(Unit) && Unit->IsAlive())
			Squad = Unit;
	if (!Check(Squad != nullptr, TEXT("The Scrambler squad has a member to pulse with")))
		return false;
	Barracks->ForceGroup->SetActorTickEnabled(false);
	Caster = Squad;
	Arena.Place(Squad, Ground + FVector(-300.f, 0.f, 65.f));
	Victim = Arena.Spawn(!Case.bMachine, LancerIndex, Ground, Ground + FVector(-180.f, 0.f, 65.f));
	if (!Check(Victim.IsValid(), TEXT("The hostile Lancer fixture spawns")))
		return false;
	const UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(Squad);
	if (!Check(Audio != nullptr, TEXT("The audio subsystem is running in the test world")))
		return false;
	PulseCues = Audio->GetPlayCount(ECoopAudioEvent::Pulse);
	BreakCues = Audio->GetPlayCount(ECoopAudioEvent::ShieldBreak);
	return Check(Victim->GetShield() == LancerShield && Caster->GetLastPulseServerTime() < 0.,
		TEXT("A hostile Lancer with its full shield stands in a produced Scrambler's pulse radius, which is ready at spawn"));
}

bool FSquadScenario::Step(double Now)
{
	if (CaseIndex >= UE_ARRAY_COUNT(Cases))
		return true;
	const FCase& Case = Cases[CaseIndex];
	if (Stage == 0)
	{
		if (!Produce(Case))
			return true;
		if (Case.Role == EUnitRole::Assault)
		{
			++CaseIndex;
			return false;
		}
		if (!StartPulse(Case))
			return true;
		Next(1, Now);
		return false;
	}
	if (Victim->GetShield() > 0)
		return !Check(!After(Now, 1.5), TEXT("The produced Scrambler pulses at a shielded hostile in range"));
	const UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(Caster.Get());
	if (!Check(Victim->GetHealth() == LancerHealth && Caster->GetLastPulseServerTime() > 0.,
			TEXT("The pulse empties the shield without HP damage and publishes its cast time")))
		return true;
	if (!Check(Audio && Audio->GetPlayCount(ECoopAudioEvent::Pulse) == PulseCues + 1
				&& Audio->GetPlayCount(ECoopAudioEvent::ShieldBreak) == BreakCues + 1,
			TEXT("One pulse cue plays at the cast and one shield-break cue for the Lancer whose shield it emptied")))
		return true;
	++CaseIndex;
	Stage = 0;
	return false;
}
}

bool FShieldedSquadWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSquadScenario(this));
	return true;
}

#endif
