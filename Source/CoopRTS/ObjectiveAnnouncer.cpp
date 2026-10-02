#include "ObjectiveAnnouncer.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "CoopAudioSubsystem.h"
#include "MapRegion.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UObjectiveAnnouncer::UObjectiveAnnouncer()
{
	SetIsReplicatedByDefault(true);
	Events.Reserve(HistoryLimit);
}

UObjectiveAnnouncer* UObjectiveAnnouncer::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	const ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	return State ? State->FindComponentByClass<UObjectiveAnnouncer>() : nullptr;
}

FObjectiveForce UObjectiveAnnouncer::DescribeForce(const AArmyUnit* Unit)
{
	FObjectiveForce Force;
	if (!IsValid(Unit))
		return Force;
	Force.TeamIndex = Unit->GetTeamIndex();
	Force.CommanderIndex = Unit->GetCommanderIndex();
	Force.UnitIndex = Unit->GetUnitIndex();
	if (const AArmyGroup* Group = Unit->GetGroup())
	{
		Force.ForceNumber = Group->ForceNumber;
		if (const ACommandPlayerState* Owner = Group->GetOwningPlayerState())
		{
			Force.CommanderIndex = Owner->CommanderIndex;
			Force.PlayerName = Force.TeamIndex == 5 ? TEXT("JEV") : Owner->GetPlayerName();
		}
	}
	if (Force.PlayerName.IsEmpty() && Force.TeamIndex == 5)
		Force.PlayerName = TEXT("JEV");
	return Force;
}

void UObjectiveAnnouncer::Raise(FName Id, int32 AffectedTeam, const FVector& Location, const TArray<FObjectiveForce>& Forces, int32 DamageTier)
{
	const ACommandGameState* State = Cast<ACommandGameState>(GetOwner());
	if (!State || !State->HasAuthority())
		return;
	const double Now = State->GetServerWorldTimeSeconds();
	const FObjectiveForce* Attacker = Forces.IsEmpty() ? nullptr : &Forces[0];
	if (!Throttle.Accept(Id, AffectedTeam, Attacker ? Attacker->CommanderIndex : -1,
			Attacker ? Attacker->ForceNumber : 0, DamageTier, Now))
		return;
	AppendEvent(*State, Id, AffectedTeam, Location, Forces, DamageTier, Now);
}

void UObjectiveAnnouncer::RaiseFromUnit(FName Id, int32 AffectedTeam, const FVector& Location, const AArmyUnit* Unit, int32 DamageTier)
{
	const ACommandGameState* State = Cast<ACommandGameState>(GetOwner());
	if (!State || !State->HasAuthority() || !IsValid(Unit))
		return;
	const AArmyGroup* Group = Unit->GetGroup();
	const int32 ForceNumber = Group ? Group->ForceNumber : 0;
	const double Now = State->GetServerWorldTimeSeconds();
	if (!Throttle.Accept(Id, AffectedTeam, Unit->GetCommanderIndex(), ForceNumber, DamageTier, Now))
		return;
	const FObjectiveForce Force = DescribeForce(Unit);
	AppendEvent(*State, Id, AffectedTeam, Location, MakeArrayView(&Force, 1), DamageTier, Now);
}

void UObjectiveAnnouncer::AppendEvent(const ACommandGameState& State, FName Id, int32 AffectedTeam, const FVector& Location,
	TConstArrayView<FObjectiveForce> Forces, int32 DamageTier, float ServerTime)
{
	FObjectiveEvent Event;
	Event.Sequence = NextSequence++;
	Event.Id = Id;
	Event.ServerTime = ServerTime;
	Event.Location = Location;
	Event.AffectedTeam = AffectedTeam;
	Event.DamageTier = DamageTier;
	if (const AMapRegion* Region = State.FindRegionAt(Location))
	{
		Event.RegionIndex = Region->RegionIndex;
		Event.RegionName = Region->DisplayName.ToString();
	}
	Event.Forces.Reserve(Forces.Num());
	for (const FObjectiveForce& Force : Forces)
	{
		if (!Event.Forces.ContainsByPredicate([&Force](const FObjectiveForce& Existing)
			{ return Existing.TeamIndex == Force.TeamIndex && Existing.CommanderIndex == Force.CommanderIndex && Existing.ForceNumber == Force.ForceNumber; }))
			Event.Forces.Add(Force);
	}
	Event.Forces.Sort([](const FObjectiveForce& A, const FObjectiveForce& B)
	{
		if (A.TeamIndex != B.TeamIndex)
			return A.TeamIndex < B.TeamIndex;
		if (A.CommanderIndex != B.CommanderIndex)
			return A.CommanderIndex < B.CommanderIndex;
		return A.ForceNumber < B.ForceNumber;
	});
	if (Events.Num() == HistoryLimit)
		Events.RemoveAt(0, 1, EAllowShrinking::No);
	Events.Add(MoveTemp(Event));
	GetOwner()->ForceNetUpdate();
	MulticastAnnounce(Id);
}

void UObjectiveAnnouncer::MulticastAnnounce_Implementation(FName Id)
{
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		Audio->PlayAnnouncer(Id);
}

void UObjectiveAnnouncer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UObjectiveAnnouncer, Events);
}
