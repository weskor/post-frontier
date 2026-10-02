#include "HUDPanels.h"
#include "Content/MatchContent.h"

namespace CommandHUDPanels
{
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
	const TCHAR* Keys = TEXT("QWERTASDFGZXCVB");
	TStringBuilder<64> Label;
	Label.Appendf(TEXT("B %c  %s"), Keys[BuildSlot(Button.Action)], *Title);
	Paint.Text(Label.ToView(), Rect.X + 8.f, Rect.Y + 4.f, 10.f, Palette::Text, true, EAlign::Left, Rect.W - 16.f);
	TStringBuilder<48> Detail;
	Detail.Appendf(TEXT("%d  /  %.0fs"), Definition->BuildCost, Definition->BuildDuration);
	if (Definition->bRequiresDeposit)
		Detail << TEXT("  /  deposit");
	Paint.Text(Detail.ToView(), Rect.X + 8.f, Rect.Y + 21.f, 9.f, Palette::Gold);
	if (!Button.Available())
		DrawBlockReason(Paint, Button, Rect.X + 8.f, Rect.Y + 34.f, 9.f, Rect.W - 16.f);
}
void DrawBuildPanel(const FPainter& Paint, const FLayout& Layout)
{
	Paint.Panel(Layout.Build);
	const TCHAR* Categories[] = { TEXT("PRODUCTION"), TEXT("ECONOMY"), TEXT("TECH") };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Categories); ++Index)
	{
		const FRect Button = BuildCard(Layout.Build, Index, UE_ARRAY_COUNT(Categories));
		Paint.Text(Categories[Index], Button.X, Layout.Build.Y + 9.f, 9.f, Palette::Muted, true);
	}
}

}
