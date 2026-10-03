#include "HUDPanels.h"

namespace CommandHUDPanels
{
void DrawResearchCard(const FPainter& Paint, const FButton& Button, bool bHover)
{
	const EArmyDoctrine Choice = Button.Action == EHUDAction::ResearchSiege ? EArmyDoctrine::SiegeOptics
		: Button.Action == EHUDAction::ResearchRepairs                      ? EArmyDoctrine::FieldRepairs
																			: EArmyDoctrine::EntrenchedFrontline;
	const FRect& Rect = Button.Rect;
	const bool bOn = Button.Available();
	const FLinearColor Accent = Button.bActive ? Palette::Good : FLinearColor(.72f, .52f, 1.f);
	Paint.Fill(Rect, Button.bActive ? Tint(Palette::Good, .16f, .96f) : !bOn ? Palette::CardOff
			: bHover                                                         ? Palette::CardHover
																			 : Palette::Card);
	Paint.Fill({ Rect.X, Rect.Y, Rect.W, 3.f }, Accent.CopyWithNewOpacity(bOn || Button.bActive ? 1.f : .3f));
	Paint.Outline(Rect, Button.bActive ? Palette::Good.CopyWithNewOpacity(.8f) : bOn && bHover ? Accent
																							   : Palette::Edge);
	const float Inner = Rect.W - 20.f;
	FString Title(ResearchName(Choice));
	Title.ToUpperInline();
	Paint.Text(Title, Rect.X + 10.f, Rect.Y + 10.f, 10.5f, bOn || Button.bActive ? Palette::Text : Palette::Muted, true, EAlign::Left, Inner);
	const TCHAR* First = Choice == EArmyDoctrine::SiegeOptics ? TEXT("Siege range +25%")
		: Choice == EArmyDoctrine::FieldRepairs               ? TEXT("Units heal 5 HP/s after")
															  : TEXT("Frontline takes -25% damage");
	const TCHAR* Second = Choice == EArmyDoctrine::SiegeOptics ? TEXT("Outgoing damage -25%")
		: Choice == EArmyDoctrine::FieldRepairs                ? TEXT("5 s without move/fire/damage")
															   : TEXT("while stationary and holding");
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
	else
		DrawBlockReason(Paint, Button, Rect.X + 10.f, Baseline - Paint.Ascent(9.f), 9.f, Inner);
}

}
