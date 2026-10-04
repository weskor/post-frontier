#include "HUDPanels.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Commands/PingCommandComponent.h"
#include "Commands/AbilityCommandComponent.h"
#include "Content/MatchContent.h"
#include "MapPresentation.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/AnnouncerPolicy.h"
#include "Rules/MapPresentationPolicy.h"
#include "PressureView.h"
#include "TeamPanelFeed.h"

namespace CommandHUDPanels
{
FStringView ObjectiveRegionName(FStringView Name)
{
	// Legacy map display names remain in data; objective presentation uses World.md.
	static const struct
	{
		const TCHAR* Legacy;
		const TCHAR* Current;
	} Names[] = {
		{ TEXT("The Bunker"), TEXT("Hardline") }, { TEXT("The Cluster"), TEXT("The Lattice") },
		{ TEXT("Uplink"), TEXT("Skyhook") }, { TEXT("Relay Plant"), TEXT("Fusion Works") },
		{ TEXT("Power Yard"), TEXT("Reactor Yard") }, { TEXT("Cooling"), TEXT("Heat Sink") },
		{ TEXT("Substation 7"), TEXT("Fusion Tap 7") }, { TEXT("Fibre Junction"), TEXT("Lightline Junction") },
		{ TEXT("Cooling Plant"), TEXT("Cryo Plant") }
	};
	for (const auto& Entry : Names)
		if (Name == Entry.Legacy)
			return Entry.Current;
	return Name;
}

void DrawAssaultRoleGlyph(const FPainter& Paint, const FRect& Glyph, const FLinearColor& Color)
{
	Paint.Outline({ Glyph.X, Glyph.Y + 2.f, 4.f, 7.f }, Color);
	Paint.Fill({ Glyph.X + 7.f, Glyph.Y, 1.f, 10.f }, Color);
	Paint.Fill({ Glyph.X + 6.f, Glyph.Y + 1.f, 3.f, 2.f }, Color);
	Paint.Fill({ Glyph.X + 5.f, Glyph.Y + 7.f, 5.f, 1.f }, Color);
}

void DrawSupportRoleGlyph(const FPainter& Paint, const FRect& Glyph, const FLinearColor& Color)
{
	Paint.Fill({ Glyph.X + 4.f, Glyph.Y + 4.f, 2.f, 2.f }, Color);
	Paint.Fill({ Glyph.X + 2.f, Glyph.Y + 2.f, 1.f, 6.f }, Color);
	Paint.Fill({ Glyph.X + 7.f, Glyph.Y + 2.f, 1.f, 6.f }, Color);
	Paint.Fill({ Glyph.X, Glyph.Y, 1.f, 10.f }, Color);
	Paint.Fill({ Glyph.X + 9.f, Glyph.Y, 1.f, 10.f }, Color);
	Paint.Fill({ Glyph.X, Glyph.Y, 2.f, 1.f }, Color);
	Paint.Fill({ Glyph.X, Glyph.Y + 9.f, 2.f, 1.f }, Color);
	Paint.Fill({ Glyph.X + 8.f, Glyph.Y, 2.f, 1.f }, Color);
	Paint.Fill({ Glyph.X + 8.f, Glyph.Y + 9.f, 2.f, 1.f }, Color);
}

void DrawObjectiveForceBadge(const FPainter& Paint, const FContext& Context, const FObjectiveForce& Force,
	const FRect& Rect, float Alpha)
{
	const FLinearColor Color = (Force.TeamIndex == 5 ? Palette::Enemy
													 : AArmyUnit::GetCommanderColor(Force.CommanderIndex))
								   .CopyWithNewOpacity(Alpha);
	Paint.Fill(Rect, Palette::Card.CopyWithNewOpacity(.96f * Alpha));
	Paint.Outline(Rect, Color);
	const UMatchContent* Content = MatchContent(Context);
	const UArmyUnitDefinition* Unit = Content ? Content->Unit(Force.UnitIndex) : nullptr;
	const FRect Glyph{ Rect.X + 5.f, Rect.Center().Y - 5.f, 10.f, 10.f };
	if (!Unit)
		Paint.Outline(Glyph, Color);
	else if (Unit->Role == EUnitRole::Frontline)
	{
		Paint.Fill(Glyph, Color);
		Paint.Fill({ Glyph.X + 3.f, Glyph.Y + 2.f, 4.f, 6.f }, Palette::Card.CopyWithNewOpacity(Alpha));
	}
	else if (Unit->Role == EUnitRole::Assault)
		DrawAssaultRoleGlyph(Paint, Glyph, Color);
	else if (Unit->Role == EUnitRole::Support)
		DrawSupportRoleGlyph(Paint, Glyph, Color);
	else if (Unit->Role == EUnitRole::Ranged)
	{
		Paint.Outline(Glyph, Color);
		Paint.Fill({ Glyph.X + 4.f, Glyph.Y - 2.f, 2.f, 14.f }, Color);
		Paint.Fill({ Glyph.X - 2.f, Glyph.Y + 4.f, 14.f, 2.f }, Color);
	}
	else
	{
		Paint.Fill({ Glyph.X, Glyph.Y, 10.f, 3.f }, Color);
		Paint.Fill({ Glyph.X, Glyph.Y + 4.f, 8.f, 3.f }, Color);
		Paint.Fill({ Glyph.X, Glyph.Y + 8.f, 6.f, 3.f }, Color);
	}
	TStringBuilder<32> Label;
	if (Force.TeamIndex == 5)
		Label.Appendf(TEXT("JEV F%d"), Force.ForceNumber);
	else
		Label.Appendf(TEXT("C%d F%d"), Force.CommanderIndex + 1, Force.ForceNumber);
	Paint.TextIn(Label.ToView(), { Rect.X + 21.f, Rect.Y, Rect.W - 25.f, Rect.H }, 9.f, Color, true);
}

void ForEachAlert(const FContext& Context, const FLayout& Layout,
	TFunctionRef<void(const FObjectiveEvent&, const FRect&, float)> Visit)
{
	if (!Context.State || !Context.Controller)
		return;
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Context.State);
	const UPingCommandComponent* Pings = Context.Controller->PingCommands;
	static const TArray<FObjectiveEvent> EmptyEvents;
	const FObjectiveEventView Objectives = Announcer ? Announcer->GetEvents() : FObjectiveEventView{ EmptyEvents, 0 };
	const FObjectiveEventView TeamPings = Pings ? Pings->GetEvents() : FObjectiveEventView{ EmptyEvents, 0 };
	const UAbilityCommandComponent* Abilities = Context.Controller->AbilityCommands;
	const FObjectiveEventView TeamAbilities = Abilities ? Abilities->GetEvents() : FObjectiveEventView{ EmptyEvents, 0 };
	const UPressureView* Pressure = UPressureView::Get(Context.State);
	const FObjectiveEventView LocalRows = Pressure ? FObjectiveEventView{ Pressure->Rows(), 0 } : FObjectiveEventView{ EmptyEvents, 0 };
	const UGiftFeed* Gifts = UGiftFeed::Get(Context.State);
	const FObjectiveEventView GiftRows = Gifts ? FObjectiveEventView{ Gifts->Rows(), 0 } : FObjectiveEventView{ EmptyEvents, 0 };
	const float Now = Context.State->GetServerWorldTimeSeconds();
	float Y = Layout.Alerts.Y;
	const auto VisitRing = [&](const FObjectiveEventView& Events, float Lifetime, bool bCompact) {
		for (int32 Index = Events.Num() - 1; Index >= 0; --Index)
		{
			const FObjectiveEvent& Event = Events[Index];
			const float Age = FMath::Max(0.f, Now - Event.ServerTime);
			if (Age >= Lifetime)
				continue;
			const int32 ForceRows = bCompact ? 0 : FMath::DivideAndRoundUp(Event.Forces.Num(), 2);
			const float Height = 2.f * Pad + AlertLineHeight * (2 + ForceRows);
			const FRect Rect{ Layout.Alerts.X, Y, Layout.Alerts.W, Height };
			if (Rect.Bottom() > Layout.Alerts.Bottom())
				return false;
			const float Alpha = FMath::Clamp((Lifetime - Age) / UObjectiveAnnouncer::FadeSeconds, 0.f, 1.f);
			Visit(Event, Rect, Alpha);
			Y = Rect.Bottom() + RowGap;
		}
		return true;
	};
	// Objective rows rank above this client's own pressure rows (stuns, releases), which rank above the team rows (ability
	// rows, then gifts), which rank above pings.
	if (VisitRing(Objectives, UObjectiveAnnouncer::FeedLifetime, false)
		&& VisitRing(LocalRows, UObjectiveAnnouncer::FeedLifetime, true)
		&& VisitRing(TeamAbilities, UAbilityCommandComponent::Lifetime, true)
		&& VisitRing(GiftRows, UObjectiveAnnouncer::FeedLifetime, true))
		VisitRing(TeamPings, UPingCommandComponent::Lifetime, true);
}

bool HitTestAlert(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint,
	FVector& OutWorld, int32& OutSequence)
{
	bool bHit = false;
	ForEachAlert(Context, Layout, [&](const FObjectiveEvent& Event, const FRect& Rect, float) {
		if (Rect.Contains(VirtualPoint))
		{
			OutWorld = Event.Location;
			OutSequence = Event.Sequence;
			bHit = true;
		}
	});
	return bHit;
}

// A team row's title and stripe colour: Fortify rows are cyan, a supply cut is red. The Drill Rig count is the live
// number of offline rigs in the region, since the row carries no snapshot of it.
static FLinearColor TeamRowTitle(const FContext& Context, const FObjectiveEvent& Event, FStringBuilderBase& Title)
{
	const FStringView Region = ObjectiveRegionName(Event.RegionName);
	if (Event.Id == FName(MapPresentation::SupplyCutEventId))
	{
		MapPresentation::AppendCutFeedText(Title, Region, MapView::OfflineRigs(*Context.State, Event.AffectedTeam, Event.RegionIndex));
		return Palette::Bad;
	}
	if (Event.Id == FName(FortifyPolicy::CastEventId) && !Event.Forces.IsEmpty())
		FortifyPolicy::AppendCastFeedText(Title, Event.Forces[0].CommanderIndex, Region);
	else if (Event.Id == FName(FortifyPolicy::EndedEventId))
		FortifyPolicy::AppendEndedFeedText(Title, Region);
	return FLinearColor(.42f, .90f, 1.f);
}

// A pressure row's title is the text its observer composed; a stun is amber, a release Machine cyan.
static FLinearColor LocalRowTitle(const FObjectiveEvent& Event, FStringBuilderBase& Title)
{
	Title << Event.TargetForceOwnerName;
	return Event.Id == FName(PressureView::StunRowId) ? Palette::Warn : FLinearColor(.42f, .90f, 1.f);
}

// The second line of a feed row: what kind of row it is and what a click does.
static void AlertSubtitle(const FObjectiveEvent& Event, bool bPing, bool bGift, bool bLocal, bool bAbility, FStringBuilderBase& Region)
{
	if (bPing)
	{
		Region << TEXT("Team ping");
		if (!Event.Forces.IsEmpty() && Event.Forces[0].ForceNumber > 0)
			Region.Appendf(TEXT("  |  %s's Force %d"), *Event.TargetForceOwnerName, Event.Forces[0].ForceNumber);
		Region << TEXT("  |  Click to focus");
	}
	else if (bGift)
		Region << TEXT("Team  |  Click to open Team log");
	else if (bLocal)
	{
		if (!Event.RegionName.IsEmpty())
			Region << ObjectiveRegionName(Event.RegionName) << TEXT("  |  ");
		Region << (Event.Id == FName(PressureView::StunRowId) ? TEXT("Your building") : TEXT("JEV release"));
	}
	else if (bAbility)
		Region << (Event.Id == FName(MapPresentation::SupplyCutEventId) ? TEXT("Team alert") : TEXT("Team ability")) << TEXT("  |  Click to focus");
	else
		Region << (Event.RegionName.IsEmpty() ? FStringView(TEXT("Outside regions")) : ObjectiveRegionName(Event.RegionName)) << TEXT("  |  Click to focus");
}

void DrawObjectiveAlerts(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	ForEachAlert(Context, Layout, [&](const FObjectiveEvent& Event, const FRect& Rect, float Alpha) {
		Paint.Fill(Rect, Palette::Panel.CopyWithNewOpacity(Palette::Panel.A * Alpha));
		Paint.Outline(Rect, Palette::Edge.CopyWithNewOpacity(Palette::Edge.A * Alpha));
		const AnnouncerPolicy::FDefinition* Definition = AnnouncerPolicy::Find(Event.Id);
		const bool bAbility = UAbilityCommandComponent::IsAbilitySequence(Event.Sequence);
		const bool bLocal = PressureView::IsLocalSequence(Event.Sequence);
		const bool bGift = GiftFeed::IsGiftSequence(Event.Sequence);
		const bool bPing = Event.Sequence < 0 && !bAbility;
		TStringBuilder<256> Title;
		if (bGift)
		{
			Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Palette::Gold.CopyWithNewOpacity(Alpha));
			Title << Event.TargetForceOwnerName;
		}
		else if (bLocal)
			Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, LocalRowTitle(Event, Title).CopyWithNewOpacity(Alpha));
		else if (bAbility)
			Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, TeamRowTitle(Context, Event, Title).CopyWithNewOpacity(Alpha));
		else
		{
			if (bPing && !Event.Forces.IsEmpty())
				Title << Event.Forces[0].PlayerName << TEXT(": ");
			Title << (Definition ? Definition->Text : TEXT("Objective update"));
		}
		Paint.Text(Title.ToView(), Rect.X + Pad, Rect.Y + Pad,
			10.f, Palette::Text.CopyWithNewOpacity(Alpha), true, EAlign::Left, Rect.W - 2.f * Pad);
		TStringBuilder<128> Region;
		AlertSubtitle(Event, bPing, bGift, bLocal, bAbility, Region);
		Paint.Text(Region.ToView(), Rect.X + Pad, Rect.Y + Pad + AlertLineHeight,
			9.f, Palette::Muted.CopyWithNewOpacity(Alpha), false, EAlign::Left, Rect.W - 2.f * Pad);
		for (int32 Index = 0; !bPing && !bAbility && !bLocal && !bGift && Index < Event.Forces.Num(); ++Index)
		{
			const FObjectiveForce& Force = Event.Forces[Index];
			const float CellWidth = (Rect.W - 2.f * Pad - Gap) * .5f;
			const float X = Rect.X + Pad + (Index % 2) * (CellWidth + Gap);
			const float Y = Rect.Y + Pad + AlertLineHeight * (Index / 2 + 2);
			DrawObjectiveForceBadge(Paint, Context, Force, { X, Y, 90.f, AlertLineHeight - 1.f }, Alpha);
			Paint.TextIn(Force.PlayerName, { X + 94.f, Y, CellWidth - 94.f, AlertLineHeight },
				9.f, Palette::Text.CopyWithNewOpacity(Alpha));
		}
	});
}
}
