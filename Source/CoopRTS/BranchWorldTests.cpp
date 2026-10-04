#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "BranchFixture.h"

// The tier-2 purchase on real buildings: 100 Power + 50 Data once, atomically, refused with the reason the
// panel shows; the building's production waits 20 s and then resumes branched; a stun freezes the upgrade.

namespace BranchTests
{
// Every refusal spends nothing; the purchase spends exactly the price, once.
class FPurchase : public FBranchScenario
{
public:
	using FBranchScenario::FBranchScenario;

protected:
	bool Run() override
	{
		ACommandBuilding* Fresh = Fixture->SpawnBarracks(0, 1.f, 1);
		if (!Check(Fresh != nullptr, TEXT("A second, unlocked Barracks stands next to the first")))
			return true;
		Wallet->Resources = 10000;
		Wallet->Data = 200;
		const FCommandResult Unlocked = FBranchCommands::Purchase(Wallet, Fresh);
		if (!Check(!Unlocked.IsAccepted() && Unlocked.Message == TEXT("Lock a type first") && Untouched(10000, 200) && Fresh->Branch.Phase == EBranchPhase::None,
				TEXT("A building whose type is not locked refuses and spends nothing")))
			return true;
		Fresh->Destroy();

		Wallet->Data = 49;
		if (!Check(Reject() == TEXT("Need 1 more Data") && Untouched(10000, 49), TEXT("One Data short refuses without spending the Power")))
			return true;
		Wallet->Resources = 99;
		Wallet->Data = 500;
		if (!Check(Reject() == TEXT("Need 1 more Power") && Untouched(99, 500), TEXT("One Power short refuses without spending the Data")))
			return true;
		Wallet->Resources = 0;
		Wallet->Data = 0;
		if (!Check(Reject() == TEXT("Need 100 more Power and 50 more Data") && Untouched(0, 0), TEXT("An empty wallet is told the whole gap")))
			return true;
		Wallet->Resources = 10000;
		Wallet->Data = 200;
		if (!Check(Reject(State->EnemyCommander.Get()) == TEXT("Not your building") && Untouched(10000, 200) && Producer->Branch.Phase == EBranchPhase::None,
				TEXT("Only the owning commander buys")))
			return true;
		Producer->ApplyStun(5.f);
		if (!Check(Producer->IsStunned() && Reject() == TEXT("Stunned: wait for the stun to end") && Untouched(10000, 200),
				TEXT("A stunned building refuses and spends nothing")))
			return true;
		Producer->StunEndServerTime = -1.;

		if (!Buy() || !Check(Wallet->Resources == 9900 && Wallet->Data == 150 && Producer->IsUpgrading() && Producer->Branch.ProgressSeconds == 0.f, TEXT("The purchase spends exactly 100 Power and 50 Data and starts the upgrade")))
			return true;
		Check(Reject() == TEXT("Already upgrading") && Untouched(9900, 150), TEXT("A second purchase during the upgrade spends nothing"));
		Producer->Branch = { EBranchPhase::Done, BranchPolicy::UpgradeSeconds };
		Check(Reject() == TEXT("Already upgraded this battle") && Untouched(9900, 150), TEXT("A finished upgrade is once per battle per building"));
		return true;
	}

private:
	bool Untouched(int32 Power, int32 Data) const { return Wallet->Resources == Power && Wallet->Data == Data; }
};

// Production waits for the whole upgrade, then resumes: the next recruit is paid and arrives branched.
class FPauseThenBranched : public FBranchScenario
{
public:
	using FBranchScenario::FBranchScenario;

protected:
	bool Run() override
	{
		if (Stage < 2)
		{
			if (!PutForceInFar())
				return false;
			SetStage(2);
		}
		switch (Stage)
		{
		case 2:
			if (!Buy() || !Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(), TEXT("Production is enabled while the upgrade runs")))
				return true;
			Paid = Wallet->Resources;
			SetStage(3);
			return false;
		case 3:
			return Upgrading();
		case 4:
			return Resumed();
		default:
			return Arrived();
		}
	}

private:
	bool Upgrading()
	{
		const double Elapsed = StageSeconds();
		if (Producer->IsUpgrading())
			return !Check(Elapsed < 20.4 && Producer->ProductionProgressSeconds == 0.f && Force->GetPendingRecruitCount() == 0
					&& Producer->GetProductionState() == EProductionState::Paused && Wallet->Resources == Paid,
				TEXT("Production does not progress or pay while the building upgrades"));
		if (!Check(Elapsed >= 19.8 && Elapsed <= 20.4 && Producer->Branch.Phase == EBranchPhase::Done && Producer->Branch.ProgressSeconds == BranchPolicy::UpgradeSeconds,
				TEXT("The upgrade takes 20 s")))
			return true;
		SetStage(4);
		return false;
	}
	bool Resumed()
	{
		if (Force->RecruitsInTransit == 0)
			return !Check(StageSeconds() < 6., TEXT("Production resumes by itself once the upgrade is done"));
		if (!Check(Wallet->Resources == Paid - UnitCost(), TEXT("The first recruit after the upgrade is paid once at the unchanged price")))
			return true;
		SetStage(5);
		return false;
	}
	bool Arrived()
	{
		if (Joined() < 2)
			return !Check(StageSeconds() < 12., TEXT("The recruit reaches the force along the supply chain"));
		const AArmyUnit* Recruit = nullptr;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->GetCompositionSlot() == 1)
				Recruit = Unit;
		Check(Recruit && Recruit->GetUnitIndex() == BranchIndex() && Recruit->MaxHealth() == Warden->MaxHealth && Recruit->GetHealth() == Warden->MaxHealth,
			TEXT("A recruit made after the upgrade is a Warden at full Warden health"));
		return true;
	}

	int32 Paid = 0;
};

// A stun freezes the upgrade like any other progress and the building resumes it afterwards.
class FStunFreezesUpgrade : public FBranchScenario
{
public:
	using FBranchScenario::FBranchScenario;

protected:
	bool Run() override
	{
		const double Elapsed = StageSeconds();
		switch (Stage)
		{
		case 0:
			if (!Buy())
				return true;
			BoughtAt = Now();
			SetStage(1);
			return false;
		case 1:
			if (Elapsed < 5.)
				return false;
			Frozen = Producer->Branch.ProgressSeconds;
			Producer->ApplyStun(5.f);
			SetStage(2);
			return false;
		case 2:
			if (!Check(Producer->IsUpgrading() && FMath::IsNearlyEqual(Producer->Branch.ProgressSeconds, Frozen, .05f),
					TEXT("A stunned building's upgrade does not advance")))
				return true;
			if (Elapsed >= 4.5)
				SetStage(3);
			return false;
		default:
			if (Producer->IsUpgrading())
				return !Check(Now() - BoughtAt < 26., TEXT("The upgrade resumes when the stun ends"));
			// 5 s before the stun, 5 s frozen, 15 s after: the stun pushed completion from 20 s to 25 s.
			const double Total = Now() - BoughtAt;
			Check(Producer->Branch.Phase == EBranchPhase::Done && Frozen > 4.8f && Frozen < 5.3f && Total >= 24.5 && Total <= 25.6,
				TEXT("The stun added its whole duration to the upgrade"));
			return true;
		}
	}

private:
	double BoughtAt = 0.;
	float Frozen = 0.f;
};
}

using namespace BranchTests;
BRANCH_WORLD_TEST(FBranchPurchaseTest, "Purchase", FPurchase(this))
BRANCH_WORLD_TEST(FBranchPauseTest, "PauseThenBranched", FPauseThenBranched(this))
BRANCH_WORLD_TEST(FBranchStunTest, "StunFreezesUpgrade", FStunFreezesUpgrade(this))
#endif
