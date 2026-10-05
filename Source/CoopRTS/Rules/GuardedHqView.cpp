#include "GuardedHqView.h"

GuardedHqView::EFeedKind GuardedHqView::Classify(FStringView EventId)
{
	if (EventId.EndsWith(TEXT("_node_lost")))
		return EFeedKind::NodeLost;
	if (EventId.EndsWith(TEXT("_hq_online")))
		return EFeedKind::BackOnline;
	if (EventId.EndsWith(TEXT("_emergency")))
		return EFeedKind::Emergency;
	return EFeedKind::Plain;
}

void GuardedHqView::AppendFeedTitle(FStringBuilderBase& Out, FStringView Text, EFeedKind Kind, int32 Number)
{
	if (Kind != EFeedKind::NodeLost && Kind != EFeedKind::BackOnline)
	{
		Out << Text;
		return;
	}
	// The announcer's sentence ends in a full stop that the number replaces.
	Out << (Text.EndsWith(TEXT(".")) ? Text.LeftChop(1) : Text);
	if (Kind == EFeedKind::NodeLost)
		Out.Appendf(TEXT(" \u00B7 %d left"), FMath::Max(0, Number));
	else
		Out.Appendf(TEXT(" at %d%% HP"), FMath::Clamp(Number, 0, 100));
}

GuardedHqView::FNodeMarks GuardedHqView::PlaceNodeMarks(FVector2D Hq, FVector2D Origin, double Size, int32 Count)
{
	FNodeMarks Marks;
	Marks.Count = FMath::Clamp(Count, 0, HqHoldPolicy::NodesPerHq);
	if (Marks.Count == 0)
		return Marks;
	const double Below = Hq.Y + MinimapNodeDrop;
	const bool bFitsBelow = Below + MinimapNodeRadius <= Origin.Y + Size;
	const double Y = bFitsBelow ? Below : Hq.Y - MinimapNodeDrop;
	const double Width = (Marks.Count - 1) * MinimapNodePitch;
	// Keep the whole row inside the square; a square too small for it keeps the row centred on the HQ.
	const double Low = Origin.X + MinimapNodeRadius + Width * .5, High = Origin.X + Size - MinimapNodeRadius - Width * .5;
	const double CentreX = Low <= High ? FMath::Clamp(Hq.X, Low, High) : Hq.X;
	for (int32 Index = 0; Index < Marks.Count; ++Index)
		Marks.Centre[Index] = FVector2D(CentreX - Width * .5f + Index * MinimapNodePitch, Y);
	return Marks;
}
