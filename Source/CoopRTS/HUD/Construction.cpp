#include "HUDPanels.h"
#include "CommandPlayerController.h"
#include "Content/MatchContent.h"

namespace CommandHUDPanels
{
const FBuildHotkey BuildHotkeys[6] = {
	{ EKeys::Q, TEXT("Q"), EHUDAction::BuildSlot0 },
	{ EKeys::W, TEXT("W"), EHUDAction::BuildSlot1 },
	{ EKeys::E, TEXT("E"), EHUDAction::BuildSlot2 },
	{ EKeys::R, TEXT("R"), EHUDAction::BuildSlot3 },
	{ EKeys::T, TEXT("T"), EHUDAction::BuildSlot4 },
	{ EKeys::A, TEXT("A"), EHUDAction::BuildSlot5 }
};

void DrawBuildCard(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover)
{
	const UMatchContent* Content = MatchContent(Context);
	const UBuildingDefinition* Definition = Content ? Content->Building(BuildSlot(Button.Action)) : nullptr;
	if (!Definition)
		return;
	const FRect& Rect = Button.Rect;
	Paint.Fill(Rect, !Button.Available() ? Palette::CardOff : bHover ? Palette::CardHover
																	 : Palette::Card);
	if (Button.bActive)
		Paint.Outline(Rect, Definition->Accent);
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Definition->Accent);
	const FString Title = Definition->DisplayName.ToString().ToUpper();
	TStringBuilder<64> Label;
	Label.Appendf(TEXT("B %s  %s"), BuildHotkeys[BuildSlot(Button.Action)].Letter, *Title);
	Paint.Text(Label.ToView(), Rect.X + 8.f, Rect.Y + 4.f, 10.f, Palette::Text, true, EAlign::Left, Rect.W - 16.f);
	TStringBuilder<48> Detail;
	Detail.Appendf(TEXT("%d  /  %.0fs"), Definition->BuildCost, Definition->BuildDuration);
	if (Definition->bRequiresDeposit)
		Detail << TEXT("  /  deposit");
	Paint.Text(Detail.ToView(), Rect.X + 8.f, Rect.Y + 21.f, 9.f, Palette::Gold);
	if (!Button.Available())
		DrawBlockReason(Paint, Button, Rect.X + 8.f, Rect.Y + 34.f, 9.f, Rect.W - 16.f);
}
void DrawBuildPanel(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	Paint.Panel(Layout.Build);
	const bool bHotkeyPending = Context.Controller && Context.Controller->IsBuildHotkeyPending();
	const float Right = Layout.Build.Right() - Pad;
	const float PendingWidth = bHotkeyPending ? Paint.Text(TEXT("B ACTIVE"), Right, Layout.Build.Y + 9.f, 9.f,
													Palette::Gold, true, EAlign::Right)
											  : 0.f;
	if (bHotkeyPending)
		Paint.Outline(Layout.Build, Palette::Gold);
	const UMatchContent* Content = MatchContent(Context);
	const int32 BuildCount = Content ? FMath::Min(Content->Buildings.Num(), static_cast<int32>(UE_ARRAY_COUNT(BuildHotkeys))) : 0;
	for (int32 Index = 0; Index < BuildCount; ++Index)
	{
		const UBuildingDefinition* Definition = Content->Building(Index);
		if (!Definition)
			continue;
		const EBuildingKind Kind = Definition->GetKind();
		const TCHAR* Category = Kind == EBuildingKind::Barracks ? TEXT("PRODUCTION")
			: Kind == EBuildingKind::Extractor                  ? TEXT("ECONOMY")
																: TEXT("TECH");
		const FRect Button = BuildCard(Layout.Build, Index, BuildCount);
		const float CategoryRight = bHotkeyPending ? FMath::Min(Button.Right(), Right - PendingWidth - Gap) : Button.Right();
		if (CategoryRight > Button.X)
			Paint.Text(Category, Button.X, Layout.Build.Y + 9.f, 9.f, Palette::Muted, true, EAlign::Left, CategoryRight - Button.X);
	}
}

}
