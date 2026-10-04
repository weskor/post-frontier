#include "PressureView.h"

#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "EngineUtils.h"
#include "EnemyCommander.h"
#include "Engine/World.h"
#include "Headquarters.h"
#include "MapRegion.h"

namespace
{
// A counter-armor tag is refreshed this often; the count walks every unit.
constexpr double ArmorCountSeconds = .5;
constexpr double JevSearchSeconds = 1.;
// Rows older than this are past their fade in the feed and can go.
constexpr float RowLifetime = UObjectiveAnnouncer::FeedLifetime + 1.f;

bool IsCounterRelease(int32 Next)
{
	return JevRelease::VersionOf(Next) == JevRelease::EVersion::V12;
}
}

bool UPressureView::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

UPressureView* UPressureView::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UPressureView>() : nullptr;
}

void UPressureView::Observe(const ACommandGameState& State, const ACommandPlayerState* Wallet)
{
	FindJev(State);
	RefreshCounterArmor(State);
	WatchStuns(State, Wallet);
	WatchReleases(State);
	const float Now = State.GetServerWorldTimeSeconds();
	FeedRows.RemoveAll([Now](const FObjectiveEvent& Row) { return Row.ServerTime > Now || Now - Row.ServerTime > RowLifetime; });
}

void UPressureView::FindJev(const ACommandGameState& State)
{
	const double Now = State.GetServerWorldTimeSeconds();
	if (!Jev.IsValid() && Now >= NextJevSearch && State.GetWorld())
	{
		NextJevSearch = Now + JevSearchSeconds;
		for (TActorIterator<AEnemyCommander> It(State.GetWorld()); It; ++It)
			if (It->TeamIndex == 5)
			{
				Jev = *It;
				break;
			}
	}
	const AEnemyCommander* Commander = Jev.Get();
	const EArmorClass Armor = Schedule.CounterArmor;
	Schedule = JevIntent::FReleaseView();
	Schedule.CounterArmor = Armor;
	// The schedule replicates with the actor, and its defaults are the match's opening state (clock start 0, v1.1 next).
	if (!Commander)
		return;
	Schedule.bKnown = true;
	Schedule.Current = Commander->Release.Current;
	Schedule.Next = Commander->Release.Next;
	Schedule.NextAt = Commander->Release.NextAt;
	Schedule.ClockStartServerTime = Commander->Release.ClockStartServerTime;
}

// The tag of a v1.2 release names the armor its wave counters, the humans' most numerous class when it launches.
void UPressureView::RefreshCounterArmor(const ACommandGameState& State)
{
	const double Now = State.GetServerWorldTimeSeconds();
	if (!Schedule.bKnown || !IsCounterRelease(Schedule.Next))
	{
		Schedule.CounterArmor = EArmorClass::Unset;
		return;
	}
	if (Now < NextArmorCount || !State.GetWorld())
		return;
	NextArmorCount = Now + ArmorCountSeconds;
	JevRelease::FArmorCounts Counts;
	for (TActorIterator<AArmyUnit> It(State.GetWorld()); It; ++It)
		if (It->IsAlive() && It->GetTeamIndex() == 0)
			if (const int32 Armor = static_cast<int32>(It->GetArmorClass()); Armor >= 0 && Armor < UE_ARRAY_COUNT(Counts.Count))
				++Counts.Count[Armor];
	Schedule.CounterArmor = JevRelease::MostNumerous(Counts);
}

void UPressureView::WatchStuns(const ACommandGameState& State, const ACommandPlayerState* Wallet)
{
	const double Now = State.GetServerWorldTimeSeconds();
	for (const ACommandBuilding* Building : State.Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive())
			continue;
		const PressureHud::FStunObservation Seen = StunHistory.Observe(Building->GetUniqueID(), Now, Building->StunEndServerTime);
		if (!Seen.bFeed || !Wallet || Building->TeamIndex != 0 || Building->OwningPlayerState != Wallet)
			continue;
		const UBuildingDefinition* Definition = Building->GetDefinition();
		const AMapRegion* Region = State.FindRegionAt(Building->GetActorLocation());
		TStringBuilder<96> Title;
		PressureHud::AppendStunFeed(Title, Definition ? FStringView(Definition->DisplayName.ToString()).Left(48) : FStringView(TEXT("Building")),
			Building->ForceNumber);
		FObjectiveEvent Row;
		Row.Id = FName(PressureView::StunRowId);
		Row.ServerTime = Now;
		Row.Location = Building->GetActorLocation();
		Row.RegionIndex = Region ? Region->RegionIndex : INDEX_NONE;
		Row.RegionName = Region ? Region->DisplayName.ToString() : FString();
		Row.AffectedTeam = 0;
		Row.TargetForceOwnerName = FString(Title.ToView());
		Post(MoveTemp(Row));
	}
}

void UPressureView::WatchReleases(const ACommandGameState& State)
{
	if (!Schedule.bKnown)
		return;
	const int32 Previous = SeenRelease;
	SeenRelease = Schedule.Current;
	// A release already in force when this client first looked is history, not news.
	if (Previous == INDEX_NONE || Schedule.Current <= Previous)
		return;
	TStringBuilder<96> Title;
	Title << TEXT("JEV ");
	JevIntent::AppendReleaseName(Title, Schedule.Current);
	Title << TEXT(" released");
	TStringBuilder<48> Tag;
	JevIntent::AppendReleaseTag(Tag, Schedule.Current, Schedule.CounterArmor);
	if (Tag.Len() > 0)
		Title << TEXT(": ") << Tag.ToView();
	FObjectiveEvent Row;
	Row.Id = FName(PressureView::ReleaseRowId);
	Row.ServerTime = State.GetServerWorldTimeSeconds();
	Row.AffectedTeam = 5;
	if (const AHeadquarters* Main = State.EnemyHeadquarters.Get(); IsValid(Main))
		Row.Location = Main->GetActorLocation();
	Row.TargetForceOwnerName = FString(Title.ToView());
	Post(MoveTemp(Row));
}

void UPressureView::Post(FObjectiveEvent&& Row)
{
	Row.Sequence = ++LocalSequence;
	if (FeedRows.Num() >= MaxRows)
		FeedRows.RemoveAt(0);
	FeedRows.Add(MoveTemp(Row));
}

int32 UPressureView::AdvanceCutFocus(TConstArrayView<int32> Cut)
{
	CutFocus = PressureHud::NextCutFocus(Cut, CutFocus);
	return CutFocus;
}
