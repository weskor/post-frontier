#include "EnemyCommanderTurn.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Content/MatchContent.h"
#include "GameState/GameStateEconomy.h"
#include "GameState/GameStateTerritory.h"
#include "MapRegion.h"
#include "ObjectiveAnnouncer.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogJevRelease, Log, All);

// A release's free wave: what it buys, where it assembles and which order it carries, and the Split-Brain Cut
// (Rules/JevThreatPolicy) that rides on v2.0. The schedule and its published state are EnemyCommanderRelease.cpp.

namespace
{
// Wave forces assemble this far from the HQ toward the humans, side by side.
constexpr float AssemblyDistance = 900.f;
constexpr float AssemblySpacing = 700.f;
// A cut force assembles past the wave's own line, so the two never stack.
constexpr float CutAssemblyDistance = 1800.f;
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

// Slot of Count forces abreast, centred on the line from the main toward the humans, Distance out from the HQ.
FVector AssemblyPoint(const FJevTurn& Turn, int32 Slot, int32 Count, float Distance = AssemblyDistance)
{
	const FVector Forward = (Turn.EnemyHome - Turn.Home).GetSafeNormal2D();
	const FVector Side(-Forward.Y, Forward.X, 0.f);
	return Turn.Home + Forward * Distance + Side * ((Slot - (Count - 1) * .5f) * AssemblySpacing);
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

// The authored Split-Brain Cut pairs: regions whose actors carry the tag "SplitBrain.k" (JevThreat::TagPrefix).
TArray<JevThreat::FPair> AuthoredPairs(const FJevTurn& Turn)
{
	TArray<JevThreat::FTaggedRegion> Tagged;
	for (const AMapRegion* Region : Turn.Regions)
		if (Region && Region->RegionIndex >= 0 && Region->RegionIndex < ForceOrders::MaxRegions)
			for (const FName& Tag : Region->Tags)
				if (const int32 Pair = JevThreat::PairOfTag(Tag.ToString()); Pair != INDEX_NONE)
					Tagged.Add({ Region->RegionIndex, Pair });
	return JevThreat::BuildPairs(Tagged);
}

// Why the threat did not happen, with the controller of both regions of every authored pair.
void LogSkip(const FJevTurn& Turn, TConstArrayView<JevThreat::FPair> Pairs, JevThreat::ESkip Skip, float MatchSeconds)
{
	FString Held;
	for (const JevThreat::FPair& Pair : Pairs)
		Held += FString::Printf(TEXT(" pair %d,%d controlled by %d,%d"), Pair.A, Pair.B, Turn.Summary.Regions[Pair.A].Controller,
			Turn.Summary.Regions[Pair.B].Controller);
	UE_LOG(LogJevRelease, Display, TEXT("JEV %s skipped at=%.1f: %s;%s"), JevThreat::Name, MatchSeconds, JevThreat::SkipReason(Skip),
		*Held);
}

// Seconds the roster's slowest unit needs from JEV's main to Target; negative when no route leads there.
float MarchSeconds(const FJevTurn& Turn, TConstArrayView<int32> Roster, float SpeedFactor, int32 Target)
{
	float Speed = 0.f;
	for (const int32 Unit : Roster)
		if (const UArmyUnitDefinition* Definition = Turn.Content->Unit(Unit))
			Speed = Speed == 0.f ? Definition->MoveSpeed : FMath::Min(Speed, Definition->MoveSpeed);
	Speed *= SpeedFactor;
	JevPlanner::FForce Marching;
	Marching.Source = Turn.Summary.Home;
	Marching.Position = Turn.Home;
	Marching.UnitCount = Roster.Num();
	Marching.ClassSpeeds = MakeArrayView(&Speed, 1);
	return JevPlanner::TravelSeconds(Turn.Summary, Marching, Target);
}

// The plan clients see until the force exists: it arrives Lead seconds from now plus its march.
FJevCutPlan CutPlan(const FJevTurn& Turn, int32 Ticket, int32 Target, int32 Units, float Lead, float March)
{
	FJevCutPlan Plan;
	Plan.Ticket = Ticket;
	Plan.Source = Turn.Summary.Home;
	Plan.Target = Target;
	Plan.SizeBand = JevPlanner::SizeBand(Units);
	Plan.EtaSeconds = Lead + March;
	Plan.EtaIssuedAt = Turn.State->GetServerWorldTimeSeconds();
	TStringBuilder<192> Memo;
	JevThreat::AppendMemo(Memo, Ticket, Plan.SizeBand, Turn.Regions[Target] ? Turn.Regions[Target]->DisplayName.ToString() : FString(),
		Plan.EtaSeconds);
	Plan.Memo = Memo.ToString();
	return Plan;
}

void AnnounceCut(const FJevTurn& Turn, int32 Region)
{
	if (UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Turn.State))
		Announcer->Raise(FName(JevThreat::AnnouncerId), 0, GameStateTerritory::RegionAnchor(*Turn.State, Region), {});
}

// The catalogue index of each unit one cut force buys with Budget (JevThreat::Compose); empty when the catalogue has no
// Assault unit or the budget buys none.
TArray<int32> CutRoster(const FJevTurn& Turn, int32 Budget)
{
	const int32 Assault = Turn.Content->UnitIndexForRole(EUnitRole::Assault);
	const int32 Escort = Turn.Content->UnitIndexForRole(EUnitRole::Frontline);
	const UArmyUnitDefinition* AssaultUnit = Turn.Content->Unit(Assault);
	const UArmyUnitDefinition* EscortUnit = Turn.Content->Unit(Escort);
	TArray<int32> Roster;
	if (!AssaultUnit)
		return Roster;
	const JevThreat::FComposition Bought = JevThreat::Compose(Budget, AssaultUnit->UnitCost, AssaultUnit->Capacity,
		EscortUnit ? EscortUnit->UnitCost : 0);
	Roster.Init(Assault, Bought.Assault);
	for (int32 Count = 0; Count < Bought.Escort; ++Count)
		Roster.Add(Escort);
	return Roster;
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

void AEnemyCommander::AdvanceThreat(FJevTurn& Turn)
{
	for (;;)
	{
		const JevThreat::EStep Step = JevThreat::NextStep(ThreatStage, GetMatchSeconds());
		if (Step == JevThreat::EStep::None)
			return;
		if (Step == JevThreat::EStep::Publish)
			PublishThreat(Turn);
		else
			LaunchThreat(Turn);
	}
}

void AEnemyCommander::PublishThreat(FJevTurn& Turn)
{
	// The threat happens once per battle, published or skipped.
	ThreatStage = JevThreat::EStage::Done;
	const int32 Humans = HumanCommanders(*Turn.State);
	const TArray<JevThreat::FPair> Pairs = AuthoredPairs(Turn);
	const JevThreat::FChoice Choice = JevThreat::ChoosePair(Turn.Summary, Pairs);
	if (Choice.Skip != JevThreat::ESkip::None)
	{
		LogSkip(Turn, Pairs, Choice.Skip, GetMatchSeconds());
		return;
	}
	const JevThreat::FTargets Targets = JevThreat::ChooseTargets(Turn.Summary, Choice.Pair, Humans);
	const int32 Budget = JevThreat::ForceBudget(Humans);
	const TArray<int32> Roster = CutRoster(Turn, Budget);
	const float Lead = FMath::Max(0.f, JevThreat::LaunchTime() - GetMatchSeconds());
	const float SpeedFactor = JevRelease::BehaviourFor(JevThreat::TriggerRelease).SpeedFactor;
	for (int32 Index = 0; Index < Targets.Count; ++Index)
	{
		FPendingCut Cut;
		Cut.Target = Targets.Region[Index];
		Cut.Budget = Budget;
		Cut.Roster = Roster;
		const float March = Cut.Roster.IsEmpty() ? -1.f : MarchSeconds(Turn, Cut.Roster, SpeedFactor, Cut.Target);
		if (March < 0.f)
		{
			UE_LOG(LogJevRelease, Display, TEXT("JEV %s has no force for region %d: no unit bought or no route"), JevThreat::Name,
				Cut.Target);
			continue;
		}
		Release.Cuts.Add(CutPlan(Turn, NextTicketNumber++, Cut.Target, Cut.Roster.Num(), Lead, March));
		PendingCuts.Add(MoveTemp(Cut));
	}
	if (PendingCuts.IsEmpty())
		return;
	ThreatStage = JevThreat::EStage::Published;
	AnnounceCut(Turn, PendingCuts[0].Target);
	ForceNetUpdate();
	UE_LOG(LogJevRelease, Display, TEXT("JEV %s published at=%.1f pair=%d,%d held=%d fallback=%d forces=%d budget=%d humans=%d"),
		JevThreat::Name, GetMatchSeconds(), Choice.Pair.A, Choice.Pair.B, Choice.Held, Choice.bFallback, PendingCuts.Num(), Budget,
		Humans);
}

void AEnemyCommander::LaunchThreat(FJevTurn& Turn)
{
	ThreatStage = JevThreat::EStage::Done;
	Release.Cuts.Reset();
	const JevRelease::FBehaviour Behaviour = JevRelease::BehaviourFor(JevThreat::TriggerRelease);
	for (int32 Slot = 0; Slot < PendingCuts.Num(); ++Slot)
	{
		const FPendingCut& Cut = PendingCuts[Slot];
		AArmyGroup* Force = AArmyGroup::SpawnFreeForce(*Turn.World, *Turn.Commander,
			AssemblyPoint(Turn, Slot, PendingCuts.Num(), CutAssemblyDistance), Cut.Roster, FreeForceNumber(Turn),
			Behaviour.SpeedFactor);
		FJevWaveEvent Event;
		Event.Release = JevThreat::TriggerRelease;
		Event.bCut = true;
		Event.MatchSeconds = GetMatchSeconds();
		Event.Budget = Cut.Budget;
		Event.TargetRegion = Cut.Target;
		if (Force)
		{
			Event.Units = Force->GetAliveCount();
			Event.Forces = 1;
			WaveForces.Add(Force);
			FCommandService::SetRetreatThreshold(Turn.Commander, Force, ERetreatThreshold::Never);
			ExecuteWaveForce(Turn, Force, Cut.Target, false);
			// A cut force holds its order until it has arrived, so the planner does not re-route it on the way.
			if (FJevCommittedForce* Entry = CommittedForces.FindByPredicate(
					[Force](const FJevCommittedForce& Candidate) { return Candidate.Force == Force; }))
			{
				Entry->Plan.CommittedUntil = Turn.Now + Entry->Plan.EtaSeconds + JevPlanner::CommitmentSeconds;
				if (FJevPublishedPlan* Published = Turn.State->EnemyPlans.FindByPredicate(
						[Force](const FJevPublishedPlan& Candidate) { return Candidate.Force == Force; }))
				{
					Published->CommittedUntil = Entry->Plan.CommittedUntil;
					Published->RemainingCommitment = JevPlanner::Remaining(Entry->Plan, Turn.Now);
				}
			}
		}
		RecordWave(Event);
		--Release.WaveCount; // Published like a wave, but no release wave.
		UE_LOG(LogJevRelease, Display, TEXT("JEV %s launched at=%.1f target=%d units=%d budget=%d"), JevThreat::Name,
			Event.MatchSeconds, Cut.Target, Event.Units, Cut.Budget);
	}
	PendingCuts.Reset();
	ForceNetUpdate();
}
