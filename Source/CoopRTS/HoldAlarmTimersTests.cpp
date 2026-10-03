#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldTimersTest, "CoopRTS.Hold.Timers",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace HoldAlarmTimersTests
{
class FScenario : public HoldAlarmFixture::FScenario
{
public:
	FScenario(FAutomationTestBase* InTest)
		: HoldAlarmFixture::FScenario(InTest, HoldAlarmFixture::ECase::Timers) {}

	virtual bool Update() override
	{
		double Now = 0.;
		const EStep Preparation = PrepareUpdate(Now);
		if (Preparation != EStep::Continue)
			return Preparation == EStep::Done;
		switch (Stage)
		{
		case EStage::Posts:
			return RunPosts(Now);
		case EStage::Respond:
			return RunRespond(Now);
		case EStage::Combat:
			return RunCombat(Now);
		case EStage::Return:
			return RunReturn(Now);
		case EStage::EarlyQuiet:
			return RunEarlyQuiet(Now);
		case EStage::StickyAcquire:
			return RunStickyAcquire(Now);
		case EStage::Sticky:
			return RunSticky(Now);
		case EStage::StickyDeath:
			return RunStickyDeath(Now);
		case EStage::StickyReacquire:
			return RunStickyReacquire(Now);
		case EStage::LateQuiet:
			return RunLateQuiet(Now);
		case EStage::AttackAcquire:
			return RunAttackAcquire(Now);
		case EStage::AttackEngaged:
			return RunAttackEngaged(Now);
		default:
			return bFailed;
		}
	}

private:
	virtual bool AdvanceResponse(double Now) override
	{
		Teleport(Threats[0].Get(), FarOutside);
		SetStage(EStage::EarlyQuiet, Now);
		return bFailed;
	}

	bool RunEarlyQuiet(double Now)
	{
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
			return bFailed;
		if (!Check(bCommitObserved && Now - ResponseStarted >= 8., TEXT("Commitment survives six-second quiet and ends no earlier than eight seconds")))
			return true;
		BeginAlarm(Now);
		SetStage(EStage::StickyAcquire, Now);
		return bFailed;
	}

	bool RunStickyAcquire(double Now)
	{
		if (Responders().Num() != 2 || !Holders[Responders()[0]]->HoldThreat)
			return bFailed;
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
			AArmyUnit* Unit = StickyHolder->GetUnits()[0];
			if (!Check(Unit->GetDamageType() == EDamageType::Piercing && Threats[1]->GetArmorClass() == EArmorClass::Heavy
						&& FVector::Dist2D(Unit->GetActorLocation(), Threats[1]->GetActorLocation()) < Unit->WeaponRange()
						&& FVector::Dist2D(Unit->GetActorLocation(), FirstTarget->GetActorLocation()) > Unit->WeaponRange() + 100.
						&& StickyHolder->IsHoldTargetPermitted(*Threats[1], *Region, Unit->WeaponRange()),
					TEXT("A permitted Heavy counter is in range while the sticky movement threat remains out of range")))
				return true;
			CounterLocation = Closer;
			CounterInitialHealth = Threats[1]->GetHealth();
			CounterInitialAttacks = Unit->AttackCount;
			Unit->NextAttackTime = 0.f;
		}
		SetStage(EStage::Sticky, Now);
		return bFailed;
	}

	bool RunSticky(double Now)
	{
		if (!CheckResponseFeed())
			return true;
		if (!Check(StickyHolder->HoldThreat == FirstTarget.Get(), TEXT("Live target stays sticky when a closer eligible hostile enters")))
			return true;
		if (!bObservedCounterDamage)
		{
			AArmyUnit* Unit = StickyHolder->GetUnits()[0];
			if (Unit->AttackCount == CounterInitialAttacks || Threats[1]->GetHealth() == CounterInitialHealth)
				return bFailed;
			if (!Check(Unit->Target == Threats[1].Get() && Threats[1]->IsAlive()
						&& Threats[1]->GetHealth() < CounterInitialHealth
						&& FVector::Dist2D(Unit->GetActorLocation(), FirstTarget->GetActorLocation()) > Unit->WeaponRange()
						&& FVector::Dist2D(Unit->GetActorLocation(), Threats[1]->GetActorLocation()) <= Unit->WeaponRange(),
					TEXT("The unit selects and damages its local counter independently of the farther sticky movement/overlay threat")))
				return true;
			bObservedCounterDamage = true;
			Unit->NextAttackTime = TNumericLimits<float>::Max();
		}
		if (Now - StageStarted < 2.)
			return bFailed;
		// Clear the local counter from weapon range for the original target's
		// natural death; restore it afterward to observe death-driven retargeting.
		Teleport(Threats[1].Get(), FarOutside);
		EnableWeapons();
		// Only this holder fires, isolating the original target's natural death.
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			if (Holder != StickyHolder)
				for (AArmyUnit* Unit : Holder->GetUnits())
					Unit->NextAttackTime = TNumericLimits<float>::Max();
		SetStage(EStage::StickyDeath, Now);
		return bFailed;
	}

	bool RunStickyDeath(double Now)
	{
		if (FirstTarget.IsValid() && FirstTarget->IsAlive())
			return bFailed;
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			for (AArmyUnit* Unit : Holder->GetUnits())
				Unit->NextAttackTime = TNumericLimits<float>::Max();
		Teleport(Threats[1].Get(), CounterLocation);
		SetStage(EStage::StickyReacquire, Now);
		return bFailed;
	}

	bool RunStickyReacquire(double Now)
	{
		if (StickyHolder->HoldThreat != Threats[1].Get())
			return bFailed;
		if (!Check(Threats[1]->IsAlive() && TotalAttacks() > 0, TEXT("Death releases the old lock and selects the remaining living nearest threat")))
			return true;
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			for (AArmyUnit* Unit : Holder->GetUnits())
				Unit->NextAttackTime = TNumericLimits<float>::Max();
		Teleport(Threats[1].Get(), FarOutside);
		SetStage(EStage::LateQuiet, Now);
		return bFailed;
	}

	bool RunLateQuiet(double Now)
	{
		if (QuietStarted < 0. && StickyHolder->GetHoldQuietSince() >= StageStarted - .1)
			QuietStarted = StickyHolder->GetHoldQuietSince();
		if (QuietStarted < 0.)
			return bFailed;
		{
			const double Quiet = QuietStarted;
			if (Now - Quiet < 5.9 && !Check(StickyHolder->bHoldResponding, TEXT("Quiet region retains the responder for six game seconds")))
				return true;
			if (Now - Quiet >= 5. && StickyHolder->bHoldResponding)
				bQuietObserved = true;
			if (StickyHolder->bHoldResponding || !AtPosts())
				return bFailed;
			if (!Check(bQuietObserved && Now - Quiet >= 6., TEXT("Responder returns only after six uninterrupted quiet game seconds")))
				return true;
		}
		if (!SetupEngagedAttack())
			return true;
		SetStage(EStage::AttackAcquire, Now);
		return bFailed;
	}

	bool RunAttackAcquire(double Now)
	{
		if (!IsStationaryEngaged())
			return bFailed;
		EngagedOrderSerial = AttackProbe->OrderSerial;
		SetStage(EStage::AttackEngaged, Now);
		return bFailed;
	}

	bool RunAttackEngaged(double Now)
	{
		if (!Check(AttackProbe->OrderSerial == EngagedOrderSerial,
				TEXT("Attack request caching does not re-issue accepted travel while a stationary unit is engaged"))
			|| !Check(IsStationaryEngaged(), TEXT("The live target remains engaged throughout two order-maintenance periods")))
			return true;
		if (Now - StageStarted < 4.25)
			return bFailed;
		Test->AddInfo(TEXT("The real Attack retained its order serial while a displaced unit engaged a living target at weapon range."));
		return Finish();
	}

	bool SetupEngagedAttack()
	{
		// A non-Hold Attack exercises accepted travel request caching. Its
		// one-member fixture must not withdraw before combat stops the move.
		const AHeadquarters* HQ = State->EnemyHeadquarters.Get();
		const AMapRegion* Home = HQ ? State->FindRegionAt(HQ->GetActorLocation()) : nullptr;
		if (!Check(Home && Home != Region.Get() && Threats[1].IsValid() && Threats[1]->IsAlive(),
				TEXT("A separate enemy home supplies the stationary Attack encounter")))
			return false;
		const double Range = Holders[0]->GetUnits()[0]->WeaponRange();
		const FVector Anchor = State->GetRegionAnchor(Home->RegionIndex);
		FVector Start, Target;
		bool bFound = false;
		for (int32 DirectionIndex = 0; DirectionIndex < 8 && !bFound; ++DirectionIndex)
		{
			const double Angle = DirectionIndex * PI / 4.;
			const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0.);
			const FVector Lateral(-Direction.Y, Direction.X, 0.);
			for (double Radius : { 1000., 900., 800., 700., 600., 500., 400., 300. })
				if (Project(Anchor + Direction * Radius, Start)
					&& Project(Start + Lateral * (Range * .6), Target)
					&& Home->Contains(Start) && Home->Contains(Target)
					&& FVector::Dist2D(Start, Anchor) <= 1050. && FVector::Dist2D(Target, Anchor) <= 1050.
					&& FVector::Dist2D(Start, HQ->GetActorLocation()) > Range + 200.
					&& FVector::Dist2D(Start, Anchor) > 170.
					&& FVector::Dist2D(Start, Target) > 170. && FVector::Dist2D(Start, Target) < Range
					&& Reachable(Start, Anchor))
				{
					bFound = true;
					break;
				}
		}
		if (!Check(bFound, TEXT("Map navigation supplies an in-range duel within the target anchor's acquisition radius and outside HQ weapon range")))
			return false;
		ACommandPlayerState* Wallet = ArmyTestSetup::Controller(State->GetWorld())->GetPlayerState<ACommandPlayerState>();
		AttackProbe = Spawn(Wallet, 50, Start);
		if (!Check(AttackProbe.IsValid() && AttackProbe->SpawnMember(UnitIndex, Start, 2),
				TEXT("A real region-Attack combat unit spawns")))
			return false;
		// Ordinary combat acquires and stops at weapon range, while the held
		// cooldown keeps the living target available throughout the observation.
		AttackProbe->GetUnits()[0]->NextAttackTime = TNumericLimits<float>::Max();
		Teleport(Threats[1].Get(), Target);
		return Check(FCommandService::SetRetreatThreshold(Wallet, AttackProbe.Get(), ERetreatThreshold::Never).IsAccepted()
				&& FCommandService::IssueForceOrder(Wallet, AttackProbe.Get(), EForceVerb::Attack, Home->RegionIndex).IsAccepted()
				&& AttackProbe->Verb == EForceVerb::Attack && AttackProbe->Status == EForceStatus::Marching
				&& AttackProbe->TargetRegionIndex == Home->RegionIndex
				&& FVector::Dist2D(AttackProbe->GetCenter(), AttackProbe->Destination) > 170.,
			TEXT("The command service accepts a displaced region Attack with Never retreat threshold"));
	}

	bool IsStationaryEngaged() const
	{
		if (!AttackProbe.IsValid() || AttackProbe->GetUnits().IsEmpty() || !Threats[1].IsValid() || !Threats[1]->IsAlive())
			return false;
		const AArmyUnit* Unit = AttackProbe->GetUnits()[0];
		const AAIController* AI = Cast<AAIController>(Unit->GetController());
		return AttackProbe->Verb == EForceVerb::Attack && AttackProbe->Status == EForceStatus::Marching
			&& AttackProbe->RetreatThreshold == ERetreatThreshold::Never && !AttackProbe->IsHoldingRegion()
			&& Unit->Target == Threats[1].Get() && AI && AI->GetMoveStatus() == EPathFollowingStatus::Idle
			&& Unit->GetVelocity().SizeSquared2D() <= 1.
			&& FVector::Dist2D(Unit->GetActorLocation(), Threats[1]->GetActorLocation()) <= Unit->WeaponRange()
			&& FVector::Dist2D(AttackProbe->GetCenter(), AttackProbe->Destination) > 170.;
	}
};
}

bool FHoldTimersTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmTimersTests::FScenario(this));
	return true;
}

#endif
