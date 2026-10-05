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

void GuardedHqView::AppendFeedTitle(FStringBuilderBase& Out, FStringView Text, FStringView EventId, int32 Number)
{
	const EFeedKind Kind = Classify(EventId);
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

namespace
{
using GuardedHqView::FNodeMarks;

// A centred row of Count marks at height Y, shifted to stay inside the square; a square too small for it keeps the row
// centred on the HQ.
FNodeMarks Row(FVector2D Hq, FVector2D Origin, double Size, int32 Count, double Y)
{
	FNodeMarks Marks;
	Marks.Count = Count;
	const double Width = (Count - 1) * GuardedHqView::MinimapNodePitch;
	const double Radius = GuardedHqView::MinimapNodeRadius;
	const double Low = Origin.X + Radius + Width * .5, High = Origin.X + Size - Radius - Width * .5;
	const double CentreX = Low <= High ? FMath::Clamp(Hq.X, Low, High) : Hq.X;
	for (int32 Index = 0; Index < Count; ++Index)
		Marks.Centre[Index] = FVector2D(CentreX - Width * .5 + Index * GuardedHqView::MinimapNodePitch, Y);
	return Marks;
}

int32 DepositHits(const FNodeMarks& Marks, TConstArrayView<FVector2D> Deposits)
{
	const double Reach = GuardedHqView::MinimapNodeRadius + GuardedHqView::MinimapDepositRadius + GuardedHqView::MinimapMarkGap;
	int32 Hits = 0;
	for (int32 Index = 0; Index < Marks.Count; ++Index)
		for (const FVector2D& Deposit : Deposits)
			if (FVector2D::Distance(Marks.Centre[Index], Deposit) < Reach)
			{
				++Hits;
				break;
			}
	return Hits;
}
}

GuardedHqView::FNodeMarks GuardedHqView::PlaceNodeMarks(FVector2D Hq, FVector2D Origin, double Size, int32 Count, TConstArrayView<FVector2D> Deposits)
{
	Count = FMath::Clamp(Count, 0, HqHoldPolicy::NodesPerHq);
	if (Count == 0)
		return FNodeMarks();
	const double Bottom = Origin.Y + Size;
	const double Candidates[] = { Hq.Y + MinimapNodeDrop, Hq.Y - MinimapNodeDrop, Hq.Y + MinimapNodeFarDrop, Hq.Y - MinimapNodeFarDrop };
	FNodeMarks Best = Row(Hq, Origin, Size, Count, Candidates[0]);
	int32 BestHits = INT32_MAX;
	for (const double Y : Candidates)
	{
		if (Y - MinimapNodeRadius < Origin.Y || Y + MinimapNodeRadius > Bottom)
			continue;
		const FNodeMarks Marks = Row(Hq, Origin, Size, Count, Y);
		const int32 Hits = DepositHits(Marks, Deposits);
		if (Hits < BestHits)
		{
			Best = Marks;
			BestHits = Hits;
		}
	}
	// Neither row fitting the square (a square smaller than the clearances) keeps the row below.
	return Best;
}
