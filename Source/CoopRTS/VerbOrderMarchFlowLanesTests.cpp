#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

// Forces of one wave march to the same region. Their destinations must not pile onto one slot rectangle, and all
// of them must reach Holding. In the second variant four forces march and the first three are wiped as they set
// out: the fourth stands on a lane outside capture range, and must still take the region.
namespace VerbOrderLanesTests
{
using namespace VerbOrderTests;

template <bool bLoseLaneZero>
class FScenarioT : public FScenarioBase
{
public:
	FScenarioT(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	static constexpr int32 ForceTotal = bLoseLaneZero ? 4 : 12;
	// A force's slots span 440 x 280 cm in the encounter layout.
	static constexpr float RectX = 440.f, RectY = 280.f;

	bool SpawnWave()
	{
		Wave.Add(Force);
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

	bool CheckSpread()
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
		return Check(Heavy <= 2 && Sum / Pairs <= .08f,
			TEXT("The forces do not pile onto one slot rectangle: at most two pairs share over half of it, and the mean overlap is small"));
	}

	bool RunScenario() override
	{
		if (Stage == 0)
		{
			if (!SpawnWave())
				return true;
			for (const TWeakObjectPtr<AArmyGroup>& Group : Wave)
				if (!Check(FCommandService::IssueForceOrder(Wallet, Group.Get(), EForceVerb::MoveHold, Intermediate).IsAccepted()
							&& Group->WaypointRegionIndex == Intermediate,
						TEXT("Every force of the wave accepts the order with the first region as its waypoint")))
					return true;
			if (bLoseLaneZero)
			{
				const float Reach = FVector::Dist2D(Wave.Last()->Destination, State->GetRegionAnchor(Intermediate));
				Test->AddInfo(FString::Printf(TEXT("The fourth force's destination is %.0f cm from the anchor"), Reach));
				if (!Check(Reach > ACapturePoint::CaptureRadius, TEXT("The fourth force stands on a lane outside capture range")))
					return true;
				for (int32 Index = 0; Index < ForceTotal - 1; ++Index)
				{
					const TArray<TObjectPtr<AArmyUnit>> Members = Wave[Index]->GetUnits();
					for (AArmyUnit* Unit : Members)
						Unit->ReceiveAttack(Unit->GetHealth(), Hostile->GetUnits()[0]);
				}
			}
			else if (!CheckSpread())
				return true;
			SetStage(1);
		}
		const int32 First = bLoseLaneZero ? ForceTotal - 1 : 0;
		for (int32 Index = First; Index < ForceTotal; ++Index)
			if (!Wave[Index].IsValid() || !Wave[Index]->IsHoldingRegion() || Wave[Index]->TargetRegionIndex != Intermediate)
				return false;
		return Check(State->GetRegionController(Intermediate) == 0,
			bLoseLaneZero ? TEXT("The surviving force took the region from its lane") : TEXT("All twelve forces hold the region the wave captured"));
	}

	TArray<TWeakObjectPtr<AArmyGroup>> Wave;
	int32 Next = 0;
};
}

namespace VerbOrderLanesTests
{
using FScenario = FScenarioT<false>;
}

namespace VerbOrderLaneCaptureTests
{
using FScenario = VerbOrderLanesTests::FScenarioT<true>;
}

VERB_WORLD_TEST(FVerbLanesTest, "Lanes", Lanes)
VERB_WORLD_TEST(FVerbLaneCaptureTest, "LaneCapture", LaneCapture)

#endif
