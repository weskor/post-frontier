#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

namespace HoldAlarmFixture
{
double FScenario::Power(const AArmyGroup& Group)
{
	double Sum = 0.;
	for (const AArmyUnit* Unit : Group.GetUnits())
		if (IsValid(Unit) && Unit->IsAlive())
			Sum += Unit->GetDefinition()->UnitCost;
	return Sum;
}

void FScenario::BeginAlarm(double Now)
{
	if (Case != ECase::Jev)
		++ExpectedResponseEvents;
	Teleport(Threats[0].Get(), Case == ECase::Border ? Outside : Intrusion);
	ThreatInitialHealth = Threats[0]->GetHealth();
	bShootBuilding = Building.IsValid();
	if (bShootBuilding)
		Threats[0]->NextAttackTime = 0.f;
	ExpectedNearest = SortedByDistance(Threats[0]->GetActorLocation(), InitialCenters);
	SetStage(EStage::Respond, Now);
}

TArray<int32> FScenario::Responders() const
{
	TArray<int32> Result;
	for (int32 Index = 0; Index < Holders.Num(); ++Index)
		if (Holders[Index]->bHoldResponding)
			Result.Add(Index);
	return Result;
}

TArray<int32> FScenario::SortedByDistance(const FVector& Target, const TArray<FVector>& Centers)
{
	TArray<int32> Result;
	for (int32 Index = 0; Index < Centers.Num(); ++Index)
		Result.Add(Index);
	Result.Sort([&](int32 A, int32 B) { return FVector::DistSquared2D(Centers[A], Target) < FVector::DistSquared2D(Centers[B], Target); });
	return Result;
}

bool FScenario::CheckNearest(int32 Count)
{
	double Sum = 0.;
	for (int32 Index = 0; Index < Holders.Num(); ++Index)
	{
		const bool Expected = ExpectedNearest.Find(Index) < Count;
		if (!Check(Holders[Index]->bHoldResponding == Expected, TEXT("Only the nearest sufficient subset responds to the small feint")))
			return false;
		if (Expected)
			Sum += Power(*Holders[Index]);
	}
	const double ThreatPower = Threats[0]->GetDefinition()->UnitCost;
	if (!Check(Sum >= 1.25 * ThreatPower && Sum - Power(*Holders[ExpectedNearest[Count - 1]]) < 1.25 * ThreatPower,
			TEXT("Responding living Power reaches 1.25 times the threat without an unnecessary extra holder")))
		return false;
	if (Case == ECase::SharedCommanders)
		return Check(Holders[ExpectedNearest[0]]->GetOwningPlayerState() != Holders[ExpectedNearest[1]]->GetOwningPlayerState(),
			TEXT("The sufficient nearest response pools forces of two distinct commander wallets"));
	return true;
}

bool FScenario::CheckTargets()
{
	for (int32 Index = 0; Index < Holders.Num(); ++Index)
	{
		if (!Check(Holders[Index]->HoldPostLocation.Equals(Posts[Index], 1.), TEXT("An active alarm never reassigns a holder's post")))
			return false;
		const AArmyUnit* Target = Holders[Index]->HoldThreat;
		if (Holders[Index]->bHoldResponding
			&& !Check(IsValid(Target) && Target->IsAlive()
					&& Threats.ContainsByPredicate([Target](const TWeakObjectPtr<AArmyUnit>& Threat) { return Threat.Get() == Target; })
					&& Holders[Index]->IsHoldTargetPermitted(*Target, *Region, Holders[Index]->GetUnits()[0]->WeaponRange()),
				TEXT("Responders expose an actual eligible living threat")))
			return false;
		if (Case == ECase::Border && Holders[Index]->bHoldResponding
			&& !Check(Holders[Index]->HoldThreatenedAsset == Building.Get() && Holders[Index]->HoldThreatKind == EHoldThreatKind::Building,
				TEXT("Outside damage exposes the attacked building and building threat kind")))
			return false;
	}
	return true;
}

bool FScenario::CheckIdleHolders(const TArray<int32>& Active)
{
	for (int32 Index = 0; Index < Holders.Num(); ++Index)
		if (!Active.Contains(Index) && !Check(!Holders[Index]->bHoldResponding && FVector::Dist2D(Holders[Index]->GetCenter(), Posts[Index]) < 220. && Holders[Index]->GetUnits()[0]->AttackCount == 0, TEXT("Unneeded holders stay at their posts and do not join combat")))
			return false;
	return true;
}

void FScenario::EnableWeapons()
{
	for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
		for (AArmyUnit* Unit : Holder->GetUnits())
			Unit->NextAttackTime = 0.f;
}

uint32 FScenario::TotalAttacks() const
{
	uint32 Result = 0;
	for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
		for (const AArmyUnit* Unit : Holder->GetUnits())
			Result += Unit->AttackCount;
	return Result;
}

bool FScenario::AllEnteredThreatsDead() const
{
	const int32 Count = Case == ECase::Proportional ? 3 : 1;
	for (int32 Index = 0; Index < Count; ++Index)
		if (Threats[Index].IsValid() && Threats[Index]->IsAlive())
			return false;
	return true;
}

double FScenario::DistanceToBorder(const FVector& Location) const
{
	double Best = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Region->Polygon.Num(); ++Index)
	{
		const FVector2D A = Region->Polygon[Index], B = Region->Polygon[(Index + 1) % Region->Polygon.Num()];
		const FVector P(Location.X, Location.Y, 0.), Start(A.X, A.Y, 0.), End(B.X, B.Y, 0.);
		Best = FMath::Min(Best, FMath::PointDistToSegment(P, Start, End));
	}
	return Best;
}

int32 FScenario::ResponseEventCount() const
{
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(State.Get());
	if (!Announcer)
		return INDEX_NONE;
	int32 Count = 0;
	for (const FObjectiveEvent& Event : Announcer->GetEvents())
		if (Event.Id == TEXT("region_defenders_responding") && Event.RegionIndex == Region->RegionIndex
			&& (Case == ECase::Jev || Event.AffectedTeam == Holders[0]->GetTeamIndex()))
			++Count;
	return Count;
}

bool FScenario::CheckResponseFeed(bool bCheckInitialForces)
{
	if (!Check(ResponseEventCount() == ExpectedResponseEvents,
			Case == ECase::Jev
				? TEXT("JEV responses produce no player-feed/voice event, including across combat and return")
				: TEXT("Each response episode produces one region alert; polling, growth and target changes do not spam it")))
		return false;
	if (Case == ECase::Jev || !bCheckInitialForces)
		return true;
	const FObjectiveEvent* Latest = nullptr;
	for (const FObjectiveEvent& Event : UObjectiveAnnouncer::Get(State.Get())->GetEvents())
		if (Event.Id == TEXT("region_defenders_responding") && Event.RegionIndex == Region->RegionIndex
			&& Event.AffectedTeam == Holders[0]->GetTeamIndex())
			Latest = &Event;
	if (!Check(Latest && Latest->Forces.Num() == FirstResponders.Num(),
			TEXT("The regional alert attributes exactly the initial responders")))
		return false;
	for (int32 Index : FirstResponders)
	{
		const AArmyGroup* Holder = Holders[Index].Get();
		const ACommandPlayerState* Owner = Holder->GetOwningPlayerState();
		if (!Check(Latest->Forces.ContainsByPredicate([Holder, Owner](const FObjectiveForce& Force) {
				return Force.TeamIndex == Holder->GetTeamIndex() && Force.CommanderIndex == Owner->CommanderIndex
					&& Force.ForceNumber == Holder->ForceNumber;
			}),
				TEXT("The alert retains the responding force identity across commanders and factions")))
			return false;
	}
	return true;
}
}

#endif
