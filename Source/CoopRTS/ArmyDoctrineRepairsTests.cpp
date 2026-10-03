#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyDoctrineFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoctrineRepairsTest, "CoopRTS.Doctrine.FieldRepairs",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ArmyDoctrineRepairsTests
{
using namespace ArmyDoctrineFixture;

class FRepairsScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool Step(double Now) override
	{
		AArmyUnit* Patient = ClampPatient.IsValid() ? ClampPatient.Get() : Actors.Armies[0]->GetUnits()[0].Get();
		AArmyUnit* Other = Actors.Armies[1]->GetUnits()[0];
		AArmyUnit* Attacker = Actors.Enemy->GetUnits()[0];
		switch (Stage)
		{
		case 0:
			return Stage0(Now, Patient, Other, Attacker);
		case 1:
			return Stage1(Now, Patient, Other);
		case 2:
			return Stage2(Now, Patient, Other, Attacker);
		case 3:
			return Stage3(Now, Patient, Attacker);
		case 4:
			return Stage4(Now, Patient);
		case 5:
			return Stage5(Now, Patient, Attacker);
		case 6:
			return Stage6(Now, Patient);
		case 7:
			return Stage7(Now, Patient);
		case 8:
			return Stage8(Now, Patient, Attacker);
		case 9:
			return Stage9(Now, Patient, Other, Attacker);
		case 10:
			return Stage10(Now, Other);
		case 11:
			return Stage11(Now, Other);
		default:
			return true;
		}
	}

	bool Stage0(double Now, AArmyUnit* Patient, AArmyUnit* Other, AArmyUnit* Attacker)
	{
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::FieldRepairs);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::FieldRepairs,
				TEXT("FieldRepairs choice accepted for the owning player")))
			return true;
		Patient->ReceiveAttack(55, Attacker);
		Other->ReceiveAttack(55, Attacker);
		// Damage is an interrupt and the unselected second owned army also inherits healing.
		if (!Check(WeaponHit(Attacker, Patient) > 0 && WeaponHit(Attacker, Other) > 0,
				TEXT("Actual hostile weapon damages both owned army members")))
			return true;
		Expected = Patient->GetHealth();
		OtherExpected = Other->GetHealth();
		Attacker->ReceiveAttack(20, Patient);
		EnemyExpected = Attacker->GetHealth();
		Next(1, Now);
		return false;
	}

	bool Stage1(double Now, AArmyUnit* Patient, AArmyUnit* Other)
	{
		if (!After(Now, 4.7))
			return false;
		if (!Check(Patient->GetHealth() == Expected && Other->GetHealth() == OtherExpected,
				TEXT("Both stationary armies wait five uninterrupted seconds before healing")))
			return true;
		Next(2, Now);
		return false;
	}

	bool Stage2(double Now, AArmyUnit* Patient, AArmyUnit* Other, AArmyUnit* Attacker)
	{
		if (!After(Now, 1.2))
			return false;
		if (!Check(Patient->GetHealth() > Expected && Other->GetHealth() > OtherExpected
					&& Patient->GetHealth() <= Patient->MaxHealth() && Other->GetHealth() <= Other->MaxHealth(),
				TEXT("Both owned armies gain real bounded health after the stationary delay")))
			return true;
		if (!Check(Attacker->GetHealth() == EnemyExpected && EnemyExpected < Attacker->MaxHealth(),
				TEXT("Stationary hostile member does not inherit the owner's FieldRepairs")))
			return true;
		Expected = Patient->GetHealth();
		if (!Check(WeaponHit(Attacker, Patient) > 0,
				TEXT("Hostile weapon interrupts ongoing repairs")))
			return true;
		Expected = Patient->GetHealth();
		Next(3, Now);
		return false;
	}

	bool Stage3(double Now, AArmyUnit* Patient, AArmyUnit* Attacker)
	{
		if (!After(Now, 3.0))
			return false;
		if (!Check(Patient->GetHealth() == Expected,
				TEXT("Taking damage resets repairs and cannot spend banked healing")))
			return true;
		// Firing a real frontline weapon is a separate interrupt.
		const int32 EnemyBefore = Attacker->GetHealth();
		if (!Check(WeaponHit(Patient, Attacker) > 0 && Attacker->GetHealth() < EnemyBefore,
				TEXT("Repairing frontline fires an actual hostile-damaging shot")))
			return true;
		Next(4, Now);
		return false;
	}

	bool Stage4(double Now, AArmyUnit* Patient)
	{
		if (!After(Now, 4.7))
			return false;
		if (!Check(Patient->GetHealth() == Expected,
				TEXT("Firing also restarts the full five-second quiet interval")))
			return true;
		Next(5, Now);
		return false;
	}

	bool Stage5(double Now, AArmyUnit* Patient, AArmyUnit* Attacker)
	{
		if (!After(Now, 1.3))
			return false;
		if (!Check(Patient->GetHealth() > Expected, TEXT("Repairs resume after five seconds without fire")))
			return true;
		Patient->ReceiveAttack(20, Attacker);
		Expected = Patient->GetHealth();
		StartPosition = Patient->GetActorLocation();
		FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[0].Get(), EForceVerb::MoveHold,
			ArmyTestSetup::TravelRegion(Actors.Armies[0].Get(), Actors.State->EnemyHeadquarters->GetActorLocation()));
		if (!Check(Actors.Armies[0]->Status == EForceStatus::Marching,
				TEXT("Owned MoveHold begins an actual navigation interruption")))
			return true;
		Next(6, Now);
		return false;
	}

	bool Stage6(double Now, AArmyUnit* Patient)
	{
		if (!After(Now, 2.))
			return false;
		if (!Check(FVector::Dist2D(Patient->GetActorLocation(), StartPosition) > 100.f
					&& Patient->GetVelocity().SizeSquared2D() > FMath::Square(1.f)
					&& Patient->GetHealth() == Expected,
				TEXT("Navigating member remains in motion without healing before Hold")))
			return true;
		FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[0].Get(), EForceVerb::MoveHold,
			ArmyTestSetup::RegionAt(Actors.State.Get(), Actors.State->FriendlyHeadquarters->GetActorLocation()));
		Next(7, Now);
		return false;
	}

	bool Stage7(double Now, AArmyUnit* Patient)
	{
		if (Actors.Armies[0]->Status != EForceStatus::Holding
			|| Patient->GetVelocity().SizeSquared2D() > FMath::Square(1.f))
		{
			StageStarted = Now;
			return false;
		}
		if (!After(Now, 4.7))
			return false;
		if (!Check(Patient->GetHealth() == Expected,
				TEXT("Movement interruption cannot bank healing across the subsequent Hold")))
			return true;
		Next(8, Now);
		return false;
	}

	bool Stage8(double Now, AArmyUnit* Patient, AArmyUnit* Attacker)
	{
		if (!After(Now, 1.4))
			return false;
		if (!Check(Patient->GetHealth() > Expected && Patient->GetHealth() <= Patient->MaxHealth(),
				TEXT("Stationary survivor recovers after the new complete delay")))
			return true;
		// Use an equivalent untouched group member for the near-max clamp probe.
		Patient = Actors.Armies[0]->GetUnits()[1];
		ClampPatient = Patient;
		Patient->ReceiveAttack(1, Attacker);
		WeaponHit(Attacker, Patient);
		Next(9, Now);
		return false;
	}

	bool Stage9(double Now, AArmyUnit* Patient, AArmyUnit* Other, AArmyUnit* Attacker)
	{
		if (!After(Now, 8.))
			return false;
		if (!Check(Patient->GetHealth() == Patient->MaxHealth()
					&& Other->GetHealth() == Other->MaxHealth(),
				TEXT("Continuous repairs clamp both living units to max health, never overflow")))
			return true;
		Patient->ReceiveAttack(Patient->GetHealth(), Attacker);
		if (!Check(!Patient->IsAlive() && !Actors.Armies[0]->GetUnits().Contains(Patient),
				TEXT("Lethal damage removes a patient; FieldRepairs cannot resurrect it")))
			return true;
		Other->ReceiveAttack(20, Attacker);
		AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
		AArmyUnit* Siege = Actors.Armies[1]->GetUnits()[4];
		if (!Check(IsValid(HQ) && Siege->IsAlive(),
				TEXT("Live HQ and allied siege are available for terminal healing check")))
			return true;
		HQ->Health = 1;
		Siege->SetActorLocation(HQ->GetActorLocation() + FVector(150.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->NextAttackTime = 0.f;
		const uint32 BeforeShot = Siege->AttackCount;
		Siege->FireAt(HQ);
		if (!Check(Siege->AttackCount == BeforeShot + 1 && HQ->Health == 0,
				TEXT("Real weapon destroys HQ before terminal repair observation")))
			return true;
		Next(10, Now);
		return false;
	}

	bool Stage10(double Now, AArmyUnit* Other)
	{
		if (Actors.State->MatchResult == EMatchResult::Ongoing)
			return false;
		if (!Check(Actors.State->MatchResult == EMatchResult::Victory,
				TEXT("HQ weapon hit publishes terminal Victory")))
			return true;
		OtherExpected = Other->GetHealth();
		Next(11, Now);
		return false;
	}

	bool Stage11(double Now, AArmyUnit* Other)
	{
		if (!After(Now, 6.))
			return false;
		if (!Check(Other->GetHealth() == OtherExpected && OtherExpected < Other->MaxHealth(),
				TEXT("Terminal match blocks healing even after an uninterrupted six seconds")))
			return true;
		Test->AddInfo(TEXT("Repairs: five-second delay, health gain in both armies, damage/fire/movement resets, no banking, max cap, no resurrection or terminal healing."));
		return true;
	}
	int32 Expected = 0;
	int32 OtherExpected = 0;
	int32 EnemyExpected = 0;
	FVector StartPosition = FVector::ZeroVector;
	TWeakObjectPtr<AArmyUnit> ClampPatient;
};
}

bool FDoctrineRepairsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ArmyDoctrineRepairsTests::FRepairsScenario(this));
	return true;
}

#endif
