#include "HUDPanels.h"
#include "CommandPlayerController.h"

namespace CommandHUDPanels
{
// HUD-owned wording for the typed production state.
const TCHAR* StatusText(EProductionState State)
{
	switch (State)
	{
	case EProductionState::MatchFinished:
		return TEXT("MATCH FINISHED");
	case EProductionState::NotProducer:
		return TEXT("NO BARRACKS");
	case EProductionState::UnderConstruction:
		return TEXT("UNDER CONSTRUCTION");
	case EProductionState::Unconfigured:
		return TEXT("UNCONFIGURED");
	case EProductionState::ForceUnavailable:
		return TEXT("FORCE UNAVAILABLE");
	case EProductionState::Paused:
		return TEXT("PAUSED");
	case EProductionState::ForceComplete:
		return TEXT("FORCE COMPLETE");
	case EProductionState::WalletUnavailable:
		return TEXT("WALLET UNAVAILABLE");
	case EProductionState::InsufficientResources:
		return TEXT("INSUFFICIENT RESOURCES");
	case EProductionState::DeploymentBlocked:
		return TEXT("DEPLOYMENT BLOCKED");
	default:
		return TEXT("PRODUCING");
	}
}
const TCHAR* OrderTitle(EForceVerb Verb)
{
	switch (Verb)
	{
	case EForceVerb::MoveHold:
		return TEXT("MOVE & HOLD");
	case EForceVerb::Attack:
		return TEXT("ATTACK");
	case EForceVerb::Retreat:
		return TEXT("RETREAT");
	default:
		return TEXT("UNKNOWN");
	}
}

FLinearColor OrderColor(EForceVerb Verb)
{
	switch (Verb)
	{
	case EForceVerb::MoveHold:
		return FLinearColor(.38f, .92f, .48f);
	case EForceVerb::Attack:
		return FLinearColor(1.f, .36f, .30f);
	case EForceVerb::Retreat:
		return FLinearColor(1.f, .86f, .32f);
	default:
		return Palette::Muted;
	}
}

const TCHAR* ForceStatusTitle(EForceStatus Status)
{
	switch (Status)
	{
	case EForceStatus::Marching:
		return TEXT("MARCHING");
	case EForceStatus::Holding:
		return TEXT("HOLDING");
	case EForceStatus::Withdrawing:
		return TEXT("WITHDRAWING");
	case EForceStatus::Retreating:
		return TEXT("RETREATING");
	case EForceStatus::Refilling:
		return TEXT("REFILLING");
	default:
		return TEXT("UNKNOWN");
	}
}

const TCHAR* ResearchName(EArmyDoctrine Doctrine)
{
	switch (Doctrine)
	{
	case EArmyDoctrine::SiegeOptics:
		return TEXT("Siege Optics");
	case EArmyDoctrine::FieldRepairs:
		return TEXT("Field Repairs");
	case EArmyDoctrine::EntrenchedFrontline:
		return TEXT("Entrenched Frontline");
	default:
		return TEXT("None");
	}
}

FLinearColor Tint(const FLinearColor& Accent, float Amount, float Alpha)
{
	return FLinearColor(.03f + Accent.R * Amount, .045f + Accent.G * Amount, .06f + Accent.B * Amount, Alpha);
}
bool ProjectOverlay(const FPainter& Paint, const FContext& Context, const FVector& Position, FVector2D& Screen)
{
	// UE projection rejects points behind the camera; all overlays use HUD-scaled coordinates.
	if (!Context.Controller->ProjectWorldLocationToScreen(Position, Screen))
		return false;
	Screen /= Paint.Scale;
	return true;
}

bool OverlayFits(const FPainter& Paint, const FRect& Rect)
{
	return Rect.X >= 0.f && Rect.Y >= 0.f
		&& Rect.Right() <= Paint.Canvas->ClipX / Paint.Scale
		&& Rect.Bottom() <= Paint.Canvas->ClipY / Paint.Scale;
}
void BlockReason(const FButton& Button, FStringBuilderBase& Reason)
{
	switch (Button.Block)
	{
	case EBlock::Terminal:
		Reason << TEXT("Match over.");
		break;
	case EBlock::Funds:
		Reason.Appendf(TEXT("Need %d more Power."), Button.Shortfall);
		break;
	case EBlock::ForceLocked:
		Reason << TEXT("Force type locked after Start.");
		break;
	case EBlock::Chosen:
		Reason << TEXT("Specialization locked: one per commander.");
		break;
	case EBlock::ForceCap:
		Reason << TEXT("Force cap reached. A production building must be gone before adding another.");
		break;
	default:
		break;
	}
}

float DrawBlockReason(const FPainter& Paint, const FButton& Button, float X, float Y, float Size, float MaxWidth,
	EAlign Align)
{
	TStringBuilder<128> Reason;
	BlockReason(Button, Reason);
	return Paint.Text(Reason.ToView(), X, Y, Size, Button.Block == EBlock::Funds ? Palette::Warn : Palette::Faint, false, Align, MaxWidth);
}
void DrawInspectorHeader(const FPainter& Paint, const FRect& Inspector, const FLinearColor& Accent, FStringView Title,
	FStringView Subtitle, int32 Owner, int32 Health, int32 MaxHealth, FStringView Status, const FLinearColor& StatusColor)
{
	Paint.Fill({ Inspector.X, Inspector.Y, 3.f, Inspector.H }, Accent);
	const float X = Inspector.X + Pad;
	const float Right = Inspector.Right() - Pad;
	const float TitleWidth = Paint.Text(Title, X, Inspector.Y + Pad - 2.f, 13.f, Palette::Text, true, EAlign::Left, Inspector.W * .5f);
	if (Owner >= 0)
		Paint.Fill({ X, Inspector.Y + Pad + 23.f, 9.f, 9.f }, AArmyUnit::GetCommanderColor(Owner));
	Paint.Text(Subtitle, X + (Owner >= 0 ? 14.f : 0.f), Inspector.Y + Pad + 19.f, 9.5f, Palette::Muted, false, EAlign::Left,
		Inspector.W * .55f);
	if (!Status.IsEmpty())
		Paint.Text(Status, Right, Inspector.Y + Pad - 1.f, 9.5f, StatusColor, true, EAlign::Right,
			Inspector.W - 2.f * Pad - TitleWidth - 20.f);
	if (MaxHealth > 0)
	{
		const FRect Bar{ Right - 190.f, Inspector.Y + Pad + 19.f, 190.f, 16.f };
		const float Fraction = static_cast<float>(Health) / MaxHealth;
		Paint.Bar(Bar, Fraction, (Fraction > .5f ? Palette::Good : Fraction > .25f ? Palette::Warn
																				   : Palette::Bad)
									 .CopyWithNewOpacity(.55f));
		TStringBuilder<32> Value;
		Value.Appendf(TEXT("HP  %d / %d"), Health, MaxHealth);
		Paint.TextIn(Value.ToView(), Bar, 8.f, Palette::Text, true, EAlign::Center);
	}
	Paint.Fill({ X, Inspector.Y + Pad + HeaderHeight - 8.f, Inspector.W - 2.f * Pad, 1.f }, Palette::Edge);
}

void ColumnLabel(const FPainter& Paint, const FRect& ColumnRect, FStringView Label, FStringView Detail,
	const FLinearColor& DetailColor)
{
	const float Width = Paint.Text(Label, ColumnRect.X, ColumnRect.Y, 8.5f, Palette::Muted, true);
	if (!Detail.IsEmpty())
		Paint.Text(Detail, ColumnRect.Right(), ColumnRect.Y, 8.5f, DetailColor, false, EAlign::Right, ColumnRect.W - Width - 8.f);
}

}
