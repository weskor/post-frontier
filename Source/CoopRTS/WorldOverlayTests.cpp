#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "HAL/PlatformTime.h"
#include "PlanningFixture.h"
#include "WorldOverlay.h"

// The overlay only ever draws one frame's submissions. A world that does not flush (planning stands the world still, a missed
// tick) must not grow a backlog, and a backlog drawn once must not freeze the frame that removes it: the owner's planning
// freeze was 1.5 million pending lines instanced on unpause and then removed instance by instance.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldOverlayBoundedTest, "CoopRTS.Presentation.Paused.OverlayBounded",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace WorldOverlayTests
{
constexpr int32 BurstLines = 200000;
constexpr int32 FrameLines = 20000;
constexpr int32 BacklogFrames = 10;
constexpr int32 HeldFrames = 120;
// One frame of work, with generous headroom for a loaded machine: the quadratic removal took minutes.
constexpr double FrameBudgetSeconds = 2.;

class FScenario final : public PlanningFixture::FScenario
{
public:
	using PlanningFixture::FScenario::FScenario;

private:
	enum EStage : int32
	{
		Burst,
		Rendered,
		Shrink,
		Backlog,
		Held,
	};

	void Cleanup() override
	{
		if (IsValid(Probe))
			Probe->Destroy();
	}

	bool Step() override
	{
		switch (Stage)
		{
		case Burst:
			return Grow();
		case Rendered:
			return ++Frames > 3 ? Advance(Shrink) : false;
		case Shrink:
			return Shrunk();
		case Backlog:
			return Backlogged();
		default:
			return HeldStaysBounded();
		}
	}

	void Submit(int32 Count) const
	{
		for (int32 Index = 0; Index < Count; ++Index)
			Probe->Line(FVector(Index, 0., 0.), FVector(Index, 1., 0.), FColor::White, 1.f);
	}

	// A private overlay that never ticks by itself; the test ticks it, as the engine would once per frame.
	AWorldOverlay* SpawnProbe()
	{
		Probe = World->SpawnActor<AWorldOverlay>();
		if (IsValid(Probe))
			Probe->SetActorTickEnabled(false);
		return Probe;
	}

	// Seconds one tick takes.
	double TimedTick() const
	{
		const double Start = FPlatformTime::Seconds();
		Probe->Tick(0.f);
		return FPlatformTime::Seconds() - Start;
	}

	bool Advance(int32 Next)
	{
		Enter(Next);
		Frames = 0;
		return false;
	}

	// A huge submission with no tick in between draws exactly what was submitted, within a frame.
	bool Grow()
	{
		if (!Check(SpawnProbe() != nullptr, TEXT("A probe overlay spawns")))
			return true;
		Probe->Tick(0.f);
		Probe->Tick(0.f);
		Own = Probe->DrawnLineCount();
		Submit(BurstLines);
		const double Seconds = TimedTick();
		if (!Check(Probe->DrawnLineCount() == BurstLines + Own,
				FString::Printf(TEXT("A burst draws exactly the lines submitted this frame (%d of %d + %d)"), Probe->DrawnLineCount(), BurstLines, Own))
			|| !Check(Seconds < FrameBudgetSeconds, FString::Printf(TEXT("A %d-line frame flushes within a frame (%.2f s)"), BurstLines, Seconds)))
			return true;
		return Advance(Rendered);
	}

	// The renderer has seen the burst (as in the owner's freeze); the next, normal frame shrinks back by a clear and re-add,
	// within a frame.
	bool Shrunk()
	{
		const double Seconds = TimedTick();
		if (!Check(Probe->DrawnLineCount() == Own, FString::Printf(TEXT("The next frame draws only its own lines (%d, expected %d)"), Probe->DrawnLineCount(), Own))
			|| !Check(Seconds < FrameBudgetSeconds, FString::Printf(TEXT("Shrinking from %d lines flushes within a frame (%.2f s)"), BurstLines, Seconds)))
			return true;
		return Advance(Backlog);
	}

	// Frames that submit and never flush leave at most the last two frames behind, and drawing them is one normal flush.
	bool Backlogged()
	{
		Submit(FrameLines);
		if (++Frames < BacklogFrames)
			return false;
		const int32 Queued = Probe->PendingLineCount();
		if (!Check(Queued >= FrameLines && Queued <= 2 * FrameLines,
				FString::Printf(TEXT("%d unflushed frames of %d lines leave at most two frames pending (%d)"), BacklogFrames, FrameLines, Queued)))
			return true;
		const double Flush = TimedTick();
		if (!Check(Probe->DrawnLineCount() == Queued + Own, FString::Printf(TEXT("The backlog draws as the pending lines only (%d, expected %d)"), Probe->DrawnLineCount(), Queued + Own))
			|| !Check(Flush < FrameBudgetSeconds, FString::Printf(TEXT("Flushing the bounded backlog takes a frame (%.2f s)"), Flush)))
			return true;
		return Advance(Held);
	}

	// Planning holds the world still for a minute with the HUD drawing every frame: the real overlay's pending list stays one
	// frame's worth.
	bool HeldStaysBounded()
	{
		AWorldOverlay* Overlay = AWorldOverlay::Get(World);
		if (!Check(State->IsPlanning() && World->IsPaused() && Overlay, TEXT("Planning holds the world while the overlay is watched")))
			return true;
		Pending = FMath::Max(Pending, Overlay->PendingLineCount());
		if (++Frames < HeldFrames)
			return false;
		Check(Pending < 10000, FString::Printf(TEXT("The pending list stays bounded over %d paused frames (largest %d)"), HeldFrames, Pending));
		return Done();
	}

	AWorldOverlay* Probe = nullptr;
	int32 Own = 0;
	int32 Frames = 0;
	int32 Pending = 0;
};
}

bool FWorldOverlayBoundedTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(WorldOverlayTests::FScenario(this));
	return true;
}
#endif
