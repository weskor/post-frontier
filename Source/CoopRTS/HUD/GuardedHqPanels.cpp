#include "GuardedHqPanels.h"

#include "CommandGameState.h"
#include "FailoverNode.h"
#include "Headquarters.h"
#include "Rules/HqHoldPolicy.h"

namespace CommandHUDPanels
{
namespace
{
constexpr float PipHalf = 5.f;

void Line(const FPainter& Paint, const FVector2D& From, const FVector2D& To, const FLinearColor& Color, float Thickness = 1.f)
{
	FCanvasLineItem Item(From * Paint.Scale, To * Paint.Scale);
	Item.SetColor(Color);
	Item.LineThickness = Paint.Scale * Thickness;
	Paint.Canvas->DrawItem(Item);
}

void Diamond(const FPainter& Paint, const FVector2D& Center, float Half, const FLinearColor& Color, bool bFilled)
{
	if (bFilled)
	{
		for (float Y = -Half; Y <= Half; Y += 1.f)
		{
			const float Width = Half - FMath::Abs(Y);
			Paint.Fill({ Center.X - Width, Center.Y + Y, 2.f * Width + 1.f, 1.f }, Color);
		}
		return;
	}
	const FVector2D Points[] = { { 0.f, -Half }, { Half, 0.f }, { 0.f, Half }, { -Half, 0.f } };
	for (int32 Index = 0; Index < 4; ++Index)
		Line(Paint, Center + Points[Index], Center + Points[(Index + 1) % 4], Color);
}

// ▲ holding, ‖ paused, ▼ decaying: shapes, so no font has to carry them.
void StateGlyph(const FPainter& Paint, const FVector2D& Center, HqHoldPolicy::EHoldState State, const FLinearColor& Color)
{
	if (State == HqHoldPolicy::EHoldState::Paused)
	{
		Paint.Fill({ Center.X - 3.f, Center.Y - 4.f, 2.f, 8.f }, Color);
		Paint.Fill({ Center.X + 1.f, Center.Y - 4.f, 2.f, 8.f }, Color);
		return;
	}
	const bool bUp = State == HqHoldPolicy::EHoldState::Holding;
	for (int32 Row = 0; Row < 8; ++Row)
	{
		const float Width = bUp ? 1.f + Row : 8.f - Row;
		Paint.Fill({ Center.X - Width * .5f, Center.Y - 4.f + Row, Width, 1.f }, Color);
	}
}

struct FHoldText
{
	const TCHAR* Text;
	FLinearColor Color;
};

// The three state lines, per side: JEV holds the humans' main, the humans hold the Lattice's uplink.
FHoldText HoldText(int32 Team, HqHoldPolicy::EHoldState State)
{
	const bool bHumans = Team == 0;
	switch (State)
	{
	case HqHoldPolicy::EHoldState::Holding:
		return bHumans ? FHoldText{ TEXT("JEV HOLDING THE MAIN"), Palette::Bad } : FHoldText{ TEXT("UPLINK HELD"), Palette::Good };
	case HqHoldPolicy::EHoldState::Paused:
		return bHumans ? FHoldText{ TEXT("PAUSED: defenders in the main"), Palette::Warn }
					   : FHoldText{ TEXT("PAUSED: JEV in the main"), Palette::Warn };
	default:
		return bHumans ? FHoldText{ TEXT("DECAYING: main is clear"), Palette::Good }
					   : FHoldText{ TEXT("DECAYING: send a force in"), Palette::Bad };
	}
}
}

const TCHAR* HqName(int32 Team)
{
	return Team == 5 ? TEXT("The Lattice") : TEXT("Hardline");
}

void AppendClock(FStringBuilderBase& Out, int32 Seconds)
{
	Seconds = FMath::Max(0, Seconds);
	Out.Appendf(TEXT("%d:%02d"), Seconds / 60, Seconds % 60);
}

void DrawShieldGlyph(const FPainter& Paint, const FVector2D& Center, const FLinearColor& Color)
{
	const FVector2D Points[] = { { -4.f, -5.f }, { 4.f, -5.f }, { 3.5f, 2.f }, { 0.f, 6.f }, { -3.5f, 2.f } };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Points); ++Index)
		Line(Paint, Center + Points[Index], Center + Points[(Index + 1) % UE_ARRAY_COUNT(Points)], Color, 1.5f);
}

void DrawImmuneHatch(const FPainter& Paint, const FRect& Rect)
{
	const FLinearColor Color(0.f, 0.f, 0.f, .35f);
	for (float X = Rect.X; X + Rect.H <= Rect.Right(); X += 7.f)
		Line(Paint, { X, Rect.Bottom() }, { X + Rect.H, Rect.Y }, Color);
}

void DrawHqBar(const FPainter& Paint, const FRect& Rect, const AHeadquarters* HQ, const FLinearColor& Color)
{
	const int32 Team = IsValid(HQ) ? HQ->TeamIndex : 0;
	TStringBuilder<32> Value;
	if (IsValid(HQ) && HQ->IsOffline())
	{
		// The hold replaces the health bar: progress over the full hold, a plain fill with no tick marks.
		Paint.Bar(Rect, HqHoldPolicy::Fraction(HQ->GetHold()), Color.CopyWithNewOpacity(.62f));
		Paint.Outline(Rect, Color.CopyWithNewOpacity(.55f));
		TStringBuilder<48> Label;
		Label << (Team == 5 ? TEXT("THE LATTICE OFFLINE") : TEXT("HARDLINE OFFLINE"));
		Paint.TextIn(Label.ToView(), Rect, 9.f, Palette::Text, true, EAlign::Left, 7.f);
		AppendClock(Value, HqHoldPolicy::WholeSeconds(HQ->GetHold()));
		Value << TEXT(" / ");
		AppendClock(Value, FMath::RoundToInt(HqHoldPolicy::HoldSeconds));
		Paint.TextIn(Value.ToView(), Rect, 9.f, Palette::Text, true, EAlign::Right, 7.f);
		return;
	}
	const int32 Health = IsValid(HQ) && HQ->IsAlive() ? HQ->Health : 0;
	const int32 Max = IsValid(HQ) ? FMath::Max(1, HQ->MaxHealth()) : 1;
	Paint.Bar(Rect, static_cast<float>(Health) / Max, Color.CopyWithNewOpacity(.62f));
	const bool bImmune = IsValid(HQ) && HQ->IsImmune();
	if (bImmune)
		DrawImmuneHatch(Paint, Rect);
	Paint.Outline(Rect, Color.CopyWithNewOpacity(.55f));
	if (bImmune)
		DrawShieldGlyph(Paint, { Rect.X + 12.f, Rect.Y + Rect.H * .5f }, Palette::Text);
	Paint.TextIn(HqName(Team), Rect, 9.f, Palette::Text, true, EAlign::Left, bImmune ? 22.f : 7.f);
	Value.Appendf(TEXT("%d / %d"), Health, Max);
	Paint.TextIn(Value.ToView(), Rect, 9.f, Palette::Text, true, EAlign::Right, 7.f);
}

bool DrawHoldLine(const FPainter& Paint, const FRect& Row, const AHeadquarters* HQ)
{
	if (!IsValid(HQ) || !HQ->IsOffline())
		return false;
	const FHoldText Text = HoldText(HQ->TeamIndex, HQ->GetHoldState());
	StateGlyph(Paint, { Row.X + 4.f, Row.Y + 7.f }, HQ->GetHoldState(), Text.Color);
	Paint.Text(Text.Text, Row.X + 12.f, Row.Y + 1.f, 9.f, Text.Color, true, EAlign::Left, Row.W - 12.f);
	return true;
}

void DrawNodePips(const FPainter& Paint, const FRect& Row, const AHeadquarters* HQ)
{
	if (!IsValid(HQ) || HQ->GetNodes().IsEmpty())
		return;
	const FLinearColor Color = HQ->TeamIndex == 5 ? Palette::Enemy : Palette::Friendly;
	float X = Row.Right() - 4.f - PipHalf;
	for (int32 Index = HQ->GetNodes().Num() - 1; Index >= 0; --Index, X -= 2.f * PipHalf + 5.f)
	{
		const AFailoverNode* Node = HQ->GetNodes()[Index].Get();
		const bool bStanding = IsValid(Node) && Node->IsAlive();
		const FVector2D Center(X, Row.Y + 7.f);
		Diamond(Paint, Center, PipHalf, bStanding ? Color : Palette::Faint, bStanding);
		// A double outline while the opening's plating lasts.
		if (bStanding && Node->IsPlated())
			Diamond(Paint, Center, PipHalf + 2.5f, Color, false);
	}
	// X is now one pip step left of the first pip.
	Paint.Text(TEXT("Nodes"), X + PipHalf - Paint.TextWidth(TEXT("Nodes"), 9.f) - 2.f, Row.Y + 1.f, 9.f, Palette::Muted);
}

void AppendHqLabel(FStringBuilderBase& Out, const AHeadquarters& HQ)
{
	const TCHAR* Name = HQ.TeamIndex == 5 ? TEXT("LATTICE") : TEXT("HARDLINE");
	if (HQ.IsOffline())
	{
		Out.Appendf(TEXT("%s HQ OFFLINE "), Name);
		AppendClock(Out, HqHoldPolicy::WholeSeconds(HQ.GetHold()));
		Out << TEXT(" / ");
		AppendClock(Out, FMath::RoundToInt(HqHoldPolicy::HoldSeconds));
		return;
	}
	Out.Appendf(TEXT("%s HQ %d/%d"), Name, HQ.IsAlive() ? HQ.Health : 0, HQ.MaxHealth());
	if (const int32 Nodes = HQ.NodesStanding(); HQ.IsImmune())
		Out.Appendf(TEXT(" \u00B7 immune (%d %s)"), Nodes, Nodes == 1 ? TEXT("node") : TEXT("nodes"));
}

void AppendStandingSentence(FStringBuilderBase& Out, const ACommandGameState& State)
{
	const AHeadquarters* Own = State.FriendlyHeadquarters;
	const AHeadquarters* Hostile = State.EnemyHeadquarters;
	if (IsValid(Own) && Own->IsOffline())
	{
		Out << TEXT("Hardline is offline: clear JEV out of your main before the hold completes ");
		AppendClock(Out, FMath::RoundToInt(HqHoldPolicy::HoldSeconds));
		return;
	}
	if (IsValid(Hostile) && Hostile->IsOffline())
	{
		Out << TEXT("The Lattice is offline: keep a force in its main to hold the uplink ");
		AppendClock(Out, FMath::RoundToInt(HqHoldPolicy::HoldSeconds));
		return;
	}
	if (IsValid(Hostile) && Hostile->NodesStanding() == 0)
	{
		Out << TEXT("Destroy the Lattice HQ to take it offline.");
		return;
	}
	Out << TEXT("Objective: break the Lattice's Failover Nodes, take its HQ offline, then hold the uplink ");
	AppendClock(Out, FMath::RoundToInt(HqHoldPolicy::HoldSeconds));
}
}
