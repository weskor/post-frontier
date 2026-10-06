#include "ArmyGroup.h"

#include "AIController.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MapRegion.h"
#include "Rules/LanePolicy.h"
#include "Rules/MarchSpeedPolicy.h"

// How a marching force stays together and spreads over a shared anchor: per-member speed, the fitted-slot cache
// and lanes. The rules live in Rules/MarchSpeedPolicy.h and Rules/LanePolicy.h.

void AArmyGroup::FitSlots(const ACommandGameState* State, FFittedSlots& Out) const
{
	FitSlotsAt(State ? State->FindRegionAt(Destination) : nullptr, Destination, Out);
}

void AArmyGroup::FitSlotsAt(const AMapRegion* Region, const FVector& Centre, FFittedSlots& Out) const
{
	const ArmyGroupPolicy::FFormation Shape = FormationShape();
	const bool bHit = FitCache.bValid && FitCache.Region == Region && FitCache.Centre == Centre
		&& FitCache.Shape.bProduced == Shape.bProduced && FitCache.Shape.Capacity == Shape.Capacity
		&& FitCache.Shape.bOpposing == Shape.bOpposing;
	if (!bHit)
	{
		const TConstArrayView<FVector2D> Polygon = Region ? TConstArrayView<FVector2D>(Region->Polygon) : TConstArrayView<FVector2D>();
		const ArmyGroupPolicy::FFit Fit = ArmyGroupPolicy::FitForce(Shape, Polygon, Centre);
		FitCache.bValid = true;
		FitCache.Region = Region;
		FitCache.Centre = Centre;
		FitCache.Shape = Shape;
		FitCache.Slots.Centre = Fit.Centre;
		FitCache.Slots.Goals.Reset();
		for (int32 Slot = 0; Slot < ArmyGroupPolicy::SlotCount(Shape); ++Slot)
			FitCache.Slots.Goals.Add(ArmyGroupPolicy::FittedSlot(Shape, Fit, Polygon, Slot));
	}
	Out = FitCache.Slots;
}

float AArmyGroup::GetFormationRadius() const
{
	float Radius = 0.f;
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive())
			Radius = FMath::Max(Radius, static_cast<float>(FormationOffset(Unit->GetCompositionSlot()).Size2D()));
	return Radius;
}

// Members in combat (pursuing) or wedged (settled) do not speak for the march, and are left out of the mean too.
float AArmyGroup::GetMarchSpread() const
{
	FVector Center = FVector::ZeroVector;
	int32 Count = 0;
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive() && !Unit->bPursuing && !IsUnitSettled(*Unit))
		{
			Center += Unit->GetActorLocation();
			++Count;
		}
	float Spread = 0.f;
	for (const AArmyUnit* Unit : Units)
		if (Count > 0 && IsValid(Unit) && Unit->IsAlive() && !Unit->bPursuing && !IsUnitSettled(*Unit))
			Spread = FMath::Max(Spread, static_cast<float>(FVector::Dist2D(Unit->GetActorLocation(), Center / Count)));
	return Spread;
}

// The force moves at its slowest member's speed; each member runs at that speed times its catch-up factor
// (MarchSpeedPolicy): behind its slot it hurries, ahead of it it eases. Only forces under way are adjusted, and a
// member in combat keeps the force's speed.
void AArmyGroup::UpdateMarchSpeed()
{
	const float Speed = GetMarchSpeed();
	TArray<AArmyUnit*, TInlineAllocator<8>> Members;
	TArray<MarchSpeedPolicy::FMember, TInlineAllocator<8>> Layout;
	FVector Center = FVector::ZeroVector;
	for (AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive())
		{
			Members.Add(Unit);
			Layout.Add({ FVector2D(Unit->GetActorLocation()), FVector2D(FormationOffset(Unit->GetCompositionSlot())) });
			Center += Unit->GetActorLocation();
		}
	const bool bUnderWay = Status == EForceStatus::Marching || Status == EForceStatus::Withdrawing || Status == EForceStatus::Retreating;
	FVector2D Heading = FVector2D::ZeroVector;
	if (bUnderWay && !Members.IsEmpty())
	{
		// Toward the destination, unless the force is already within the band of it.
		const FVector2D ToDestination = FVector2D(Destination) - FVector2D(Center / Members.Num());
		if (ToDestination.Size() > MarchSpeedPolicy::BandHalfWidth)
			Heading = ToDestination.GetSafeNormal();
	}
	TArray<float, TInlineAllocator<8>> Factors;
	MarchSpeedPolicy::Factors(Layout, Heading, Factors);
	for (int32 Index = 0; Index < Members.Num(); ++Index)
		Members[Index]->GetCharacterMovement()->MaxWalkSpeed = Speed * (Members[Index]->bPursuing ? 1.f : Factors[Index]);
}

bool AArmyGroup::ReleaseLaneIfUnserved(const ACommandGameState& State, const AMapRegion& Region, int32 RegionIndex)
{
	const bool bUnserved = LaneIndex > 0 && AppliedWaypoint == RegionIndex && Region.Anchor
		&& State.GetRegionController(RegionIndex) != TeamIndex
		&& !(TeamIndex == 0 ? Region.Anchor->bFriendlyPresent : Region.Anchor->bEnemyPresent)
		&& HasArrivedAtRegion(State, RegionIndex);
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bUnserved)
	{
		LaneWaitingSince = -1.f;
		return false;
	}
	if (LaneWaitingSince < 0.f)
		LaneWaitingSince = Now;
	if (Now - LaneWaitingSince < LanePolicy::ReleaseSeconds)
		return false;
	LaneIndex = 0;
	LaneWaitingSince = -1.f;
	AppliedWaypoint = INDEX_NONE;
	return true;
}

// A force already holding the region stands at its post, not on a lane, and frees its lane.
void AArmyGroup::AssignLane(int32 RegionIndex, const FVector& Anchor)
{
	TArray<int32, TInlineAllocator<16>> Used;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		if (*It != this && It->TeamIndex == TeamIndex && It->AppliedWaypoint == RegionIndex && It->LaneIndex != INDEX_NONE
			&& !It->IsHoldingRegion())
			Used.Add(It->LaneIndex);
	LaneIndex = LanePolicy::Allocate(Used);
	LaneWaypoint = RegionIndex;
	// The approach direction: from the route region before the waypoint to its anchor, so ranks lie on the side
	// the force comes from even when a leg crosses several regions. Without a route, from the force itself.
	LaneHeading = FVector2D::ZeroVector;
	if (const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>())
	{
		uint64 Graph[ForceOrders::MaxRegions];
		const int32 Count = ForceOrderGraph::ReadGraph(*State, Graph);
		int32 Previous = MarchSourceRegion(*State);
		for (int32 Step = 0; Step < Count && Previous != INDEX_NONE && Previous != RegionIndex; ++Step)
		{
			const int32 Next = ForceOrders::NextWaypoint(Graph, Count, Previous, RegionIndex);
			if (Next == RegionIndex || Next == INDEX_NONE || Next == Previous)
				break;
			Previous = Next;
		}
		if (Previous != INDEX_NONE && Previous != RegionIndex)
			LaneHeading = FVector2D(Anchor - State->GetRegionAnchor(Previous)).GetSafeNormal();
	}
	if (LaneHeading.IsNearlyZero())
		LaneHeading = FVector2D(Anchor - GetMarchCenter()).GetSafeNormal();
}

// Orders the force to its lane around the anchor: the whole lane offset first, then half of it, each only where
// the point lies in the region and the formation has complete paths there. False leaves the plain anchor order,
// which also copes with blocked slots, to the caller.
bool AArmyGroup::IssueOnLane(EArmyOrder Phase, const AMapRegion& Region, const FVector& Anchor)
{
	if (LaneIndex <= 0)
		return false;
	const FVector2D Offset = LanePolicy::Offset(LaneIndex, LaneHeading);
	for (const float Scale : { 1.f, .5f })
	{
		const FVector Point = Anchor + FVector(Offset * Scale, 0.f);
		if (Region.Contains(Point) && IssueTravel(Phase, Point))
			return true;
	}
	return false;
}
