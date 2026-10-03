#include "CommandPlayerController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Components/BoxComponent.h"
#include "InputCoreTypes.h"
#include "MapRegion.h"
#include "Rules/ControllerInputPolicy.h"
#include "Rules/PlacementPolicy.h"
#include "WorldOverlay.h"

namespace
{
FColor VerbColor(EForceVerb Verb)
{
	return Verb == EForceVerb::Attack ? FColor::Red : Verb == EForceVerb::Retreat ? FColor::Yellow
																				  : FColor::Green;
}

void OutlineRegion(AWorldOverlay& Overlay, const AMapRegion* Region, FColor Color)
{
	if (!IsValid(Region) || Region->Polygon.Num() < 3)
		return;
	for (int32 Index = 0; Index < Region->Polygon.Num(); ++Index)
	{
		const FVector2D& A = Region->Polygon[Index];
		const FVector2D& B = Region->Polygon[(Index + 1) % Region->Polygon.Num()];
		Overlay.Line(FVector(A.X, A.Y, 13.f), FVector(B.X, B.Y, 13.f), Color, 3.f);
	}
}

const AMapRegion* FindRegion(const ACommandGameState* State, int32 RegionIndex)
{
	if (State)
		for (const AMapRegion* Region : State->Regions)
			if (IsValid(Region) && Region->RegionIndex == RegionIndex)
				return Region;
	return nullptr;
}
}

void ACommandPlayerController::DrawWorldOverlay(AWorldOverlay& Overlay) const
{
	if (bPlacingBuilding)
		DrawPlacementOverlay(Overlay);
	DrawRegionOverlay(Overlay);
	DrawSelectionOverlay(Overlay);
}

void ACommandPlayerController::DrawPlacementOverlay(AWorldOverlay& Overlay) const
{
	FVector Location;
	FString Reason;
	bool bCanPlace = false;
	const UBuildingDefinition* Placement = GetPlacementDefinition();
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!Placement || !State || !GetPlacementPreview(Location, Reason, bCanPlace))
		return;
	const float CellSize = PlacementPolicy::BuildGridCellSize;
	const int32 Cells = PlacementPolicy::FootprintCells(ACommandBuilding::GetFootprintRadius(*Placement));
	const ControllerInputPolicy::FPlacementWindow Window = ControllerInputPolicy::PlacementWindow(Cells);
	const int32 WindowCells = Window.WindowCells;
	const FVector FootprintMin = Location - FVector(Cells * CellSize * .5f, Cells * CellSize * .5f, 0.f);
	const FVector WindowMin = FootprintMin - FVector(Window.Margin * CellSize, Window.Margin * CellSize, 0.f);
	for (int32 X = 0; X < WindowCells; ++X)
		for (int32 Y = 0; Y < WindowCells; ++Y)
		{
			const FVector Center = WindowMin + FVector((X + .5f) * CellSize, (Y + .5f) * CellSize, 0.f);
			const bool bTerritory = State->IsInBuildTerritory(PlacementIndex, 0, Center);
			Overlay.Cell(Center + FVector(0.f, 0.f, 4.f),
				FVector2D(CellSize * .5f - 1.f, CellSize * .5f - 1.f),
				bTerritory ? FColor(20, 65, 30, 40) : FColor(65, 20, 20, 40));
		}
	for (int32 Line = 0; Line <= WindowCells; ++Line)
	{
		const FVector Offset = WindowMin + FVector(0.f, 0.f, 8.f);
		Overlay.Line(Offset + FVector(Line * CellSize, 0.f, 0.f),
			Offset + FVector(Line * CellSize, WindowCells * CellSize, 0.f), FColor(80, 100, 110), 1.f);
		Overlay.Line(Offset + FVector(0.f, Line * CellSize, 0.f),
			Offset + FVector(WindowCells * CellSize, Line * CellSize, 0.f), FColor(80, 100, 110), 1.f);
	}
	for (int32 X = 0; X < Cells; ++X)
		for (int32 Y = 0; Y < Cells; ++Y)
			Overlay.Cell(FootprintMin + FVector((X + .5f) * CellSize, (Y + .5f) * CellSize, 12.f),
				FVector2D(CellSize * .5f - 2.f, CellSize * .5f - 2.f),
				bCanPlace ? FColor(0, 220, 45, 180) : FColor(240, 25, 20, 180));
}

void ACommandPlayerController::DrawRegionOverlay(AWorldOverlay& Overlay) const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const AMapRegion* HoveredRegion = nullptr;
	float MouseX, MouseY;
	if (bAssigningOrder && !SelectedForces.IsEmpty() && GetMousePosition(MouseX, MouseY))
	{
		const int32 RegionIndex = GetOrderPreview(FVector2D(MouseX, MouseY),
			IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift))
									  .RegionIndex;
		HoveredRegion = FindRegion(State, RegionIndex);
	}
	if (IsOwnedBuilding(SelectedBuilding) && SelectedBuilding->IsProducer() && IsValid(SelectedBuilding->ForceGroup))
	{
		const AMapRegion* Target = FindRegion(State, SelectedBuilding->ForceGroup->TargetRegionIndex);
		if (Target && Target != HoveredRegion)
			OutlineRegion(Overlay, Target, VerbColor(SelectedBuilding->ForceGroup->Verb));
	}
	OutlineRegion(Overlay, HoveredRegion, FColor::Cyan);
}

void ACommandPlayerController::DrawSelectionOverlay(AWorldOverlay& Overlay) const
{
	if (IsValid(SelectedBuilding) && SelectedBuilding->GetDefinition())
	{
		if (const UBoxComponent* Footprint = Cast<UBoxComponent>(SelectedBuilding->GetRootComponent()))
		{
			const FVector Extent = Footprint->GetScaledBoxExtent();
			Overlay.Square(Footprint->GetComponentLocation() + FVector(0.f, 0.f, 10.f - Extent.Z),
				FVector2D(Extent.X + 30.f, Extent.Y + 30.f), FColor::Cyan, 3.f);
		}
	}
	for (const AArmyGroup* Force : SelectedForces)
		if (IsOwnedForce(Force))
			for (const AArmyUnit* Unit : Force->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive())
					Overlay.Square(Unit->GetActorLocation() + FVector(0.f, 0.f, -80.f),
						FVector2D(55.f, 55.f), FColor::Cyan, 2.f);
	if (GetNetMode() == NM_DedicatedServer || !IsOwnedBuilding(SelectedBuilding)
		|| !SelectedBuilding->IsProducer() || !IsValid(SelectedBuilding->ForceGroup))
		return;
	const AArmyGroup* Force = SelectedBuilding->ForceGroup;
	const FVector Center = Force->GetCenter() + FVector(0.f, 0.f, 24.f);
	Overlay.Line(SelectedBuilding->GetActorLocation() + FVector(0.f, 0.f, 24.f), Center, FColor::Cyan, 2.f);
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (State && Force->TargetRegionIndex != INDEX_NONE)
		Overlay.Line(Center, State->GetRegionAnchor(Force->TargetRegionIndex) + FVector(0.f, 0.f, 24.f),
			VerbColor(Force->Verb), 2.f);
}
