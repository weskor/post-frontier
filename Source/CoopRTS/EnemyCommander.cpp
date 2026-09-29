#include "EnemyCommander.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "Headquarters.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AEnemyCommander::AEnemyCommander()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // Plans and rationale are replicated through GameState.
}

void AEnemyCommander::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || (EvaluateElapsed += DeltaSeconds) < 2.f) return;
	EvaluateElapsed = 0.f;
	EvaluatePlan();
}

bool AEnemyCommander::Choose(EGoal Next, ACapturePoint* Site, AArmyUnit* Threat,
	const FString& Reason, bool bEmergency)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!IsValid(Army) || !State || State->MatchResult != EMatchResult::Ongoing) return false;
	const bool bSame = Goal == Next && GoalSite == Site;
	const EArmyOrder Expected = Next == EGoal::RetreatReinforce ? EArmyOrder::Retreat : EArmyOrder::Attack;
	if (bSame && (Army->Units.IsEmpty() || (Army->Order == Expected
		&& (Next != EGoal::DefendHQ || Army->AttackTarget == Threat)))) return true;
	bool bAccepted = false;
	switch (Next)
	{
	case EGoal::Capture:
	case EGoal::Contest:
		bAccepted = IsValid(Site) && Army->IssueAttack(Site->GetActorLocation(), nullptr);
		break;
	case EGoal::DefendHQ:
		bAccepted = IsValid(Threat) && Army->IssueAttack(Threat->GetActorLocation(), Threat);
		break;
	case EGoal::AttackHQ:
		bAccepted = IsValid(State->FriendlyHeadquarters)
			&& Army->IssueAttack(State->FriendlyHeadquarters->GetActorLocation(), State->FriendlyHeadquarters.Get());
		break;
	case EGoal::RetreatReinforce:
		bAccepted = Army->Units.IsEmpty() || Army->IssueRetreat();
		break;
	default: break;
	}
	if (!bAccepted) return false; // Navigation rejects without replacing old intent.
	Goal = Next;
	GoalSite = Site;
	CommitUntil = GetWorld()->GetTimeSeconds() + 9.f;
	const TCHAR* Name = Next == EGoal::Capture ? TEXT("CAPTURE") : Next == EGoal::Contest ? TEXT("CONTEST")
		: Next == EGoal::DefendHQ ? TEXT("DEFEND HQ") : Next == EGoal::RetreatReinforce ? TEXT("RETREAT / REINFORCE")
		: TEXT("ATTACK HQ");
	State->EnemyPlan = Site ? FString::Printf(TEXT("%s SITE %d"), Name, Site->SiteIndex + 1) : Name;
	State->EnemyPlanRationale = FString::Printf(TEXT("%s%s"), bEmergency ? TEXT("EMERGENCY: ") : TEXT("COMMIT 9s: "), *Reason);
	State->ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("Enemy plan=%s rationale=%s"), *State->EnemyPlan, *State->EnemyPlanRationale);
	return true;
}

void AEnemyCommander::EvaluatePlan()
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsValid(Army) || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters)) return;
	const FVector Center = Army->GetCenter();
	const FVector EnemyHQ = State->EnemyHeadquarters->GetActorLocation();
	const FVector FriendlyHQ = State->FriendlyHeadquarters->GetActorLocation();
	int32 Living = 0;
	float HealthFraction = 0.f;
	for (const AArmyUnit* Unit : Army->Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive()) continue;
		++Living;
		HealthFraction += static_cast<float>(Unit->Health) / FMath::Max(1, Unit->MaxHealth());
	}
	if (Living) HealthFraction /= Living;
	int32 NearbyThreats = 0;
	int32 HQThreats = 0;
	int32 PlayerHQDefenders = 0;
	AArmyUnit* ClosestHQThreat = nullptr;
	float ClosestHQDistance = TNumericLimits<float>::Max();
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		if (It->TeamIndex != 0) continue;
		for (AArmyUnit* Unit : It->Units)
		{
			if (!IsValid(Unit) || !Unit->IsAlive()) continue;
			const FVector Location = Unit->GetActorLocation();
			if (FVector::DistSquared2D(Location, Center) < FMath::Square(1400.f)) ++NearbyThreats;
			if (FVector::DistSquared2D(Location, FriendlyHQ) < FMath::Square(1650.f)) ++PlayerHQDefenders;
			const float HQDistance = FVector::DistSquared2D(Location, EnemyHQ);
			if (HQDistance < FMath::Square(1550.f))
			{
				++HQThreats;
				if (HQDistance < ClosestHQDistance) { ClosestHQDistance = HQDistance; ClosestHQThreat = Unit; }
			}
		}
	}
	// Both emergencies can interrupt commitment. An immediate threat to the HQ
	// takes precedence unless fewer than two defenders can still fight.
	if (Living >= 2 && HQThreats && IsValid(ClosestHQThreat))
	{
		Choose(EGoal::DefendHQ, nullptr, ClosestHQThreat,
			FString::Printf(TEXT("%d intruders within 1550 of HQ"), HQThreats), true);
		return;
	}
	const bool bBroken = Living <= 2 || (Living < 5 && NearbyThreats >= Living)
		|| (HealthFraction < .48f && NearbyThreats >= 2 && FVector::DistSquared2D(Center, Army->HomeLocation) > FMath::Square(600.f));
	if (bBroken)
	{
		Choose(EGoal::RetreatReinforce, nullptr, nullptr,
			FString::Printf(TEXT("strength %d/6, health %.0f%%, nearby threats %d"), Living, HealthFraction * 100.f, NearbyThreats), true);
		if (Army->CanReinforceAtCurrentLocation()) Army->TryReinforce();
		return;
	}
	// A retreat commitment waits at a valid source for affordable paid recovery;
	// never feed an incomplete roster back into the same fight.
	if (Goal == EGoal::RetreatReinforce && Army->GetReinforcementCost() > 0)
	{
		if (Army->CanReinforceAtCurrentLocation()) Army->TryReinforce();
		if (Army->GetReinforcementCost() > 0) return;
	}
	// A fully restored roster has completed this commitment; score fresh goals.
	if (Goal == EGoal::RetreatReinforce && Army->GetReinforcementCost() == 0) CommitUntil = 0.f;
	if (GetWorld()->GetTimeSeconds() < CommitUntil && Goal != EGoal::None
		&& ((Goal != EGoal::Capture && Goal != EGoal::Contest)
			|| (GoalSite.IsValid() && GoalSite->ControllingTeam != 5))) return;

	ACapturePoint* BestSite = nullptr;
	float BestScore = -TNumericLimits<float>::Max();
	float BestRisk = 0.f;
	for (ACapturePoint* Site : State->CaptureSites)
	{
		if (!IsValid(Site) || Site->ControllingTeam == 5) continue;
		int32 Defenders = 0;
		for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		{
			if (It->TeamIndex != 0) continue;
			for (const AArmyUnit* Unit : It->Units)
				if (IsValid(Unit) && Unit->IsAlive() && FVector::DistSquared2D(Unit->GetActorLocation(), Site->GetActorLocation()) < FMath::Square(1200.f)) ++Defenders;
		}
		const float Travel = FVector::Dist2D(Center, Site->GetActorLocation()) / 700.f;
		const float Risk = Defenders * (Living < Defenders ? 1.7f : .65f);
		const float Value = Site->SiteKind == ECaptureSiteKind::Resource ? 8.f : 6.f;
		const float Urgency = Site->ControllingTeam == 0 ? 3.f : 0.f;
		const float Score = Value + Urgency - Travel - Risk;
		if (Score > BestScore) { BestScore = Score; BestSite = Site; BestRisk = Risk; }
	}
	const bool bAdvantage = Living >= 4 && Living >= PlayerHQDefenders + 2;
	const float AttackScore = bAdvantage ? 11.f - FVector::Dist2D(Center, FriendlyHQ) / 750.f
		- PlayerHQDefenders * .8f : -TNumericLimits<float>::Max();
	if (AttackScore > BestScore)
	{
		Choose(EGoal::AttackHQ, nullptr, nullptr,
			FString::Printf(TEXT("advantage %d versus %d HQ defenders, score %.1f"), Living, PlayerHQDefenders, AttackScore), false);
	}
	else if (BestSite)
	{
		Choose(BestSite->ControllingTeam == 0 ? EGoal::Contest : EGoal::Capture, BestSite, nullptr,
			FString::Printf(TEXT("value %d, travel %.1f, risk %.1f, urgency %d, score %.1f"),
				BestSite->SiteKind == ECaptureSiteKind::Resource ? 8 : 6,
				FVector::Dist2D(Center, BestSite->GetActorLocation()) / 700.f, BestRisk,
				BestSite->ControllingTeam == 0 ? 3 : 0, BestScore), false);
	}
}
