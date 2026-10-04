#include "EnemyCommanderTurn.h"

#include "CommandGameState.h"
#include "Engine/World.h"

// The schedule's clock and its published state. The wave itself is EnemyCommanderWave.cpp.

namespace
{
// Match second 0 as a server world time: the end of planning. Every read of match time goes through here.
float ClockStart(const ACommandGameState& State, float Skew)
{
	return State.GetBattleClockStartServerTime() - Skew;
}
}

float AEnemyCommander::GetMatchSeconds() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!State)
		return 0.f;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	const float Skew = ClockSkew;
#else
	const float Skew = 0.f;
#endif
	return State->GetServerWorldTimeSeconds() - ClockStart(*State, Skew);
}

bool AEnemyCommander::TickRelease()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (TeamIndex != 5 || !State || State->MatchResult != EMatchResult::Ongoing)
		return false;
	const float Seconds = GetMatchSeconds();
	const int32 Current = FMath::Max(0, JevRelease::IndexAt(Seconds));
	FJevReleaseState Published;
	Published.Current = Current;
	Published.Next = Current + 1;
	Published.NextAt = JevRelease::ReleaseTime(Published.Next);
	Published.bNextShown = JevRelease::TimelineVisible(Published.Next, Seconds);
	Published.ClockStartServerTime = State->GetServerWorldTimeSeconds() - Seconds;
	if (Release.Current != Published.Current || Release.bNextShown != Published.bNextShown
		|| !FMath::IsNearlyEqual(Release.ClockStartServerTime, Published.ClockStartServerTime, .001f))
	{
		Release.Current = Published.Current;
		Release.Next = Published.Next;
		Release.NextAt = Published.NextAt;
		Release.bNextShown = Published.bNextShown;
		Release.ClockStartServerTime = Published.ClockStartServerTime;
		ForceNetUpdate();
	}
	return Current > LaunchedUpTo || JevThreat::NextStep(ThreatStage, Seconds) != JevThreat::EStep::None;
}

void AEnemyCommander::AdvanceReleases(FJevTurn& Turn)
{
	// Published before any wave launches, so clients show the class the wave is about to buy against.
	if (const EArmorClass Counter = JevRelease::MostNumerous(Turn.EnemyArmor); Release.CounterArmor != Counter)
	{
		Release.CounterArmor = Counter;
		ForceNetUpdate();
	}
	const int32 Current = JevRelease::IndexAt(GetMatchSeconds());
	while (LaunchedUpTo < Current)
		LaunchWave(Turn, ++LaunchedUpTo);
	AdvanceThreat(Turn);
}

void AEnemyCommander::RecordWave(const FJevWaveEvent& Event)
{
	if (Release.Waves.Num() >= MaxPublishedWaves)
		Release.Waves.RemoveAt(0);
	Release.Waves.Add(Event);
	++Release.WaveCount;
	ForceNetUpdate();
}
