#include "Rules/ControllerInputPolicy.h"

bool ControllerInputPolicy::IsDoubleClick(double Now, double Previous)
{
	return Now - Previous <= DoubleClickSeconds;
}

bool ControllerInputPolicy::IsBuildHotkeyLive(double Now, double Started)
{
	return Now - Started < BuildHotkeyWindowSeconds;
}

float ControllerInputPolicy::FeedbackOpacity(double ElapsedSeconds)
{
	return FMath::Clamp(static_cast<float>(FeedbackSeconds - ElapsedSeconds), 0.f, 1.f);
}

FVector2D ControllerInputPolicy::EdgePanAxis(const FVector2D& Mouse, const FIntPoint& ViewportSize)
{
	if (Mouse.X < 0. || Mouse.Y < 0. || Mouse.X >= ViewportSize.X || Mouse.Y >= ViewportSize.Y)
		return FVector2D::ZeroVector;
	const auto Axis = [](double Position, int32 Extent) {
		return Position <= EdgePanMargin ? -1.f : Position >= Extent - EdgePanMargin ? 1.f
																					 : 0.f;
	};
	// Screen Y maps to forward with the top edge positive; screen X maps to right.
	return FVector2D(-Axis(Mouse.Y, ViewportSize.Y), Axis(Mouse.X, ViewportSize.X));
}

bool ControllerInputPolicy::GroundPoint(const FVector& Origin, const FVector& Direction, FVector& OutPoint)
{
	if (FMath::Abs(Direction.Z) < KINDA_SMALL_NUMBER)
		return false;
	const double Time = -Origin.Z / Direction.Z;
	if (Time <= 0. || !FMath::IsFinite(Time))
		return false;
	OutPoint = Origin + Direction * Time;
	return true;
}

ControllerInputPolicy::FPlacementWindow ControllerInputPolicy::PlacementWindow(int32 FootprintCells)
{
	FPlacementWindow Window;
	Window.WindowCells = FMath::Min(FootprintCells + WindowPaddingCells, MaxWindowCells);
	Window.Margin = (Window.WindowCells - FootprintCells) / 2;
	return Window;
}

int32 ControllerInputPolicy::AlertCycleSequence(TConstArrayView<int32> Sequences, int32 LatestSeenSequence, int32 FocusedSequence)
{
	if (Sequences.IsEmpty())
		return INDEX_NONE;
	int32 Index = Sequences.Num() - 1;
	if (LatestSeenSequence == Sequences.Last())
	{
		for (int32 Cursor = 0; Cursor < Sequences.Num(); ++Cursor)
		{
			if (Sequences[Cursor] == FocusedSequence)
			{
				Index = FMath::Max(0, Cursor - 1);
				break;
			}
		}
	}
	return Sequences[Index];
}

FVector2D ControllerInputPolicy::MinimapPoint(const FVector2D& WorldXY, const FVector2D& HalfExtent, const FVector2D& MapOrigin, double MapSize)
{
	return MapOrigin + FVector2D((WorldXY.Y + HalfExtent.Y) / (2. * HalfExtent.Y), (HalfExtent.X - WorldXY.X) / (2. * HalfExtent.X)) * MapSize;
}

double ControllerInputPolicy::MinimapDiamondDistance(const FVector2D& WorldDelta, const FVector2D& HalfExtent, double MapSize)
{
	return MapSize * (FMath::Abs(WorldDelta.Y) / (2. * HalfExtent.Y) + FMath::Abs(WorldDelta.X) / (2. * HalfExtent.X));
}

bool ControllerInputPolicy::IsWithinMarker(const FVector2D& Position, const FVector2D& Marker, double Radius)
{
	return FMath::Abs(Position.X - Marker.X) <= Radius && FMath::Abs(Position.Y - Marker.Y) <= Radius;
}

ControllerInputPolicy::EFortifyStep ControllerInputPolicy::FortifyStep(bool bArmed, bool bCastPending, EFortifyInput Input)
{
	switch (Input)
	{
	case EFortifyInput::HKey:
		return bArmed ? EFortifyStep::Cancel : EFortifyStep::Arm;
	case EFortifyInput::LeftClick:
		return !bArmed ? EFortifyStep::Ignore : bCastPending ? EFortifyStep::Wait
															  : EFortifyStep::Cast;
	}
	return EFortifyStep::Ignore;
}

bool ControllerInputPolicy::FortifyStaysArmed(bool bArmed, bool bAccepted)
{
	return bArmed && !bAccepted;
}
