#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Commands/BranchCommands.h"
#include "SupplyDeliveryFixture.h"
#include "UObject/UObjectGlobals.h"

namespace BranchTests
{
// The supply scenario (main - Neck - Far, one paid Frontline Barracks, JEV gone) whose Brawler branch is the real
// Warden of Build/Content/units.json: +30% HP, same price and capacity.
class FBranchScenario : public SupplyTests::FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;

protected:
	ACommandBuilding* SpawnProducer() override
	{
		Warden = const_cast<UArmyUnitDefinition*>(State->Content->FindUnit(TEXT("warden")));
		if (!Check(Warden != nullptr, TEXT("The catalogue has the Warden")))
			return nullptr;
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
