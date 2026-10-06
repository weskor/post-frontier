#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

// Twelve six-member forces of one wave march to the same region. Their destinations must not pile onto one
// slot rectangle, and all twelve must reach Holding.
namespace VerbOrderLanesTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	static constexpr int32 ForceTotal = 12;
	// A force's slots span 440 x 280 cm in the encounter layout.
	static constexpr float RectX = 440.f, RectY = 280.f;

	bool SpawnWave()
	{
		Wave.Add(Force.Get());
		const FVector Base = State->GetRegionAnchor(Home);
		const AMapRegion* HomeRegion = Region(State, Home);
		for (int32 Index = 1; Index < ForceTotal; ++Index)
		{
			FVector Start = FVector::ZeroVector;
			bool bFound = false;
			// A grid of 800 x 600 cm cells around the home anchor; the first free, in-region cell.
			for (int32 Cell = Next; Cell < 64 && !bFound; ++Cell, Next = Cell)
			{
				Start = Base + FVector((Cell % 8 - 3.5f) * 800.f, (Cell / 8 - 3.5f) * 600.f, 100.f);
				bFound = HomeRegion->Contains(Start) && FVector::Dist2D(Start, Force->GetCenter()) > 700.f;
			}
			AArmyGroup* Group = bFound ? SpawnGroup(GameWorld, PC, 10 + Index, Start) : nullptr;
			if (!Check(Group != nullptr && Group->GetJoinedCount() == 6, TEXT("Each force of the wave spawns with six members")))
				return false;
			Wave.Add(Group);
		}
		return true;
	}

	// Fraction of one slot rectangle two forces' rectangles share.
	static float Overlap(const FVector& A, const FVector& B)
	{
		return FMath::Max(0.f, RectX - FMath::Abs(A.X - B.X)) * FMath::Max(0.f, RectY - FMath::Abs(A.Y - B.Y)) / (RectX * RectY);
	}

	bool RunScenario() override
	{
		if (Stage == 0)
		{
			if (!SpawnWave())
				return true;
			for (AArmyGroup* Group : Wave)
				if (!Check(FCommandService::IssueForceOrder(Wallet, Group, EForceVerb::MoveHold, Intermediate).IsAccepted(),
						TEXT("Every force of the wave accepts the order")))
					return true;
			SetStage(1);
		}
		if (Stage == 1)
		{
			// Destinations are read as ordered, before holding moves them to posts.
			int32 Heavy = 0, Pairs = 0;
			float Sum = 0.f, Worst = 0.f;
			for (int32 A = 0; A < Wave.Num(); ++A)
				for (int32 B = A + 1; B < Wave.Num(); ++B, ++Pairs)
				{
					const float Shared = Overlap(Wave[A]->Destination, Wave[B]->Destination);
					Sum += Shared;
					Worst = FMath::Max(Worst, Shared);
					Heavy += Shared > .5f;
				}
			Test->AddInfo(FString::Printf(TEXT("%d pairs: %d share over half a rectangle, mean %.2f, worst %.2f"), Pairs, Heavy, Sum / Pairs, Worst));
			if (!Check(Heavy <= 4 && Sum / Pairs <= .12f,
					TEXT("The twelve forces do not pile onto one slot rectangle: few pairs share over half of it, and the mean overlap is small")))
				return true;
			SetStage(2);
		}
		for (const AArmyGroup* Group : Wave)
			if (!Group->IsHoldingRegion() || Group->TargetRegionIndex != Intermediate)
				return false;
		return Check(State->GetRegionController(Intermediate) == 0, TEXT("All twelve forces hold the region the wave captured"));
	}

	TArray<AArmyGroup*> Wave;
	int32 Next = 0;
};
}

VERB_WORLD_TEST(FVerbLanesTest, "Lanes", Lanes)

#endif
