#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "DepositSite.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevCommitmentWorldTest, "CoopRTS.Enemy.Planner.Commitment",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevEscalationWorldTest, "CoopRTS.Enemy.Planner.Escalation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevTargetDestroyedWorldTest, "CoopRTS.Enemy.Planner.TargetDestroyed",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevForeignAttackWorldTest, "CoopRTS.Enemy.Planner.ForeignAttack",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevRejectedOrderWorldTest, "CoopRTS.Enemy.Planner.RejectedOrder",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevCommittedClaimsWorldTest, "CoopRTS.Enemy.Planner.CommittedClaims",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
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

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 100.)
			return Fail(*FString::Printf(TEXT("JEV world proof %d exceeded bounded deadline at stage %d"), int32(Proof), Stage));
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !PC || ArmyTestSetup::GameSeconds(World) < 3.
			|| (Stage == 0 && !ArmyTestSetup::NavigationReady(World)))
			return false;
		if (State->MatchResult != EMatchResult::Ongoing)
			return Fail(TEXT("Isolated planner fixture must not end the match"));
		if (Stage == 0)
			return Begin(World, State, PC);
		if (!Planner.IsValid() || !Forces[0].IsValid() || !Forces[1].IsValid())
			return Fail(TEXT("Both independent producer forces and their tested commander must survive"));
		const float Now = ArmyTestSetup::GameSeconds(World);
		if (Stage == 1)
		{
			for (const TWeakObjectPtr<AArmyGroup>& Force : Forces)
				if (Force->GetJoinedCount() != 6)
					return false;
			for (const TWeakObjectPtr<AArmyGroup>& Force : Forces)
			{
				ACommandBuilding* Producer = Force->GetProductionBuilding();
				FCommandService::ConfigureProduction(State->EnemyCommander, Producer, EUnitRole::Frontline, false);
				Producer->SetActorTickEnabled(false);
			}
			State->EnemyCommander->Resources = 0;
			Planner->EvaluatePlan();
			if (!Plan(State, 0) || !Plan(State, 1))
				return false; // Dynamic navigation may reject the initial real commands.
			if (!PublishedMatches(State, Now))
				return true;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Initial[Index] = *Plan(State, Index);
				InitialSerial[Index] = Forces[Index]->OrderSerial;
				if (!Check(FMath::IsNearlyEqual(Initial[Index].CommittedUntil - Now, 25.f, .01f),
						TEXT("Every accepted initial plan starts a full 25-second server-time commitment")))
					return true;
			}
			if (!Check(Initial[0].TicketNumber != Initial[1].TicketNumber,
					TEXT("Independent forces publish distinct tickets")))
				return true;
			if ((Proof == EJevWorldProof::TargetDestroyed || Proof == EJevWorldProof::ForeignAttack)
				&& !Check(Initial[0].Verb == EForceVerb::Attack && IsValid(Initial[0].TargetStructure)
						&& Cast<ACommandBuilding>(Initial[0].TargetStructure),
					TEXT("Destruction proof must start with an accepted concrete hostile building Attack")))
				return true;
			if (Proof == EJevWorldProof::Escalation
				&& !Check(Initial[0].TargetRegionIndex != Initial[0].SourceRegionIndex,
					TEXT("Escalation proof must start with a committed order away from its source")))
				return true;
			if (Proof == EJevWorldProof::RejectedOrder || Proof == EJevWorldProof::CommittedClaims)
				for (const TWeakObjectPtr<AArmyGroup>& Force : Forces)
					Park(*Force);
			if (Proof == EJevWorldProof::ForeignAttack)
				for (const TWeakObjectPtr<AArmyGroup>& Force : Forces)
					for (AArmyUnit* Unit : Force->GetUnits())
						Unit->NextAttackTime = TNumericLimits<float>::Max();
			AcceptedAt = Now;
			Stage = 2;
			return false;
		}
		if (Stage == 2 && Now - AcceptedAt < (Proof == EJevWorldProof::Commitment ? 2.f : .25f))
		{
			Planner->EvaluatePlan();
			return !PublishedMatches(State, Now) || !Unchanged(State, 0) || !Unchanged(State, 1);
		}
		if (Stage == 2 && Proof == EJevWorldProof::ForeignAttack
			&& ArmyTestSetup::CurrentRegion(Forces[0].Get()) != Initial[0].TargetRegionIndex)
		{
			if (Now >= Initial[0].CommittedUntil - 2.f)
				return Fail(TEXT("Real Attack must enter its player-owned target before commitment expires"));
			Planner->EvaluatePlan();
			return !PublishedMatches(State, Now) || !Unchanged(State, 0) || !Unchanged(State, 1);
		}
		if (Stage == 2)
		{
			if (Proof == EJevWorldProof::Commitment || Proof == EJevWorldProof::RejectedOrder)
			{
				// Exhaust every real deposit: the economic world summary changes, but
				// none of the still-neutral destination regions becomes invalid.
				for (ADepositSite* Deposit : State->Deposits)
					if (IsValid(Deposit))
						Deposit->Remaining = 0;
				AArmyGroup* Damager = ArmyTestSetup::SpawnGroup(World, PC, 42,
					ArmyTestSetup::FromFriendlyHQ(State, -250.f, 700.f, 100.f));
				if (!Damager)
					return Fail(TEXT("Real hostile damage fixture must spawn"));
				for (AArmyUnit* Unit : Forces[0]->GetUnits())
					Unit->ReceiveAttack(Unit->GetHealth() - FMath::Max(1, Unit->MaxHealth() / 4), Damager->GetUnits()[0]);
				Damager->Destroy(); // No live attacker/source invasion exception remains.
				if (!Check(Forces[0]->GetAliveCount() == 6 && Forces[0]->GetJoinedCount() == 6
							&& JoinedHealth(*Forces[0]) < .35f && JoinedHealth(*Forces[1]) == 1.f,
						TEXT("Health scores change below recovery threshold without casualties or damage to the independent force")))
					return true;
			}
			else if (Proof == EJevWorldProof::Escalation || Proof == EJevWorldProof::ForeignAttack)
			{
				InvadedRegion = ArmyTestSetup::CurrentRegion(Forces[0].Get());
				if (!Check(InvadedRegion != INDEX_NONE && InvadedRegion != ArmyTestSetup::CurrentRegion(Forces[1].Get()),
						TEXT("Source invasion fixture must isolate one force's actual polygon")))
					return true;
				if (!Check(State->GetRegionController(InvadedRegion) == (Proof == EJevWorldProof::Escalation ? 5 : 0),
						TEXT("Defense invasion uses JEV control; foreign Attack invasion uses player control")))
					return true;
				Intruder = ArmyTestSetup::SpawnGroup(World, PC, 43,
					State->GetRegionAnchor(InvadedRegion) + FVector(0.f, 350.f, 100.f));
				if (!Intruder.IsValid())
					return Fail(TEXT("Real source-region invasion must spawn"));
				Park(*Intruder);
				if (!Check(State->IsRegionContested(InvadedRegion, 5),
						TEXT("Actual hostile members must occupy the force's source region")))
					return true;
			}
			else if (Proof == EJevWorldProof::CommittedClaims)
			{
				if (!Check(Initial[0].TargetRegionIndex != Initial[1].TargetRegionIndex,
						TEXT("Independent expansion plans reserve distinct targets")))
					return true;
				for (AMapRegion* Region : State->Regions)
					if (IsValid(Region) && IsValid(Region->Anchor) && Region->RegionRole != ERegionRole::Main
						&& Region->RegionIndex != Initial[1].TargetRegionIndex)
						Region->Anchor->ControllingTeam = 5;
				if (!Check(State->GetRegionController(Initial[0].TargetRegionIndex) == 5
							&& State->GetRegionController(Initial[1].TargetRegionIndex) == INDEX_NONE,
						TEXT("Capture invalidates the earlier force while the later force's remaining neutral target stays reserved")))
					return true;
			}
			else
			{
				ACommandBuilding* Target = Cast<ACommandBuilding>(Initial[0].TargetStructure);
				Target->ReceiveAttack(Target->Health, Forces[0]->GetUnits()[0]);
				if (!Check(!IsValid(Target) || !Target->IsAlive(),
						TEXT("Real lethal damage destroys the concrete hostile structure")))
					return true;
			}
			Stage = 3;
		}
		if (Proof == EJevWorldProof::RejectedOrder && Now >= Initial[0].CommittedUntil && Stage == 3)
		{
			AArmyGroup* Force = Forces[0].Get();
			const int32 Number = Force->ForceNumber;
			Force->Initialize({ 0, State->EnemyCommander.Get(), Force->GetArmyIndex(),
				Force->GetProductionBuilding(), Force->GetHomeLocation() });
			Force->ForceNumber = Number;
			if (!Check(!FCommandService::IssueForceOrder(State->EnemyCommander, Force, EForceVerb::MoveHold,
						   ArmyTestSetup::CurrentRegion(Force))
						&& Force->OrderSerial == InitialSerial[0],
					TEXT("Real command service rejects mismatched team identity without changing the live accepted order")))
				return true;
			RejectedAt = Now;
			Stage = 4;
		}
		Planner->EvaluatePlan();
		if (!PublishedMatches(State, Now))
			return true;
		if (Proof == EJevWorldProof::RejectedOrder)
		{
			if (Now < Initial[0].CommittedUntil)
				return !Unchanged(State, 0) || !Unchanged(State, 1);
			const FJevPublishedPlan* Actual = Plan(State, 0);
			if (!Check(Actual->Verb == Initial[0].Verb && Actual->TargetRegionIndex == Initial[0].TargetRegionIndex
						&& Actual->TargetStructure == Initial[0].TargetStructure && !Actual->bEscalated
						&& Forces[0]->OrderSerial == InitialSerial[0],
					TEXT("Rejected proposals keep the live accepted order published, not the rejected desired order")))
				return true;
			if (Stage == 4)
			{
				RejectedFallback = *Actual;
				if (!Check(Actual->TicketNumber != Initial[0].TicketNumber
							&& FMath::IsNearlyEqual(Actual->CommittedUntil - Now, 25.f, .01f),
						TEXT("All rejected fresh candidates adopt the actual order with a new full commitment")))
					return true;
				Stage = 5;
			}
			if (!Check(Actual->TicketNumber == RejectedFallback.TicketNumber
						&& Actual->CommittedUntil == RejectedFallback.CommittedUntil && Actual->Memo == RejectedFallback.Memo,
					TEXT("Actual-order fallback remains published and committed across repeated evaluations")))
				return true;
			if (Now - RejectedAt < 2.f)
				return false;
			Test->AddInfo(TEXT("JEV rejected order: real ownership rejection, all candidates rejected, live actual-order publication and stable fallback commitment."));
			return true;
		}
		if (Proof == EJevWorldProof::Commitment)
		{
			if (Now < Initial[0].CommittedUntil)
			{
				LastHeldAt = Now;
				return !Unchanged(State, 0) || !Unchanged(State, 1);
			}
			const FJevPublishedPlan* Fresh = Plan(State, 0);
			if (!Check(LastHeldAt >= Initial[0].CommittedUntil - .5f
						&& Now - AcceptedAt >= 25.f && Fresh->TicketNumber != Initial[0].TicketNumber
						&& Fresh->CommittedUntil > Initial[0].CommittedUntil
						&& (Fresh->Verb == EForceVerb::Retreat
							|| (Fresh->Verb == EForceVerb::MoveHold && State->GetRegionController(Fresh->TargetRegionIndex) == 5)),
					TEXT("Actual order survives the entire 25 seconds despite changed scores, then a fresh ticket accepts health-driven safe recovery")))
				return true;
			Test->AddInfo(TEXT("JEV commitment: full server-time window, real accepted order, changed economics/health, independent force and faithful publication/memos."));
			return true;
		}
		if (!Unchanged(State, 1))
			return true;
		const FJevPublishedPlan* Changed = Plan(State, 0);
		if (Proof == EJevWorldProof::ForeignAttack)
		{
			if (!Check(Now < Initial[0].CommittedUntil && !Changed->bEscalated
						&& Changed->Verb == EForceVerb::Attack && State->GetRegionController(InvadedRegion) == 0
						&& State->IsRegionContested(InvadedRegion, 5),
					TEXT("Attack arrival among actual player-region defenders must not become a defending escalation")))
				return true;
			if (!Unchanged(State, 0))
				return true;
			if (ForeignObservedAt == 0.f)
				ForeignObservedAt = Now;
			if (Now - ForeignObservedAt < 1.f)
				return false;
			Test->AddInfo(TEXT("JEV foreign Attack: real target arrival and hostile player-region occupants preserve the accepted Attack, ticket, deadline and memo."));
			return true;
		}
		if (Proof == EJevWorldProof::CommittedClaims)
		{
			if (!Check(Changed->TicketNumber != Initial[0].TicketNumber
						&& Changed->TargetRegionIndex != Initial[1].TargetRegionIndex
						&& Now < Initial[1].CommittedUntil,
					TEXT("Earlier force's replacement cannot steal a later force's still-committed target")))
				return true;
			Test->AddInfo(TEXT("JEV committed claims: earlier invalidation respects later held destination before evaluation order reaches that force."));
			return true;
		}
		if (Proof == EJevWorldProof::Escalation)
		{
			if (!Check(Changed->TicketNumber == Initial[0].TicketNumber
						&& Changed->CommittedUntil == Initial[0].CommittedUntil && Changed->bEscalated
						&& Changed->Verb == EForceVerb::MoveHold && Changed->SourceRegionIndex == InvadedRegion
						&& Changed->TargetRegionIndex == InvadedRegion && !Changed->TargetStructure
						&& Changed->Memo.Contains(TEXT("Escalated: defending ") + RegionName(State, InvadedRegion))
						&& Forces[0]->OrderSerial > InitialSerial[0],
					TEXT("Own-region invasion must accept a visibly defending MoveHold with the same ticket and deadline")))
				return true;
			if (Now - AcceptedAt < 5.f)
				return false; // Exercise repeated evaluations while the intrusion remains real.
			Test->AddInfo(TEXT("JEV escalation: real own-source invasion, accepted defense, stable ticket/deadline, unchanged independent force and exact defending memo."));
			return true;
		}
		if (!Check(Now < Initial[0].CommittedUntil && Changed->TicketNumber != Initial[0].TicketNumber
					&& Changed->CommittedUntil > Initial[0].CommittedUntil
					&& Changed->TargetStructure != Initial[0].TargetStructure
					&& Forces[0]->OrderSerial > InitialSerial[0],
				TEXT("Concrete target death must accept a fresh independent plan before the old deadline")))
			return true;
		Test->AddInfo(TEXT("JEV target invalidation: real hostile structure death, accepted replacement before expiry, fresh ticket, independent force and faithful publication/memos."));
		return true;
	}

private:
	bool Begin(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC)
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<ACommandBuilding> It(World); It; ++It)
			It->Destroy();
		State->bVerificationIncomePaused = true;
		State->EnemyCommander->Resources = 240; // Exactly two six-infantry paid setup rosters; zero during intent proof.
		for (TActorIterator<ACapturePoint> It(World); It; ++It)
			It->SetActorTickEnabled(false); // Incidental capture would legally invalidate a held expansion target.
		const int32 Home = ArmyTestSetup::RegionAt(State, State->EnemyHeadquarters->GetActorLocation());
		const AMapRegion* HomeRegion = State->FindRegionAt(State->EnemyHeadquarters->GetActorLocation());
		AMapRegion* ForwardSource = nullptr;
		for (AMapRegion* Region : State->Regions)
			if (IsValid(Region) && Region->RegionRole != ERegionRole::Main && IsValid(Region->Anchor)
				&& HomeRegion->Neighbours.Contains(Region->RegionIndex)
				&& (!ForwardSource || Region->RegionIndex < ForwardSource->RegionIndex))
				ForwardSource = Region;
		if (!ForwardSource)
			return Fail(TEXT("Generated enemy main needs an anchored neighbour for independent source fixtures"));
		for (AMapRegion* Region : State->Regions)
			if (IsValid(Region) && IsValid(Region->Anchor))
				Region->Anchor->ControllingTeam = -1;
		ForwardSource->Anchor->ControllingTeam = 5;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			AArmyGroup* Guard = ArmyTestSetup::SpawnGroup(World, PC, 40 + Index,
				ArmyTestSetup::FromFriendlyHQ(State, -250.f, Index ? 500.f : -500.f, 100.f));
			if (!Guard)
				return Fail(TEXT("Passive home guards must prevent empty-opponent HQ advantage"));
			Park(*Guard);
			const int32 Source = Index ? ForwardSource->RegionIndex : Home;
			const FVector Anchor = State->GetRegionAnchor(Source);
			const FVector Assembly = Anchor + FVector(Index ? 0.f : -600.f, 0.f, 100.f);
			ACommandBuilding* Producer = SpawnBuilding(World, State->EnemyCommander, ArmyTestSetup::BarracksIndex,
				Anchor + FVector(0.f, -800.f, 5.f));
			if (!Producer)
				return Fail(TEXT("Explicit completed producer fixture must spawn"));
			const FTransform Transform(Assembly);
			AArmyGroup* Force = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Force)
				return Fail(TEXT("Independent producer force allocation must succeed"));
			Force->Initialize({ 5, State->EnemyCommander.Get(), Index, Producer, Transform.GetLocation() });
			Force->FinishSpawning(Transform);
			Producer->ForceGroup = Force;
			Producer->ProductionUnitIndex = ArmyTestSetup::UnitIndex(State, EUnitRole::Frontline);
			Producer->ProductionRole = EUnitRole::Frontline;
			Producer->bForceConfigured = true;
			if (!FCommandService::SetRallyPoint(State->EnemyCommander, Producer, Source)
				|| !FCommandService::ConfigureProduction(State->EnemyCommander, Producer, EUnitRole::Frontline, true))
				return Fail(TEXT("Independent producer fixtures must accept normal rally and paid production commands"));
			Forces[Index] = Force;
		}
		if (Proof == EJevWorldProof::TargetDestroyed || Proof == EJevWorldProof::ForeignAttack)
			for (AMapRegion* Region : State->Regions)
				if (IsValid(Region) && Region->RegionRole != ERegionRole::Main && IsValid(Region->Anchor)
					&& Region != ForwardSource)
				{
					Region->Anchor->ControllingTeam = 0;
					if (!SpawnBuilding(World, PC->GetPlayerState<ACommandPlayerState>(), ArmyTestSetup::WorkshopIndex,
							State->GetRegionAnchor(Region->RegionIndex) + FVector(0.f, 700.f, 5.f)))
						return Fail(TEXT("Concrete hostile target fixtures must spawn outside anchor arrival footprints"));
				}
		if (!Templates.Load())
			return Fail(TEXT("World proof must load the real writer-authored memo templates"));
		Planner = World->SpawnActor<AEnemyCommander>();
		if (!Planner.IsValid())
			return Fail(TEXT("Isolated real planner must spawn"));
		Planner->SetActorTickEnabled(false); // Explicit bounded evaluations, no competing ordinary AI.
		Stage = 1;
		return false;
	}
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
				&& Current->EtaSeconds == Initial[Index].EtaSeconds && Current->Memo == Initial[Index].Memo
				&& Current->bEscalated == Initial[Index].bEscalated,
			TEXT("Held force preserves its actual accepted order, stable ticket, full intent, memo and original deadline"));
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
}

bool FJevCommitmentWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::Commitment));
	return true;
}
bool FJevEscalationWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::Escalation));
	return true;
}
bool FJevTargetDestroyedWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::TargetDestroyed));
	return true;
}
bool FJevForeignAttackWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::ForeignAttack));
	return true;
}
bool FJevRejectedOrderWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::RejectedOrder));
	return true;
}
bool FJevCommittedClaimsWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::CommittedClaims));
	return true;
}
#endif
