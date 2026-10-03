#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

namespace HoldAlarmFixture
{
bool FScenario::Check(bool Condition, const TCHAR* Message)
{
	if (!Condition)
	{
		Test->AddError(Message);
		bFailed = true;
	}
	return Condition;
}

void FScenario::SetStage(EStage Next, double Now)
{
	Stage = Next;
	StageStarted = Now;
}

bool FScenario::CheckTimeout()
{
	if (FPlatformTime::Seconds() - Started > 180.)
	{
		Test->AddError(FString::Printf(TEXT("Hold scenario timed out in stage %d"), static_cast<int32>(Stage)));
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			if (Holder.IsValid())
				Test->AddInfo(FString::Printf(TEXT("%s region=%d post=%d responding=%d center=%s postLocation=%s threat=%s start=%.2f quiet=%.2f"),
					*Holder->GetName(), Holder->HoldRegionIndex, Holder->HoldPostIndex, Holder->bHoldResponding,
					*Holder->GetCenter().ToCompactString(), *Holder->HoldPostLocation.ToCompactString(),
					*GetNameSafe(Holder->HoldThreat), Holder->GetHoldResponseStarted(), Holder->GetHoldQuietSince()));
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			if (Holder.IsValid())
				for (const AArmyUnit* Unit : Holder->GetUnits())
				{
					const AAIController* AI = Cast<AAIController>(Unit->GetController());
					Test->AddInfo(FString::Printf(TEXT("Hold member %s pos=%s velocity=%s pursuing=%d goal=%s move=%d attacks=%u next=%.2f target=%s targetPos=%s targetHP=%d"),
						*Unit->GetName(), *Unit->GetActorLocation().ToCompactString(), *Unit->GetVelocity().ToCompactString(),
						Unit->bPursuing, *Unit->PursuitGoal.ToCompactString(), AI ? static_cast<int32>(AI->GetMoveStatus()) : -1,
						Unit->AttackCount, Unit->NextAttackTime, *GetNameSafe(Unit->Target),
						Unit->Target ? *Unit->Target->GetActorLocation().ToCompactString() : TEXT("none"),
						Cast<AArmyUnit>(Unit->Target) ? Cast<AArmyUnit>(Unit->Target)->GetHealth() : -1));
				}
		return true;
	}
	return false;
}

FScenario::EStep FScenario::PrepareUpdate(double& Now)
{
	if (bFailed || CheckTimeout())
		return EStep::Done;
	if (Stage == EStage::Setup)
		return Setup() ? EStep::Done : EStep::Waiting;
	if (!Check(State.IsValid() && Region.IsValid(), TEXT("The isolated map and held region survive")))
		return EStep::Done;
	Now = State->GetWorld()->GetTimeSeconds();
	if (!ObserveHolders())
		return EStep::Done;
	if (bShootBuilding && Threats[0].IsValid() && Threats[0]->IsAlive() && Building.IsValid())
		Threats[0]->FireAt(Building.Get()); // Real cooldown, range, damage and notification.
	return EStep::Continue;
}

bool FScenario::ObserveHolders()
{
	for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
	{
		if (!Check(Holder.IsValid() && Power(*Holder) > 0., TEXT("Every holder retains living Power")))
			return false;
		if (Stage == EStage::Posts
			&& FVector::Dist2D(Holder->GetCenter(), State->GetRegionAnchor(Region->RegionIndex)) <= ACapturePoint::CaptureRadius)
			ReachedAnchors.Add(Holder.Get());
		if (Holder->bHoldResponding && FVector::Dist2D(Holder->GetCenter(), InitialCenters[Holders.IndexOfByKey(Holder)]) > 100.)
			MovedHolders.Add(Holder.Get());
		if (!ObserveCaseHolder(Holder))
			return false;
	}
	return ObserveCaseBorders();
}

bool FScenario::RunPosts(double Now)
{
	if (!AtPosts())
		return bFailed;
	if (!CheckPosts())
		return true;
	if (!CheckPostsForCase())
		return true;
	InitialCenters.Reset();
	for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
		InitialCenters.Add(Holder->GetCenter());
	if (!ChooseIntrusion())
		return true;
	ExpectedResponseEvents = ResponseEventCount();
	if (!Check(ExpectedResponseEvents >= 0, TEXT("The authoritative objective history is available")))
		return true;
	if (Case == ECase::Jev && !Check(ExpectedResponseEvents == 0, TEXT("The isolated JEV fixture starts without a player response announcement")))
		return true;
	BeginAlarm(Now);
	return bFailed;
}

bool FScenario::RunRespond(double Now)
{
	if (Responders().Num() != 2)
		return bFailed;
	if (!CheckNearest(2) || !CheckTargets())
		return true;
	FirstResponders = Responders();
	FirstTarget = Holders[FirstResponders[0]]->HoldThreat;
	ResponseStarted = Holders[FirstResponders[0]]->GetHoldResponseStarted();
	if (!CheckResponseFeed(true))
		return true;
	if (!Check(FirstTarget == Threats[0].Get(), TEXT("Nearest eligible intrusion or damaging attacker is the response target")))
		return true;
	return AdvanceResponse(Now);
}

bool FScenario::RunCombat(double Now)
{
	if (!CheckResponseFeed())
		return true;
	if (Building.IsValid() && Building->Health < BuildingInitialHealth)
		bObservedBuildingDamage = true;
	if (Threats[0].IsValid() && Threats[0]->GetHealth() < ThreatInitialHealth)
		bObservedThreatDamage = true;
	if (!AllEnteredThreatsDead())
		return bFailed;
	if (!Check(MovedHolders.Num() > 0, TEXT("A responding force actually travels away from its assigned post"))
		|| !Check(TotalAttacks() > 0, TEXT("Responders execute live weapon attacks"))
		|| !Check(bObservedThreatDamage, TEXT("Live responder weapons lower the hostile's health before killing it")))
		return true;
	if (!CheckCombatForCase())
		return true;
	if (Building.IsValid() && (!Check(bObservedBuildingDamage, TEXT("The far/edge building took live weapon damage")) || !Check(Building->IsAlive(), TEXT("Responders save the attacked building"))))
		return true;
	bShootBuilding = false;
	BeginReturn();
	SetStage(EStage::Return, Now);
	return bFailed;
}

bool FScenario::RunReturn(double Now)
{
	if (!Responders().IsEmpty() || !AtPosts() || Now - StageStarted < 2.)
		return bFailed;
	return Finish();
}

bool FScenario::AtPosts() const
{
	for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
		if (!Holder->IsHoldingRegion() || Holder->HoldRegionIndex != Region->RegionIndex
			|| FVector::Dist2D(Holder->GetCenter(), Holder->HoldPostLocation) > 220.
			|| Holder->GetUnits()[0]->GetVelocity().SizeSquared2D() > 1.)
			return false;
	return true;
}

bool FScenario::CheckPosts()
{
	if (!Check(!Region->Anchor || State->GetRegionController(Region->RegionIndex) == Holders[0]->GetTeamIndex(),
			TEXT("A capturable target is secured by live anchor occupancy before holders settle at posts")))
		return false;
	TArray<int32> Occupancy;
	Occupancy.Init(0, Region->GetDefendPosts().Num());
	Posts.Reset();
	for (int32 Index = 0; Index < Holders.Num(); ++Index)
	{
		const AArmyGroup* Holder = Holders[Index].Get();
		if (!Check(ReachedAnchors.Contains(Holders[Index].Get()) && Holder->Verb == EForceVerb::MoveHold
					&& Holder->Status == EForceStatus::Holding && Holder->TargetRegionIndex == Region->RegionIndex
					&& Holder->Orders.Num() == 1 && Holder->Orders[0].Verb == EForceVerb::MoveHold
					&& Holder->Orders[0].RegionIndex == Region->RegionIndex,
				TEXT("Every accepted MoveHold physically reaches its target anchor before occupying regional posts")))
			return false;
		if (!Check(!Holder->bHoldResponding && Occupancy.IsValidIndex(Holder->HoldPostIndex), TEXT("Idle holders occupy real authored defend posts")))
			return false;
		++Occupancy[Holder->HoldPostIndex];
		Posts.Add(Holder->HoldPostLocation);
		for (int32 Other = 0; Other < Index; ++Other)
			if (!Check(FVector::Dist2D(Posts[Index], Posts[Other]) > 100., TEXT("Even post overflow receives distinct observable formation locations")))
				return false;
	}
	int32 Min = Holders.Num(), Max = 0;
	for (int32 Count : Occupancy)
	{
		Min = FMath::Min(Min, Count);
		Max = FMath::Max(Max, Count);
	}
	return Check(Max - Min <= 1, TEXT("Overflow shares least-occupied posts rather than stacking one post"));
}

bool FScenario::Finish()
{
	if (!CheckResponseFeed())
		return true;
	if (!Check(AtPosts() && Responders().IsEmpty(), TEXT("Quiet holders physically return to their unchanged defend posts")))
		return true;
	for (int32 Index = 0; Index < Holders.Num(); ++Index)
		if (!Check(Holders[Index]->HoldPostLocation.Equals(Posts[Index], 1.), TEXT("The response preserves each original post through return")))
			return true;
	Test->AddInfo(TEXT("Isolated hold scenario passed with live map navigation, natural game-time response, and observed combat/return state."));
	return true;
}
}

#endif
