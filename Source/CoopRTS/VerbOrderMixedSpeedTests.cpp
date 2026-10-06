#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"
#include "Rules/MarchSpeedPolicy.h"

namespace VerbOrderMixedSpeedTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MixedSpeed) {}

private:
	bool RunScenario() override
	{
		if (const EStepResult Result = StartSelection(); Result != EStepResult::Continue)
			return Result == EStepResult::Finished;
		if (Stage == 2)
			return At(Second.Get(), Home) && Second->Status == EForceStatus::Holding
				&& Check(FVector::Dist2D(StartPosition, Second->GetCenter()) > 500.f,
					TEXT("The formerly capped fast force physically completes its next authored-speed march"));
		for (const AArmyGroup* Group : { Force.Get(), Second.Get() })
		{
			const bool bHolding = Group->Status == EForceStatus::Holding;
			const float Speed = bHolding ? Group->GetBaseMarchSpeed() : ExpectedSpeed;
			if (!Check(FMath::IsNearlyEqual(Group->MarchSpeed, bHolding ? 0.f : ExpectedSpeed)
						&& FMath::IsNearlyEqual(Group->GetMarchSpeed(), Speed),
					TEXT("Selection cap applies while marching and ends at completed MoveHold arrival")))
				return true;
			for (const AArmyUnit* Unit : Group->GetUnits())
				if (!Check(FMath::IsNearlyEqual(Unit->GetCharacterMovement()->MaxWalkSpeed, Speed,
							   bHolding ? .01f : Speed * FMath::Max(MarchSpeedPolicy::MaxCatchUp - 1.f, 1.f - MarchSpeedPolicy::MinAhead) + .01f),
						TEXT("Moving characters stay within the catch-up band of the shared cap; completed orders restore authored formation speed")))
					return true;
		}
		if (Holding(Target) && Second->Status == EForceStatus::Holding && At(Second.Get(), Target))
		{
			if (!Check(FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f
						&& FVector::Dist2D(SecondStart, Second->GetCenter()) > 500.f,
					TEXT("Both selected formations physically traverse the route at their common speed")))
				return true;
			Empty = EmptyGroup(Wallet, 0, FromFriendlyHQ(State, 900.f, -600.f, 100.f));
			AArmyGroup* const Selection[] = { Second.Get(), Empty.Get() };
			if (!Check(Empty.IsValid() && Empty->GetBaseMarchSpeed() == 0.f
						&& FCommandService::IssueForceOrder(Wallet, Selection, EForceVerb::MoveHold, Home).IsAccepted()
						&& FMath::IsNearlyEqual(Second->GetMarchSpeed(), Second->GetBaseMarchSpeed()),
					TEXT("A memberless orphan does not erase the authored cap of a real selected force")))
				return true;
			StartPosition = Second->GetCenter();
			SetStage(2);
		}
		return false;
	}
	EStepResult StartSelection()
	{
		if (Stage == 0)
		{
			Second = EmptyGroup(Wallet, 0, FromFriendlyHQ(State, 800.f, -500.f, 100.f));
			if (!Check(Second.IsValid(), TEXT("Second selected force spawns")))
				return EStepResult::Finished;
			int32 FastestIndex = INDEX_NONE;
			for (int32 Index = 0; Index < State->Content->Units.Num(); ++Index)
				if (const UArmyUnitDefinition* Definition = State->Content->Unit(Index);
					Definition && (FastestIndex == INDEX_NONE || Definition->MoveSpeed > State->Content->Unit(FastestIndex)->MoveSpeed))
					FastestIndex = Index;
			if (!Check(FastestIndex != INDEX_NONE, TEXT("Catalogue supplies a faster comparison unit")))
				return EStepResult::Finished;
			for (int32 Slot = 0; Slot < 6; ++Slot)
				if (!Check(Second->SpawnMember(FastestIndex,
							   Second->GetHomeLocation() + FVector(0.f, Slot * 70.f, 0.f), Slot)
							!= nullptr,
						TEXT("Homogeneous comparison force has real faster catalogue members")))
					return EStepResult::Finished;
			ExpectedSpeed = TNumericLimits<float>::Max();
			for (const AArmyGroup* Group : { Force.Get(), Second.Get() })
				for (const AArmyUnit* Unit : Group->GetUnits())
					ExpectedSpeed = FMath::Min(ExpectedSpeed, Unit->GetDefinition()->MoveSpeed);
			if (!Check(Force->GetBaseMarchSpeed() < Second->GetBaseMarchSpeed(), TEXT("Fixture combines genuinely different base force speeds")))
				return EStepResult::Finished;
			TArray<AArmyGroup*> Selection{ Force.Get(), Second.Get() };
			const FCommandResult Result = FCommandService::IssueForceOrder(Wallet, Selection, EForceVerb::MoveHold, Target);
			if (!Check(Result.IsAccepted(),
					*FString::Printf(TEXT("Bulk owner command orders both selected forces: %s"), *Result.Message)))
				return EStepResult::Finished;
			StartPosition = Force->GetCenter();
			SecondStart = Second->GetCenter();
			SetStage(1);
		}
		return EStepResult::Continue;
	}
};
}

VERB_WORLD_TEST(FVerbMixedSpeedTest, "MixedSelectionSpeed", MixedSpeed)

#endif
