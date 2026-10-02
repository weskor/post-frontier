#include "HUDPanels.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "DepositSite.h"
#include "CommandPlayerController.h"

namespace CommandHUDPanels
{
static void DrawCaptureSites(const FPainter& Paint, const FContext& Context)
{
	const float Line = Paint.LineHeight(10.f, true);
	for (const ACapturePoint* Site : Context.State->CaptureSites)
	{
		if (!IsValid(Site))
			continue;
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Site->GetActorLocation() + FVector(0.f, 0.f, 110.f), Screen))
			continue;
		const FRect Back{ Screen.X - 48.f, Screen.Y - Line - 18.f, 96.f, Line + 12.f };
		if (!OverlayFits(Paint, Back))
			continue;
		TStringBuilder<32> Label;
		Label.Appendf(TEXT("REGION %d"), Site->SiteIndex + 1);
		const FLinearColor OwnerColor = Site->ControllingTeam == 0 ? Palette::Good
			: Site->ControllingTeam == 5                           ? Palette::Bad
																   : Palette::Gold;
		const FLinearColor CaptureColor = Site->CaptureProgress > 0.f ? Palette::Good
			: Site->CaptureProgress < 0.f                             ? Palette::Bad
																	  : OwnerColor;
		Paint.Fill(Back, FLinearColor(.005f, .008f, .012f, .95f));
		Paint.TextIn(Label.ToView(), { Back.X, Back.Y + 2.f, Back.W, Line }, 10.f, OwnerColor, true, EAlign::Center);
		Paint.Bar({ Back.X + 4.f, Back.Y + Line + 4.f, Back.W - 8.f, 4.f },
			FMath::Abs(Site->CaptureProgress), CaptureColor);
	}
}

static void DrawDeposits(const FPainter& Paint, const FContext& Context)
{
	const float Line = Paint.LineHeight(10.f, true);
	for (const ADepositSite* Deposit : Context.State->Deposits)
	{
		if (!IsValid(Deposit))
			continue;
		FVector2D Screen;
		if (!ProjectOverlay(Paint, Context, Deposit->GetActorLocation() + FVector(0.f, 0.f, 100.f), Screen))
			continue;
		const FRect Back{ Screen.X - 74.f, Screen.Y, 148.f, Line + 16.f };
		if (!OverlayFits(Paint, Back))
			continue;
		const bool bTaken = IsValid(Deposit->Extractor);
		const bool bEmpty = Deposit->Remaining <= 0;
		const FLinearColor Color = bEmpty ? Palette::Muted : bTaken ? Palette::Gold
																	: Palette::Good;
		TStringBuilder<80> Label;
		Label.Appendf(TEXT("%s  %d  +%d/s  %s"), Deposit->bRich ? TEXT("RICH") : TEXT("POWER"),
			Deposit->Remaining, bEmpty ? 0 : Deposit->RatePerSecond(),
			bEmpty ? TEXT("EMPTY") : bTaken ? TEXT("TAKEN")
											: TEXT("FREE"));
		Paint.Fill(Back, FLinearColor(.005f, .008f, .012f, .95f));
		Paint.TextIn(Label.ToView(), Back, 8.f, Color, true, EAlign::Center);
	}
}

void DrawSectorOverlays(const FPainter& Paint, const FContext& Context)
{
	if (!Context.State)
		return;
	DrawCaptureSites(Paint, Context);
	DrawDeposits(Paint, Context);
}

}
