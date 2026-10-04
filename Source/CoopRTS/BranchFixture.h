#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Commands/BranchCommands.h"
#include "SupplyDeliveryFixture.h"
#include "UObject/UObjectGlobals.h"

namespace BranchTests
{
// The supply scenario (main - Neck - Far, one paid Frontline Barracks, JEV gone) with one test-only branch of
// the Brawler appended to the catalogue: the Warden, +30% HP, same price and capacity. Real branch definitions
// come from Build/Content/units.json; this one keeps the purchase, pause and refit scenarios independent of them.
class FBranchScenario : public SupplyTests::FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;
	~FBranchScenario() override
	{
		if (IsValid(Warden) && State && IsValid(State->Content))
			State->Content->Units.Remove(Warden);
	}

protected:
	ACommandBuilding* SpawnProducer() override
	{
		const UArmyUnitDefinition* Base = State->Content->Unit(ArmyTestSetup::UnitIndex(State, EUnitRole::Frontline));
		if (!Check(Base != nullptr, TEXT("The catalogue has a Brawler to branch")))
			return nullptr;
		Warden = DuplicateObject<UArmyUnitDefinition>(Base, State->Content);
		Warden->Id = TEXT("warden-test");
		Warden->DisplayName = FText::FromString(TEXT("Warden"));
		Warden->BranchOf = Base->Id;
		Warden->BranchSummary = FText::FromString(TEXT("+30% HP"));
		Warden->MaxHealth = Base->MaxHealth * 13 / 10;
		State->Content->Units.Add(Warden);
		return SupplyTests::FScenarioBase::SpawnProducer();
	}

	int32 BaseIndex() const { return ArmyTestSetup::UnitIndex(State, EUnitRole::Frontline); }
	int32 BranchIndex() const { return State->Content->IndexOf(Warden); }
	// Living members already in the Warden form.
	int32 Branched() const
	{
		int32 Count = 0;
		for (const AArmyUnit* Unit : Force->GetUnits())
			Count += IsValid(Unit) && Unit->IsAlive() && Unit->GetUnitIndex() == BranchIndex();
		return Count;
	}
	// The wallet holds Power and Data for a purchase and the command accepts it.
	bool Buy(int32 Power = 10000, int32 Data = 200)
	{
		Wallet->Resources = Power;
		Wallet->Data = Data;
		return Check(FBranchCommands::Purchase(Wallet, Producer.Get()).IsAccepted(), TEXT("The purchase is accepted"));
	}
	FString Reject(ACommandPlayerState* Buyer = nullptr)
	{
		const FCommandResult Result = FBranchCommands::Purchase(Buyer ? Buyer : Wallet, Producer.Get());
		return Result.IsAccepted() ? FString(TEXT("accepted")) : Result.Message;
	}

	UArmyUnitDefinition* Warden = nullptr;
};
}

#define BRANCH_WORLD_TEST(ClassName, TestName, ...)                                \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(ClassName, "CoopRTS.Forces.Branch." TestName, \
		EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)  \
	bool ClassName::RunTest(const FString&)                                        \
	{                                                                              \
		ADD_LATENT_AUTOMATION_COMMAND(__VA_ARGS__);                                \
		return true;                                                               \
	}
#endif
