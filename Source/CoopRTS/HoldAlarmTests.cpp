#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "AIController.h"
#include "MapRegion.h"
#include "ObjectiveAnnouncer.h"
#include "NavigationData.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldBuildingEdgeTest, "CoopRTS.Hold.BuildingEdge",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldProportionalTest, "CoopRTS.Hold.Proportional",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldBorderTest, "CoopRTS.Hold.Border",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldTimersTest, "CoopRTS.Hold.Timers",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldSharedCommandersTest, "CoopRTS.Hold.SharedCommanders",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldJevTest, "CoopRTS.Hold.Jev",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace HoldAlarmTests
{
enum class ECase : uint8
{
	BuildingEdge,
	Proportional,
	Border,
	Timers,
	SharedCommanders,
	Jev
};

// Each filter runs alone in a fresh standalone map through its own x scope.
// Only the encounter fixtures are scripted: holders use the command service,
// navigation, combat and the world's clock, never manually ticked hold state.
class FScenario : public IAutomationLatentCommand
{
public:
	FScenario(FAutomationTestBase* InTest, ECase InCase)
		: Test(InTest), Case(InCase), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (bFailed)
			return true;
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
		if (Stage == EStage::Setup)
			return Setup();
		if (!Check(State.IsValid() && Region.IsValid(), TEXT("The isolated map and held region survive")))
			return true;
		const double Now = State->GetWorld()->GetTimeSeconds();
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
		{
			if (!Check(Holder.IsValid() && Power(*Holder) > 0., TEXT("Every holder retains living Power")))
				return true;
			if (Holder->bHoldResponding && FVector::Dist2D(Holder->GetCenter(), InitialCenters[Holders.IndexOfByKey(Holder)]) > 100.)
				MovedHolders.Add(Holder.Get());
			if (Case == ECase::Border && Threats[0].IsValid() && Threats[0]->IsAlive())
			{
				const AArmyUnit* Unit = Holder->GetUnits()[0];
				uint32& Previous = BorderAttacks.FindOrAdd(Holder.Get());
				if (Unit->AttackCount > Previous)
				{
					if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Threats[0]->GetActorLocation()) <= Unit->WeaponRange() + 50.,
							TEXT("Observed outside retaliation shot originates within the actual weapon range")))
						return true;
					bObservedBorderShot = true;
				}
				Previous = Unit->AttackCount;
			}
		}
		if (Case == ECase::Border && Stage != EStage::Posts)
			for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
				for (const AArmyUnit* Unit : Holder->GetUnits())
					if (!Check(Region->Contains(Unit->GetActorLocation()) || DistanceToBorder(Unit->GetActorLocation()) < 80.,
							TEXT("Responders stop at the polygon border instead of pursuing outside")))
						return true;
		if (bShootBuilding && Threats[0].IsValid() && Threats[0]->IsAlive() && Building.IsValid())
			Threats[0]->FireAt(Building.Get()); // Real cooldown, range, damage and notification.

		switch (Stage)
		{
		case EStage::Posts:
			if (!AtPosts())
				break;
			if (!CheckPosts())
				return true;
			InitialCenters.Reset();
			for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
				InitialCenters.Add(Holder->GetCenter());
			if (Case == ECase::SharedCommanders && !ChooseSharedIntrusion())
				return true;
			ExpectedResponseEvents = ResponseEventCount();
			if (!Check(ExpectedResponseEvents >= 0, TEXT("The authoritative objective history is available")))
				return true;
			BeginAlarm(Now);
			break;
		case EStage::Respond:
			if (Responders().Num() != 2)
				break;
			if (!CheckNearest(2) || !CheckTargets())
				return true;
			FirstResponders = Responders();
			FirstTarget = Holders[FirstResponders[0]]->HoldThreat;
			ResponseStarted = Holders[FirstResponders[0]]->GetHoldResponseStarted();
			if (!CheckResponseFeed(true))
				return true;
			if (!Check(FirstTarget == Threats[0].Get(), TEXT("Nearest eligible intrusion or damaging attacker is the response target")))
				return true;
			if (Case == ECase::Timers)
			{
				Teleport(Threats[0].Get(), FarOutside);
				SetStage(EStage::EarlyQuiet, Now);
			}
			else if (Case == ECase::Proportional)
			{
				SetStage(EStage::Feint, Now);
			}
			else
			{
				EnableWeapons();
				SetStage(EStage::Combat, Now);
			}
			break;
		case EStage::Feint:
			if (!CheckIdleHolders(FirstResponders))
				return true;
			if (Now - StageStarted < 1.)
				break;
			ExpandedCenters.Reset();
			for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
				ExpandedCenters.Add(Holder->GetCenter());
			Teleport(Threats[1].Get(), Intrusion + Side * 120.);
			Teleport(Threats[2].Get(), Intrusion - Side * 120.);
			SetStage(EStage::Expanded, Now);
			break;
		case EStage::Expanded:
			if (Responders().Num() != 4)
				break;
			for (int32 Index : FirstResponders)
				if (!Check(Holders[Index]->bHoldResponding && Holders[Index]->HoldThreat == FirstTarget.Get(),
						TEXT("Threat growth preserves committed responders and their live target")))
					return true;
			if (!CheckExpandedSelection() || !CheckIdleHolders(Responders()) || !CheckResponseFeed())
				return true;
			EnableWeapons();
			SetStage(EStage::Combat, Now);
			break;
		case EStage::Combat:
			if (!CheckResponseFeed())
				return true;
			if (Building.IsValid() && Building->Health < BuildingInitialHealth)
				bObservedBuildingDamage = true;
			if (Threats[0].IsValid() && Threats[0]->GetHealth() < ThreatInitialHealth)
				bObservedThreatDamage = true;
			if (!AllEnteredThreatsDead())
				break;
			if (!Check(MovedHolders.Num() > 0, TEXT("A responding force actually travels away from its assigned post"))
				|| !Check(TotalAttacks() > 0, TEXT("Responders execute live weapon attacks"))
				|| !Check(bObservedThreatDamage, TEXT("Live responder weapons lower the hostile's health before killing it")))
				return true;
			if (Case == ECase::Border && !Check(bObservedBorderShot, TEXT("The outside damaging shooter receives live weapon-range edge retaliation")))
				return true;
			if (Building.IsValid() && (!Check(bObservedBuildingDamage, TEXT("The far/edge building took live weapon damage")) || !Check(Building->IsAlive(), TEXT("Responders save the attacked building"))))
				return true;
			bShootBuilding = false;
			if (Case == ECase::Border)
			{
				Teleport(Threats[1].Get(), FarOutside);
				OutsideHealth = Threats[1]->GetHealth();
			}
			SetStage(EStage::Return, Now);
			break;
		case EStage::Return:
			if (Case == ECase::Border)
			{
				for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
					if (!Check(!Holder->IsHoldTargetPermitted(*Threats[1]), TEXT("Outside non-damaging hostile is not eligible for retaliation")))
						return true;
				if (!Check(Threats[1]->GetHealth() == OutsideHealth, TEXT("No responder damages the outside non-attacker")))
					return true;
			}
			if (!Responders().IsEmpty() || !AtPosts() || Now - StageStarted < 2.)
				break;
			return Finish();
		case EStage::EarlyQuiet:
			if (!Check(!Region->Contains(Threats[0]->GetActorLocation()), TEXT("Early quiet is caused by a living target leaving the region")))
				return true;
			for (int32 Index : FirstResponders)
			{
				const AArmyGroup* Holder = Holders[Index].Get();
				if (Now - ResponseStarted < 7.9 && !Check(Holder->bHoldResponding, TEXT("Early quiet cannot end the eight-game-second commitment")))
					return true;
				if (Now - StageStarted > .7 && !Check(Holder->HoldThreat != Threats[0].Get(), TEXT("Leash exit releases the sticky target")))
					return true;
			}
			if (Now - ResponseStarted >= 7. && !Responders().IsEmpty())
				bCommitObserved = true; // Quiet has lasted over six seconds, but commitment remains.
			if (!Responders().IsEmpty() || !AtPosts())
				break;
			if (!Check(bCommitObserved && Now - ResponseStarted >= 8., TEXT("Commitment survives six-second quiet and ends no earlier than eight seconds")))
				return true;
			BeginAlarm(Now);
			SetStage(EStage::StickyAcquire, Now);
			break;
		case EStage::StickyAcquire:
			if (Responders().Num() != 2 || !Holders[Responders()[0]]->HoldThreat)
				break;
			FirstResponders = Responders();
			FirstTarget = Holders[FirstResponders[0]]->HoldThreat;
			StickyHolder = Holders[FirstResponders[0]];
			if (!CheckResponseFeed(true))
				return true;
			{
				FVector Closer;
				const FVector TowardTarget = (FirstTarget->GetActorLocation() - StickyHolder->GetCenter()).GetSafeNormal2D();
				// Keep the nearer challenger off the original pursuit corridor:
				// this scenario isolates target retention, not pawn-body obstruction.
				const FVector Lateral(-TowardTarget.Y, TowardTarget.X, 0.);
				if (!Check(Project(StickyHolder->GetCenter() + Lateral * 300., Closer) && Region->Contains(Closer),
						TEXT("Sticky challenger has a navigable point inside the held region")))
					return true;
				Teleport(Threats[1].Get(), Closer);
				if (!Check(FVector::Dist2D(Closer, StickyHolder->GetCenter()) + 50.
							< FVector::Dist2D(FirstTarget->GetActorLocation(), StickyHolder->GetCenter()),
						TEXT("New challenger is materially closer than the retained threat")))
					return true;
			}
			SetStage(EStage::Sticky, Now);
			break;
		case EStage::Sticky:
			if (!CheckResponseFeed())
				return true;
			if (!Check(StickyHolder->HoldThreat == FirstTarget.Get(), TEXT("Live target stays sticky when a closer eligible hostile enters")))
				return true;
			if (Now - StageStarted < 2.)
				break;
			EnableWeapons();
			// Keep the challenger alive while the original target is killed naturally.
			for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
				if (Holder != StickyHolder)
					for (AArmyUnit* Unit : Holder->GetUnits())
						Unit->NextAttackTime = TNumericLimits<float>::Max();
			SetStage(EStage::StickyDeath, Now);
			break;
		case EStage::StickyDeath:
			if (FirstTarget.IsValid() && FirstTarget->IsAlive())
				break;
			if (StickyHolder->HoldThreat != Threats[1].Get())
				break;
			if (!Check(Threats[1]->IsAlive() && TotalAttacks() > 0, TEXT("Death releases the old lock and selects the remaining living nearest threat")))
				return true;
			for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
				for (AArmyUnit* Unit : Holder->GetUnits())
					Unit->NextAttackTime = TNumericLimits<float>::Max();
			Teleport(Threats[1].Get(), FarOutside);
			SetStage(EStage::LateQuiet, Now);
			break;
		case EStage::LateQuiet:
			if (QuietStarted < 0. && StickyHolder->GetHoldQuietSince() >= StageStarted - .1)
				QuietStarted = StickyHolder->GetHoldQuietSince();
			if (QuietStarted < 0.)
				break;
			{
				const double Quiet = QuietStarted;
				if (Now - Quiet < 5.9 && !Check(StickyHolder->bHoldResponding, TEXT("Quiet region retains the responder for six game seconds")))
					return true;
				if (Now - Quiet >= 5. && StickyHolder->bHoldResponding)
					bQuietObserved = true;
				if (StickyHolder->bHoldResponding || !AtPosts())
					break;
				if (!Check(bQuietObserved && Now - Quiet >= 6., TEXT("Responder returns only after six uninterrupted quiet game seconds")))
					return true;
			}
			if (!SetupEngagedFront())
				return true;
			SetStage(EStage::FrontAcquire, Now);
			break;
		case EStage::FrontAcquire:
			if (!IsStationaryEngaged())
				break;
			EngagedOrderSerial = FrontProbe->OrderSerial;
			SetStage(EStage::FrontEngaged, Now);
			break;
		case EStage::FrontEngaged:
			if (!Check(FrontProbe->OrderSerial == EngagedOrderSerial,
					TEXT("Front maintenance does not re-issue accepted moves while a stationary unit is engaged"))
				|| !Check(IsStationaryEngaged(), TEXT("The live target remains engaged throughout two front-maintenance periods")))
				return true;
			if (Now - StageStarted < 4.25)
				break;
			Test->AddInfo(TEXT("Automatic Secure front preserved its order serial while a displaced unit engaged a living target at weapon range."));
			return Finish();
		default:
			break;
		}
		return bFailed;
	}

private:
	enum class EStage : uint8
	{
		Setup,
		Posts,
		Respond,
		Feint,
		Expanded,
		Combat,
		Return,
		EarlyQuiet,
		StickyAcquire,
		Sticky,
		StickyDeath,
		LateQuiet,
		FrontAcquire,
		FrontEngaged
	};

	bool Check(bool Condition, const TCHAR* Message)
	{
		if (!Condition)
		{
			Test->AddError(Message);
			bFailed = true;
		}
		return Condition;
	}
	void SetStage(EStage Next, double Now)
	{
		Stage = Next;
		StageStarted = Now;
	}
	static double Power(const AArmyGroup& Group)
	{
		double Sum = 0.;
		for (const AArmyUnit* Unit : Group.GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
				Sum += Unit->GetDefinition()->UnitCost;
		return Sum;
	}
	bool Project(const FVector& Point, FVector& Ground) const
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->GetWorld());
		FNavLocation Result;
		if (!Nav || !AArenaBounds::IsTravelLocation(State->GetWorld(), Point)
			|| !Nav->ProjectPointToNavigation(Point, Result, FVector(35., 35., 300.))
			|| FVector::Dist2D(Point, Result.Location) > 35.)
			return false;
		Ground = Result.Location;
		return true;
	}
	bool Reachable(const FVector& From, const FVector& To) const
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->GetWorld());
		const FNavAgentProperties& Agent = GetDefault<AArmyUnit>()->GetNavAgentPropertiesRef();
		const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Agent, From) : nullptr;
		if (!Data)
			return false;
		FPathFindingQuery Query(nullptr, *Data, From, To);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = Nav->FindPathSync(Agent, Query);
		return Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial();
	}
	bool ClearFormation(const FVector& Center) const
	{
		for (int32 Slot = 0; Slot < 6; ++Slot)
		{
			FVector Ground;
			if (!Project(Center + FVector((1 - Slot / 2) * 220., (Slot % 2 ? 1. : -1.) * 140., 0.), Ground)
				|| !Reachable(Center, Ground))
				return false;
		}
		return true;
	}
	bool FindGeometry()
	{
		const int32 Team = Case == ECase::Jev ? 5 : 0;
		const UArmyUnitDefinition* Definition = State->Content->Unit(UnitIndex);
		const double Range = Definition->Range;
		const FVector FriendlyHQ = (Team == 0 ? State->FriendlyHeadquarters : State->EnemyHeadquarters)->GetActorLocation();
		const FVector HostileHQ = (Team == 0 ? State->EnemyHeadquarters : State->FriendlyHeadquarters)->GetActorLocation();
		for (AMapRegion* Candidate : State->Regions)
		{
			if (!IsValid(Candidate) || (Candidate->HomeTeam >= 0 && Candidate->HomeTeam != Team) || Candidate->GetDefendPosts().Num() < 2)
				continue;
			FVector Anchor;
			if (!Project(State->GetRegionAnchor(Candidate->RegionIndex), Anchor) || !ClearFormation(Anchor))
				continue;
			if (FVector::DistSquared2D(Anchor, FriendlyHQ) >= FVector::DistSquared2D(Anchor, HostileHQ))
				continue;
			bool bPostsClear = true;
			for (const FVector& Post : Candidate->GetDefendPosts())
			{
				FVector Ground;
				bPostsClear &= Project(Post, Ground) && Reachable(Anchor, Ground);
			}
			if (!bPostsClear)
				continue;
			for (int32 Edge = 0; Edge < Candidate->Polygon.Num(); ++Edge)
			{
				const FVector2D A = Candidate->Polygon[Edge];
				const FVector2D B = Candidate->Polygon[(Edge + 1) % Candidate->Polygon.Num()];
				for (double Fraction : { .25, .5, .75 })
				{
					const FVector2D XY = FMath::Lerp(A, B, Fraction);
					FVector EdgePoint(XY.X, XY.Y, Anchor.Z);
					FVector Inward(-(B.Y - A.Y), B.X - A.X, 0.);
					Inward.Normalize();
					if (!Candidate->Contains(EdgePoint + Inward * 100.))
						Inward *= -1.;
					FVector Inside, EdgeOutside, Remote;
					if (!Project(EdgePoint + Inward * FMath::Max(250., Range * .35), Inside)
						|| !Project(EdgePoint - Inward * Range * .3, EdgeOutside)
						|| !Project(EdgePoint - Inward * (Range * 2. + 350.), Remote)
						|| !Candidate->Contains(Inside) || Candidate->Contains(EdgeOutside) || Candidate->Contains(Remote)
						|| FVector::Dist2D(Inside, Anchor) <= 1400. || !Reachable(Anchor, Inside))
						continue;
					FVector ThreatPoint;
					const FVector Tangent = FVector(B.X - A.X, B.Y - A.Y, 0.).GetSafeNormal();
					if (!Project(Inside + Tangent * 350., ThreatPoint) || !Candidate->Contains(ThreatPoint)
						|| !Reachable(Anchor, ThreatPoint) || !ClearFormation(ThreatPoint))
						continue;
					bool bFarFromPosts = true;
					const FVector AlarmPoint = Case == ECase::Border ? EdgeOutside : ThreatPoint;
					for (const FVector& Post : Candidate->GetDefendPosts())
						bFarFromPosts &= FVector::Dist2D(Post, AlarmPoint) > Range + 400.;
					if (!bFarFromPosts)
						continue;
					Region = Candidate;
					BuildingLocation = Inside;
					Intrusion = ThreatPoint;
					Outside = EdgeOutside;
					FarOutside = Remote;
					Side = Tangent;
					return true;
				}
			}
		}
		return false;
	}
	AArmyGroup* Spawn(ACommandPlayerState* Wallet, int32 Index, const FVector& Home)
	{
		const FTransform Transform(Home);
		AArmyGroup* Group = State->GetWorld()->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			Wallet->GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Group)
			return nullptr;
		Group->Initialize({ Wallet->TeamIndex, Wallet, Index, nullptr, Home });
		Group->FinishSpawning(Transform);
		return Group;
	}
	static void Teleport(AArmyUnit* Unit, const FVector& Ground)
	{
		Unit->SetActorLocation(Ground + FVector(0., 0., 65.), false, nullptr, ETeleportType::TeleportPhysics);
	}
	bool Setup()
	{
		UWorld* World = ArmyTestSetup::World();
		if (!World)
			return false;
		// Remove planning immediately, before waiting for dynamic navigation.
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		State = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* Controller = ArmyTestSetup::Controller(World);
		if (!ArmyTestSetup::MapReady(State.Get()) || !Controller || World->GetTimeSeconds() < 3.)
			return false;
		ACommandPlayerState* Human = Controller->GetPlayerState<ACommandPlayerState>();
		if (!Human || Human->CommanderIndex < 0 || !IsValid(State->EnemyCommander))
			return false;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav || Nav->IsNavigationBuildInProgress())
			return false;
		UnitIndex = ArmyTestSetup::UnitIndex(State.Get(), EUnitRole::Ranged);
		if (!Check(UnitIndex >= 0 && State->Content->Unit(UnitIndex)->UnitCost > 0,
				TEXT("The map catalogue supplies positive-Power ranged fixtures")))
			return true;
		if (!FindGeometry())
			return false;
		for (TActorIterator<ACommandBuilding> It(World); It; ++It)
			if (It->IsProducer())
				FCommandService::ConfigureProduction(It->OwningPlayerState, *It,
					It->bForceConfigured ? It->ProductionRole : EUnitRole::Unset, false);
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		ACommandPlayerState* HolderWallet = Case == ECase::Jev ? State->EnemyCommander.Get() : Human;
		ACommandPlayerState* ThreatWallet = Case == ECase::Jev ? Human : State->EnemyCommander.Get();
		if (Case == ECase::SharedCommanders)
		{
			SecondCommander = World->SpawnActor<ACommandPlayerState>();
			if (!Check(SecondCommander.IsValid(), TEXT("Second independent commander wallet spawns")))
				return true;
			SecondCommander->TeamIndex = 0;
			SecondCommander->CommanderIndex = (Human->CommanderIndex + 1) % 5;
		}
		const int32 Count = Case == ECase::Proportional ? 5 : Case == ECase::SharedCommanders ? 4
			: Case == ECase::Timers                                                           ? 3
																							  : 2;
		const FVector Anchor = State->GetRegionAnchor(Region->RegionIndex);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			ACommandPlayerState* Wallet = Case == ECase::SharedCommanders && Index % 2 ? SecondCommander.Get() : HolderWallet;
			AArmyGroup* Holder = Spawn(Wallet, Index, Anchor);
			Holders.Add(Holder);
			FVector Ground;
			if (!Check(Holder && Project(Anchor + Side * (Index * 130. - Count * 65.), Ground)
						&& Holder->SpawnMember(UnitIndex, Ground, 2),
					TEXT("A real living holder spawns on map navigation")))
				return true;
			Holder->ForceNumber = Index + 1;
			Holder->GetUnits()[0]->NextAttackTime = TNumericLimits<float>::Max();
			if (!Check(FCommandService::AssignFront(Wallet, Holder, EFrontOrder::Defend, Anchor).IsAccepted(),
					TEXT("Real Defend front accepts the complete region hold")))
				return true;
		}
		FVector Staging;
		const AHeadquarters* OpposingHQ = Case == ECase::Jev ? State->FriendlyHeadquarters.Get() : State->EnemyHeadquarters.Get();
		if (!Check(Project(OpposingHQ->GetActorLocation() + FVector(Case == ECase::Jev ? 1200. : -1200., 700., 0.), Staging)
					&& !Region->Contains(Staging),
				TEXT("Opposing staging is navigable and outside the held region")))
			return true;
		Enemy = Spawn(ThreatWallet, 40, Staging);
		if (!Check(Enemy.IsValid(), TEXT("Isolated hostile fixture group spawns")))
			return true;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			FVector Ground;
			AArmyUnit* Unit = nullptr;
			if (Project(Staging + FVector(0., Index * 130., 0.), Ground))
				Unit = Enemy->SpawnMember(UnitIndex, Ground, Index);
			if (!Check(Unit != nullptr, TEXT("Hostile fixture member spawns on real navigation")))
				return true;
			Unit->NextAttackTime = TNumericLimits<float>::Max();
			Threats.Add(Unit);
		}
		if (!Check(FCommandService::IssueOrder(ThreatWallet, Enemy.Get(), EArmyOrder::Hold, FVector::ZeroVector).IsAccepted(),
				TEXT("Explicit fixture Hold stops in place without assigning a region")))
			return true;
		Enemy->SetActorTickEnabled(false); // Scripted stationary feint/shooter, no unrelated acquisition.
		if (Case == ECase::BuildingEdge || Case == ECase::Border || Case == ECase::Jev)
		{
			const FTransform Transform(BuildingLocation);
			Building = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Check(Building.IsValid(), TEXT("Completed Drill Rig fixture spawns")))
				return true;
			Building->BuildingIndex = ArmyTestSetup::ExtractorIndex;
			Building->OwningPlayerState = HolderWallet;
			Building->ConstructionProgress = 1.f;
			Building->TeamIndex = HolderWallet->TeamIndex;
			Building->FinishSpawning(Transform);
			BuildingInitialHealth = Building->Health;
			if (!Check(Region->Contains(Building->GetActorLocation()) && FVector::Dist2D(Building->GetActorLocation(), Anchor) > 1050.,
					TEXT("The owned Drill Rig is inside the region beyond the former anchor reaction radius")))
				return true;
		}
		InitialCenters.SetNum(Holders.Num());
		SetStage(EStage::Posts, World->GetTimeSeconds());
		return false;
	}
	bool AtPosts() const
	{
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			if (!Holder->IsHoldingRegion() || Holder->HoldRegionIndex != Region->RegionIndex
				|| FVector::Dist2D(Holder->GetCenter(), Holder->HoldPostLocation) > 220.
				|| Holder->GetUnits()[0]->GetVelocity().SizeSquared2D() > 1.)
				return false;
		return true;
	}
	bool CheckPosts()
	{
		TArray<int32> Occupancy;
		Occupancy.Init(0, Region->GetDefendPosts().Num());
		Posts.Reset();
		for (int32 Index = 0; Index < Holders.Num(); ++Index)
		{
			const AArmyGroup* Holder = Holders[Index].Get();
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
	bool ChooseSharedIntrusion()
	{
		// Pick an actual map-nav intrusion whose nearest pair belongs to different
		// commanders, so separate commander-local quotas would mobilize too many.
		TArray<FVector> Candidates;
		Candidates.Add(Intrusion);
		for (int32 A = 0; A < Holders.Num(); ++A)
			for (int32 B = A + 1; B < Holders.Num(); ++B)
				if (Holders[A]->GetOwningPlayerState() != Holders[B]->GetOwningPlayerState())
					Candidates.Add((InitialCenters[A] + InitialCenters[B]) * .5);
		for (const FVector& Candidate : Candidates)
		{
			FVector Point;
			if (!Project(Candidate, Point) || !Region->Contains(Point))
				continue;
			TArray<int32> Sorted = SortedByDistance(Point, InitialCenters);
			if (FVector::Dist2D(Point, InitialCenters[Sorted[0]]) <= State->Content->Unit(UnitIndex)->Range + 250.)
				continue;
			if (Holders[Sorted[0]]->GetOwningPlayerState() != Holders[Sorted[1]]->GetOwningPlayerState())
			{
				Intrusion = Point;
				return true;
			}
		}
		return Check(false, TEXT("Authored posts permit a nearest response pair spanning both commanders"));
	}
	void BeginAlarm(double Now)
	{
		++ExpectedResponseEvents;
		Teleport(Threats[0].Get(), Case == ECase::Border ? Outside : Intrusion);
		ThreatInitialHealth = Threats[0]->GetHealth();
		bShootBuilding = Building.IsValid();
		if (bShootBuilding)
			Threats[0]->NextAttackTime = 0.f;
		ExpectedNearest = SortedByDistance(Threats[0]->GetActorLocation(), InitialCenters);
		SetStage(EStage::Respond, Now);
	}
	TArray<int32> Responders() const
	{
		TArray<int32> Result;
		for (int32 Index = 0; Index < Holders.Num(); ++Index)
			if (Holders[Index]->bHoldResponding)
				Result.Add(Index);
		return Result;
	}
	static TArray<int32> SortedByDistance(const FVector& Target, const TArray<FVector>& Centers)
	{
		TArray<int32> Result;
		for (int32 Index = 0; Index < Centers.Num(); ++Index)
			Result.Add(Index);
		Result.Sort([&](int32 A, int32 B) { return FVector::DistSquared2D(Centers[A], Target) < FVector::DistSquared2D(Centers[B], Target); });
		return Result;
	}
	bool CheckNearest(int32 Count)
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
	bool CheckTargets()
	{
		for (int32 Index = 0; Index < Holders.Num(); ++Index)
		{
			if (!Check(Holders[Index]->HoldPostLocation.Equals(Posts[Index], 1.), TEXT("An active alarm never reassigns a holder's post")))
				return false;
			const AArmyUnit* Target = Holders[Index]->HoldThreat;
			if (Holders[Index]->bHoldResponding && !Check(IsValid(Target) && Target->IsAlive() && Threats.ContainsByPredicate([Target](const TWeakObjectPtr<AArmyUnit>& Threat) { return Threat.Get() == Target; }) && Holders[Index]->IsHoldTargetPermitted(*Target), TEXT("Responders expose an actual eligible living threat")))
				return false;
			if (Case == ECase::Border && Holders[Index]->bHoldResponding
				&& !Check(Holders[Index]->HoldThreatenedAsset == Building.Get() && Holders[Index]->HoldThreatKind == EHoldThreatKind::Building,
					TEXT("Outside damage exposes the attacked building and building threat kind")))
				return false;
		}
		return true;
	}
	bool CheckIdleHolders(const TArray<int32>& Active)
	{
		for (int32 Index = 0; Index < Holders.Num(); ++Index)
			if (!Active.Contains(Index) && !Check(!Holders[Index]->bHoldResponding && FVector::Dist2D(Holders[Index]->GetCenter(), Posts[Index]) < 220. && Holders[Index]->GetUnits()[0]->AttackCount == 0, TEXT("Unneeded holders stay at their posts and do not join combat")))
				return false;
		return true;
	}
	bool CheckExpandedSelection()
	{
		TArray<int32> Remaining;
		for (int32 Index = 0; Index < Holders.Num(); ++Index)
			Remaining.Add(Index);
		auto Distance = [this](int32 Index) {
			double Nearest = TNumericLimits<double>::Max();
			for (int32 Threat = 0; Threat < 3; ++Threat)
				Nearest = FMath::Min(Nearest, FVector::DistSquared2D(ExpandedCenters[Index], Threats[Threat]->GetActorLocation()));
			return Nearest;
		};
		Remaining.StableSort([&](int32 A, int32 B) { return Distance(A) < Distance(B); });
		Remaining.RemoveAll([&](int32 Index) { return FirstResponders.Contains(Index); });
		for (int32 Rank = 0; Rank < Remaining.Num(); ++Rank)
			if (!Check(Holders[Remaining[Rank]]->bHoldResponding == (Rank < 2), TEXT("Growing threat adds the nearest remaining holders, not the whole reserve")))
				return false;
		double Sum = 0.;
		for (int32 Index : Responders())
			Sum += Power(*Holders[Index]);
		const double ThreatPower = 3. * Threats[0]->GetDefinition()->UnitCost;
		return Check(Sum >= ThreatPower * 1.25 && Sum - Power(*Holders[Remaining[1]]) < ThreatPower * 1.25,
				   TEXT("All three living intruders contribute Power to the expanded 1.25 response"))
			&& CheckTargets();
	}
	void EnableWeapons()
	{
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			for (AArmyUnit* Unit : Holder->GetUnits())
				Unit->NextAttackTime = 0.f;
	}
	uint32 TotalAttacks() const
	{
		uint32 Result = 0;
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			for (const AArmyUnit* Unit : Holder->GetUnits())
				Result += Unit->AttackCount;
		return Result;
	}
	bool AllEnteredThreatsDead() const
	{
		const int32 Count = Case == ECase::Proportional ? 3 : 1;
		for (int32 Index = 0; Index < Count; ++Index)
			if (Threats[Index].IsValid() && Threats[Index]->IsAlive())
				return false;
		return true;
	}
	double DistanceToBorder(const FVector& Location) const
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
	int32 ResponseEventCount() const
	{
		const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(State.Get());
		if (!Announcer)
			return INDEX_NONE;
		int32 Count = 0;
		for (const FObjectiveEvent& Event : Announcer->GetEvents())
			if (Event.Id == TEXT("region_defenders_responding") && Event.RegionIndex == Region->RegionIndex
				&& Event.AffectedTeam == Holders[0]->GetTeamIndex())
				++Count;
		return Count;
	}
	bool CheckResponseFeed(bool bCheckInitialForces = false)
	{
		if (!Check(ResponseEventCount() == ExpectedResponseEvents,
				TEXT("Each response episode produces one region alert; polling, growth and target changes do not spam it")))
			return false;
		if (!bCheckInitialForces)
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
				}), TEXT("The alert retains the responding force identity across commanders and factions")))
				return false;
		}
		return true;
	}

	bool SetupEngagedFront()
	{
		// A non-Hold front exercises the maintenance guard itself; regional
		// Hold bypasses maintenance. Keep this duel outside the held region.
		const AHeadquarters* HQ = State->EnemyHeadquarters.Get();
		const AMapRegion* Home = HQ ? State->FindRegionAt(HQ->GetActorLocation()) : nullptr;
		if (!Check(Home && Home != Region.Get() && Threats[1].IsValid() && Threats[1]->IsAlive(),
				TEXT("A separate enemy home supplies the engaged-front fixture")))
			return false;
		const double Range = Threats[1]->WeaponRange();
		FVector Start, Target;
		bool bFound = false;
		const FVector Directions[] = { FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector };
		for (const FVector& Post : Home->GetDefendPosts())
		{
			for (const FVector& Direction : Directions)
				if (Project(Post + Direction * 400., Start) && Project(Start + Direction * (Range * .75), Target)
					&& Home->Contains(Start) && Home->Contains(Target)
					&& FVector::Dist2D(Start, HQ->GetActorLocation()) > Range + 200.
					&& FVector::Dist2D(Start, Target) > 170.
					&& FVector::Dist2D(Start, Target) < Range)
				{
					bFound = true;
					break;
				}
			if (bFound)
				break;
		}
		if (!Check(bFound, TEXT("Map-derived navigation provides a displaced in-range encounter away from the HQ")))
			return false;
		ACommandPlayerState* Wallet = ArmyTestSetup::Controller(State->GetWorld())->GetPlayerState<ACommandPlayerState>();
		FrontProbe = Spawn(Wallet, 50, Start);
		if (!Check(FrontProbe.IsValid() && FrontProbe->SpawnMember(UnitIndex, Start, 2),
				TEXT("A real automatic-front combat unit spawns")))
			return false;
		// Isolate maintenance from target death; ordinary combat still acquires,
		// locks and stops at weapon range while its cooldown is held.
		FrontProbe->GetUnits()[0]->NextAttackTime = TNumericLimits<float>::Max();
		Teleport(Threats[1].Get(), Target);
		return Check(FCommandService::AssignFront(Wallet, FrontProbe.Get(), EFrontOrder::Secure, Target).IsAccepted(),
			TEXT("The normal command service accepts the automatic Secure front"));
	}
	bool IsStationaryEngaged() const
	{
		if (!FrontProbe.IsValid() || FrontProbe->GetUnits().IsEmpty() || !Threats[1].IsValid() || !Threats[1]->IsAlive())
			return false;
		const AArmyUnit* Unit = FrontProbe->GetUnits()[0];
		const AAIController* AI = Cast<AAIController>(Unit->GetController());
		return FrontProbe->bAutomaticFront && !FrontProbe->IsHoldingRegion()
			&& Unit->Target == Threats[1].Get() && AI && AI->GetMoveStatus() == EPathFollowingStatus::Idle
			&& FVector::Dist2D(Unit->GetActorLocation(), Threats[1]->GetActorLocation()) <= Unit->WeaponRange()
			&& FVector::Dist2D(FrontProbe->GetCenter(), FrontProbe->FrontLocation) > 170.;
	}

	bool Finish()
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

	FAutomationTestBase* Test;
	ECase Case;
	EStage Stage = EStage::Setup;
	double Started;
	double StageStarted = 0., ResponseStarted = 0., QuietStarted = -1.;
	bool bFailed = false, bShootBuilding = false, bObservedBuildingDamage = false, bObservedThreatDamage = false;
	bool bCommitObserved = false, bQuietObserved = false, bObservedBorderShot = false;
	int32 UnitIndex = INDEX_NONE, BuildingInitialHealth = 0, ThreatInitialHealth = 0, OutsideHealth = 0;
	int32 EngagedOrderSerial = 0;
	int32 ExpectedResponseEvents = 0;
	TWeakObjectPtr<ACommandGameState> State;
	TWeakObjectPtr<AMapRegion> Region;
	TWeakObjectPtr<ACommandPlayerState> SecondCommander;
	TWeakObjectPtr<AArmyGroup> Enemy, StickyHolder, FrontProbe;
	TWeakObjectPtr<ACommandBuilding> Building;
	TWeakObjectPtr<AArmyUnit> FirstTarget;
	TArray<TWeakObjectPtr<AArmyGroup>> Holders;
	TArray<TWeakObjectPtr<AArmyUnit>> Threats;
	TArray<FVector> Posts, InitialCenters, ExpandedCenters;
	TArray<int32> ExpectedNearest, FirstResponders;
	TMap<AArmyGroup*, uint32> BorderAttacks;
	TSet<AArmyGroup*> MovedHolders;
	FVector Intrusion = FVector::ZeroVector, BuildingLocation = FVector::ZeroVector;
	FVector Outside = FVector::ZeroVector, FarOutside = FVector::ZeroVector, Side = FVector::RightVector;
};
}

bool FHoldBuildingEdgeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmTests::FScenario(this, HoldAlarmTests::ECase::BuildingEdge));
	return true;
}
bool FHoldProportionalTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmTests::FScenario(this, HoldAlarmTests::ECase::Proportional));
	return true;
}
bool FHoldBorderTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmTests::FScenario(this, HoldAlarmTests::ECase::Border));
	return true;
}
bool FHoldTimersTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmTests::FScenario(this, HoldAlarmTests::ECase::Timers));
	return true;
}
bool FHoldSharedCommandersTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmTests::FScenario(this, HoldAlarmTests::ECase::SharedCommanders));
	return true;
}
bool FHoldJevTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmTests::FScenario(this, HoldAlarmTests::ECase::Jev));
	return true;
}

#endif
