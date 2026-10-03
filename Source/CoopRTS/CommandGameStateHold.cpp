#include "CommandGameState.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CombatTarget.h"
#include "CommandBuilding.h"
#include "Content/UnitDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
		if (State.GetRegionController(Neighbour) == Team)
			continue;
		const FVector Anchor = State.GetRegionAnchor(Neighbour);
		const FVector2D Border = HoldPolicy::ClosestBoundary(Region.Polygon, FVector2D(Anchor));
		Borders.Add(FVector(Border.X, Border.Y, Region.GetActorLocation().Z));
	}
}
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
	for (FHoldDamageSource& Source : HoldDamageSources)
		if (Source.Attacker == Attacker && Source.Team == VictimTeam && Source.Region == Region->RegionIndex)
		{
			Source.Victim = Victim;
			Source.Expires = Expires;
			return;
		}
	HoldDamageSources.Add({ Attacker, Victim, VictimTeam, Region->RegionIndex, Expires });
}

bool ACommandGameState::IsDamagingRegion(const AArmyUnit& Attacker, int32 RegionIndex, int32 DefendingTeam) const
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (const FHoldDamageSource& Source : HoldDamageSources)
	{
		if (Source.Attacker.Get() != &Attacker || Source.Team != DefendingTeam || Source.Region != RegionIndex || !Source.Victim.IsValid())
			continue;
		const AMapRegion* VictimRegion = FindRegionAt(Source.Victim->GetActorLocation());
		if (VictimRegion && VictimRegion->RegionIndex == RegionIndex
			&& HoldPolicy::IsDamageCurrent(Now, Source.Expires,
				CombatTarget::IsAliveHostile(Source.Victim.Get(), Attacker.GetTeamIndex()),
				FVector::DistSquared2D(Attacker.GetActorLocation(), Source.Victim->GetActorLocation()) <= FMath::Square(Attacker.WeaponRange())))
			return true;
	}
	return false;
}

void ACommandGameState::UpdateRegionAlarms()
{
	const double Now = GetWorld()->GetTimeSeconds();
	HoldDamageSources.RemoveAllSwap([Now](const FHoldDamageSource& Source) {
		return !Source.Attacker.IsValid() || !Source.Attacker->IsAlive() || Source.Expires <= Now;
	},
		EAllowShrinking::No);
	FGroups Groups;
	FUnits Units;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		if (It->IsHoldingRegion())
			Groups.Add(*It);
		for (AArmyUnit* Unit : It->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
				Units.Add(Unit);
	}
	for (const AMapRegion* Region : Regions)
	{
		if (!IsValid(Region) || Region->GetDefendPosts().IsEmpty())
			continue;
		for (int32 Team : { 0, 5 })
		{
			FGroups Holders;
			for (AArmyGroup* Group : Groups)
				if (Group->HoldRegionIndex == Region->RegionIndex && Group->GetTeamIndex() == Team)
					Holders.Add(Group);
			if (Holders.IsEmpty())
				continue;
			FUnits Threats;
			FPositions ThreatPositions;
			int32 ThreatPower = 0;
			for (AArmyUnit* Unit : Units)
				if (HoldPolicy::IsAlarmSource(Unit->IsAlive(), Unit->GetTeamIndex() != Team,
						Region->Contains(Unit->GetActorLocation()), IsDamagingRegion(*Unit, Region->RegionIndex, Team)))
				{
					Threats.Add(Unit);
					ThreatPositions.Add(Unit->GetActorLocation());
					ThreatPower += Unit->GetDefinition() ? Unit->GetDefinition()->UnitCost : 0;
				}
			const bool bAlarm = !Threats.IsEmpty();
			FPositions Assets, Borders;
			CollectPostContext(*this, *Region, Team, Assets, Borders);
			TArray<int32, TInlineAllocator<3>> Occupancy;
			Occupancy.Init(0, Region->GetDefendPosts().Num());
			for (AArmyGroup* Holder : Holders)
			{
				if (!bAlarm)
					Holder->HoldPostIndex = INDEX_NONE;
				else if (Occupancy.IsValidIndex(Holder->HoldPostIndex))
					++Occupancy[Holder->HoldPostIndex];
			}
			bool bRegionWasResponding = false;
			int32 RespondingCount = 0;
			TArray<HoldPolicy::FCandidate, TInlineAllocator<32>> Candidates;
			for (AArmyGroup* Holder : Holders)
			{
				bRegionWasResponding |= Holder->bHoldResponding;
				if (!Occupancy.IsValidIndex(Holder->HoldPostIndex))
				{
					Holder->HoldPostIndex = HoldPolicy::ChoosePost(Region->GetDefendPosts(), Occupancy, Assets, Borders);
					TArray<int32, TInlineAllocator<32>> UsedSlots;
					FPositions UsedLocations;
					for (const AArmyGroup* Other : Holders)
						if (Other != Holder && Other->HoldPostIndex == Holder->HoldPostIndex)
						{
							UsedSlots.Add(Other->HoldPostSlot);
							UsedLocations.Add(Other->HoldPostLocation);
						}
					Holder->HoldPostSlot = HoldPolicy::ChoosePostSlot(UsedSlots);
					++Occupancy[Holder->HoldPostIndex];
					Holder->HoldPostLocation = HoldPolicy::ChoosePostLocation(Region->Polygon,
						Region->GetDefendPosts()[Holder->HoldPostIndex], Holder->HoldPostSlot, UsedLocations);
				}
				double Nearest = TNumericLimits<double>::Max();
				for (AArmyUnit* Threat : Threats)
					Nearest = FMath::Min(Nearest, FVector::DistSquared2D(Holder->GetCenter(), Threat->GetActorLocation()));
				Candidates.Add({ Nearest, LivingPower(*Holder), Holder->bHoldResponding, false });
			}
			HoldPolicy::SelectResponders(Candidates, ThreatPower);
			for (int32 Index = 0; Index < Holders.Num(); ++Index)
			{
				AArmyGroup* Holder = Holders[Index];
				const bool bWasResponding = Holder->bHoldResponding;
				const AArmyUnit* PreviousThreat = Holder->HoldThreat;
				const AActor* PreviousAsset = Holder->HoldThreatenedAsset;
				Holder->bHoldResponding = HoldPolicy::UpdateClock(Holder->HoldClock, Now, bAlarm, Candidates[Index].bSelected);
				RespondingCount += Holder->bHoldResponding ? 1 : 0;
				TArray<bool, TInlineAllocator<128>> Permitted;
				int32 Current = INDEX_NONE;
				for (int32 ThreatIndex = 0; ThreatIndex < Threats.Num(); ++ThreatIndex)
				{
					Permitted.Add(Holder->IsHoldTargetPermitted(*Threats[ThreatIndex]));
					if (Holder->HoldThreat == Threats[ThreatIndex])
						Current = ThreatIndex;
				}
				const int32 Chosen = Holder->bHoldResponding
					? HoldPolicy::ChooseThreat(ThreatPositions, Permitted, Holder->GetCenter(), Current)
					: INDEX_NONE;
				Holder->HoldThreat = Threats.IsValidIndex(Chosen) ? Threats[Chosen] : nullptr;
				if (Holder->HoldThreat)
				{
					Holder->HoldThreatenedAsset = nullptr;
					Holder->HoldThreatKind = EHoldThreatKind::Intrusion;
					for (const FHoldDamageSource& Source : HoldDamageSources)
						if (Source.Attacker == Holder->HoldThreat && Source.Region == Region->RegionIndex
							&& Source.Team == Team && Source.Victim.IsValid())
						{
							Holder->HoldThreatenedAsset = Source.Victim.Get();
							Holder->HoldThreatKind = Cast<AHeadquarters>(Source.Victim.Get()) ? EHoldThreatKind::Headquarters
								: Cast<ACommandBuilding>(Source.Victim.Get())                 ? EHoldThreatKind::Building
																							  : EHoldThreatKind::Force;
							break;
						}
				}
				if (!Holder->bHoldResponding)
					Holder->HoldThreatenedAsset = nullptr;
				if (bWasResponding != Holder->bHoldResponding)
					UE_LOG(LogTemp, Display, TEXT("Hold region=%d team=%d force=%s responding=%d threatPower=%d"),
						Region->RegionIndex, Team, *Holder->GetName(), Holder->bHoldResponding, ThreatPower);
				if (bWasResponding != Holder->bHoldResponding || PreviousThreat != Holder->HoldThreat || PreviousAsset != Holder->HoldThreatenedAsset)
					Holder->ForceNetUpdate();
			}
			if (!bRegionWasResponding && RespondingCount > 0)
				if (UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this))
				{
					TArray<FObjectiveForce> Responders;
					Responders.Reserve(RespondingCount);
					for (const AArmyGroup* Holder : Holders)
						if (Holder->bHoldResponding)
							for (const AArmyUnit* Unit : Holder->GetUnits())
								if (IsValid(Unit) && Unit->IsAlive())
								{
									Responders.Add(UObjectiveAnnouncer::DescribeForce(Unit));
									break;
								}
					static const FName RespondingEvent(TEXT("region_defenders_responding"));
					Announcer->Raise(RespondingEvent, Team, GetRegionAnchor(Region->RegionIndex), Responders);
				}
		}
	}
}
