#include "ArmyUnit.h"
#include "CombatTarget.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "EngineUtils.h"
#include "GroundHeight.h"
#include "MapPresentation.h"
#include "MapRegion.h"
#include "WorldOverlay.h"

namespace
{
using namespace MapPresentation;

constexpr double GroundCellCm = 50.;
constexpr double BorderStepCm = 250.;
constexpr double HatchStepCm = 300.;
constexpr float HatchSpacingCm = 320.f;
constexpr double CableStepCm = 200.;
constexpr double BorderLift = 14., CableLift = 18.;
constexpr double DashCm = 200., DashGapCm = 130.;
// A snapped cable leaves a stub this long on each side of the break, with this gap between the stubs.
constexpr double StubCm = 520., BreakGapCm = 260.;
constexpr float BorderWidth = 7.f, FlashBorderWidth = 12.f, HatchWidth = 3.f, CableWidth = 6.f;

const FColor CutRed(255, 72, 56);
const FColor HatchRed(255, 72, 56, 70);
const FColor FlashHatch(255, 72, 56, 140);
const FColor Dimmed(150, 158, 168);
const FColor FriendlyCable(64, 184, 163);
const FColor HostileCable(230, 92, 82);
const FColor PulseCyan(120, 230, 255);
const FColor SparkWhite(255, 244, 190);

FColor Faded(const FColor& Color, float Alpha)
{
	return FColor(Color.R, Color.G, Color.B, static_cast<uint8>(FMath::Clamp(Alpha, 0.f, 1.f) * Color.A));
}

// Walks a polyline in Dash/Gap pieces; Closed adds the edge from the last point to the first.
void Dashed(AWorldOverlay& Overlay, TConstArrayView<FVector> Points, int32 First, int32 Last, bool bClosed, const FColor& Color,
	float Width)
{
	double Phase = 0.;
	const int32 Count = Points.Num();
	const int32 Edges = bClosed ? Count : Last - First;
	for (int32 Edge = 0; Edge < Edges; ++Edge)
	{
		const FVector& A = Points[(First + Edge) % Count];
		const FVector& B = Points[(First + Edge + 1) % Count];
		const double Length = FVector::Dist(A, B);
		if (Length <= UE_SMALL_NUMBER)
			continue;
		for (double At = 0.; At < Length;)
		{
			const bool bDash = Phase < DashCm;
			const double Step = FMath::Min(Length - At, (bDash ? DashCm : DashCm + DashGapCm) - Phase);
			if (bDash)
				Overlay.Line(FMath::Lerp(A, B, At / Length), FMath::Lerp(A, B, (At + Step) / Length), Color, Width);
			At += Step;
			Phase += Step;
			if (Phase >= DashCm + DashGapCm)
				Phase = 0.;
		}
	}
}

void DrawSpark(AWorldOverlay& Overlay, const FVector& Center, double Reach, const FColor& Color, float Width)
{
	for (const FVector& Axis : { FVector(1., 0., 0.), FVector(0., 1., 0.), FVector(0., 0., 1.), FVector(.7, .7, 0.), FVector(.7, -.7, 0.) })
		Overlay.Line(Center - Axis * Reach, Center + Axis * Reach, Color, Width);
}

const FColor& TeamCable(int32 Team)
{
	return Team == 5 ? HostileCable : FriendlyCable;
}
}

double FMapPresentationWorld::GroundAt(const UWorld& World, double X, double Y, double Fallback)
{
	const FIntPoint Cell(FMath::RoundToInt(X / GroundCellCm), FMath::RoundToInt(Y / GroundCellCm));
	if (const float* Known = Ground.Find(Cell))
		return *Known;
	const float Z = static_cast<float>(GroundHeight::At(World, Cell.X * GroundCellCm, Cell.Y * GroundCellCm, Fallback));
	Ground.Add(Cell, Z);
	return Z;
}

const FMapPresentationWorld::FRegionGeometry& FMapPresentationWorld::Geometry(const UWorld& World, const AMapRegion& Region)
{
	if (const FRegionGeometry* Known = Regions.Find(Region.RegionIndex))
		return *Known;
	FRegionGeometry& Out = Regions.Add(Region.RegionIndex);
	const double Fallback = Region.GetActorLocation().Z;
	const auto Lift = [&](double X, double Y) { return FVector(X, Y, GroundAt(World, X, Y, Fallback) + BorderLift); };
	for (int32 Index = 0; Index < Region.Polygon.Num(); ++Index)
	{
		const FVector2D& A = Region.Polygon[Index];
		const FVector2D& B = Region.Polygon[(Index + 1) % Region.Polygon.Num()];
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / BorderStepCm));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const FVector2D P = FMath::Lerp(A, B, static_cast<double>(Step) / Steps);
			Out.Border.Add(Lift(P.X, P.Y));
		}
	}
	ForEachHatch(Region.Polygon, HatchSpacingCm, [&](const FVector2D& A, const FVector2D& B) {
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(A, B) / HatchStepCm));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const FVector2D From = FMath::Lerp(A, B, static_cast<double>(Step) / Steps);
			const FVector2D To = FMath::Lerp(A, B, static_cast<double>(Step + 1) / Steps);
			Out.Hatch.Add(Lift(From.X, From.Y));
			Out.Hatch.Add(Lift(To.X, To.Y));
		}
	});
	return Out;
}

const FMapPresentationWorld::FCableGeometry& FMapPresentationWorld::Cable(const UWorld& World, const ACommandGameState& State,
	const AMapRegion& From, const AMapRegion& To)
{
	const uint32 Key = (static_cast<uint32>(From.RegionIndex) << 16) | static_cast<uint32>(To.RegionIndex);
	if (const FCableGeometry* Known = Cables.Find(Key))
		return *Known;
	FCableGeometry& Out = Cables.Add(Key);
	const FVector A = State.GetRegionAnchor(From.RegionIndex), B = State.GetRegionAnchor(To.RegionIndex);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::Dist2D(A, B) / CableStepCm));
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const FVector P = FMath::Lerp(A, B, static_cast<double>(Step) / Steps);
		Out.Path.Add(FVector(P.X, P.Y, GroundAt(World, P.X, P.Y, P.Z) + CableLift));
		// The break is the first sample outside the region the cable starts in: the border lies just before it.
		if (Out.Break == INDEX_NONE && !From.Contains(P))
			Out.Break = Step;
	}
	if (Out.Break == INDEX_NONE)
		Out.Break = Steps / 2;
	return Out;
}

void FMapPresentationWorld::DrawCuts(AWorldOverlay& Overlay, const ACommandGameState& State, int32 Team)
{
	const float Age = MapView::FlashAge(State, Team);
	const bool bLit = MapPresentation::FlashLit(Age);
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region) || !MapView::IsCutOff(State, Team, Region->RegionIndex))
			continue;
		const FRegionGeometry& Shape = Geometry(*State.GetWorld(), *Region);
		if (bLit)
		{
			for (int32 Index = 0; Index < Shape.Border.Num(); ++Index)
				Overlay.Line(Shape.Border[Index], Shape.Border[(Index + 1) % Shape.Border.Num()], CutRed, FlashBorderWidth);
		}
		else
			Dashed(Overlay, Shape.Border, 0, Shape.Border.Num(), true, CutRed, BorderWidth);
		for (int32 Index = 0; Index + 1 < Shape.Hatch.Num(); Index += 2)
			Overlay.Line(Shape.Hatch[Index], Shape.Hatch[Index + 1], bLit ? FlashHatch : HatchRed, HatchWidth);
	}
}

void FMapPresentationWorld::DrawCables(AWorldOverlay& Overlay, const ACommandGameState& State, int32 Team)
{
	const float Age = MapView::FlashAge(State, Team);
	MapView::ForEachCable(State, Team, [&](const AMapRegion& From, const AMapRegion& To, ECable Kind) {
		const FCableGeometry& Geo = Cable(*State.GetWorld(), State, From, To);
		const int32 Last = Geo.Path.Num() - 1;
		if (Kind == ECable::Live)
		{
			for (int32 Index = 0; Index < Last; ++Index)
				Overlay.Line(Geo.Path[Index], Geo.Path[Index + 1], TeamCable(Team), CableWidth);
		}
		else if (Kind == ECable::Beyond)
			Dashed(Overlay, Geo.Path, 0, Last, false, Dimmed, CableWidth);
		else
		{
			// Stubs either side of the break; the cut-off region's end is dimmed, the far one keeps team colour.
			const bool bCutFirst = MapView::IsCutOff(State, Team, From.RegionIndex);
			const int32 Stub = FMath::Max(1, FMath::RoundToInt(StubCm / CableStepCm));
			const int32 Gap = FMath::Max(1, FMath::RoundToInt(BreakGapCm / CableStepCm / 2.));
			const int32 Before = FMath::Clamp(Geo.Break - Gap, 0, Last), After = FMath::Clamp(Geo.Break + Gap, 0, Last);
			const int32 FirstEnd = FMath::Max(0, Before - Stub), SecondEnd = FMath::Min(Last, After + Stub);
			Dashed(Overlay, Geo.Path, FirstEnd, Before, false, bCutFirst ? Dimmed : Faded(TeamCable(Team), .55f), CableWidth);
			Dashed(Overlay, Geo.Path, After, SecondEnd, false, bCutFirst ? Faded(TeamCable(Team), .55f) : Dimmed, CableWidth);
			const float Spark = MapPresentation::SparkAlpha(Age);
			if (Spark > 0.f)
				DrawSpark(Overlay, Geo.Path[Geo.Break], 150. * (.5 + .5 * Spark), Faded(SparkWhite, Spark), 5.f);
		}
	});
}

void FMapPresentationWorld::DrawPulses(AWorldOverlay& Overlay, const ACommandGameState& State)
{
	const UWorld* World = State.GetWorld();
	const float Now = State.GetServerWorldTimeSeconds();
	for (const AArmyUnit* Scrambler : TActorRange<AArmyUnit>(World))
	{
		const UArmyUnitDefinition* Definition = Scrambler->GetDefinition();
		const float Radius = Definition ? Definition->PulseRadius : 0.f;
		const float Age = Radius > 0.f ? PulseAge(Now, static_cast<float>(Scrambler->GetLastPulseServerTime())) : -1.f;
		if (Age < 0.f)
			continue;
		const FVector Origin = Scrambler->GetActorLocation();
		const FVector Floor(Origin.X, Origin.Y, GroundAt(*World, Origin.X, Origin.Y, Origin.Z - 90.) + BorderLift);
		const FPulseRing Ring = PulseRing(Age, Radius);
		if (Ring.bActive && Ring.Radius > 1.f)
		{
			Overlay.Ring(Floor, Ring.Radius, Faded(PulseCyan, Ring.Alpha), 9.f);
			Overlay.Ring(Floor, FMath::Max(1.f, Ring.Radius - 14.f), Faded(PulseCyan, Ring.Alpha * .5f), 4.f);
		}
		const int32 Team = Scrambler->GetTeamIndex();
		for (const AArmyUnit* Other : TActorRange<AArmyUnit>(World))
		{
			// A unit with no shield draws no bar, so it has nothing to spark.
			if (Other->MaxShield() <= 0 || !CombatTarget::IsAliveHostile(Other, Team))
				continue;
			const float Spark = PulseSparkAlpha(Age, FVector::Dist2D(Origin, Other->GetActorLocation()), Radius);
			if (Spark > 0.f)
				DrawSpark(Overlay, Other->GetActorLocation() + FVector(0., 0., 80.), 55., Faded(SparkWhite, Spark), 4.f);
		}
		for (const ACommandBuilding* Building : State.Buildings)
		{
			const UBuildingDefinition* BuildingDefinition = Building ? Building->GetDefinition() : nullptr;
			if (!BuildingDefinition || !CombatTarget::IsAliveHostile(Building, Team))
				continue;
			const float Distance = FMath::Max(0.f,
				FVector::Dist2D(Origin, Building->GetActorLocation()) - ACommandBuilding::GetFootprintRadius(*BuildingDefinition));
			const float Spark = PulseSparkAlpha(Age, Distance, Radius);
			if (Spark > 0.f)
				DrawSpark(Overlay, Building->GetActorLocation() + FVector(0., 0., 140.), 90., Faded(SparkWhite, Spark), 6.f);
		}
	}
}

void FMapPresentationWorld::Draw(AWorldOverlay& Overlay, const ACommandGameState& State)
{
	for (const int32 Team : MapView::Teams)
	{
		DrawCables(Overlay, State, Team);
		DrawCuts(Overlay, State, Team);
	}
	DrawPulses(Overlay, State);
}
