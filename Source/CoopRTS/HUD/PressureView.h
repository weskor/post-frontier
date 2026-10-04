#pragma once

#include "CoreMinimal.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/JevIntent.h"
#include "Rules/PressureHud.h"
#include "Subsystems/WorldSubsystem.h"
#include "PressureView.generated.h"

class ACommandGameState;
class ACommandPlayerState;
class AEnemyCommander;

// Local feed rows (own-building stuns, JEV releases) are not replicated, so they carry a sequence above every
// replicated ring's, and nothing on the server can answer a click on them.
namespace PressureView
{
inline constexpr int32 LocalSequenceBase = MAX_int32 / 2;
inline bool IsLocalSequence(int32 Sequence) { return Sequence >= LocalSequenceBase; }
inline constexpr const TCHAR* StunRowId = TEXT("pressure_stun");
inline constexpr const TCHAR* ReleaseRowId = TEXT("pressure_release");
}

// What this client has seen change in the economy and in JEV's schedule: the release view, per-building stun
// history, and the feed rows they post. Everything is derived from replicated state; the rows and the stun start
// times exist only because the wire carries end times, not events.
UCLASS()
class COOPRTS_API UPressureView : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	static UPressureView* Get(const UObject* Context);

	// Once a frame from the HUD, before anything reads the view. Wallet is the local commander's.
	void Observe(const ACommandGameState& State, const ACommandPlayerState* Wallet);

	// JEV's schedule and the facts its release tags read; bKnown is false until it replicates.
	const JevIntent::FReleaseView& Release() const { return Schedule; }
	const PressureHud::FStunWatch& Stuns() const { return StunHistory; }
	// Newest last, at most MaxRows; hold times are the feed's.
	const TArray<FObjectiveEvent>& Rows() const { return FeedRows; }
	// The cut region a click on the chip should focus next; remembers it so the next click moves on.
	int32 AdvanceCutFocus(TConstArrayView<int32> Cut);

	static constexpr int32 MaxRows = 6;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Reads match time as if the battle clock had started Seconds earlier, so a world test can stand in the release window.
	void SetClockSkew(float Seconds) { ClockSkew = Seconds; }
#endif

private:
	void FindJev(const ACommandGameState& State);
	void WatchStuns(const ACommandGameState& State, const ACommandPlayerState* Wallet);
	void WatchReleases(const ACommandGameState& State);
	void Post(FObjectiveEvent&& Row);

	TWeakObjectPtr<const AEnemyCommander> Jev;
	JevIntent::FReleaseView Schedule;
	PressureHud::FStunWatch StunHistory;
	TArray<FObjectiveEvent> FeedRows;
	double NextJevSearch = 0.;
	int32 SeenRelease = INDEX_NONE;
	int32 LocalSequence = PressureView::LocalSequenceBase;
	int32 CutFocus = INDEX_NONE;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	float ClockSkew = 0.f;
#endif
};
