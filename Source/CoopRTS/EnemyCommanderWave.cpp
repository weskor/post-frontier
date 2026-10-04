#include "EnemyCommanderTurn.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "EngineUtils.h"

// A release's free wave: what it buys, where it assembles and which order it carries.
// The schedule and its published state are EnemyCommanderRelease.cpp.

namespace
{
// Wave forces assemble this far from the HQ toward the humans, side by side.
constexpr float AssemblyDistance = 900.f;
constexpr float AssemblySpacing = 700.f;
// Producers hold the low force numbers; free forces take the next unused ones.
constexpr int32 FirstFreeForceNumber = 4;

// Human commanders in the roster, counted as the economy counts them for JEV's income factor.
int32 HumanCommanders(const ACommandGameState& State)
{
	int32 Humans = 0;
	for (const APlayerState* Player : State.PlayerArray)
		if (const ACommandPlayerState* Commander = Cast<ACommandPlayerState>(Player))
			if (IsValid(Commander) && Commander->TeamIndex == 0 && Commander->CommanderIndex >= 0
				&& Commander->CommanderIndex < 5)
				++Humans;
	return Humans;
}

// One option per catalogue unit. Waves buy today's three combat roles only; a unit with no
// option here (cost 0) is never bought.
TArray<JevRelease::FUnitOption> UnitOptions(const UMatchContent& Content)
{
	TArray<JevRelease::FUnitOption> Options;
	Options.SetNum(Content.Units.Num());
	for (const EUnitRole Role : { EUnitRole::Frontline, EUnitRole::Ranged, EUnitRole::Siege })
	{
		const int32 Index = Content.UnitIndexForRole(Role);
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

int32 CostOf(const AArmyGroup& Force)
{
	int32 Cost = 0;
	for (const AArmyUnit* Unit : Force.GetUnits())
		Cost += Unit->GetDefinition()->UnitCost;
	return Cost;
}
}

void AEnemyCommander::LaunchWave(FJevTurn& Turn, int32 ReleaseIndex)
{
	const JevRelease::FBehaviour Behaviour = JevRelease::BehaviourFor(ReleaseIndex);
	const int32 Budget = JevRelease::WaveBudget(ReleaseIndex, HumanCommanders(*Turn.State));
	const int32 Target = Behaviour.bRaid ? JevRelease::RaidRegion(Turn.Summary) : Turn.Summary.EnemyHome;
	if (Budget <= 0 || !JevExecution::ValidRegion(Target))
		return;
	const TArray<JevRelease::FUnitOption> Options = UnitOptions(*Turn.Content);
	const JevRelease::FPurchase Bought = JevRelease::Purchase(WaveCarry + Budget, Options, Behaviour,
		JevRelease::MostNumerous(Turn.EnemyArmor));
	FJevWaveEvent Event;
	Event.Release = ReleaseIndex;
	Event.MatchSeconds = GetMatchSeconds();
	Event.Budget = WaveCarry + Budget;
	Event.TargetRegion = Target;
	WaveCarry = Bought.Carry;
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
			AssemblyPoint(Turn, Slot, Sizes.Num()), Members, FreeForceNumber(Turn));
		// Whatever could not be placed carries on rather than vanishing.
		WaveCarry += Requested - (Force ? CostOf(*Force) : 0);
		if (!Force)
			continue;
		Wave.Add(Force);
		Event.Units += Force->GetAliveCount();
	}
	Event.Forces = Wave.Num();
	for (AArmyGroup* Force : Wave)
		ExecuteWaveForce(Turn, Force, Target, false);
	if (Behaviour.bCoordinated)
		for (AArmyGroup* Force : Turn.Forces)
			ExecuteWaveForce(Turn, Force, Target, true);
	RecordWave(Event);
}
