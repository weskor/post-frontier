#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "DepositSite.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "MatchSimulationJson.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"

using namespace MatchSimulationJson;

namespace
{
int32 TeamSlot(int32 Team) { return Team == 0 ? 0 : Team == 5 ? 1
															  : INDEX_NONE; }
}

void FMatchSimulation::Observe(ACommandGameState& State)
{
	ObservePlans(State);
	ObserveUnits();
	ObserveBuildings(State);
	ObserveRegions(State);
	ObserveDeposits(State);
	ObserveHeadquarters(State);
}

void FMatchSimulation::ObservePlans(ACommandGameState& State)
{
	// Retained accepted transitions include tickets created and released between
	// snapshots, and retain creation verbs even if a ticket has already escalated.
	while (ObservedPlanHistory < State.EnemyPlanHistory.Num())
	{
		const auto& Entry = State.EnemyPlanHistory[ObservedPlanHistory++];
		const TSharedRef<FJsonObject> Row = Event(Entry.bEscalation ? TEXT("plan_escalated") : TEXT("plan_created"), 5);
		Row->SetNumberField(TEXT("time"), FMath::Max(0., Entry.TimeSeconds - StartWorldTime));
		PlanFields(*Row, Entry.Plan, Entry.ForceNumber, Entry.TargetStructureName, StartWorldTime, Entry.Plan.RemainingCommitment);
		Row->SetNumberField(TEXT("source_controller"), Entry.SourceController);
		Row->SetBoolField(TEXT("order_changed"), Entry.bOrderChanged);
	}
}

void FMatchSimulation::ObserveUnits()
{
	for (auto It = ObservedUnits.CreateIterator(); It; ++It)
	{
		AArmyUnit* Unit = It.Key().Get();
		if (!IsValid(Unit) || !Unit->IsAlive())
		{
			++Casualties[It.Value().TeamSlot];
			It.RemoveCurrent();
		}
	}
	for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive())
			continue;
		const int32 Slot = TeamSlot(It->GetTeamIndex());
		if (Slot == INDEX_NONE)
			continue;
		const TWeakObjectPtr<AArmyUnit> Key(*It);
		FObservedUnit* Previous = ObservedUnits.Find(Key);
		if (!Previous)
		{
			++Produced[Slot];
			Attacks[Slot] += It->AttackCount;
			ObservedHealthLoss[Slot] += FMath::Max(0, It->MaxHealth() - It->GetHealth());
			ObservedUnits.Add(Key, { Slot, It->GetHealth(), It->AttackCount });
		}
		else
		{
			ObservedHealthLoss[Slot] += FMath::Max(0, Previous->Health - It->GetHealth());
			Attacks[Slot] += static_cast<uint32>(It->AttackCount - Previous->Attacks);
			Previous->Health = It->GetHealth();
			Previous->Attacks = It->AttackCount;
		}
	}
}

void FMatchSimulation::ObserveBuildings(ACommandGameState& State)
{
	for (const ACommandBuilding* Building : State.Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive())
			continue;
		const int32 Slot = TeamSlot(Building->TeamIndex);
		const int32 Kind = Building->Kind == EBuildingKind::Extractor ? 0 : Building->IsProducer() ? 1
																								   : INDEX_NONE;
		if (Slot == INDEX_NONE || Kind == INDEX_NONE)
			continue;
		if (!FirstPlaced[Slot][Kind])
		{
			FirstPlaced[Slot][Kind] = true;
			const TSharedRef<FJsonObject> Row = Event(Kind == 0 ? TEXT("first_extractor") : TEXT("first_barracks"), Building->TeamIndex);
			Row->SetArrayField(TEXT("position"), Position(Building->GetActorLocation()));
		}
		if (Building->IsComplete() && !FirstComplete[Slot][Kind])
		{
			FirstComplete[Slot][Kind] = true;
			Event(Kind == 0 ? TEXT("first_extractor_complete") : TEXT("first_barracks_complete"), Building->TeamIndex);
		}
	}
}

void FMatchSimulation::ObserveRegions(ACommandGameState& State)
{
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region))
			continue;
		const int32 Owner = State.GetRegionController(Region->RegionIndex);
		int32& Previous = RegionOwners.FindChecked(Region->RegionIndex);
		if (Owner == Previous)
			continue;
		const TSharedRef<FJsonObject> Row = Event(TEXT("region_control"), Owner);
		Row->SetNumberField(TEXT("region"), Region->RegionIndex);
		Row->SetNumberField(TEXT("previous_team"), Previous);
		const int32 Slot = TeamSlot(Owner);
		if (Slot != INDEX_NONE && !FirstCapture[Slot] && Region->RegionRole != ERegionRole::Main)
		{
			FirstCapture[Slot] = true;
			Event(TEXT("first_capture"), Owner)->SetNumberField(TEXT("region"), Region->RegionIndex);
		}
		Previous = Owner;
	}
}

void FMatchSimulation::ObserveDeposits(ACommandGameState& State)
{
	for (ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && Deposit->Remaining == 0 && !Depleted.Contains(Deposit))
		{
			Depleted.Add(Deposit);
			const TSharedRef<FJsonObject> Row = Event(TEXT("deposit_depleted"), IsValid(Deposit->Extractor) ? Deposit->Extractor->TeamIndex : -1);
			Row->SetStringField(TEXT("deposit"), Deposit->GetName());
			Row->SetNumberField(TEXT("region"), Deposit->RegionIndex);
		}
}

void FMatchSimulation::ObserveHeadquarters(ACommandGameState& State)
{
	const AHeadquarters* HQs[] = { State.FriendlyHeadquarters, State.EnemyHeadquarters };
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		if (HQs[Slot]->Health < PreviousHQHealth[Slot])
		{
			const TSharedRef<FJsonObject> Row = Event(TEXT("hq_damage"), Slot == 0 ? 0 : 5);
			Row->SetNumberField(TEXT("damage"), PreviousHQHealth[Slot] - HQs[Slot]->Health);
			Row->SetNumberField(TEXT("health"), HQs[Slot]->Health);
		}
		PreviousHQHealth[Slot] = HQs[Slot]->Health;
		// The battle's terminal fact: the hold on this HQ's main completed. `team` is the side that lost its HQ.
		const HqHoldPolicy::EPhase Phase = HQs[Slot]->GetPhase();
		if (Phase == HqHoldPolicy::EPhase::Lost && PreviousHqPhase[Slot] != Phase)
		{
			const TSharedRef<FJsonObject> Row = Event(TEXT("hold_completed"), Slot == 0 ? 0 : 5);
			Row->SetNumberField(TEXT("winner"), Slot == 0 ? 5 : 0);
			Row->SetNumberField(TEXT("hold_seconds"), HQs[Slot]->GetHold().Progress);
		}
		PreviousHqPhase[Slot] = Phase;
	}
}
#endif
