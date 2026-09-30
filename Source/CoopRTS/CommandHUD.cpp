#include "CommandHUD.h"

#include "ArenaBounds.h"
#include "ArmyUnit.h"
#include "CanvasItem.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "CommandPlayerController.h"
#include "CommandMinimap.h"
#include "CommandPlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineFontServices.h"
#include "Fonts/FontMeasure.h"
#include "Headquarters.h"
#include "Misc/StringBuilder.h"

namespace
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
	constexpr float DeckHeight = 186.f;
	constexpr float BuildWidth = 174.f;
	constexpr float InspectorWidth = 720.f;
	constexpr float MinimapSize = 144.f;
	constexpr float ModeHeight = 62.f;
	constexpr float FeedbackHeight = 26.f;
	constexpr float BannerWidth = 580.f;
	constexpr float BannerHeight = 112.f;
	constexpr float HeaderHeight = 44.f;
	constexpr float LabelHeight = 18.f;
	constexpr float RowHeight = 26.f;
	constexpr float RowGap = 4.f;
	constexpr float ColumnGap = 14.f;
	constexpr float KeyHeight = 17.f;

	namespace Palette
	{
		const FLinearColor Panel(.012f, .018f, .027f, .88f);
		const FLinearColor Edge(.32f, .46f, .60f, .34f);
		const FLinearColor Text(.90f, .94f, 1.f);
		const FLinearColor Muted(.60f, .67f, .76f);
		const FLinearColor Faint(.40f, .46f, .54f);
		const FLinearColor Gold(1.f, .80f, .36f);
		const FLinearColor Good(.46f, .94f, .56f);
		const FLinearColor Warn(1.f, .70f, .28f);
		const FLinearColor Bad(1.f, .38f, .32f);
		const FLinearColor Friendly(.30f, .72f, 1.f);
		const FLinearColor Enemy(1.f, .32f, .26f);
		const FLinearColor Card(.05f, .075f, .10f, .96f);
		const FLinearColor CardHover(.075f, .11f, .15f, .98f);
		const FLinearColor CardOff(.03f, .036f, .045f, .92f);
		const FLinearColor Track(.10f, .12f, .15f, 1.f);
		const FLinearColor Key(.10f, .13f, .17f, 1.f);
		const FLinearColor KeyEdge(.36f, .46f, .56f, .85f);
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
	};

	// Local presentation inputs shared by drawing and hit testing.
	struct FContext
	{
		const ACommandPlayerController* Controller = nullptr;
		const ACommandGameState* State = nullptr;
		const ACommandPlayerState* Wallet = nullptr;
		const ACommandBuilding* Building = nullptr;
		int32 Balance = 0;
		bool bTerminal = false;
		bool bExpanded = true;
	};

	FContext MakeContext(const ACommandPlayerController* Controller)
	{
		FContext Context;
		Context.Controller = Controller;
		if (!Controller) return Context;
		const UWorld* World = Controller->GetWorld();
		Context.State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		Context.Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		Context.Balance = Context.Wallet ? Context.Wallet->Resources : 0;
		Context.bTerminal = Context.State && Context.State->MatchResult != EMatchResult::Ongoing;
		Context.bExpanded = Controller->IsHUDExpanded();
		// The controller drops selections that stop being owned, so these are the local commander's.
		const ACommandBuilding* Building = Controller->GetSelectedBuilding();
		Context.Building = IsValid(Building) && Building->IsAlive() ? Building : nullptr;
		return Context;
	}

	// Migration lookups: the fixed three cards/rows still key on legacy enums until the deck enumerates MatchContent.
	const UBuildingDefinition* KindDefinition(const FContext& Context, EBuildingKind Kind)
	{
		const UMatchContent* Content = Context.State ? Context.State->Content.Get() : nullptr;
		return Content ? Content->Building(Content->BuildingIndexForKind(Kind)) : nullptr;
	}

	const UArmyUnitDefinition* RoleDefinition(const FContext& Context, EUnitRole Role)
	{
		const UMatchContent* Content = Context.State ? Context.State->Content.Get() : nullptr;
		return Content ? Content->Unit(Content->UnitIndexForRole(Role)) : nullptr;
	}

	// HUD-owned wording for the typed production state.
	const TCHAR* StatusText(EProductionState State)
	{
		switch (State)
		{
		case EProductionState::MatchFinished: return TEXT("MATCH FINISHED");
		case EProductionState::NotProducer: return TEXT("NO BARRACKS");
		case EProductionState::UnderConstruction: return TEXT("UNDER CONSTRUCTION");
		case EProductionState::Unconfigured: return TEXT("UNCONFIGURED");
		case EProductionState::ForceUnavailable: return TEXT("FORCE UNAVAILABLE");
		case EProductionState::Paused: return TEXT("PAUSED");
		case EProductionState::ForceComplete: return TEXT("FORCE COMPLETE");
		case EProductionState::WalletUnavailable: return TEXT("WALLET UNAVAILABLE");
		case EProductionState::InsufficientResources: return TEXT("INSUFFICIENT RESOURCES");
		case EProductionState::DeploymentBlocked: return TEXT("DEPLOYMENT BLOCKED");
		default: return TEXT("PRODUCING");
		}
	}

	struct FLayout
	{
		float Scale = 0.f;
		float Width = 0.f;
		float Height = 0.f;
		FRect Top;
		FRect Bottom;
		FRect Build;
		FRect Inspector;
		FRect Minimap;
		FRect Construction;
		FRect Feedback;
		FRect Banner;
		bool bFeedback = false;
		bool bBanner = false;
	};

	FLayout MakeLayout(const FContext& Context, float PixelWidth, float PixelHeight)
	{
		FLayout Layout;
		if (PixelWidth <= 0.f || PixelHeight <= 0.f) return Layout;
		Layout.Scale = FMath::Clamp(FMath::Min(PixelWidth / ReferenceWidth, PixelHeight / ReferenceHeight), MinScale, MaxScale);
		Layout.Width = PixelWidth / Layout.Scale;
		Layout.Height = PixelHeight / Layout.Scale;
		Layout.Top = {Margin, Margin, FMath::Min(980.f, Layout.Width - 2.f * Margin), TopHeight};
		Layout.Minimap = {Margin, Layout.Height - Margin - MinimapSize, MinimapSize, MinimapSize};
		const float X = Layout.Minimap.Right() + Gap;
		Layout.Construction = {X, Layout.Height - Margin - DeckHeight, BuildWidth, 28.f};
		Layout.Build = {X, Layout.Construction.Bottom() + Gap, BuildWidth, DeckHeight - 28.f - Gap};
		const float InspectorX = Layout.Build.Right() + Gap;
		Layout.Inspector = {InspectorX, Layout.Height - Margin - DeckHeight,
			FMath::Min(InspectorWidth, Layout.Width - InspectorX - Margin), DeckHeight};
		Layout.Bottom = Context.bExpanded ? Layout.Inspector
			: FRect{InspectorX, Layout.Height - Margin - ModeHeight, Layout.Inspector.W, ModeHeight};
		Layout.bFeedback = Context.Controller && !Context.Controller->GetOrderFeedback().IsEmpty();
		Layout.Feedback = {Layout.Bottom.X, Layout.Bottom.Y - Gap * .5f - FeedbackHeight, Layout.Bottom.W, FeedbackHeight};
		Layout.bBanner = Context.bTerminal;
		Layout.Banner = {(Layout.Width - BannerWidth) * .5f, FMath::Max(TopHeight + 24.f, Layout.Height * .2f),
			BannerWidth, BannerHeight};
		return Layout;
	}

	float BodyTop(const FRect& Inspector) { return Inspector.Y + Pad + HeaderHeight; }

	FRect Column(const FRect& Inspector, int32 Index, int32 Count)
	{
		const float Width = (Inspector.W - 2.f * Pad - (Count - 1) * ColumnGap) / Count;
		const float Top = BodyTop(Inspector);
		return {Inspector.X + Pad + Index * (Width + ColumnGap), Top, Width, Inspector.Bottom() - Pad - Top};
	}

	FRect Row(const FRect& ColumnRect, int32 Index)
	{
		return {ColumnRect.X, ColumnRect.Y + LabelHeight + Index * (RowHeight + RowGap), ColumnRect.W, RowHeight};
	}

	FRect BuildCard(const FRect& Build, int32 Index)
	{
		return {Build.X + Pad, Build.Y + Pad + Index * 46.f, Build.W - 2.f * Pad, 40.f};
	}

	FRect ResearchCard(const FRect& Inspector, int32 Index)
	{
		const FRect Area = Column(Inspector, Index, 3);
		return {Area.X, Area.Y + LabelHeight, Area.W, Area.H - LabelHeight};
	}

	FRect CancelButton(const FRect& Inspector)
	{
		return {Inspector.Right() - Pad - 230.f, Inspector.Bottom() - Pad - 36.f, 230.f, 36.f};
	}

	enum class EBlock : uint8 { None, Terminal, Funds, ForceLocked, Chosen };

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
		EArmyDoctrine::SiegeOptics, EArmyDoctrine::FieldRepairs, EArmyDoctrine::EntrenchedFrontline};

	EHUDAction ResearchAction(EArmyDoctrine Choice)
	{
		return Choice == EArmyDoctrine::SiegeOptics ? EHUDAction::ResearchSiege
			: Choice == EArmyDoctrine::FieldRepairs ? EHUDAction::ResearchRepairs : EHUDAction::ResearchEntrenched;
	}

	// Single geometry authority: every clickable control, its rectangle and whether it can be clicked.
	template <typename Fn>
	void ForEachButton(const FContext& Context, const FLayout& Layout, Fn&& Visit)
	{
		if (!Context.Controller || Layout.Scale <= 0.f) return;
		Visit(FButton{EHUDAction::Construction, Layout.Construction, EBlock::None, false, 0});
		if (!Context.bExpanded) return;
		auto Emit = [&Context, &Visit](EHUDAction Action, const FRect& Rect, int32 Cost, EBlock Lock, bool bActive)
		{
			const int32 Shortfall = FMath::Max(0, Cost - Context.Balance);
			const EBlock Block = Context.bTerminal ? EBlock::Terminal : Lock != EBlock::None ? Lock
				: Shortfall > 0 ? EBlock::Funds : EBlock::None;
			Visit(FButton{Action, Rect, Block, bActive, Shortfall});
		};
		const UBuildingDefinition* Barracks = KindDefinition(Context, EBuildingKind::Barracks);
		const UBuildingDefinition* Outpost = KindDefinition(Context, EBuildingKind::Outpost);
		const UBuildingDefinition* Workshop = KindDefinition(Context, EBuildingKind::Workshop);
		Emit(EHUDAction::BuildBarracks, BuildCard(Layout.Build, 0), Barracks ? Barracks->BuildCost : 0, EBlock::None, false);
		Emit(EHUDAction::BuildOutpost, BuildCard(Layout.Build, 1), Outpost ? Outpost->BuildCost : 0, EBlock::None, false);
		Emit(EHUDAction::BuildWorkshop, BuildCard(Layout.Build, 2), Workshop ? Workshop->BuildCost : 0, EBlock::None, false);

		const ACommandBuilding* Building = Context.Building;
		if (!Building) return;
		if (!Building->IsComplete())
		{
			Emit(EHUDAction::CancelConstruction, CancelButton(Layout.Inspector), 0, EBlock::None, false);
			return;
		}
		if (Building->Kind == EBuildingKind::Barracks)
		{
			const FRect Recipes = Column(Layout.Inspector, 0, 3);
			const FRect Production = Column(Layout.Inspector, 1, 3);
			const FRect Fronts = Column(Layout.Inspector, 2, 3);
			const EUnitRole Role = Building->ProductionRole;
			const EBlock RoleLock = Building->bForceConfigured ? EBlock::ForceLocked : EBlock::None;
			Emit(EHUDAction::RecipeFrontline, Row(Recipes, 0), 0, RoleLock, Role == EUnitRole::Frontline);
			Emit(EHUDAction::RecipeRanged, Row(Recipes, 1), 0, RoleLock, Role == EUnitRole::Ranged);
			Emit(EHUDAction::RecipeSiege, Row(Recipes, 2), 0, RoleLock, Role == EUnitRole::Siege);
			const UArmyUnitDefinition* Recipe = Building->GetProductionDefinition();
			if (!Recipe) Recipe = RoleDefinition(Context, Role);
			Emit(EHUDAction::ToggleProduction, Row(Production, 1),
				Building->bForceConfigured || !Recipe ? 0 : ACommandBuilding::GetConfigurationCost(*Recipe),
				EBlock::None, Building->bProductionEnabled);
			const bool bFront = Building->HasConfiguredFront();
			Emit(EHUDAction::FrontSecure, Row(Fronts, 0), 0, EBlock::None, bFront && Building->FrontOrder == EFrontOrder::Secure);
			Emit(EHUDAction::FrontDefend, Row(Fronts, 1), 0, EBlock::None, bFront && Building->FrontOrder == EFrontOrder::Defend);
			Emit(EHUDAction::FrontFallBack, Row(Fronts, 2), 0, EBlock::None, bFront && Building->FrontOrder == EFrontOrder::FallBack);
		}
		else if (Building->Kind == EBuildingKind::Workshop)
		{
			const EArmyDoctrine Owned = Context.Wallet ? Context.Wallet->Doctrine : EArmyDoctrine::None;
			int32 Index = 0;
			for (const EArmyDoctrine Choice : ResearchChoices)
			{
				const bool bOpen = Owned == EArmyDoctrine::None;
				Emit(ResearchAction(Choice), ResearchCard(Layout.Inspector, Index), bOpen ? ACommandBuilding::ResearchCost : 0,
					bOpen ? EBlock::None : EBlock::Chosen, Owned == Choice);
				++Index;
			}
		}
	}

	EHUDAction HitTest(const FContext& Context, const FLayout& Layout, const FVector2D& VirtualPoint)
	{
		EHUDAction Result = EHUDAction::None;
		ForEachButton(Context, Layout, [&Result, &VirtualPoint](const FButton& Button)
		{
			if (Button.Available() && Button.Rect.Contains(VirtualPoint)) Result = Button.Action;
		});
		return Result;
	}

	const TCHAR* KindTitle(EBuildingKind Kind)
	{
		switch (Kind)
		{
		case EBuildingKind::Barracks: return TEXT("BARRACKS");
		case EBuildingKind::Outpost: return TEXT("OUTPOST");
		case EBuildingKind::Workshop: return TEXT("WORKSHOP");
		default: return TEXT("BUILDING");
		}
	}

	FLinearColor KindColor(EBuildingKind Kind)
	{
		switch (Kind)
		{
		case EBuildingKind::Barracks: return FLinearColor(1.f, .58f, .25f);
		case EBuildingKind::Outpost: return FLinearColor(.40f, .90f, .55f);
		case EBuildingKind::Workshop: return FLinearColor(.72f, .52f, 1.f);
		default: return Palette::Muted;
		}
	}

	const TCHAR* RoleTitle(EUnitRole Role)
	{
		switch (Role)
		{
		case EUnitRole::Frontline: return TEXT("FRONTLINE");
		case EUnitRole::Ranged: return TEXT("RANGED");
		case EUnitRole::Siege: return TEXT("SIEGE");
		default: return TEXT("UNKNOWN");
		}
	}

	const TCHAR* FrontTitle(EFrontOrder Order)
	{
		switch (Order)
		{
		case EFrontOrder::Secure: return TEXT("SECURE");
		case EFrontOrder::Defend: return TEXT("DEFEND");
		case EFrontOrder::FallBack: return TEXT("FALL BACK");
		default: return TEXT("UNKNOWN");
		}
	}

	const TCHAR* FrontPurpose(EFrontOrder Order)
	{
		switch (Order)
		{
		case EFrontOrder::Secure: return TEXT("attack-move");
		case EFrontOrder::Defend: return TEXT("guard area");
		case EFrontOrder::FallBack: return TEXT("regroup");
		default: return TEXT("");
		}
	}

	// Matches the front rings drawn in the world by the controller.
	FLinearColor FrontColor(EFrontOrder Order)
	{
		switch (Order)
		{
		case EFrontOrder::Secure: return FLinearColor(1.f, .36f, .30f);
		case EFrontOrder::Defend: return FLinearColor(.38f, .92f, .48f);
		case EFrontOrder::FallBack: return FLinearColor(1.f, .86f, .32f);
		default: return Palette::Muted;
		}
	}

	const TCHAR* ResearchName(EArmyDoctrine Doctrine)
	{
		switch (Doctrine)
		{
		case EArmyDoctrine::SiegeOptics: return TEXT("Siege Optics");
		case EArmyDoctrine::FieldRepairs: return TEXT("Field Repairs");
		case EArmyDoctrine::EntrenchedFrontline: return TEXT("Entrenched Frontline");
		default: return TEXT("None");
		}
	}


	FLinearColor Tint(const FLinearColor& Accent, float Amount, float Alpha)
	{
		return FLinearColor(.03f + Accent.R * Amount, .045f + Accent.G * Amount, .06f + Accent.B * Amount, Alpha);
	}

	enum class EAlign : uint8 { Left, Center, Right };

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
			if (Width <= 0.f || Height <= 0.f) return;
			FCanvasTileItem Tile(FVector2D(Left, Top), FVector2D(Width, Height), Color);
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Tile);
		}

		void Outline(const FRect& Rect, const FLinearColor& Color, float Thickness = 1.f) const
		{
			const float T = FMath::Max(1.f, FMath::RoundToFloat(Thickness * Scale)) / Scale;
			Fill({Rect.X, Rect.Y, Rect.W, T}, Color);
			Fill({Rect.X, Rect.Bottom() - T, Rect.W, T}, Color);
			Fill({Rect.X, Rect.Y + T, T, Rect.H - 2.f * T}, Color);
			Fill({Rect.Right() - T, Rect.Y + T, T, Rect.H - 2.f * T}, Color);
		}

		void Bar(const FRect& Rect, float Fraction, const FLinearColor& Color) const
		{
			Fill(Rect, Palette::Track);
			const float Clamped = FMath::Clamp(Fraction, 0.f, 1.f);
			if (Clamped > 0.f) Fill({Rect.X, Rect.Y, Rect.W * Clamped, Rect.H}, Color);
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
			if (!Measure.IsValid()) return Size * 1.3f;
			const FSlateFontInfo FontInfo = Info(Size, bBold);
			return (Measure->GetMaxCharacterHeight(FontInfo) + Measure->GetBaseline(FontInfo)) / Scale;
		}

		// Draws one line with its top at Y, clipped with an ellipsis beyond MaxWidth. Returns drawn width.
		float Text(FStringView Value, float X, float Y, float Size, const FLinearColor& Color, bool bBold = false,
			EAlign Align = EAlign::Left, float MaxWidth = 0.f) const
		{
			if (Value.IsEmpty() || !Measure.IsValid()) return 0.f;
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
			if (Align == EAlign::Center) Left -= Width * .5f;
			else if (Align == EAlign::Right) Left -= Width;
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
			const float X = Align == EAlign::Left ? Rect.X + Inset : Align == EAlign::Right ? Rect.Right() - Inset : Rect.Center().X;
			return Text(Value, X, Y, Size, Color, bBold, Align, MaxWidth > 0.f ? MaxWidth : Rect.W - 2.f * Inset);
		}

		float KeyWidth(FStringView Key, FStringView Label) const
		{
			return FMath::Max(20.f, TextWidth(Key, 8.5f, true) + 10.f) + 5.f + TextWidth(Label, 9.5f);
		}

		float DrawKey(float X, float Y, FStringView Key, FStringView Label) const
		{
			const FRect Cap{X, Y, FMath::Max(20.f, TextWidth(Key, 8.5f, true) + 10.f), KeyHeight};
			Fill(Cap, Palette::Key);
			Outline(Cap, Palette::KeyEdge);
			TextIn(Key, Cap, 8.5f, Palette::Text, true, EAlign::Center);
			const float LabelWidth = TextIn(Label, {Cap.Right() + 5.f, Y, 400.f, KeyHeight}, 9.5f, Palette::Muted);
			return Cap.W + 5.f + LabelWidth;
		}
	};

	struct FForces
	{
		int32 Barracks = 0;
		int32 CompletedBarracks = 0;
		int32 Producing = 0;
		int32 Workshops = 0;
		int32 Outposts = 0;
		int32 Constructing = 0;
		int32 ConfiguredForces = 0;
		int32 OpenSectors = 0;
		int32 CapturedSectors = 0;
	};

	FForces CountForces(const FContext& Context)
	{
		FForces Forces;
		if (!Context.State || !Context.Wallet) return Forces;
		for (const ACommandBuilding* Building : Context.State->Buildings)
		{
			if (!IsValid(Building) || !Building->IsAlive() || Building->TeamIndex != 0
				|| Building->OwningPlayerState != Context.Wallet) continue;
			if (!Building->IsComplete()) ++Forces.Constructing;
			if (Building->Kind == EBuildingKind::Barracks)
			{
				++Forces.Barracks;
				if (Building->IsComplete()) ++Forces.CompletedBarracks;
				if (Building->IsComplete() && Building->GetProductionState() == EProductionState::Producing) ++Forces.Producing;
				if (Building->bForceConfigured) ++Forces.ConfiguredForces;
			}
			else if (Building->Kind == EBuildingKind::Workshop) ++Forces.Workshops;
			else ++Forces.Outposts;
		}
		// Sectors held by the team that do not yet have any friendly outpost, finished or not.
		for (const ACapturePoint* Site : Context.State->CaptureSites)
		{
			if (!IsValid(Site) || Site->ControllingTeam != 0) continue;
			++Forces.CapturedSectors;
			bool bHasOutpost = false;
			for (const ACommandBuilding* Building : Context.State->Buildings)
				if (IsValid(Building) && Building->IsAlive() && Building->TeamIndex == 0
					&& Building->Kind == EBuildingKind::Outpost && Building->OutpostSite == Site) bHasOutpost = true;
			if (!bHasOutpost) ++Forces.OpenSectors;
		}
		return Forces;
	}


	void DrawHQBar(const FPainter& Paint, const FRect& Rect, const TCHAR* Label, const AHeadquarters* HQ, const FLinearColor& Color)
	{
		const int32 Health = IsValid(HQ) ? HQ->Health : 0;
		const int32 Max = IsValid(HQ) ? FMath::Max(1, HQ->MaxHealth()) : 1;
		Paint.Bar(Rect, static_cast<float>(Health) / Max, Color.CopyWithNewOpacity(.62f));
		Paint.Outline(Rect, Color.CopyWithNewOpacity(.55f));
		Paint.TextIn(Label, Rect, 8.5f, Palette::Text, true, EAlign::Left, 7.f);
		TStringBuilder<32> Value;
		Value.Appendf(TEXT("%d / %d"), Health, Max);
		Paint.TextIn(Value.ToView(), Rect, 8.5f, Palette::Text, true, EAlign::Right, 7.f);
	}

	void DrawTopBar(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FLayout& Layout)
	{
		const FRect& Top = Layout.Top;
		Paint.Panel(Top);
		if (!Context.Wallet || !Context.State || Context.Wallet->CommanderIndex < 0)
		{
			Paint.TextIn(TEXT("Syncing commander, wallet and territory..."), Top, 10.f, Palette::Warn, false, EAlign::Left, Pad);
			return;
		}
		TStringBuilder<128> Economy;
		Economy.Appendf(TEXT("C%d   %d resources  +%d/s   Forces %d   Sectors %d/%d"),
			Context.Wallet->CommanderIndex + 1, Context.Balance, Context.Wallet->GetIncomePerSecond(),
			Forces.ConfiguredForces, Context.State->ControlledResourceSites, Context.State->CaptureSites.Num());
		Paint.TextIn(Economy.ToView(), {Top.X + Pad, Top.Y, Top.W - 2.f * Pad - 360.f, Top.H},
			10.f, Palette::Gold, true);
		const float HQX = Top.Right() - Pad - 350.f;
		DrawHQBar(Paint, {HQX, Top.Y + 7.f, 170.f, 18.f}, TEXT("YOUR HQ"), Context.State->FriendlyHeadquarters, Palette::Friendly);
		DrawHQBar(Paint, {HQX + 180.f, Top.Y + 7.f, 170.f, 18.f}, TEXT("ENEMY HQ"), Context.State->EnemyHeadquarters, Palette::Enemy);
	}

	float DrawBlockReason(const FPainter& Paint, const FButton& Button, float X, float Y, float Size, float MaxWidth,
		EAlign Align = EAlign::Left)
	{
		TStringBuilder<48> Reason;
		switch (Button.Block)
		{
		case EBlock::Terminal: Reason << TEXT("Match over"); break;
		case EBlock::Funds: Reason.Appendf(TEXT("Need %d more"), Button.Shortfall); break;
		case EBlock::ForceLocked: Reason << TEXT("Type locked"); break;
		case EBlock::Chosen: Reason << TEXT("Locked: one per commander"); break;
		default: return 0.f;
		}
		return Paint.Text(Reason.ToView(), X, Y, Size, Button.Block == EBlock::Funds ? Palette::Warn : Palette::Faint, false, Align, MaxWidth);
	}

	void DrawBuildCard(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
	{
		const EBuildingKind Kind = Button.Action == EHUDAction::BuildBarracks ? EBuildingKind::Barracks
			: Button.Action == EHUDAction::BuildOutpost ? EBuildingKind::Outpost : EBuildingKind::Workshop;
		const UBuildingDefinition* Definition = KindDefinition(Context, Kind);
		const FRect& Rect = Button.Rect;
		Paint.Fill(Rect, !Button.Available() ? Palette::CardOff : bHover ? Palette::CardHover : Palette::Card);
		Paint.Fill({Rect.X, Rect.Y, 3.f, Rect.H}, KindColor(Kind));
		Paint.Text(KindTitle(Kind), Rect.X + 8.f, Rect.Y + 4.f, 10.f, Palette::Text, true, EAlign::Left, Rect.W - 16.f);
		TStringBuilder<48> Detail;
		Detail.Appendf(TEXT("%d  /  %.0fs"), Definition ? Definition->BuildCost : 0, Definition ? Definition->BuildDuration : 0.f);
		if (Button.Available()) Paint.Text(Detail.ToView(), Rect.X + 8.f, Rect.Y + 21.f, 9.f, Palette::Gold);
		else DrawBlockReason(Paint, Button, Rect.X + 8.f, Rect.Y + 21.f, 9.f, Rect.W - 16.f);
	}

	void DrawResearchCard(const FPainter& Paint, const FButton& Button, bool bHover)
	{
		const EArmyDoctrine Choice = Button.Action == EHUDAction::ResearchSiege ? EArmyDoctrine::SiegeOptics
			: Button.Action == EHUDAction::ResearchRepairs ? EArmyDoctrine::FieldRepairs : EArmyDoctrine::EntrenchedFrontline;
		const FRect& Rect = Button.Rect;
		const bool bOn = Button.Available();
		const FLinearColor Accent = Button.bActive ? Palette::Good : KindColor(EBuildingKind::Workshop);
		Paint.Fill(Rect, Button.bActive ? Tint(Palette::Good, .16f, .96f) : !bOn ? Palette::CardOff : bHover ? Palette::CardHover : Palette::Card);
		Paint.Fill({Rect.X, Rect.Y, Rect.W, 3.f}, Accent.CopyWithNewOpacity(bOn || Button.bActive ? 1.f : .3f));
		Paint.Outline(Rect, Button.bActive ? Palette::Good.CopyWithNewOpacity(.8f) : bOn && bHover ? Accent : Palette::Edge);
		const float Inner = Rect.W - 20.f;
		FString Title(ResearchName(Choice));
		Title.ToUpperInline();
		Paint.Text(Title, Rect.X + 10.f, Rect.Y + 10.f, 10.5f, bOn || Button.bActive ? Palette::Text : Palette::Muted, true, EAlign::Left, Inner);
		const TCHAR* First = Choice == EArmyDoctrine::SiegeOptics ? TEXT("Siege range +25%")
			: Choice == EArmyDoctrine::FieldRepairs ? TEXT("Units heal 5 HP/s after") : TEXT("Frontline takes -25% damage");
		const TCHAR* Second = Choice == EArmyDoctrine::SiegeOptics ? TEXT("Outgoing damage -25%")
			: Choice == EArmyDoctrine::FieldRepairs ? TEXT("5 s without move/fire/damage") : TEXT("while stationary at a Defend front");
		Paint.Text(First, Rect.X + 10.f, Rect.Y + 30.f, 9.f, Palette::Muted, false, EAlign::Left, Inner);
		Paint.Text(Second, Rect.X + 10.f, Rect.Y + 45.f, 9.f, Palette::Muted, false, EAlign::Left, Inner);
		const float Baseline = Rect.Bottom() - 11.f;
		if (Button.bActive)
			Paint.TextOnBaseline(TEXT("OWNED  \u00B7  ACTIVE"), Rect.X + 10.f, Baseline, 9.5f, Palette::Good, true);
		else if (bOn)
		{
			TStringBuilder<16> Cost;
			Cost.Appendf(TEXT("%d"), ACommandBuilding::ResearchCost);
			Paint.TextOnBaseline(Cost.ToView(), Rect.X + 10.f, Baseline, 13.f, Palette::Gold, true);
			Paint.TextOnBaseline(TEXT("click to buy"), Rect.Right() - 10.f, Baseline, 8.5f, Palette::Faint, false, EAlign::Right);
		}
		else DrawBlockReason(Paint, Button, Rect.X + 10.f, Baseline - Paint.Ascent(9.f), 9.f, Inner);
	}

	void DrawCommandRow(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
	{
		const ACommandBuilding* Building = Context.Building;
		if (!Building) return;
		TStringBuilder<48> Left;
		TStringBuilder<48> Right;
		FLinearColor Accent = Palette::Friendly;
		FLinearColor RightColor = Palette::Faint;
		switch (Button.Action)
		{
		case EHUDAction::RecipeFrontline:
		case EHUDAction::RecipeRanged:
		case EHUDAction::RecipeSiege:
		{
			const EUnitRole Role = Button.Action == EHUDAction::RecipeFrontline ? EUnitRole::Frontline
				: Button.Action == EHUDAction::RecipeRanged ? EUnitRole::Ranged : EUnitRole::Siege;
			const UArmyUnitDefinition* Definition = RoleDefinition(Context, Role);
			Left << RoleTitle(Role);
			if (Definition && (Button.Block == EBlock::None || Button.bActive))
			{
				Right.Appendf(TEXT("%d  \u00B7  %d/%.1fs"), ACommandBuilding::GetForceCapacity(*Definition),
					ACommandBuilding::GetUnitCost(*Definition), ACommandBuilding::GetUnitDuration(*Definition));
				RightColor = Palette::Gold;
			}
			break;
		}
		case EHUDAction::ToggleProduction:
		{
			const bool bEnabled = Building->bProductionEnabled;
			Left << (!Building->bForceConfigured ? TEXT("START & LOCK") : bEnabled ? TEXT("PAUSE") : TEXT("RESUME"));
			if (!Building->bForceConfigured)
			{
				const UArmyUnitDefinition* Recipe = RoleDefinition(Context, Building->ProductionRole);
				const int32 Fee = Recipe ? ACommandBuilding::GetConfigurationCost(*Recipe) : 0;
				if (Fee > 0 && Button.Block != EBlock::Funds) Right.Appendf(TEXT("+%d fee"), Fee);
			}
			else if (Button.Block != EBlock::Terminal) Right << (bEnabled ? TEXT("enabled") : TEXT("paused"));
			Accent = bEnabled ? Palette::Warn : Palette::Good;
			RightColor = Palette::Muted;
			break;
		}
		case EHUDAction::FrontSecure:
		case EHUDAction::FrontDefend:
		case EHUDAction::FrontFallBack:
		{
			const EFrontOrder Order = Button.Action == EHUDAction::FrontSecure ? EFrontOrder::Secure
				: Button.Action == EHUDAction::FrontDefend ? EFrontOrder::Defend : EFrontOrder::FallBack;
			Left << FrontTitle(Order);
			Accent = FrontColor(Order);
			Right << (Button.bActive ? TEXT("CURRENT") : FrontPurpose(Order));
			RightColor = Button.bActive ? Accent : Palette::Faint;
			break;
		}
		case EHUDAction::CancelConstruction:
			Left << TEXT("CANCEL BUILD");
			Accent = Palette::Bad;
			Right.Appendf(TEXT("refund %d"), FMath::FloorToInt(
				(Building->GetDefinition() ? Building->GetDefinition()->BuildCost : 0) * (1.f - Building->ConstructionProgress)));
			RightColor = Palette::Gold;
			break;
		default:
			return;
		}
		const FRect& Rect = Button.Rect;
		const bool bOn = Button.Available();
		const bool bActiveRecipeOrFront = Button.bActive && Button.Action != EHUDAction::ToggleProduction;
		Paint.Fill(Rect, bActiveRecipeOrFront ? Tint(Accent, .2f, .96f) : !bOn ? Palette::CardOff : bHover ? Palette::CardHover : Palette::Card);
		Paint.Fill({Rect.X, Rect.Y, 3.f, Rect.H}, Accent.CopyWithNewOpacity(bOn || Button.bActive ? 1.f : .3f));
		Paint.Outline(Rect, bActiveRecipeOrFront ? Accent.CopyWithNewOpacity(.85f) : bOn && bHover ? Accent.CopyWithNewOpacity(.7f) : Palette::Edge);
		float RightWidth = 0.f;
		if (Right.Len() > 0) RightWidth = Paint.TextIn(Right.ToView(), Rect, 9.f, RightColor, false, EAlign::Right, 9.f);
		else if (!bOn)
		{
			const float Y = Rect.Y + (Rect.H - Paint.LineHeight(9.f)) * .5f;
			RightWidth = DrawBlockReason(Paint, Button, Rect.Right() - 9.f, Y, 9.f, Rect.W * .5f, EAlign::Right);
		}
		Paint.TextIn(Left.ToView(), Rect, 10.f, bOn || Button.bActive ? Palette::Text : Palette::Muted, true, EAlign::Left, 11.f,
			Rect.W - 22.f - RightWidth - 8.f);
	}

	void DrawButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
	{
		switch (Button.Action)
		{
		case EHUDAction::Construction:
			Paint.Fill(Button.Rect, bHover ? Palette::CardHover : Palette::Panel);
			Paint.Outline(Button.Rect, Palette::Friendly);
			Paint.TextIn(TEXT("CONSTRUCTION"), Button.Rect, 10.f, Palette::Text, true, EAlign::Center);
			break;
		case EHUDAction::BuildBarracks:
		case EHUDAction::BuildOutpost:
		case EHUDAction::BuildWorkshop:
			DrawBuildCard(Paint, Context, Button, bHover);
			break;
		case EHUDAction::ResearchSiege:
		case EHUDAction::ResearchRepairs:
		case EHUDAction::ResearchEntrenched:
			DrawResearchCard(Paint, Button, bHover);
			break;
		default:
			DrawCommandRow(Paint, Context, Button, bHover);
		}
	}

	void DrawBuildPanel(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FLayout& Layout)
	{
		Paint.Panel(Layout.Build);
	}

	void DrawInspectorHeader(const FPainter& Paint, const FRect& Inspector, const FLinearColor& Accent, FStringView Title,
		FStringView Subtitle, int32 Owner, int32 Health, int32 MaxHealth, FStringView Status, const FLinearColor& StatusColor)
	{
		Paint.Fill({Inspector.X, Inspector.Y, 3.f, Inspector.H}, Accent);
		const float X = Inspector.X + Pad;
		const float Right = Inspector.Right() - Pad;
		const float TitleWidth = Paint.Text(Title, X, Inspector.Y + Pad - 2.f, 13.f, Palette::Text, true, EAlign::Left, Inspector.W * .5f);
		if (Owner >= 0) Paint.Fill({X, Inspector.Y + Pad + 23.f, 9.f, 9.f}, AArmyUnit::GetCommanderColor(Owner));
		Paint.Text(Subtitle, X + (Owner >= 0 ? 14.f : 0.f), Inspector.Y + Pad + 19.f, 9.5f, Palette::Muted, false, EAlign::Left,
			Inspector.W * .55f);
		if (!Status.IsEmpty())
			Paint.Text(Status, Right, Inspector.Y + Pad - 1.f, 9.5f, StatusColor, true, EAlign::Right,
				Inspector.W - 2.f * Pad - TitleWidth - 20.f);
		if (MaxHealth > 0)
		{
			const FRect Bar{Right - 190.f, Inspector.Y + Pad + 19.f, 190.f, 16.f};
			const float Fraction = static_cast<float>(Health) / MaxHealth;
			Paint.Bar(Bar, Fraction, (Fraction > .5f ? Palette::Good : Fraction > .25f ? Palette::Warn : Palette::Bad).CopyWithNewOpacity(.55f));
			TStringBuilder<32> Value;
			Value.Appendf(TEXT("HP  %d / %d"), Health, MaxHealth);
			Paint.TextIn(Value.ToView(), Bar, 8.f, Palette::Text, true, EAlign::Center);
		}
		Paint.Fill({X, Inspector.Y + Pad + HeaderHeight - 8.f, Inspector.W - 2.f * Pad, 1.f}, Palette::Edge);
	}

	void ColumnLabel(const FPainter& Paint, const FRect& ColumnRect, FStringView Label, FStringView Detail = FStringView(),
		const FLinearColor& DetailColor = Palette::Faint)
	{
		const float Width = Paint.Text(Label, ColumnRect.X, ColumnRect.Y, 8.5f, Palette::Muted, true);
		if (!Detail.IsEmpty())
			Paint.Text(Detail, ColumnRect.Right(), ColumnRect.Y, 8.5f, DetailColor, false, EAlign::Right, ColumnRect.W - Width - 8.f);
	}

	void DrawBuildingInspector(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FRect& Inspector)
	{
		const ACommandBuilding* Building = Context.Building;
		const int32 Owner = Context.Wallet ? Context.Wallet->CommanderIndex : -1;
		TStringBuilder<48> Title;
		Title << KindTitle(Building->Kind);
		TStringBuilder<48> Subtitle;
		Subtitle.Appendf(TEXT("C%d  \u00B7  your building"), Owner + 1);

		if (!Building->IsComplete())
		{
			const float Progress = FMath::Clamp(Building->ConstructionProgress, 0.f, 1.f);
			DrawInspectorHeader(Paint, Inspector, KindColor(Building->Kind), Title.ToView(), Subtitle.ToView(), Owner,
				Building->Health, Building->MaxHealth(), Context.bTerminal ? TEXT("HALTED") : TEXT("UNDER CONSTRUCTION"),
				Context.bTerminal ? Palette::Faint : Palette::Warn);
			const float Top = BodyTop(Inspector);
			const FRect Bar{Inspector.X + Pad, Top + LabelHeight + 2.f, CancelButton(Inspector).X - Inspector.X - 2.f * Pad - 12.f, 14.f};
			Paint.Text(TEXT("CONSTRUCTION"), Bar.X, Top, 8.5f, Palette::Muted, true);
			Paint.Bar(Bar, Progress, KindColor(Building->Kind).CopyWithNewOpacity(.85f));
			TStringBuilder<64> Status;
			if (Context.bTerminal) Status.Appendf(TEXT("%d%%  \u00B7  halted: match over"), FMath::FloorToInt(Progress * 100.f));
			else Status.Appendf(TEXT("%d%%  \u00B7  %.0fs remaining"), FMath::FloorToInt(Progress * 100.f),
				FMath::CeilToFloat((1.f - Progress) * (Building->GetDefinition() ? Building->GetDefinition()->BuildDuration : 0.f)));
			Paint.Text(Status.ToView(), Bar.X, Bar.Bottom() + 7.f, 11.f, Palette::Text, true);
			Paint.Text(Building->Kind == EBuildingKind::Barracks ? TEXT("When complete: choose a permanent force type, Start and set a front.")
				: Building->Kind == EBuildingKind::Outpost ? TEXT("When complete: secures this sector's income and build rights.")
				: TEXT("When complete: buy one specialization for your forces."),
				Bar.X, Bar.Bottom() + 32.f, 9.f, Palette::Muted, false, EAlign::Left, Bar.W);
			Paint.Text(TEXT("Cancelling refunds the unbuilt share of the cost."), Bar.X, Bar.Bottom() + 48.f, 9.f, Palette::Faint,
				false, EAlign::Left, Bar.W);
			return;
		}

		if (Building->IsProducer())
		{
			const EProductionState ProductionState = Building->GetProductionState();
			const FString Status = StatusText(ProductionState);
			const FLinearColor StatusColor = ProductionState == EProductionState::Producing || ProductionState == EProductionState::ForceComplete ? Palette::Good
				: ProductionState == EProductionState::Paused || ProductionState == EProductionState::MatchFinished ? Palette::Muted : Palette::Warn;
			DrawInspectorHeader(Paint, Inspector, KindColor(Building->Kind), Title.ToView(), Subtitle.ToView(), Owner,
				Building->Health, Building->MaxHealth(), Status, StatusColor);
			const FRect Recipes = Column(Inspector, 0, 3);
			const FRect Production = Column(Inspector, 1, 3);
			const FRect Fronts = Column(Inspector, 2, 3);
			ColumnLabel(Paint, Recipes, TEXT("FORCE TYPE"), Building->bForceConfigured ? TEXT("LOCKED") : TEXT("choose before Start"));
			int32 Joined = 0, Travelling = 0;
			Building->GetForceCounts(Joined, Travelling);
			const UArmyUnitDefinition* Recipe = Building->GetProductionDefinition();
			if (!Recipe) Recipe = RoleDefinition(Context, Building->ProductionRole);
			const int32 Capacity = Recipe ? ACommandBuilding::GetForceCapacity(*Recipe) : 0;
			const int32 Vacancies = FMath::Max(0, Capacity - Joined - Travelling);
			TStringBuilder<32> ForceCounts;
			ForceCounts.Appendf(TEXT("joined %d/%d"), Joined, Capacity);
			ColumnLabel(Paint, Production, TEXT("FORCE"), ForceCounts.ToView());
			const bool bFront = Building->HasConfiguredFront();
			TStringBuilder<32> FrontState;
			if (bFront) FrontState.Appendf(TEXT("%s set"), FrontTitle(Building->FrontOrder));
			else FrontState << TEXT("unset: gather at barracks");
			ColumnLabel(Paint, Fronts, TEXT("FRONT"), FrontState.ToView(), bFront ? FrontColor(Building->FrontOrder) : Palette::Warn);

			const FRect Progress = Row(Production, 0);
			const float Duration = FMath::Max(KINDA_SMALL_NUMBER, Building->GetProductionDuration());
			Paint.Bar({Progress.X, Progress.Y, Progress.W, 5.f}, Building->ProductionProgressSeconds / Duration, StatusColor);
			TStringBuilder<64> Timer;
			const int32 BuildingCount = Building->bForceConfigured && (Building->ProductionProgressSeconds > 0.f
				|| Status == TEXT("PRODUCING") || Status == TEXT("DEPLOYMENT BLOCKED")) ? 1 : 0;
			Timer.Appendf(TEXT("Building %d: %.1f/%.1fs"), BuildingCount, Building->ProductionProgressSeconds, Duration);
			Paint.Text(Timer.ToView(), Progress.X, Progress.Y + 7.f, 9.f, Palette::Text, true, EAlign::Left, Progress.W);
			TStringBuilder<64> Recruits;
			Recruits.Appendf(TEXT("Travelling %d  \u00B7  Vacant %d"), Travelling, Vacancies);
			Paint.Text(Recruits.ToView(), Production.X, Row(Production, 2).Y, 9.f, Palette::Text, false, EAlign::Left, Production.W);
			TStringBuilder<48> UnitPrice;
			UnitPrice.Appendf(TEXT("%d resources per unit"), Building->GetProductionCost());
			Paint.Text(UnitPrice.ToView(), Production.X, Row(Production, 2).Y + 13.f, 8.f, Palette::Gold, false, EAlign::Left, Production.W);
			TStringBuilder<128> Remedy;
			if (ProductionState == EProductionState::Unconfigured)
			{
				Remedy << TEXT("First Start permanently locks this building's force type");
				const int32 Fee = Recipe ? ACommandBuilding::GetConfigurationCost(*Recipe) : 0;
				if (Fee > 0) Remedy.Appendf(TEXT("; Siege configuration costs %d resources once."), Fee);
				else Remedy << TEXT("; no configuration fee.");
			}
			else if (ProductionState == EProductionState::ForceComplete) Remedy << TEXT("Force full: no spending; casualties automatically open replacement slots.");
			else if (ProductionState == EProductionState::InsufficientResources) Remedy.Appendf(TEXT("Need %d more: resumes with income."),
				FMath::Max(0, Building->GetProductionCost() - Context.Balance));
			else if (ProductionState == EProductionState::Paused) Remedy << TEXT("Paused: click Resume.");
			else if (ProductionState == EProductionState::DeploymentBlocked) Remedy << TEXT("Deployment blocked: clear barracks exit; auto retry.");
			else if (ProductionState == EProductionState::Producing) Remedy << TEXT("Building one unit; pays at barracks deployment, then walks to this force.");
			else Remedy << Status;
			Paint.Text(Remedy.ToView(), Inspector.X + Pad, Inspector.Bottom() - 22.f, 10.f, StatusColor,
				true, EAlign::Left, Inspector.W - 2.f * Pad);
			return;
		}

		if (Building->Kind == EBuildingKind::Workshop)
		{
			const EArmyDoctrine Owned = Context.Wallet ? Context.Wallet->Doctrine : EArmyDoctrine::None;
			DrawInspectorHeader(Paint, Inspector, KindColor(Building->Kind), Title.ToView(), Subtitle.ToView(), Owner,
				Building->Health, Building->MaxHealth(), Owned == EArmyDoctrine::None ? TEXT("RESEARCH AVAILABLE") : TEXT("SPECIALIZED"),
				Owned == EArmyDoctrine::None ? Palette::Good : Palette::Muted);
			TStringBuilder<64> Detail;
			if (Owned == EArmyDoctrine::None)
				Detail.Appendf(TEXT("one per commander  \u00B7  %d each  \u00B7  applies to all your forces"), ACommandBuilding::ResearchCost);
			else Detail.Appendf(TEXT("%s is permanent for this match"), ResearchName(Owned));
			const FRect Area{Inspector.X + Pad, BodyTop(Inspector), Inspector.W - 2.f * Pad, LabelHeight};
			ColumnLabel(Paint, Area, TEXT("SPECIALIZATION"), Detail.ToView());
			return;
		}

		const ACapturePoint* Site = IsValid(Building->OutpostSite) ? Building->OutpostSite.Get() : nullptr;
		const bool bEstablished = Site && Site->IsEstablishedForTeam(0);
		DrawInspectorHeader(Paint, Inspector, KindColor(Building->Kind), Title.ToView(), Subtitle.ToView(), Owner,
			Building->Health, Building->MaxHealth(), bEstablished ? TEXT("SECTOR ESTABLISHED") : TEXT("SECTOR NOT ESTABLISHED"),
			bEstablished ? Palette::Good : Palette::Warn);
		const float X = Inspector.X + Pad;
		const float Top = BodyTop(Inspector);
		const float Width = Inspector.W - 2.f * Pad;
		TStringBuilder<48> Sector;
		if (Site) Sector.Appendf(TEXT("SECTOR %d"), Site->SiteIndex + 1);
		else Sector << TEXT("SECTOR");
		Paint.Text(Sector.ToView(), X, Top, 8.5f, Palette::Muted, true);
		TStringBuilder<96> Income;
		Income.Appendf(TEXT("+%d/s income for every friendly commander while this outpost stands."), ACommandGameState::ResourceIncomePerSecond);
		Paint.Text(Income.ToView(), X, Top + 22.f, 10.5f, Palette::Text, false, EAlign::Left, Width);
		TStringBuilder<96> Rights;
		Rights.Appendf(TEXT("Team build rights within %.0f units of the sector centre."), ACapturePoint::TerritoryRadius);
		Paint.Text(Rights.ToView(), X, Top + 44.f, 10.f, Palette::Muted, false, EAlign::Left, Width);
		Paint.Text(TEXT("If destroyed, the sector loses its income and build rights."), X, Top + 64.f, 10.f, Palette::Warn,
			false, EAlign::Left, Width);
	}


	void DrawOverview(const FPainter& Paint, const FContext& Context, const FForces& Forces, const FRect& Inspector)
	{
		const int32 Owner = Context.Wallet ? Context.Wallet->CommanderIndex : -1;
		TStringBuilder<48> Subtitle;
		if (Owner >= 0) Subtitle.Appendf(TEXT("C%d  \u00B7  nothing selected"), Owner + 1);
		else Subtitle << TEXT("commander slot syncing");
		DrawInspectorHeader(Paint, Inspector, Owner >= 0 ? AArmyUnit::GetCommanderColor(Owner) : Palette::Muted,
			TEXT("COMMAND OVERVIEW"), Subtitle.ToView(), Owner, 0, 0, FStringView(), Palette::Muted);
		const FRect Base = Column(Inspector, 0, 3);
		const FRect Next = Column(Inspector, 1, 3);
		const FRect Controls = Column(Inspector, 2, 3);

		ColumnLabel(Paint, Base, TEXT("YOUR BASE"));
		TStringBuilder<64> Line;
		const float Y = Base.Y + LabelHeight;
		Line.Appendf(TEXT("Barracks %d  \u00B7  %d producing"), Forces.Barracks, Forces.Producing);
		Paint.Text(Line.ToView(), Base.X, Y, 10.f, Palette::Text, false, EAlign::Left, Base.W);
		Line.Reset();
		Line.Appendf(TEXT("Workshops %d  \u00B7  Outposts %d"), Forces.Workshops, Forces.Outposts);
		Paint.Text(Line.ToView(), Base.X, Y + 19.f, 10.f, Palette::Text, false, EAlign::Left, Base.W);
		Line.Reset();
		Line.Appendf(TEXT("%d under construction"), Forces.Constructing);
		Paint.Text(Line.ToView(), Base.X, Y + 38.f, 10.f, Forces.Constructing > 0 ? Palette::Warn : Palette::Muted, false, EAlign::Left, Base.W);
		Line.Reset();
		Line.Appendf(TEXT("Configured forces %d"), Forces.ConfiguredForces);
		Paint.Text(Line.ToView(), Base.X, Y + 57.f, 10.f, Palette::Text, false, EAlign::Left, Base.W);

		// Next step is derived from the commander's real buildings, forces and sectors.
		const TCHAR* First;
		const TCHAR* Second;
		if (Context.bTerminal) { First = TEXT("Match over."); Second = TEXT("Press Enter for a fresh match."); }
		else if (Forces.Barracks == 0) { First = TEXT("Build a Barracks inside"); Second = TEXT("the cyan HQ ring."); }
		else if (Forces.CompletedBarracks == 0) { First = TEXT("Barracks under construction."); Second = TEXT("Plan its force type and front."); }
		else if (Forces.ConfiguredForces == 0) { First = TEXT("Select your Barracks, choose"); Second = TEXT("a permanent type and Start."); }
		else if (Forces.OpenSectors > 0) { First = TEXT("Build an Outpost on your"); Second = TEXT("captured sector for income."); }
		else if (Forces.CapturedSectors == 0) { First = TEXT("Set a Secure front inside"); Second = TEXT("a sector ring to capture it."); }
		else if (Forces.Workshops == 0) { First = TEXT("A Workshop unlocks one"); Second = TEXT("paid specialization."); }
		else { First = TEXT("Set Barracks fronts and push"); Second = TEXT("toward the enemy HQ."); }
		ColumnLabel(Paint, Next, TEXT("NEXT STEP"));
		Paint.Text(First, Next.X, Y, 10.5f, Palette::Friendly, true, EAlign::Left, Next.W);
		Paint.Text(Second, Next.X, Y + 18.f, 10.5f, Palette::Friendly, true, EAlign::Left, Next.W);
		Paint.Text(TEXT("Click an owned building."), Next.X, Y + 46.f, 8.5f, Palette::Faint,
			false, EAlign::Left, Next.W);

		ColumnLabel(Paint, Controls, TEXT("CONTROLS"));
		const float Half = Controls.W * .5f;
		Paint.DrawKey(Controls.X, Y, TEXT("LMB"), TEXT("Select"));
		Paint.DrawKey(Controls.X, Y + 23.f, TEXT("Space"), TEXT("Focus"));
		Paint.DrawKey(Controls.X + Half, Y + 23.f, TEXT("F4"), TEXT("Deck"));
		Paint.DrawKey(Controls.X, Y + 46.f, TEXT("WASD"), TEXT("Pan"));
		Paint.DrawKey(Controls.X + Half, Y + 46.f, TEXT("Wheel"), TEXT("Zoom"));
	}

	void DrawModeBar(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
	{
		const FRect& Mode = Layout.Bottom;
		const ACommandPlayerController* Controller = Context.Controller;
		Paint.Panel(Mode);
		const float X = Mode.X + Pad + 6.f;
		const float Row1 = Mode.Y + 10.f;
		const float Row2 = Mode.Y + 36.f;
		const float KeysRight = Mode.Right() - Pad;
		const float KeysWidth = FMath::Max(Paint.KeyWidth(TEXT("LMB"), TEXT("Place")), Paint.KeyWidth(TEXT("RMB / Esc"), TEXT("Cancel")));
		const float TextWidth = KeysRight - KeysWidth - 16.f - X;
		if (Controller->IsPlacingBuilding())
		{
			const UBuildingDefinition* Placement = Controller->GetPlacementDefinition();
			const EBuildingKind Kind = Placement ? Placement->GetKind() : EBuildingKind::Barracks;
			Paint.Fill({Mode.X, Mode.Y, 4.f, Mode.H}, KindColor(Kind));
			TStringBuilder<32> Title;
			Title.Appendf(TEXT("PLACE %s"), KindTitle(Kind));
			const float TitleWidth = Paint.Text(Title.ToView(), X, Row1, 12.5f, Palette::Text, true);
			TStringBuilder<32> Cost;
			const int32 Price = Placement ? Placement->BuildCost : 0;
			Cost.Appendf(TEXT("%d  \u00B7  %.0fs build"), Price, Placement ? Placement->BuildDuration : 0.f);
			Paint.TextOnBaseline(Cost.ToView(), X + TitleWidth + 12.f, Row1 + Paint.Ascent(12.5f, true), 10.f,
				Context.Balance >= Price ? Palette::Gold : Palette::Warn, true);
			FVector Location;
			FString Reason;
			bool bCanPlace = false;
			const bool bGround = Controller->GetPlacementPreview(Location, Reason, bCanPlace);
			const FLinearColor Color = bCanPlace ? Palette::Good : Palette::Warn;
			Paint.Fill({X, Row2 + 4.f, 9.f, 9.f}, Color);
			Paint.Text(bGround ? FStringView(Reason) : FStringView(TEXT("Point at ground to place.")), X + 16.f, Row2, 10.5f, Color,
				false, EAlign::Left, TextWidth - 16.f);
			Paint.DrawKey(KeysRight - KeysWidth, Row1, TEXT("LMB"), TEXT("Place"));
			Paint.DrawKey(KeysRight - KeysWidth, Row2, TEXT("RMB / Esc"), TEXT("Cancel"));
			return;
		}
		if (Controller->IsAssigningFront())
		{
			const EFrontOrder Order = Controller->GetPendingFrontOrder();
			Paint.Fill({Mode.X, Mode.Y, 4.f, Mode.H}, FrontColor(Order));
			TStringBuilder<32> Title;
			Title.Appendf(TEXT("SET %s FRONT"), FrontTitle(Order));
			Paint.Text(Title.ToView(), X, Row1, 12.5f, Palette::Text, true);
			Paint.Text(TEXT("Left-click navigable ground; this barracks' persistent force heads there."), X, Row2, 10.f,
				Palette::Muted, false, EAlign::Left, TextWidth);
			Paint.DrawKey(KeysRight - KeysWidth, Row1, TEXT("LMB"), TEXT("Assign"));
			Paint.DrawKey(KeysRight - KeysWidth, Row2, TEXT("RMB / Esc"), TEXT("Cancel"));
			return;
		}
		Paint.Fill({Mode.X, Mode.Y, 4.f, Mode.H}, Palette::Edge);
		Paint.Text(TEXT("COMMAND DECK HIDDEN"), X, Row1, 11.5f, Palette::Muted, true);
		TStringBuilder<64> Selection;
		if (Context.Building)
		{
			Selection.Appendf(TEXT("Selected: your %s"), KindTitle(Context.Building->Kind));
			if (!Context.Building->IsComplete())
				Selection.Appendf(TEXT("  \u00B7  %d%% built"), FMath::FloorToInt(FMath::Clamp(Context.Building->ConstructionProgress, 0.f, 1.f) * 100.f));
			else if (Context.Building->IsProducer())
				Selection << TEXT("  \u00B7  ") << StatusText(Context.Building->GetProductionState());
		}
		else Selection << TEXT("Nothing selected  \u00B7  click an owned building");
		Paint.Text(Selection.ToView(), X, Row2, 10.f, Palette::Text, false, EAlign::Left, TextWidth);
		Paint.DrawKey(KeysRight - KeysWidth, Row1, TEXT("F4"), TEXT("Show deck"));
		Paint.TextIn(TEXT("Use Construction to reopen"), {KeysRight - KeysWidth, Row2, KeysWidth, KeyHeight}, 8.f, Palette::Muted);
	}

	void DrawFeedback(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
	{
		const FRect& Strip = Layout.Feedback;
		Paint.Fill(Strip, FLinearColor(.015f, .022f, .03f, .86f));
		Paint.Fill({Strip.X, Strip.Y, 3.f, Strip.H}, Palette::Warn);
		Paint.TextIn(Context.Controller->GetOrderFeedback(), Strip, 10.f, FLinearColor(.98f, .88f, .66f), false, EAlign::Left, 12.f);
	}

	void DrawBanner(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
	{
		const FRect& Banner = Layout.Banner;
		const bool bVictory = Context.State->MatchResult == EMatchResult::Victory;
		const FLinearColor Accent = bVictory ? Palette::Good : Palette::Bad;
		Paint.Fill(Banner, FLinearColor(.01f, .015f, .022f, .93f));
		Paint.Fill({Banner.X, Banner.Y, Banner.W, 3.f}, Accent);
		Paint.Fill({Banner.X, Banner.Bottom() - 3.f, Banner.W, 3.f}, Accent);
		const float Center = Banner.Center().X;
		Paint.Text(bVictory ? TEXT("VICTORY") : TEXT("DEFEAT"), Center, Banner.Y + 12.f, 28.f, Accent, true, EAlign::Center);
		Paint.Text(bVictory ? TEXT("The enemy HQ has been destroyed.") : TEXT("Your HQ has been destroyed."),
			Center, Banner.Y + 56.f, 11.f, Palette::Text, false, EAlign::Center);
		constexpr const TCHAR* Label = TEXT("Start a fresh match  \u00B7  commands are locked");
		const float Width = Paint.KeyWidth(TEXT("Enter"), Label);
		Paint.DrawKey(Center - Width * .5f, Banner.Y + 82.f, TEXT("Enter"), Label);
	}
}

bool ACommandHUD::IsPanelPoint(const FVector2D& Position) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller) return false;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	if (Layout.Scale <= 0.f) return false;
	const FVector2D Point = Position / Layout.Scale;
	return Layout.Top.Contains(Point) || Layout.Minimap.Contains(Point) || Layout.Construction.Contains(Point)
		|| (Context.bExpanded && Layout.Build.Contains(Point)) || Layout.Bottom.Contains(Point)
		|| (Layout.bFeedback && Layout.Feedback.Contains(Point)) || (Layout.bBanner && Layout.Banner.Contains(Point));
}

EHUDAction ACommandHUD::GetActionAtScreenPosition(const FVector2D& Position) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller) return EHUDAction::None;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	return Layout.Scale > 0.f ? HitTest(Context, Layout, Position / Layout.Scale) : EHUDAction::None;
}

bool ACommandHUD::FindActionScreenPosition(EHUDAction Action, FVector2D& OutPosition) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller) return false;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	bool bFound = false;
	ForEachButton(Context, Layout, [&](const FButton& Button)
	{
		if (Button.Action == Action && Button.Available())
		{
			OutPosition = Button.Rect.Center() * Layout.Scale;
			bFound = true;
		}
	});
	return bFound;
}

bool ACommandHUD::GetMinimapScreenRect(FVector2D& OutOrigin, float& OutSize) const
{
	const ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller) return false;
	int32 Width, Height;
	Controller->GetViewportSize(Width, Height);
	const FLayout Layout = MakeLayout(MakeContext(Controller), Width, Height);
	if (Layout.Scale <= 0.f) return false;
	OutOrigin = FVector2D(Layout.Minimap.X, Layout.Minimap.Y) * Layout.Scale;
	OutSize = Layout.Minimap.W * Layout.Scale;
	return true;
}

bool ACommandHUD::GetMinimapWorldPosition(const FVector2D& Position, FVector& OutWorld) const
{
	FVector2D Origin;
	float Size;
	return GetMinimapScreenRect(Origin, Size)
		&& CommandMinimap::ScreenToWorld(AArenaBounds::Find(GetWorld()), Position, Origin, Size, OutWorld);
}


void ACommandHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !GEngine) return;
	ACommandPlayerController* Controller = Cast<ACommandPlayerController>(GetOwningPlayerController());
	if (!Controller) return;
	const FContext Context = MakeContext(Controller);
	const FLayout Layout = MakeLayout(Context, Canvas->ClipX, Canvas->ClipY);
	const UFont* Font = GEngine->GetSmallFont();
	if (Layout.Scale <= 0.f || !Font || !FEngineFontServices::IsInitialized()) return;
	const FPainter Paint{Canvas, Layout.Scale, Font, FEngineFontServices::Get().GetFontMeasure()};
	const FForces Forces = CountForces(Context);

	EHUDAction Hover = EHUDAction::None;
	float MouseX, MouseY;
	if (Controller->GetMousePosition(MouseX, MouseY))
		Hover = HitTest(Context, Layout, FVector2D(MouseX, MouseY) / Layout.Scale);

	DrawTopBar(Paint, Context, Forces, Layout);
	CommandMinimap::Draw(Canvas, Controller, FVector2D(Layout.Minimap.X, Layout.Minimap.Y) * Layout.Scale, Layout.Minimap.W * Layout.Scale);
	if (Context.bExpanded)
	{
		DrawBuildPanel(Paint, Context, Forces, Layout);
		Paint.Panel(Layout.Inspector);
		if (Context.Building) DrawBuildingInspector(Paint, Context, Forces, Layout.Inspector);
		else DrawOverview(Paint, Context, Forces, Layout.Inspector);
	}
	else DrawModeBar(Paint, Context, Layout);
	ForEachButton(Context, Layout, [&Paint, &Context, Hover](const FButton& Button)
	{
		DrawButton(Paint, Context, Button, Button.Action == Hover);
	});
	if (Layout.bFeedback) DrawFeedback(Paint, Context, Layout);
	if (Layout.bBanner) DrawBanner(Paint, Context, Layout);
}
