#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "HAL/PlatformTime.h"

enum class EJevWorldProof
{
	Commitment,
	Escalation,
	TargetDestroyed,
	ForeignAttack,
	RejectedOrder,
	CommittedClaims
};

// Each filter runs alone in a fresh world. Only the tested commander makes
// decisions; real force executors/navigation remain live throughout the proof.
class FJevPlannerWorldScenario : public IAutomationLatentCommand
{
public:
	FJevPlannerWorldScenario(FAutomationTestBase* InTest, EJevWorldProof InProof)
		: Test(InTest), Proof(InProof) {}

	bool Update() override;

private:
	bool Stage1(ACommandGameState* State, float Now);
	bool Progress(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now);
	bool Stage2(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now);
	bool AfterSetup(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now);
	bool Evaluate(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now);
	bool PrepareHealth(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC);
	bool PrepareInvasion(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC);
	bool PrepareClaims(ACommandGameState* State);
	bool PrepareDestroyed();
	bool RejectedOrder(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now);
	bool RejectedDefense(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now);
	bool Commitment(ACommandGameState* State, float Now);
	bool ForeignAttack(ACommandGameState* State, float Now, const FJevPublishedPlan* Changed);
	bool CommittedClaims(ACommandGameState* State, float Now, const FJevPublishedPlan* Changed);
	bool Escalation(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now, const FJevPublishedPlan* Changed);
	bool ReEscalate(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now, const FJevPublishedPlan* Changed);
	bool FinishEscalation(ACommandGameState* State, float Now, int32 FirstRegion, uint32 FirstDefenseSerial, AMapRegion* NextRegion);
	bool TargetDestroyed(ACommandGameState* State, float Now, const FJevPublishedPlan* Changed);
	bool Begin(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC);
	bool BeginForces(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, int32 Home, AMapRegion* ForwardSource);
	bool BeginPlanner(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, AMapRegion* ForwardSource);
	static ACommandBuilding* SpawnBuilding(UWorld* World, ACommandPlayerState* Wallet, int32 Index, FVector Location)
	{
		Location.Z = 5.f;
		const FTransform Transform(Location);
		ACommandBuilding* Building = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(),
			Transform, Wallet->GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Building)
		{
			Building->BuildingIndex = Index;
			Building->OwningPlayerState = Wallet;
			Building->TeamIndex = Wallet->TeamIndex;
			Building->ConstructionProgress = 1.f;
			Building->FinishSpawning(Transform);
		}
		return Building;
	}
	static void Park(AArmyGroup& Force)
	{
		Force.SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Force.GetUnits())
		{
			Unit->SetActorTickEnabled(false);
			Unit->NextAttackTime = TNumericLimits<float>::Max();
		}
	}
	static float JoinedHealth(const AArmyGroup& Force)
	{
		float Health = 0.f;
		int32 Count = 0;
		for (const AArmyUnit* Unit : Force.GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
			{
				Health += float(Unit->GetHealth()) / Unit->MaxHealth();
				++Count;
			}
		return Count ? Health / Count : 0.f;
	}
	const FJevPublishedPlan* Plan(const ACommandGameState* State, int32 Index) const
	{
		return State->EnemyPlans.FindByPredicate([&](const FJevPublishedPlan& Entry) { return Entry.Force == Forces[Index].Get(); });
	}
	static FString RegionName(const ACommandGameState* State, int32 Index)
	{
		for (const AMapRegion* Region : State->Regions)
			if (IsValid(Region) && Region->RegionIndex == Index)
				return Region->DisplayName.ToString();
		return FString();
	}
	bool PublishedMatches(const ACommandGameState* State, float Now)
	{
		if (!Check(State->EnemyPlans.Num() == 2, TEXT("Only the two live enemy forces may publish active plans")))
			return false;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FJevPublishedPlan* Published = Plan(State, Index);
			const AArmyGroup* Force = Forces[Index].Get();
			if (!Check(Published && Published->ForceNumber == Force->ForceNumber && Published->Verb == Force->Verb
						&& (Force->Verb == EForceVerb::Retreat ? Published->TargetRegionIndex == Force->GetRetreatRegion()
															   : Published->TargetRegionIndex == Force->TargetRegionIndex)
						&& Published->TargetStructure == Force->TargetStructure,
					TEXT("Published force verb, effective region and concrete structure match the accepted actual order")))
				return false;
			if (!Check(!Force->Orders.IsEmpty() && Force->Orders[0].Verb == Force->Verb
						&& Force->Orders[0].RegionIndex == Force->TargetRegionIndex
						&& Force->Orders[0].Structure == Force->TargetStructure,
					TEXT("Force intent retains a real accepted active command, not publication-only metadata")))
				return false;
			JevPlanner::FPlan MemoPlan;
			MemoPlan.Verb = Published->Verb == EForceVerb::Attack ? JevPlanner::EVerb::Attack
				: Published->Verb == EForceVerb::Retreat          ? JevPlanner::EVerb::Retreat
																  : JevPlanner::EVerb::MoveAndHold;
			MemoPlan.Source = Published->SourceRegionIndex;
			MemoPlan.Target = Published->TargetRegionIndex;
			MemoPlan.SizeBand = Published->SizeBand;
			MemoPlan.EtaSeconds = Published->EtaSeconds;
			MemoPlan.bEscalated = Published->bEscalated;
			const FString Name = RegionName(State, MemoPlan.Target);
			const int32 Eta = FMath::CeilToInt(Published->EtaSeconds);
			if (!Check(!Name.IsEmpty() && Published->TicketNumber > 0 && Published->SizeBand == 6
						&& FMath::IsFinite(Published->EtaSeconds) && Published->EtaSeconds >= 0.f
						&& Published->EtaIssuedAt >= 0.f && Published->EtaIssuedAt <= Now
						&& FMath::IsNearlyEqual(Published->RemainingCommitment, FMath::Max(0.f, Published->CommittedUntil - Now), .01f)
						&& Published->Memo == Templates.Format(MemoPlan, Published->TicketNumber, Name)
						&& Published->Memo.Contains(FString::Printf(TEXT("Ticket #%d"), Published->TicketNumber))
						&& Published->Memo.Contains(TEXT("~6 units")) && Published->Memo.Contains(Name)
						&& Published->Memo.Contains(FString::Printf(TEXT("ETA %d:%02d"), Eta / 60, Eta % 60)),
					TEXT("Writer-template memo faithfully formats the accepted ticket, size, region, verb/escalation and rounded ETA; remaining time uses server clock")))
				return false;
		}
		return true;
	}
	bool Unchanged(const ACommandGameState* State, int32 Index)
	{
		const FJevPublishedPlan* Current = Plan(State, Index);
		return Check(Current && Current->TicketNumber == Initial[Index].TicketNumber
				&& Current->Verb == Initial[Index].Verb && Current->SourceRegionIndex == Initial[Index].SourceRegionIndex
				&& Current->TargetRegionIndex == Initial[Index].TargetRegionIndex
				&& Current->TargetStructure == Initial[Index].TargetStructure
				&& Current->CommittedUntil == Initial[Index].CommittedUntil
				&& Current->EtaSeconds == Initial[Index].EtaSeconds && Current->EtaIssuedAt == Initial[Index].EtaIssuedAt
				&& Current->Memo == Initial[Index].Memo
				&& Current->bEscalated == Initial[Index].bEscalated,
			TEXT("Held force preserves its actual accepted order, stable ticket, full intent, memo and original deadline"));
	}
	bool DefenseHistory(const ACommandGameState* State, int32 Ticket, int32 Region)
	{
		int32 Count = 0;
		for (const FJevPlanHistoryEntry& Entry : State->EnemyPlanHistory)
			if (Entry.bEscalation && Entry.Plan.TicketNumber == Ticket && Entry.Plan.TargetRegionIndex == Region)
			{
				if (!Check(Entry.Plan.bEscalated && Entry.Plan.Verb == EForceVerb::MoveHold
							&& Entry.Plan.SourceRegionIndex == Region && Entry.SourceController == 5
							&& Entry.bOrderChanged && Entry.ForceNumber == Forces[0]->ForceNumber,
						TEXT("Defense transition history captures the actual order, defended source and event-time JEV ownership")))
					return false;
				++Count;
			}
		return Check(Count == 1, TEXT("Each distinct defended region has exactly one escalation history event"));
	}
	bool Check(bool bCondition, const TCHAR* Message)
	{
		if (!bCondition)
			Test->AddError(Message);
		return bCondition;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	FAutomationTestBase* Test;
	EJevWorldProof Proof;
	TWeakObjectPtr<AEnemyCommander> Planner;
	TWeakObjectPtr<AArmyGroup> Forces[2];
	TWeakObjectPtr<AArmyGroup> Intruder;
	FJevPublishedPlan Initial[2];
	FJevPublishedPlan RejectedFallback;
	uint32 InitialSerial[2] = {};
	FJevMemoTemplates Templates;
	int32 Stage = 0;
	int32 InvadedRegion = INDEX_NONE;
	float AcceptedAt = 0.f;
	float LastHeldAt = 0.f;
	float RejectedAt = 0.f;
	float ForeignObservedAt = 0.f;
	double Started = FPlatformTime::Seconds();
};
#endif
