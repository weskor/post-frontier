// Probe snapshots of what the local HUD shows: alerts, objective events, pings and JEV intent.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArenaBounds.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "CommandPlayerController.h"
#include "Commands/PingCommandComponent.h"
#include "HUD/HUDPanels.h"
#include "Json.h"
#include "ObjectiveAnnouncer.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
void AlertSurfaceSnapshot(const UObject* Context, const TSharedPtr<FJsonObject>& Result)
{
	TArray<TSharedPtr<FJsonValue>> Rows;
	const ACommandPlayerController* PC = LocalController(Context->GetWorld());
	const ACommandHUD* HUD = PC ? Cast<ACommandHUD>(PC->GetHUD()) : nullptr;
	Number(Result, TEXT("focusedAlertSequence"), PC ? PC->GetFocusedAlertSequence() : 0);
	if (HUD)
	{
		auto Append = [&Rows, HUD](const FObjectiveEvent& Event) {
			FVector2D Position;
			if (!HUD->FindAlertScreenPosition(Event.Sequence, Position))
				return;
			auto Row = Object();
			Number(Row, TEXT("sequence"), Event.Sequence);
			Number(Row, TEXT("x"), Position.X);
			Number(Row, TEXT("y"), Position.Y);
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		};
		if (const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Context))
			for (const FObjectiveEvent& Event : Announcer->GetEvents())
				Append(Event);
		if (PC->PingCommands)
			for (const FObjectiveEvent& Event : PC->PingCommands->GetEvents())
				Append(Event);
	}
	Result->SetArrayField(TEXT("uiAlerts"), Rows);
}

TSharedPtr<FJsonValue> HudRectJson(const CommandHUDPanels::FRect& Value, float Scale)
{
	TArray<TSharedPtr<FJsonValue>> Edges;
	for (const float Edge : { Value.X, Value.Y, Value.W, Value.H })
		Edges.Add(MakeShared<FJsonValueNumber>(Edge * Scale));
	return MakeShared<FJsonValueArray>(Edges);
}

void JevPlansJson(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	TArray<TSharedPtr<FJsonValue>> Plans;
	for (const FJevPublishedPlan& Plan : State.EnemyPlans)
	{
		auto Entry = Object();
		Number(Entry, TEXT("ticket"), Plan.TicketNumber);
		Number(Entry, TEXT("forceNumber"), Plan.ForceNumber);
		Number(Entry, TEXT("verb"), static_cast<int32>(Plan.Verb));
		Number(Entry, TEXT("source"), Plan.SourceRegionIndex);
		Number(Entry, TEXT("target"), Plan.TargetRegionIndex);
		Number(Entry, TEXT("sizeBand"), Plan.SizeBand);
		Number(Entry, TEXT("eta"), Plan.EtaSeconds);
		Number(Entry, TEXT("etaIssuedAt"), Plan.EtaIssuedAt);
		Entry->SetBoolField(TEXT("escalated"), Plan.bEscalated);
		Entry->SetStringField(TEXT("memo"), Plan.Memo);
		Plans.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("jevPlans"), Plans);
}

void JevTimelineJson(const TSharedPtr<FJsonObject>& Intent, const CommandHUDPanels::FContext& Context,
	const CommandHUDPanels::FLayout& Layout, const CommandHUDPanels::FJevIntentModel& Model)
{
	const CommandHUDPanels::FRect Timeline = CommandHUDPanels::JevTimelineRect(Context, Layout, Model);
	if (Timeline.W > 0.f)
		Intent->SetField(TEXT("timelineRect"), HudRectJson(Timeline, Layout.Scale));
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const JevIntent::FTimelineEntry& Entry : Model.Timeline)
	{
		auto Row = Object();
		Number(Row, TEXT("ticket"), Entry.Ticket);
		Number(Row, TEXT("forceNumber"), Entry.ForceNumber);
		Number(Row, TEXT("target"), Entry.Target);
		Number(Row, TEXT("sizeBand"), Entry.SizeBand);
		Number(Row, TEXT("seconds"), Entry.Seconds);
		Row->SetBoolField(TEXT("escalated"), Entry.bEscalated);
		Entries.Add(MakeShared<FJsonValueObject>(Row));
	}
	Intent->SetArrayField(TEXT("entries"), Entries);
}

void JevBadgesJson(const TSharedPtr<FJsonObject>& Intent, const ACommandPlayerController& PC, const ACommandGameState& State,
	const CommandHUDPanels::FContext& Context, const CommandHUDPanels::FJevIntentModel& Model, int32 Width, int32 Height)
{
	TArray<TSharedPtr<FJsonValue>> Badges;
	for (const JevIntent::FRegionBadge& Badge : Model.Badges)
	{
		auto Row = Object();
		Number(Row, TEXT("region"), Badge.Region);
		Number(Row, TEXT("seconds"), Badge.Seconds);
		Number(Row, TEXT("plans"), Badge.Plans);
		Row->SetBoolField(TEXT("escalated"), Badge.bEscalated);
		TStringBuilder<128> Label;
		CommandHUDPanels::JevBadgeLabel(Context, Badge, Label);
		Row->SetStringField(TEXT("label"), Label.ToString());
		FVector2D Screen;
		const bool bProjected = PC.ProjectWorldLocationToScreen(State.GetRegionAnchor(Badge.Region) + FVector(0.f, 0.f, 110.f), Screen);
		Row->SetBoolField(TEXT("onScreen"), bProjected && Screen.X >= 0.f && Screen.Y >= 0.f && Screen.X < Width && Screen.Y < Height);
		Badges.Add(MakeShared<FJsonValueObject>(Row));
	}
	Intent->SetArrayField(TEXT("badges"), Badges);
}

void JevMemosJson(const TSharedPtr<FJsonObject>& Intent, const CommandHUDPanels::FContext& Context,
	const CommandHUDPanels::FLayout& Layout, const CommandHUDPanels::FJevIntentModel& Model)
{
	TArray<TSharedPtr<FJsonValue>> Memos;
	CommandHUDPanels::FJevMemoRow Rows[JevIntent::MemoVisible];
	const int32 Count = CommandHUDPanels::JevMemoRows(Context, Layout, Model, Rows);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		auto Row = Object();
		Row->SetStringField(TEXT("text"), Rows[Index].Memo->Text);
		Number(Row, TEXT("alpha"), Rows[Index].Alpha);
		Row->SetField(TEXT("rect"), HudRectJson(Rows[Index].Rect, Layout.Scale));
		Memos.Add(MakeShared<FJsonValueObject>(Row));
	}
	Intent->SetArrayField(TEXT("memos"), Memos);
}

void PingForcesJson(const FObjectiveEvent& Event, const TSharedPtr<FJsonObject>& Entry)
{
	TArray<TSharedPtr<FJsonValue>> Forces;
	for (const FObjectiveForce& Force : Event.Forces)
	{
		auto Contributor = Object();
		Number(Contributor, TEXT("team"), Force.TeamIndex);
		Number(Contributor, TEXT("owner"), Force.CommanderIndex);
		Number(Contributor, TEXT("forceNumber"), Force.ForceNumber);
		Contributor->SetStringField(TEXT("playerName"), Force.PlayerName);
		Forces.Add(MakeShared<FJsonValueObject>(Contributor));
	}
	Entry->SetArrayField(TEXT("forces"), Forces);
}

void PingProjectionJson(const FObjectiveEvent& Event, const ACommandPlayerController& PC, const ACommandGameState* State,
	const TSharedPtr<FJsonObject>& Entry)
{
	FVector2D Screen = FVector2D::ZeroVector;
	Entry->SetBoolField(TEXT("mapProjected"),
		PC.ProjectWorldLocationToScreen(Event.Location + FVector(0.f, 0.f, 35.f), Screen));
	Number(Entry, TEXT("mapX"), Screen.X);
	Number(Entry, TEXT("mapY"), Screen.Y);
	const ACommandHUD* HUD = Cast<ACommandHUD>(PC.GetHUD());
	FVector2D Origin;
	float Size;
	if (HUD && State && IsValid(State->Arena) && HUD->GetMinimapScreenRect(Origin, Size))
	{
		const FVector2D Half = State->Arena->HalfExtent;
		Number(Entry, TEXT("minimapX"), Origin.X + Size * (Event.Location.Y + Half.Y) / (2.f * Half.Y));
		Number(Entry, TEXT("minimapY"), Origin.Y + Size * (Half.X - Event.Location.X) / (2.f * Half.X));
	}
}
}

void ObjectiveSnapshot(const UObject* Context, const TSharedPtr<FJsonObject>& Result)
{
	TArray<TSharedPtr<FJsonValue>> Events;
	if (const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Context))
		for (const FObjectiveEvent& Event : Announcer->GetEvents())
		{
			auto Entry = Object();
			Number(Entry, TEXT("sequence"), Event.Sequence);
			Entry->SetStringField(TEXT("id"), Event.Id.ToString());
			Number(Entry, TEXT("serverTime"), Event.ServerTime);
			Vector(Entry, TEXT("position"), Event.Location);
			Number(Entry, TEXT("region"), Event.RegionIndex);
			Entry->SetStringField(TEXT("regionName"), Event.RegionName);
			Number(Entry, TEXT("affectedTeam"), Event.AffectedTeam);
			Number(Entry, TEXT("damageTier"), Event.DamageTier);
			TArray<TSharedPtr<FJsonValue>> Forces;
			for (const FObjectiveForce& Force : Event.Forces)
			{
				auto Contributor = Object();
				Number(Contributor, TEXT("team"), Force.TeamIndex);
				Number(Contributor, TEXT("owner"), Force.CommanderIndex);
				Number(Contributor, TEXT("forceNumber"), Force.ForceNumber);
				Number(Contributor, TEXT("unitIndex"), Force.UnitIndex);
				Contributor->SetStringField(TEXT("playerName"), Force.PlayerName);
				Forces.Add(MakeShared<FJsonValueObject>(Contributor));
			}
			Entry->SetArrayField(TEXT("forces"), Forces);
			Events.Add(MakeShared<FJsonValueObject>(Entry));
		}
	Result->SetArrayField(TEXT("objectiveEvents"), Events);
	AlertSurfaceSnapshot(Context, Result);
}

void PingSnapshot(UWorld* World, const TSharedPtr<FJsonObject>& Result)
{
	const ACommandPlayerController* PC = LocalController(World);
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	const float Now = State ? State->GetServerWorldTimeSeconds() : 0.f;
	Number(Result, TEXT("serverTime"), Now);
	if (PC && PC->PingCommands)
	{
		Number(Result, TEXT("pingFeedbackSerial"), PC->PingCommands->PingFeedbackSerial);
		Result->SetBoolField(TEXT("pingAccepted"), PC->PingCommands->bLastPingAccepted);
	}
	TArray<TSharedPtr<FJsonValue>> Events;
	if (PC && PC->PingCommands)
		for (const FObjectiveEvent& Event : PC->PingCommands->GetEvents())
		{
			auto Entry = Object();
			Number(Entry, TEXT("sequence"), Event.Sequence);
			Entry->SetStringField(TEXT("id"), Event.Id.ToString());
			Number(Entry, TEXT("serverTime"), Event.ServerTime);
			Vector(Entry, TEXT("position"), Event.Location);
			Number(Entry, TEXT("affectedTeam"), Event.AffectedTeam);
			Entry->SetStringField(TEXT("targetForceOwnerName"), Event.TargetForceOwnerName);
			Entry->SetBoolField(TEXT("active"), Now - Event.ServerTime < UPingCommandComponent::Lifetime);
			PingForcesJson(Event, Entry);
			PingProjectionJson(Event, *PC, State, Entry);
			Events.Add(MakeShared<FJsonValueObject>(Entry));
		}
	Result->SetArrayField(TEXT("pingEvents"), Events);
}

// Published plans next to what the HUD derives from them, in screen pixels.
void JevIntentSnapshot(UWorld* World, const TSharedPtr<FJsonObject>& Result)
{
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (!State)
		return;
	JevPlansJson(*State, Result);
	ACommandPlayerController* PC = LocalController(World);
	int32 Width = 0, Height = 0;
	if (PC)
		PC->GetViewportSize(Width, Height);
	const CommandHUDPanels::FContext Context = CommandHUDPanels::MakeContext(PC);
	const CommandHUDPanels::FLayout Layout = CommandHUDPanels::MakeLayout(Context, Width, Height);
	if (!PC || Layout.Scale <= 0.f)
		return;
	CommandHUDPanels::FJevIntentModel Model;
	CommandHUDPanels::BuildJevIntentModel(Context, Model);
	auto Intent = Object();
	JevTimelineJson(Intent, Context, Layout, Model);
	JevBadgesJson(Intent, *PC, *State, Context, Model, Width, Height);
	JevMemosJson(Intent, Context, Layout, Model);
	Result->SetObjectField(TEXT("jevIntent"), Intent);
}
}
#endif
