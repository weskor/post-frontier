#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "BranchFixture.h"
#include "Rules/BranchPolicy.h"

// Refits (decision B2): existing members change form one at a time through the supply channel, each after the
// delay of a replacement (4 s + 2 s per hop, 8 s two hops out), keep their HP fraction and pay nothing. A member
// the supply chain does not reach, and a member of an orphan force, keep the old form.

namespace BranchTests
{
// Three members in Far; the producer upgrades; the members refit lowest slot first, 8 s apart.
class FRefitOneAtATime : public FBranchScenario
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
			Produce();
			Produce();
			if (!Check(Force->RecruitsInTransit == 2, TEXT("Two recruits are paid and in transit")))
				return true;
			SetStage(3);
			return false;
		case 3:
			return Gathered();
		case 4:
			return Upgrading();
		default:
			return Refitting();
		}
	}

private:
	bool Gathered()
	{
		if (Joined() < 3)
			return !Check(StageSeconds() < 12., TEXT("Both recruits reach the force"));
		for (AArmyUnit* Unit : Force->GetUnits())
		{
			Originals.Add(Unit);
			if (Unit->GetCompositionSlot() == 1)
			{
				Hurt = Unit;
				Unit->ReceiveEnvironmentalDamage(Unit->MaxHealth() / 2);
			}
		}
		// Half of the Brawler's 330 HP: 165.
		if (!Check(Hurt && Hurt->GetHealth() == 165 && Buy(), TEXT("The member in slot 1 is at half health when the upgrade starts")))
			return true;
		SetStage(4);
		return false;
	}
	bool Upgrading()
	{
		if (!Check(Branched() == 0, TEXT("Nobody refits before the upgrade is done")))
			return true;
		if (Producer->IsUpgrading())
			return !Check(StageSeconds() < 22., TEXT("The upgrade finishes"));
		PowerAtDone = Wallet->Resources;
		DataAtDone = Wallet->Data;
		LastChange = Now();
		SetStage(5);
		return false;
	}
	bool Refitting()
	{
		const int32 Count = Branched();
		if (!Check(Count <= Seen.Num() + 1 && Joined() == 3 && MemberActors() == 3 && Wallet->Resources == PowerAtDone && Wallet->Data == DataAtDone,
				TEXT("Members refit one at a time, in place, for free")))
			return true;
		if (Count == Seen.Num())
			return !Check(Now() - LastChange < 10., TEXT("The next member refits after one delivery delay"));
		const AArmyUnit* Next = nullptr;
		for (const AArmyUnit* Unit : Originals)
			if (IsValid(Unit) && Unit->GetUnitIndex() == BranchIndex() && !Seen.Contains(Unit->GetCompositionSlot()))
				Next = Unit;
		const double Gap = Now() - LastChange;
		if (!Check(Next && Next->GetCompositionSlot() == Seen.Num() && Gap >= 7.9 && Gap <= 9.2,
				*FString::Printf(TEXT("Slot %d refits next, %.2f s after the previous change (expected 8 s)"), Seen.Num(), Gap)))
			return true;
		Seen.Add(Next->GetCompositionSlot());
		LastChange = Now();
		// 165/330 of the Warden's 429 is 214.5, rounded to the nearest point.
		if (!Check(Next->MaxHealth() == Warden->MaxHealth && Next->GetDefinition() == Warden && Next->GetUnitRole() == Warden->Role
					&& Next->GetHealth() == (Next == Hurt ? 215 : Warden->MaxHealth),
				TEXT("A refitted member takes the Warden's stats and keeps its HP fraction (165/330 becomes 215/429)")))
			return true;
		return Seen.Num() == 3;
	}

	TArray<TObjectPtr<AArmyUnit>> Originals;
	AArmyUnit* Hurt = nullptr;
	TArray<int32> Seen;
	double LastChange = 0.;
	int32 PowerAtDone = 0, DataAtDone = 0;
};

// The chain is cut after the upgrade: nobody refits while cut off, and the full delay runs again when it is whole.
class FRefitCutOff : public FBranchScenario
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
			if (!Buy())
				return true;
			SetStage(3);
			return false;
		case 3:
			if (Producer->IsUpgrading())
				return !Check(StageSeconds() < 22., TEXT("The upgrade finishes"));
			Power = Wallet->Resources;
			SetStage(4);
			return false;
		case 4:
			if (StageSeconds() < 3.)
				return !Check(Branched() == 0, TEXT("The first refit is 8 s away"));
			Fixture->SetController(Neck, 5);
			SetStage(5);
			return false;
		case 5:
			if (!Force->bSupplyCutOff)
				return !Check(StageSeconds() < 2., TEXT("Cutting the neck cuts Far off"));
			SetStage(6);
			return false;
		case 6:
			if (!Check(Branched() == 0 && Force->bSupplyCutOff && Wallet->Resources == Power, TEXT("A cut-off member keeps its old form however long the cut lasts")))
				return true;
			if (StageSeconds() >= 20.)
			{
				Fixture->SetController(Neck, 0);
				SetStage(7);
			}
			return false;
		default:
			if (Branched() == 0)
				return !Check(StageSeconds() < 9.5, TEXT("Reconnecting refits the member after the full delay"));
			Check(StageSeconds() >= 7.9 && StageSeconds() <= 9.2 && Wallet->Resources == Power && Joined() == 1,
				TEXT("The restored chain restarts the whole 8 s delay and charges nothing"));
			return true;
		}
	}

private:
	int32 Power = 0;
};

// The producer dies after the upgrade: its force is an orphan and keeps the old form.
class FRefitOrphan : public FBranchScenario
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
			if (!Buy())
				return true;
			SetStage(3);
			return false;
		case 3:
			if (Producer->IsUpgrading())
				return !Check(StageSeconds() < 22., TEXT("The upgrade finishes"));
			Producer->Destroy();
			if (!Check(!IsValid(Force->GetProductionBuilding()) && Branched() == 0, TEXT("The producer dies before the first refit and leaves an orphan")))
				return true;
			SetStage(4);
			return false;
		default:
			if (!Check(Branched() == 0 && Joined() == 1, TEXT("An orphan never refits")))
				return true;
			return StageSeconds() >= 15.;
		}
	}
};
}

using namespace BranchTests;
BRANCH_WORLD_TEST(FBranchRefitOrderTest, "RefitOneAtATime", FRefitOneAtATime(this))
BRANCH_WORLD_TEST(FBranchRefitCutOffTest, "RefitCutOff", FRefitCutOff(this))
BRANCH_WORLD_TEST(FBranchRefitOrphanTest, "RefitOrphan", FRefitOrphan(this))
#endif
