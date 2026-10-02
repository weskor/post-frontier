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
	const float TextScale = FMath::Min(1.f, Rect.H / 40.f);
	Paint.Fill(Rect, !Button.Available() ? Palette::CardOff : bHover ? Palette::CardHover
																	 : Palette::Card);
	Paint.Fill({ Rect.X, Rect.Y, 3.f, Rect.H }, Definition->Accent);
	const FString Title = Definition->DisplayName.ToString().ToUpper();
	Paint.Text(Title, Rect.X + 8.f, Rect.Y + 4.f * TextScale, 10.f * TextScale, Palette::Text, true, EAlign::Left, Rect.W - 16.f);
	TStringBuilder<48> Detail;
	Detail.Appendf(TEXT("%d  /  %.0fs"), Definition->BuildCost, Definition->BuildDuration);
	if (Definition->bRequiresDeposit)
		Detail << TEXT("  /  deposit");
	if (Button.Available())
		Paint.Text(Detail.ToView(), Rect.X + 8.f, Rect.Y + 21.f * TextScale, 9.f * TextScale, Palette::Gold);
	else
		DrawBlockReason(Paint, Button, Rect.X + 8.f, Rect.Y + 21.f * TextScale, 9.f * TextScale, Rect.W - 16.f);
}
void DrawBuildPanel(const FPainter& Paint, const FLayout& Layout)
{
	Paint.Panel(Layout.Build);
}

}
