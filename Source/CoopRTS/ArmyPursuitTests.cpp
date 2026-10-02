#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "AIController.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyPursuitTest, "CoopRTS.Combat.Pursuit",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
class FPursuitScenario : public IAutomationLatentCommand
{
public:
	explicit FPursuitScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		UWorld* World = ArmyTestSetup::World();
		if (FPlatformTime::Seconds() - Started > 75.)
		{
			Test->AddError(TEXT("Pursuit fixture timed out waiting for a standalone world or navigation"));
			return Finish();
		}
		if (!World)
			return false;
		if (!bIsolated)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			bIsolated = true;
		}
		if (Stage == 0)
		{
			const ACommandGameState* State = World->GetGameState<ACommandGameState>();
			ACommandPlayerController* Owner = ArmyTestSetup::Controller(World);
			if (FPlatformTime::Seconds() - Started < 3. || !ArmyTestSetup::MapReady(State)
				|| !Owner || !Owner->GetPlayerState<ACommandPlayerState>()
				|| Owner->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0)
				return false;
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->IsProducer())
					FCommandService::ConfigureProduction(It->OwningPlayerState, *It,
						It->bForceConfigured ? It->ProductionRole : static_cast<EUnitRole>(255), false);
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				RemoveGroup(*It);
			Anchor = ArmyTestSetup::FromFriendlyHQ(State, 1700.f, 600.f, 100.f);
			if (!Spawn(World, Owner, false))
				return Finish();
			Left = Friendly->GetUnits()[0];
			Right = Hostile->GetUnits()[0];
			Place(Left.Get(), Anchor);
			Place(Right.Get(), Anchor + FVector(125.f, 0.f, 0.f));
			Arm(Friendly.Get());
			Arm(Hostile.Get());
			// Establish an in-range lock without doing damage, then leave range
			// without issuing another order: this is the original dead-band transition.
			Left->NextAttackTime = Right->NextAttackTime = TNumericLimits<float>::Max();
			static_cast<AActor*>(Friendly.Get())->Tick(.25f);
			static_cast<AActor*>(Hostile.Get())->Tick(.25f);
			Place(Right.Get(), Anchor + FVector(200.f, 0.f, 0.f));
			Left->NextAttackTime = Right->NextAttackTime = 0.f;
			Test->TestTrue(TEXT("Melee fixture starts 200 cm apart"),
				FMath::IsNearlyEqual(FVector::Dist2D(Left->GetActorLocation(), Right->GetActorLocation()), 200.));
			Stage = 1;
			StageStarted = World->GetTimeSeconds();
			return false;
		}
		const float Elapsed = World->GetTimeSeconds() - StageStarted;
		if (Stage == 1)
		{
			if (Elapsed > 20.f)
			{
				Test->AddError(TEXT("Two melee units at 200 cm failed to close and fight to a death within 20 game seconds"));
				return Finish();
			}
			if (Left.IsValid() && Right.IsValid())
			{
				bBothClosed |= FVector::Dist2D(Left->GetActorLocation(), Anchor) > 5.f
					&& FVector::Dist2D(Right->GetActorLocation(), Anchor + FVector(200.f, 0.f, 0.f)) > 5.f;
				bBothFired |= Left->AttackCount > 0 && Right->AttackCount > 0;
			}
			if (Friendly->GetUnits().IsEmpty() || Hostile->GetUnits().IsEmpty())
			{
				Test->TestTrue(TEXT("Both melee units close the dead band"), bBothClosed);
				Test->TestTrue(TEXT("Both melee units fire before the death"), bBothFired);
				RemoveGroup(Friendly.Get());
				RemoveGroup(Hostile.Get());
				if (!Spawn(World, ArmyTestSetup::Controller(World), true))
					return Finish();
				Place(Friendly->GetUnits()[0], Anchor + FVector(0.f, -45.f, 0.f));
				Place(Friendly->GetUnits()[1], Anchor + FVector(0.f, 45.f, 0.f));
				FirstArtillery = Hostile->GetUnits()[0];
				SecondArtillery = Hostile->GetUnits()[1];
				Place(FirstArtillery.Get(), Anchor + FVector(125.f, 0.f, 0.f));
				Place(SecondArtillery.Get(), Anchor + FVector(325.f, 0.f, 0.f));
				Arm(Friendly.Get());
				Hostile->Order = EArmyOrder::Hold;
				Hostile->Destination = Hostile->GetCenter();
				Stage = 2;
				StageStarted = World->GetTimeSeconds();
			}
			return false;
		}
		if (SecondArtillery.IsValid() && SecondArtillery->GetHealth() < SecondArtillery->MaxHealth())
			bReachedSecond = true;
		if (Hostile->GetUnits().IsEmpty())
		{
			Test->TestTrue(TEXT("Brawlers reach and damage the second Artillery after killing the first"), bReachedSecond);
			Test->TestFalse(TEXT("Brawlers survive to finish both Artillery"), Friendly->GetUnits().IsEmpty());
			return Finish();
		}
		if (Friendly->GetUnits().IsEmpty() || Elapsed > 25.f)
		{
			Test->AddError(TEXT("Brawlers failed to reach and kill both Artillery within 25 game seconds"));
			return Finish();
		}
		return false;
	}

private:
	static void Place(AArmyUnit* Unit, const FVector& Position)
	{
		Unit->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
	}

	void Arm(AArmyGroup* Group)
	{
		Group->Order = EArmyOrder::Attack;
		Group->Destination = Anchor + FVector(200.f, 0.f, 0.f);
	}

	static void RemoveGroup(AArmyGroup* Group)
	{
		if (!IsValid(Group))
			return;
		for (AArmyUnit* Unit : Group->GetUnits())
			if (IsValid(Unit))
			{
				if (AController* Controller = Unit->GetController())
					Controller->Destroy();
				Unit->Destroy();
			}
		Group->Destroy();
	}

	static void KeepRole(AArmyGroup* Group, EUnitRole Role, int32 Count)
	{
		TArray<AArmyUnit*> Removed;
		for (AArmyUnit* Unit : Group->GetUnits())
			if (Unit->GetUnitRole() != Role || Count-- <= 0)
				Removed.Add(Unit);
		for (AArmyUnit* Unit : Removed)
		{
			Group->OnMemberDied(Unit);
			if (AController* Controller = Unit->GetController())
				Controller->Destroy();
			Unit->Destroy();
		}
	}

	bool Spawn(UWorld* World, ACommandPlayerController* Owner, bool bArtillery)
	{
		Friendly = ArmyTestSetup::SpawnGroup(World, Owner, 0, Anchor);
		Hostile = ArmyTestSetup::SpawnGroup(World, nullptr, -1, Anchor + FVector(500.f, 0.f, 0.f));
		if (!Friendly.IsValid() || !Hostile.IsValid())
		{
			Test->AddError(TEXT("Pursuit groups failed to spawn"));
			return false;
		}
		KeepRole(Friendly.Get(), EUnitRole::Frontline, bArtillery ? 2 : 1);
		KeepRole(Hostile.Get(), bArtillery ? EUnitRole::Siege : EUnitRole::Frontline, bArtillery ? 2 : 1);
		return Test->TestEqual(TEXT("Friendly pursuit fixture has exact member count"), Friendly->GetUnits().Num(), bArtillery ? 2 : 1)
			&& Test->TestEqual(TEXT("Hostile pursuit fixture has exact member count"), Hostile->GetUnits().Num(), bArtillery ? 2 : 1);
	}

	bool Finish()
	{
		RemoveGroup(Friendly.Get());
		RemoveGroup(Hostile.Get());
		return true;
	}

	FAutomationTestBase* Test;
	TWeakObjectPtr<AArmyGroup> Friendly, Hostile;
	TWeakObjectPtr<AArmyUnit> Left, Right, FirstArtillery, SecondArtillery;
	FVector Anchor = FVector::ZeroVector;
	double Started;
	float StageStarted = 0.f;
	int32 Stage = 0;
	bool bIsolated = false, bBothClosed = false, bBothFired = false, bReachedSecond = false;
};
}

bool FArmyPursuitTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPursuitScenario(this));
	return true;
}

#endif
