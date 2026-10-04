#pragma once

#include "CommandHUD.h"
#include "CommandBuilding.h"
#include "Commands/ForceCapState.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Fonts/FontMeasure.h"
#include "Misc/StringBuilder.h"

class ACommandPlayerController;
class ACommandGameState;
class UMatchContent;
class AArmyGroup;

namespace CommandHUDPanels
{
// Compact virtual geometry; scale stops growing on large monitors.
constexpr float ReferenceWidth = 1280.f;
constexpr float ReferenceHeight = 720.f;
constexpr float MinScale = .78f;
constexpr float MaxScale = 1.f;
constexpr float Margin = 10.f;
constexpr float Gap = 8.f;
constexpr float Pad = 10.f;
constexpr float TopHeight = 32.f;
constexpr float ObjectiveHeight = 58.f;
constexpr int32 MaxObjectiveForceRows = 3;
constexpr float AlertWidth = 390.f;
constexpr float AlertLineHeight = 17.f;
constexpr float DeckHeight = 186.f;
constexpr float InspectorWidth = 720.f;
constexpr float MinimapSize = 144.f;
constexpr float ModeHeight = 62.f;
constexpr float FeedbackHeight = 26.f;
constexpr float ScreenWidth = 620.f;
constexpr float ScreenHeight = 560.f;
constexpr float HeaderHeight = 44.f;
constexpr float LabelHeight = 18.f;
constexpr float RowHeight = 26.f;
constexpr float RowGap = 4.f;
constexpr float ColumnGap = 14.f;
constexpr float KeyHeight = 17.f;

namespace Palette
{
inline const FLinearColor Panel(.012f, .018f, .027f, .88f);
inline const FLinearColor Edge(.32f, .46f, .60f, .34f);
inline const FLinearColor Text(.90f, .94f, 1.f);
inline const FLinearColor Muted(.60f, .67f, .76f);
inline const FLinearColor Faint(.40f, .46f, .54f);
inline const FLinearColor Gold(1.f, .80f, .36f);
inline const FLinearColor Good(.46f, .94f, .56f);
inline const FLinearColor Warn(1.f, .70f, .28f);
inline const FLinearColor Bad(1.f, .38f, .32f);
inline const FLinearColor Friendly(.30f, .72f, 1.f);
inline const FLinearColor Enemy(1.f, .32f, .26f);
inline const FLinearColor Card(.05f, .075f, .10f, .96f);
inline const FLinearColor CardHover(.075f, .11f, .15f, .98f);
inline const FLinearColor CardOff(.03f, .036f, .045f, .92f);
inline const FLinearColor Track(.10f, .12f, .15f, 1.f);
inline const FLinearColor Key(.10f, .13f, .17f, 1.f);
inline const FLinearColor KeyEdge(.36f, .46f, .56f, .85f);
}

struct FRect
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;
	float Right() const { return X + W; }
	float Bottom() const { return Y + H; }
	FVector2D Center() const { return FVector2D(X + W * .5f, Y + H * .5f); }
	bool Contains(const FVector2D& Point) const
	{
		return Point.X >= X && Point.X < X + W && Point.Y >= Y && Point.Y < Y + H;
	}
	bool Intersects(const FRect& Other) const
	{
		return X < Other.Right() && Right() > Other.X && Y < Other.Bottom() && Bottom() > Other.Y;
	}
};

struct FJevIntentModel;

// Local presentation inputs shared by drawing and hit testing.
struct FContext
{
	const ACommandPlayerController* Controller = nullptr;
	const ACommandGameState* State = nullptr;
	const ACommandPlayerState* Wallet = nullptr;
	const ACommandBuilding* Building = nullptr;
	const AArmyGroup* Force = nullptr;
	int32 Balance = 0;
	int32 DataBalance = 0;
	CommandForceCap::FOccupancy ForceSlots;
	bool bTerminal = false;
	// The frame's JEV display model when the HUD draws; panel queries rebuild one when it is null.
	const FJevIntentModel* JevIntent = nullptr;
	// The controller's request: false while Attack targeting or placement needs the world.
	bool bExpanded = true;
	// Set by F4 (or a selected building) when the deck does not fit beside the cards.
	bool bDeckPinned = false;
	// Own selectable forces in force-number order: one card each.
	TArray<AArmyGroup*, TInlineAllocator<6>> Forces;
};
struct FLayout
{
	float Scale = 0.f;
	float Width = 0.f;
	float Height = 0.f;
	FRect Top;
	FRect Objectives;
	FRect Alerts;
	FRect Bottom;
	FRect Build;
	FRect Inspector;
	FRect Minimap;
	FRect Feedback;
	FRect Menu;
	FRect Pause;
	FRect Screen;
	FRect ForceBar;
	// Whether the deck is actually drawn: requested and either beside the cards or pinned.
	bool bDeck = true;
	bool bFeedback = false;
};
enum class EBlock : uint8
{
	None,
	Terminal,
	Funds,
	ForceLocked,
	Chosen,
	ForceCap
};

struct FButton
{
	EHUDAction Action;
	FRect Rect;
	EBlock Block;
	bool bActive;
	int32 Shortfall;
	bool Available() const { return Block == EBlock::None; }
};

constexpr EArmyDoctrine ResearchChoices[] = {
	EArmyDoctrine::SiegeOptics, EArmyDoctrine::FieldRepairs, EArmyDoctrine::EntrenchedFrontline
};
enum class EAlign : uint8
{
	Left,
	Center,
	Right
};

struct FPainter
{
	UCanvas* Canvas = nullptr;
	float Scale = 1.f;
	const UFont* Font = nullptr;
	TSharedPtr<FSlateFontMeasure> Measure;

	FSlateFontInfo Info(float Size, bool bBold) const
	{
		static const FName Regular(TEXT("Regular"));
		static const FName Bold(TEXT("Bold"));
		return FSlateFontInfo(Font, Size * Scale, bBold ? Bold : Regular);
	}

	void Fill(const FRect& Rect, const FLinearColor& Color) const
	{
		// Snap edges, not sizes, so adjacent fills never leave hairline gaps.
		const float Left = FMath::RoundToFloat(Rect.X * Scale);
		const float Top = FMath::RoundToFloat(Rect.Y * Scale);
		const float Width = FMath::RoundToFloat(Rect.Right() * Scale) - Left;
		const float Height = FMath::RoundToFloat(Rect.Bottom() * Scale) - Top;
		if (Width <= 0.f || Height <= 0.f)
			return;
		FCanvasTileItem Tile(FVector2D(Left, Top), FVector2D(Width, Height), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	void Outline(const FRect& Rect, const FLinearColor& Color, float Thickness = 1.f) const
	{
		const float T = FMath::Max(1.f, FMath::RoundToFloat(Thickness * Scale)) / Scale;
		Fill({ Rect.X, Rect.Y, Rect.W, T }, Color);
		Fill({ Rect.X, Rect.Bottom() - T, Rect.W, T }, Color);
		Fill({ Rect.X, Rect.Y + T, T, Rect.H - 2.f * T }, Color);
		Fill({ Rect.Right() - T, Rect.Y + T, T, Rect.H - 2.f * T }, Color);
	}

	void Bar(const FRect& Rect, float Fraction, const FLinearColor& Color) const
	{
		Fill(Rect, Palette::Track);
		const float Clamped = FMath::Clamp(Fraction, 0.f, 1.f);
		if (Clamped > 0.f)
			Fill({ Rect.X, Rect.Y, Rect.W * Clamped, Rect.H }, Color);
	}

	void Panel(const FRect& Rect) const
	{
		Fill(Rect, Palette::Panel);
		Outline(Rect, Palette::Edge);
	}

	float TextWidth(FStringView Value, float Size, bool bBold = false) const
	{
		return Measure.IsValid() && !Value.IsEmpty() ? Measure->Measure(Value, Info(Size, bBold)).X / Scale : 0.f;
	}

	float LineHeight(float Size, bool bBold = false) const
	{
		return Measure.IsValid() ? Measure->GetMaxCharacterHeight(Info(Size, bBold)) / Scale : Size * 1.6f;
	}

	// Distance from the top of the line box to the text baseline.
	float Ascent(float Size, bool bBold = false) const
	{
		if (!Measure.IsValid())
			return Size * 1.3f;
		const FSlateFontInfo FontInfo = Info(Size, bBold);
		return (Measure->GetMaxCharacterHeight(FontInfo) + Measure->GetBaseline(FontInfo)) / Scale;
	}

	// Draws one line with its top at Y, clipped with an ellipsis beyond MaxWidth. Returns drawn width.
	float Text(FStringView Value, float X, float Y, float Size, const FLinearColor& Color, bool bBold = false,
		EAlign Align = EAlign::Left, float MaxWidth = 0.f) const
	{
		if (Value.IsEmpty() || !Measure.IsValid())
			return 0.f;
		const FSlateFontInfo FontInfo = Info(Size, bBold);
		float Width = Measure->Measure(Value, FontInfo).X;
		TStringBuilder<256> Clipped;
		if (MaxWidth > 0.f && Width > MaxWidth * Scale)
		{
			const float Ellipsis = Measure->Measure(TEXT("..."), FontInfo).X;
			const int32 Fit = Measure->FindLastWholeCharacterIndexBeforeOffset(Value, FontInfo,
				FMath::Max(0, FMath::FloorToInt(MaxWidth * Scale - Ellipsis)));
			Clipped << Value.Left(FMath::Max(0, Fit + 1)).TrimEnd() << TEXT("...");
			Value = Clipped.ToView();
			Width = Measure->Measure(Value, FontInfo).X;
		}
		float Left = X * Scale;
		if (Align == EAlign::Center)
			Left -= Width * .5f;
		else if (Align == EAlign::Right)
			Left -= Width;
		FCanvasTextStringViewItem Item(FVector2D(FMath::RoundToFloat(Left), FMath::RoundToFloat(Y * Scale)),
			Value, FontInfo, Color);
		Canvas->DrawItem(Item);
		return Width / Scale;
	}

	float TextOnBaseline(FStringView Value, float X, float Baseline, float Size, const FLinearColor& Color,
		bool bBold = false, EAlign Align = EAlign::Left, float MaxWidth = 0.f) const
	{
		return Text(Value, X, Baseline - Ascent(Size, bBold), Size, Color, bBold, Align, MaxWidth);
	}

	// Vertically centred inside Rect with horizontal inset.
	float TextIn(FStringView Value, const FRect& Rect, float Size, const FLinearColor& Color, bool bBold = false,
		EAlign Align = EAlign::Left, float Inset = 0.f, float MaxWidth = 0.f) const
	{
		const float Y = Rect.Y + (Rect.H - LineHeight(Size, bBold)) * .5f;
		const float X = Align == EAlign::Left ? Rect.X + Inset : Align == EAlign::Right ? Rect.Right() - Inset
																						: Rect.Center().X;
		return Text(Value, X, Y, Size, Color, bBold, Align, MaxWidth > 0.f ? MaxWidth : Rect.W - 2.f * Inset);
	}

	float KeyWidth(FStringView Key, FStringView Label) const
	{
		return FMath::Max(20.f, TextWidth(Key, 8.5f, true) + 10.f) + 5.f + TextWidth(Label, 9.5f);
	}

	float DrawKey(float X, float Y, FStringView Key, FStringView Label) const
	{
		const FRect Cap{ X, Y, FMath::Max(20.f, TextWidth(Key, 8.5f, true) + 10.f), KeyHeight };
		Fill(Cap, Palette::Key);
		Outline(Cap, Palette::KeyEdge);
		TextIn(Key, Cap, 8.5f, Palette::Text, true, EAlign::Center);
		const float LabelWidth = TextIn(Label, { Cap.Right() + 5.f, Y, 400.f, KeyHeight }, 9.5f, Palette::Muted);
		return Cap.W + 5.f + LabelWidth;
	}
};
struct FForces
{
	int32 Barracks = 0;
	int32 CompletedBarracks = 0;
	int32 Producing = 0;
	int32 Workshops = 0;
	int32 Extractors = 0;
	int32 Constructing = 0;
	int32 ConfiguredForces = 0;
	int32 FreeDeposits = 0;
	int32 ControlledRegions = 0;
};

}
