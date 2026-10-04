#include "Rules/PlanningPolicy.h"

namespace PlanningPolicy
{
double Remaining(double Now, double Deadline)
{
	return FMath::Max(0., Deadline - Now);
}

EEnd Evaluate(int32 Humans, int32 ReadyHumans, double Now, double Deadline)
{
	if (Now >= Deadline)
		return EEnd::Expired;
	return Humans > 0 && ReadyHumans >= Humans ? EEnd::AllReady : EEnd::Continue;
}

bool MayEnd(bool bNavigationReady, double Now, double Deadline)
{
	return bNavigationReady || Now >= Deadline + NavigationGraceSeconds;
}

FRosterChange Reconcile(TConstArrayView<int32> KitSlots, TConstArrayView<int32> Roster)
{
	FRosterChange Change;
	for (int32 Slot : Roster)
		if (!KitSlots.Contains(Slot))
			Change.Join.AddUnique(Slot);
	for (int32 Slot : KitSlots)
		if (!Roster.Contains(Slot))
			Change.Leave.AddUnique(Slot);
	Change.Join.Sort();
	Change.Leave.Sort();
	return Change;
}

const TCHAR* EditRejection(bool bPlanning, bool bReady)
{
	if (!bPlanning)
		return TEXT("Planning is over.");
	return bReady ? TEXT("Ready locks your kit; press Enter to un-Ready first.") : nullptr;
}

int32 NearestRigSite(TConstArrayView<FRigSite> Sites, const FVector& From)
{
	int32 Best = INDEX_NONE;
	double BestDistance = 0.;
	for (int32 Index = 0; Index < Sites.Num(); ++Index)
	{
		if (!Sites[Index].bFree || !Sites[Index].bOwnTerritory)
			continue;
		const double Distance = FVector::DistSquared2D(Sites[Index].Position, From);
		if (Best == INDEX_NONE || Distance < BestDistance)
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	return Best;
}

FKitFill Fill(bool bBarracksPlaced, bool bRigPlaced, bool bRigSiteAvailable)
{
	FKitFill Need;
	Need.bPlaceBarracks = !bBarracksPlaced;
	Need.bPlaceRig = !bRigPlaced && bRigSiteAvailable;
	Need.bRefundRig = !bRigPlaced && !bRigSiteAvailable;
	return Need;
}

FVector DefaultBarracksSpot(const FVector& Home, int32 Team, int32 Index)
{
	const int32 Ring = Index / 32, Direction = Index % 32;
	const float Angle = Direction * PI / 16.f;
	const float Orientation = Team == 5 ? -1.f : 1.f;
	FVector Spot = Home + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * ((380.f + Ring * 160.f) * Orientation);
	Spot.Z = 5.f;
	return Spot;
}
}
