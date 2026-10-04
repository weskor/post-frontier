#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Headquarters.h"
#include "MatchSimulationJson.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

using namespace MatchSimulationJson;

void FSimulationDuelRunner::Observe()
{
	Survivors[0] = Survivors[1] = SurvivorPower[0] = SurvivorPower[1] = 0;
	for (FMember& Member : Members)
	{
		if (AArmyUnit* Unit = Member.Unit.Get())
		{
			const int32 Health = Unit->GetHealth();
			const int32 Shield = Unit->GetShield();
			const int32 RemovedHealth = FMath::Max(0, Member.Health - Health);
			const int32 RemovedShield = FMath::Max(0, Member.Shield - Shield);
			Damage[1 - Member.Side] += RemovedHealth;
			ShieldDamage[1 - Member.Side] += RemovedShield;
			// Shield loss is combat: a fight whose only effect so far is stripped shields is not a stall.
			if (RemovedHealth > 0 || RemovedShield > 0)
				LastDamageElapsed = Elapsed;
			Attacks[Member.Side] += Unit->AttackCount - Member.Attacks;
			Member.Health = Health;
			Member.Shield = Shield;
			Member.Attacks = Unit->AttackCount;
			Survivors[Member.Side] += Unit->IsAlive() ? 1 : 0;
			SurvivorPower[Member.Side] += Unit->IsAlive() ? Member.Cost : 0;
		}
		else if (Member.Health > 0)
			Error = TEXT("Live duel member disappeared without an observed combat death");
	}
	if (LastDamageElapsed < 0. && (Attacks[0] > 0 || Attacks[1] > 0))
		LastDamageElapsed = Elapsed;
}

void FSimulationDuelRunner::UpdateRow() const
{
	if (!Current.IsValid() || !Error.IsEmpty())
		return;
	static const FString SurvivorsField(TEXT("survivors"));
	static const FString PowerField(TEXT("survivor_power"));
	static const FString DamageField(TEXT("damage_dealt"));
	static const FString ShieldDamageField(TEXT("shield_damage_dealt"));
	static const FString AttacksField(TEXT("attacks"));
	static const FString DurationField(TEXT("duration"));
	static const FString NoDamageField(TEXT("no_damage_seconds"));
	UpdateNumbers(*Current, SurvivorsField, Survivors[0], Survivors[1]);
	UpdateNumbers(*Current, PowerField, SurvivorPower[0], SurvivorPower[1]);
	UpdateNumbers(*Current, DamageField, Damage[0], Damage[1]);
	UpdateNumbers(*Current, ShieldDamageField, ShieldDamage[0], ShieldDamage[1]);
	UpdateNumbers(*Current, AttacksField, Attacks[0], Attacks[1]);
	StaticCastSharedPtr<FDuelNumber>(Current->GetField<EJson::Number>(DurationField))->Set(Elapsed);
	StaticCastSharedPtr<FDuelNumber>(Current->GetField<EJson::Number>(NoDamageField))->Set(LastDamageElapsed < 0. ? 0. : Elapsed - LastDamageElapsed);
	StaticCastSharedPtr<FDuelNumber>(Report->GetField<EJson::Number>(DurationField))->Set(TotalElapsed);
}

TSharedRef<FJsonObject> FSimulationDuelRunner::GetReport() const
{
	UpdateRow();
	return Report;
}

void FSimulationDuelRunner::Tick(float DeltaTime, float TimeCap)
{
	if (!bStarted || bComplete || !Error.IsEmpty())
		return;
	if (!State.IsValid() || !Wallets[0].IsValid() || !Wallets[1].IsValid()
		|| !FMath::IsFinite(DeltaTime) || DeltaTime <= 0.f || !FMath::IsFinite(TimeCap) || TimeCap <= 0.f)
	{
		Error = TEXT("Duel lost its world/wallet or received invalid elapsed time/cap");
		return;
	}
	if (Elapsed == 0.)
	{
		Current->SetNumberField(TEXT("time_cap_seconds"), TimeCap);
		Report->SetNumberField(TEXT("time_cap_seconds"), TimeCap);
	}
	Elapsed += DeltaTime;
	TotalElapsed += DeltaTime;
	Observe();
	if (!Error.IsEmpty())
		return;
	const bool bWiped = Survivors[0] == 0 || Survivors[1] == 0;
	const bool bStalled = !bWiped && LastDamageElapsed >= 0. && Elapsed - LastDamageElapsed >= StallTimeout;
	if (!bWiped && !bStalled && Elapsed < TimeCap)
		return;
	UpdateRow();
	if (bStalled)
		FailStalled();
	else
		CompletePair(bWiped);
}

void FSimulationDuelRunner::FailStalled()
{
	Current->SetStringField(TEXT("outcome"), TEXT("stalled"));
	Report->SetObjectField(TEXT("invalid_duel"), Current.ToSharedRef());
	Report->RemoveField(TEXT("current_duel"));
	Report->SetStringField(TEXT("status"), TEXT("failed"));
	Report->SetStringField(TEXT("outcome"), TEXT("stalled"));
	const FFight& Fight = Fights[FightIndex];
	const FString Label = Fight.IsComposition()
		? FString::Printf(TEXT("%s composition of %s with %s against %s"), *Fight.Kind, *Fight.Support.ToString(),
			  *Fight.Partner.ToString(), *Fight.Target.ToString())
		: FString::Printf(TEXT("%s vs %s"), *Current->GetStringField(TEXT("left")), *Current->GetStringField(TEXT("right")));
	Error = FString::Printf(TEXT("Invalid stalled duel %s: no effective HP or shield removed for %.3f game seconds (timeout %.3f) while both sides live"),
		*Label, Elapsed - LastDamageElapsed, StallTimeout);
	Report->SetStringField(TEXT("error"), Error);
	ClearPair();
	RestoreHeadquarters();
}

void FSimulationDuelRunner::CompletePair(bool bWiped)
{
	Current->SetStringField(TEXT("outcome"), bWiped ? TEXT("wiped") : TEXT("time_cap"));
	const int32 Winner = bWiped && (Survivors[0] > 0 || Survivors[1] > 0)
		? (Survivors[0] > 0 ? 0 : 5)
		: INDEX_NONE;
	if (Winner != INDEX_NONE)
		Current->SetNumberField(TEXT("winner"), Winner);
	Append(*Report, Fights[FightIndex].IsComposition() ? TEXT("compositions") : TEXT("duels"), Current.ToSharedRef());
	ClearPair();
	++FightIndex;
	if (FightIndex == Fights.Num())
	{
		bComplete = true;
		Report->RemoveField(TEXT("current_duel"));
		Report->SetStringField(TEXT("status"), TEXT("complete"));
		Report->SetStringField(TEXT("outcome"), TEXT("matrix_complete"));
		RestoreHeadquarters();
	}
	else if (!StartPair())
	{
		ClearPair();
		RestoreHeadquarters();
	}
}

void FSimulationDuelRunner::ClearPair()
{
	for (const FMember& Member : Members)
		if (AArmyUnit* Unit = Member.Unit.Get())
		{
			if (AController* Controller = Unit->GetController())
				Controller->Destroy();
			Unit->Destroy();
		}
	Members.Reset();
	for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
		if (Group.IsValid())
			Group->Destroy();
	Groups.Reset();
}

void FSimulationDuelRunner::PauseActor(AActor& Actor)
{
	PausedActors.Add({ &Actor, Actor.IsActorTickEnabled() });
	Actor.SetActorTickEnabled(false);
}

void FSimulationDuelRunner::RestoreHeadquarters()
{
	if (!State.IsValid())
		return;
	if (Headquarters[0].IsValid())
		State->FriendlyHeadquarters = Headquarters[0].Get();
	if (Headquarters[1].IsValid())
		State->EnemyHeadquarters = Headquarters[1].Get();
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (Headquarters[Side].IsValid())
			Headquarters[Side]->SetActorEnableCollision(HQCollision[Side]);
		Headquarters[Side].Reset();
	}
	for (const FPausedActor& Paused : PausedActors)
		if (Paused.Actor.IsValid())
			Paused.Actor->SetActorTickEnabled(Paused.bTickEnabled);
	PausedActors.Reset();
}
#endif
