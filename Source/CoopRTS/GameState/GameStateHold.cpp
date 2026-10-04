#include "CommandGameState.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "Content/UnitDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameState/GameStateRegistry.h"
#include "GameState/GameStateTerritory.h"
#include "FailoverNode.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/HoldPolicy.h"

namespace
{
using FGroups = TArray<AArmyGroup*, TInlineAllocator<32>>;
using FUnits = TArray<AArmyUnit*, TInlineAllocator<128>>;
using FPositions = TArray<FVector, TInlineAllocator<128>>;

int32 LivingPower(const AArmyGroup& Group)
{
	int32 Power = 0;
	for (const AArmyUnit* Unit : Group.GetUnits())
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetDefinition())
			Power += Unit->GetDefinition()->UnitCost;
	return Power;
}

float MaximumWeaponRange(const AArmyGroup& Group)
{
	float Range = 0.f;
	for (const AArmyUnit* Unit : Group.GetUnits())
		if (IsValid(Unit) && Unit->IsAlive())
			Range = FMath::Max(Range, Unit->WeaponRange());
	return Range;
}

void CollectPostContext(const ACommandGameState& State, const AMapRegion& Region, int32 Team,
	FPositions& Assets, FPositions& Borders)
{
	for (const ACommandBuilding* Building : State.Buildings)
		if (IsValid(Building) && Building->IsAlive() && Building->TeamIndex == Team
			&& Region.Contains(Building->GetActorLocation()))
			Assets.Add(Building->GetActorLocation());
	for (const AHeadquarters* HQ : { State.FriendlyHeadquarters.Get(), State.EnemyHeadquarters.Get() })
		if (IsValid(HQ) && HQ->IsAlive() && HQ->TeamIndex == Team && Region.Contains(HQ->GetActorLocation()))
			Assets.Add(HQ->GetActorLocation());
	for (int32 Neighbour : Region.Neighbours)
	{
		if (GameStateTerritory::RegionController(State, Neighbour) == Team)
			continue;
		const FVector Anchor = GameStateTerritory::RegionAnchor(State, Neighbour);
		const FVector2D Border = HoldPolicy::ClosestBoundary(Region.Polygon, FVector2D(Anchor));
		Borders.Add(FVector(Border.X, Border.Y, Region.GetActorLocation().Z));
	}
}

// One team's alarm in one region: who threatens it, who holds it and how the holders respond.
struct FRegionAlarm
{
	FRegionAlarm(const AMapRegion& InRegion, int32 InTeam)
		: Region(InRegion), Team(InTeam)
	{
	}
	bool IsAlarmed() const { return !Threats.IsEmpty(); }

	const AMapRegion& Region;
	const int32 Team;
	FGroups Holders;
	FUnits Threats;
	FPositions ThreatPositions;
	int32 ThreatPower = 0;
	FPositions Assets, Borders;
	TArray<int32, TInlineAllocator<3>> Occupancy;
	TArray<HoldPolicy::FCandidate, TInlineAllocator<32>> Candidates;
	bool bRegionWasResponding = false;
	int32 RespondingCount = 0;
};
}

// The pass touches ArmyGroup's hold state, which only the game state (and so its nested types) may reach.
struct ACommandGameState::FHoldAlarmPass
{
	explicit FHoldAlarmPass(ACommandGameState& InState)
		: State(InState), Now(InState.GetWorld()->GetTimeSeconds())
	{
	}
	void Run();
	void CollectForces();
	void ProcessTeam(const AMapRegion& Region, int32 Team);
	void ScanThreats(FRegionAlarm& Alarm) const;
	void SeedOccupancy(FRegionAlarm& Alarm) const;
	void BuildCandidates(FRegionAlarm& Alarm) const;
	void AssignPost(FRegionAlarm& Alarm, AArmyGroup& Holder) const;
	void UpdateHolder(FRegionAlarm& Alarm, int32 Index) const;
	void SelectThreat(const FRegionAlarm& Alarm, AArmyGroup& Holder) const;
	void ClassifyThreat(const FRegionAlarm& Alarm, AArmyGroup& Holder) const;
	void AnnounceResponse(const FRegionAlarm& Alarm) const;

	ACommandGameState& State;
	const double Now;
	FGroups Groups;
	FUnits Units;
};

void ACommandGameState::FHoldAlarmPass::Run()
{
	State.HoldDamage.Prune(Now);
	CollectForces();
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region) || Region->GetDefendPosts().IsEmpty())
			continue;
		for (int32 Team : { 0, 5 })
			ProcessTeam(*Region, Team);
	}
}

void ACommandGameState::FHoldAlarmPass::CollectForces()
{
	for (TActorIterator<AArmyGroup> It(State.GetWorld()); It; ++It)
	{
		if (It->IsHoldingRegion())
			Groups.Add(*It);
		for (AArmyUnit* Unit : It->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
				Units.Add(Unit);
	}
}

void ACommandGameState::FHoldAlarmPass::ProcessTeam(const AMapRegion& Region, int32 Team)
{
	FRegionAlarm Alarm(Region, Team);
	for (AArmyGroup* Group : Groups)
		if (Group->HoldRegionIndex == Region.RegionIndex && Group->GetTeamIndex() == Team)
			Alarm.Holders.Add(Group);
	if (Alarm.Holders.IsEmpty())
		return;
	ScanThreats(Alarm);
	CollectPostContext(State, Region, Team, Alarm.Assets, Alarm.Borders);
	SeedOccupancy(Alarm);
	BuildCandidates(Alarm);
	HoldPolicy::SelectResponders(Alarm.Candidates, Alarm.ThreatPower);
	for (int32 Index = 0; Index < Alarm.Holders.Num(); ++Index)
		UpdateHolder(Alarm, Index);
	AnnounceResponse(Alarm);
}

void ACommandGameState::FHoldAlarmPass::ScanThreats(FRegionAlarm& Alarm) const
{
	for (AArmyUnit* Unit : Units)
		if (HoldPolicy::IsAlarmSource(Unit->IsAlive(), Unit->GetTeamIndex() != Alarm.Team,
				Alarm.Region.Contains(Unit->GetActorLocation()),
				State.IsDamagingRegion(*Unit, Alarm.Region.RegionIndex, Alarm.Team)))
		{
			Alarm.Threats.Add(Unit);
			Alarm.ThreatPositions.Add(Unit->GetActorLocation());
			Alarm.ThreatPower += Unit->GetDefinition() ? Unit->GetDefinition()->UnitCost : 0;
		}
}

void ACommandGameState::FHoldAlarmPass::SeedOccupancy(FRegionAlarm& Alarm) const
{
	Alarm.Occupancy.Init(0, Alarm.Region.GetDefendPosts().Num());
	for (AArmyGroup* Holder : Alarm.Holders)
	{
		if (!Alarm.IsAlarmed())
			Holder->HoldPostIndex = INDEX_NONE;
		else if (Alarm.Occupancy.IsValidIndex(Holder->HoldPostIndex))
			++Alarm.Occupancy[Holder->HoldPostIndex];
	}
}

void ACommandGameState::FHoldAlarmPass::AssignPost(FRegionAlarm& Alarm, AArmyGroup& Holder) const
{
	Holder.HoldPostIndex = HoldPolicy::ChoosePost(Alarm.Region.GetDefendPosts(), Alarm.Occupancy, Alarm.Assets, Alarm.Borders);
	TArray<int32, TInlineAllocator<32>> UsedSlots;
	FPositions UsedLocations;
	for (const AArmyGroup* Other : Alarm.Holders)
		if (Other != &Holder && Other->HoldPostIndex == Holder.HoldPostIndex)
		{
			UsedSlots.Add(Other->HoldPostSlot);
			UsedLocations.Add(Other->HoldPostLocation);
		}
	Holder.HoldPostSlot = HoldPolicy::ChoosePostSlot(UsedSlots);
	++Alarm.Occupancy[Holder.HoldPostIndex];
	Holder.HoldPostLocation = HoldPolicy::ChoosePostLocation(Alarm.Region.Polygon,
		Alarm.Region.GetDefendPosts()[Holder.HoldPostIndex], Holder.HoldPostSlot, UsedLocations);
}

void ACommandGameState::FHoldAlarmPass::BuildCandidates(FRegionAlarm& Alarm) const
{
	for (AArmyGroup* Holder : Alarm.Holders)
	{
		Alarm.bRegionWasResponding |= Holder->bHoldResponding;
		if (!Alarm.Occupancy.IsValidIndex(Holder->HoldPostIndex))
			AssignPost(Alarm, *Holder);
		double Nearest = TNumericLimits<double>::Max();
		for (const AArmyUnit* Threat : Alarm.Threats)
			Nearest = FMath::Min(Nearest, FVector::DistSquared2D(Holder->GetCenter(), Threat->GetActorLocation()));
		Alarm.Candidates.Add({ Nearest, LivingPower(*Holder), Holder->bHoldResponding, false });
	}
}

void ACommandGameState::FHoldAlarmPass::UpdateHolder(FRegionAlarm& Alarm, int32 Index) const
{
	AArmyGroup* Holder = Alarm.Holders[Index];
	const bool bWasResponding = Holder->bHoldResponding;
	const AArmyUnit* PreviousThreat = Holder->HoldThreat;
	const AActor* PreviousAsset = Holder->HoldThreatenedAsset;
	Holder->bHoldResponding = HoldPolicy::UpdateClock(Holder->HoldClock, Now, Alarm.IsAlarmed(), Alarm.Candidates[Index].bSelected);
	Alarm.RespondingCount += Holder->bHoldResponding ? 1 : 0;
	SelectThreat(Alarm, *Holder);
	ClassifyThreat(Alarm, *Holder);
	if (bWasResponding != Holder->bHoldResponding)
		UE_LOG(LogTemp, Display, TEXT("Hold region=%d team=%d force=%s responding=%d threatPower=%d"),
			Alarm.Region.RegionIndex, Alarm.Team, *Holder->GetName(), Holder->bHoldResponding, Alarm.ThreatPower);
	if (bWasResponding != Holder->bHoldResponding || PreviousThreat != Holder->HoldThreat || PreviousAsset != Holder->HoldThreatenedAsset)
		Holder->ForceNetUpdate();
}

void ACommandGameState::FHoldAlarmPass::SelectThreat(const FRegionAlarm& Alarm, AArmyGroup& Holder) const
{
	const float WeaponRange = MaximumWeaponRange(Holder);
	TArray<bool, TInlineAllocator<128>> Permitted;
	int32 Current = INDEX_NONE;
	for (int32 ThreatIndex = 0; ThreatIndex < Alarm.Threats.Num(); ++ThreatIndex)
	{
		Permitted.Add(Holder.IsHoldTargetPermitted(*Alarm.Threats[ThreatIndex], Alarm.Region, WeaponRange));
		if (Holder.HoldThreat == Alarm.Threats[ThreatIndex])
			Current = ThreatIndex;
	}
	const int32 Chosen = Holder.bHoldResponding
		? HoldPolicy::ChooseThreat(Alarm.ThreatPositions, Permitted, Holder.GetCenter(), Current)
		: INDEX_NONE;
	Holder.HoldThreat = Alarm.Threats.IsValidIndex(Chosen) ? Alarm.Threats[Chosen] : nullptr;
}

void ACommandGameState::FHoldAlarmPass::ClassifyThreat(const FRegionAlarm& Alarm, AArmyGroup& Holder) const
{
	if (Holder.HoldThreat)
	{
		Holder.HoldThreatenedAsset = nullptr;
		Holder.HoldThreatKind = EHoldThreatKind::Intrusion;
		if (AActor* Victim = State.HoldDamage.FindVictim(Holder.HoldThreat, Alarm.Region.RegionIndex, Alarm.Team))
		{
			Holder.HoldThreatenedAsset = Victim;
			Holder.HoldThreatKind = Cast<AHeadquarters>(Victim)                 ? EHoldThreatKind::Headquarters
				: Cast<ACommandBuilding>(Victim) || Cast<AFailoverNode>(Victim) ? EHoldThreatKind::Building
																				: EHoldThreatKind::Force;
		}
	}
	if (!Holder.bHoldResponding)
		Holder.HoldThreatenedAsset = nullptr;
}

void ACommandGameState::FHoldAlarmPass::AnnounceResponse(const FRegionAlarm& Alarm) const
{
	if (Alarm.Team != 0 || Alarm.bRegionWasResponding || Alarm.RespondingCount == 0)
		return;
	UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(&State);
	if (!Announcer)
		return;
	TArray<FObjectiveForce> Responders;
	Responders.Reserve(Alarm.RespondingCount);
	for (const AArmyGroup* Holder : Alarm.Holders)
		if (Holder->bHoldResponding)
			for (const AArmyUnit* Unit : Holder->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive())
				{
					Responders.Add(UObjectiveAnnouncer::DescribeForce(Unit));
					break;
				}
	static const FName RespondingEvent(TEXT("region_defenders_responding"));
	Announcer->Raise(RespondingEvent, Alarm.Team, GameStateTerritory::RegionAnchor(State, Alarm.Region.RegionIndex), Responders);
}

void ACommandGameState::NotifyRegionDamage(AActor* Victim, int32 VictimTeam, AArmyUnit* Attacker)
{
	if (!HasAuthority() || MatchResult != EMatchResult::Ongoing || !IsValid(Victim)
		|| !IsValid(Attacker) || !Attacker->IsAlive() || Attacker->GetTeamIndex() == VictimTeam)
		return;
	const AMapRegion* Region = FindRegionAt(Victim->GetActorLocation());
	if (!Region)
		return;
	// A damage source remains current for one actual weapon cycle, plus the alarm
	// sampling interval. Subsequent hits renew the same entry, not a second threat.
	const double Expires = GetWorld()->GetTimeSeconds() + Attacker->AttackInterval() + .25;
	HoldDamage.Record(Attacker, Victim, VictimTeam, Region->RegionIndex, Expires);
}

bool ACommandGameState::IsDamagingRegion(const AArmyUnit& Attacker, int32 RegionIndex, int32 DefendingTeam) const
{
	return HoldDamage.IsDamaging(*this, Attacker, RegionIndex, DefendingTeam, GetWorld()->GetTimeSeconds());
}

void ACommandGameState::UpdateRegionAlarms()
{
	FHoldAlarmPass(*this).Run();
}
