#include "EnemyCommanderTurn.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Content/MatchContent.h"
#include "GameState/GameStateEconomy.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogJevRelease, Log, All);

// A release's free wave: what it buys, where it assembles and which order it carries.
// The schedule and its published state are EnemyCommanderRelease.cpp.

namespace
{
// Wave forces assemble this far from the HQ toward the humans, side by side.
constexpr float AssemblyDistance = 900.f;
constexpr float AssemblySpacing = 700.f;
// Producers hold the low force numbers; free forces take the next unused ones.
constexpr int32 FirstFreeForceNumber = 4;

// Human commanders in the roster: the team-economy roster, the same count JEV's income factor reads.
int32 HumanCommanders(const ACommandGameState& State)
{
	return FGameStateEconomy::Roster(State).Num();
}

// One option per catalogue unit. Waves buy all five combat roles; a unit with no option here
// (cost 0) is never bought.
TArray<JevRelease::FUnitOption> UnitOptions(const UMatchContent& Content)
{
	TArray<JevRelease::FUnitOption> Options;
	Options.SetNum(Content.Units.Num());
	for (int32 Slot = 0; Slot < JevExecution::RoleSlots; ++Slot)
	{
		const int32 Index = Content.UnitIndexForRole(SlotRole(Slot));
		if (const UArmyUnitDefinition* Unit = Content.Unit(Index))
			Options[Index] = { Unit->UnitCost, Unit->ArmorClass, Unit->DamageType };
	}
	return Options;
}

int32 FreeForceNumber(const FJevTurn& Turn)
{
	for (int32 Number = FirstFreeForceNumber;; ++Number)
	{
		bool bUsed = Turn.Barracks.ContainsByPredicate(
			[Number](const ACommandBuilding* Building) { return Building->ForceNumber == Number; });
		for (TActorIterator<AArmyGroup> It(Turn.World); It && !bUsed; ++It)
			bUsed = It->GetOwningPlayerState() == Turn.Commander && It->ForceNumber == Number && It->GetAliveCount() > 0;
		if (!bUsed)
			return Number;
	}
}

// Slot of Count forces abreast, centred on the line from the main toward the humans.
FVector AssemblyPoint(const FJevTurn& Turn, int32 Slot, int32 Count)
{
	const FVector Forward = (Turn.EnemyHome - Turn.Home).GetSafeNormal2D();
	const FVector Side(-Forward.Y, Forward.X, 0.f);
	return Turn.Home + Forward * AssemblyDistance + Side * ((Slot - (Count - 1) * .5f) * AssemblySpacing);
}

// The catalogue index of each bought unit, in option order.
TArray<int32> RosterOf(const JevRelease::FPurchase& Bought)
{
	TArray<int32> Units;
	for (int32 Option = 0; Option < Bought.Counts.Num(); ++Option)
		for (int32 Count = 0; Count < Bought.Counts[Option]; ++Count)
			Units.Add(Option);
	return Units;
}

void LogWave(const FJevWaveEvent& Event, int32 Carry, const FJevTurn& Turn)
{
	UE_LOG(LogJevRelease, Display,
		TEXT("JEV %s wave release=%d at=%.1f budget=%d units=%d forces=%d target=%d carry=%d humans(L/H/S/St)=%d/%d/%d/%d"),
		Event.bEmergency ? TEXT("emergency") : TEXT("release"), Event.Release, Event.MatchSeconds, Event.Budget, Event.Units,
		Event.Forces, Event.TargetRegion, Carry, Turn.EnemyArmor.Count[0], Turn.EnemyArmor.Count[1], Turn.EnemyArmor.Count[2],
		Turn.EnemyArmor.Count[3]);
}

// A release wave raids or marches on the humans' main; an emergency wave defends JEV's own main.
int32 WaveTarget(const FJevTurn& Turn, const JevRelease::FBehaviour& Behaviour, bool bEmergency)
{
	return bEmergency ? Turn.Summary.Home : Behaviour.bRaid ? JevRelease::RaidRegion(Turn.Summary)
															: Turn.Summary.EnemyHome;
}

int32 CostOf(const AArmyGroup& Force)
{
	int32 Cost = 0;
	for (const AArmyUnit* Unit : Force.GetUnits())
		Cost += Unit->GetDefinition()->UnitCost;
	return Cost;
}
}

void AEnemyCommander::LaunchWave(FJevTurn& Turn, int32 ReleaseIndex, bool bEmergency)
{
	// An emergency wave neither spends nor adds to the release carry.
	int32 EmergencyCarry = 0;
	int32& Carry = bEmergency ? EmergencyCarry : WaveCarry;
	const JevRelease::FBehaviour Behaviour = JevRelease::BehaviourFor(ReleaseIndex);
	const int32 Budget = JevRelease::WaveBudget(ReleaseIndex, HumanCommanders(*Turn.State));
	const int32 Target = WaveTarget(Turn, Behaviour, bEmergency);
	if (Budget <= 0)
		return;
	// No place to send the wave: the budget carries to the next release rather than vanishing.
	if (!JevExecution::ValidRegion(Target))
	{
		Carry += Budget;
		return;
	}
	const TArray<JevRelease::FUnitOption> Options = UnitOptions(*Turn.Content);
	const JevRelease::FPurchase Bought = JevRelease::Purchase(Carry + Budget, Options, Behaviour,
		JevRelease::MostNumerous(Turn.EnemyArmor));
	FJevWaveEvent Event;
	Event.Release = ReleaseIndex;
	Event.bEmergency = bEmergency;
	Event.MatchSeconds = GetMatchSeconds();
	Event.Budget = Carry + Budget;
	Event.TargetRegion = Target;
	Carry = Bought.Carry;
	const TArray<int32> Roster = RosterOf(Bought);
	const TArray<int32> Sizes = JevRelease::SplitForces(Roster.Num());
	TArray<AArmyGroup*, TInlineAllocator<8>> Wave;
	int32 First = 0;
	for (int32 Slot = 0; Slot < Sizes.Num(); ++Slot)
	{
		const TConstArrayView<int32> Members = MakeArrayView(Roster).Mid(First, Sizes[Slot]);
		First += Sizes[Slot];
		int32 Requested = 0;
		for (const int32 Unit : Members)
			Requested += Options[Unit].Cost;
		AArmyGroup* Force = AArmyGroup::SpawnFreeForce(*Turn.World, *Turn.Commander,
			AssemblyPoint(Turn, Slot, Sizes.Num()), Members, FreeForceNumber(Turn), Behaviour.SpeedFactor);
		// Whatever could not be placed carries on rather than vanishing.
		Carry += Requested - (Force ? CostOf(*Force) : 0);
		if (!Force)
			continue;
		Wave.Add(Force);
		// A free force has no producer to refill it: it fights to the end.
		WaveForces.Add(Force);
		FCommandService::SetRetreatThreshold(Turn.Commander, Force, ERetreatThreshold::Never);
		Event.Units += Force->GetAliveCount();
	}
	Event.Forces = Wave.Num();
	for (AArmyGroup* Force : Wave)
		ExecuteWaveForce(Turn, Force, Target, false);
	if (Behaviour.bCoordinated && !bEmergency)
		for (AArmyGroup* Force : Turn.Forces)
			ExecuteWaveForce(Turn, Force, Target, true);
	RecordWave(Event);
	Release.WaveCount -= bEmergency ? 1 : 0; // Published like a wave, but no release wave.
	LogWave(Event, Carry, Turn);
}

void AEnemyCommander::LaunchEmergencyWave(FJevTurn& Turn)
{
	// Before v1.1 the budget is zero; the emergency wave then buys the first real budget.
	const int32 Index = FMath::Max(1, JevRelease::IndexAt(GetMatchSeconds()));
	LaunchWave(Turn, Index, true);
}

bool AEnemyCommander::IsWaveForce(const AArmyGroup* Force) const
{
	return WaveForces.ContainsByPredicate([Force](const TWeakObjectPtr<AArmyGroup>& Entry) { return Entry.Get() == Force; });
}
