#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "VerbInputFixture.h"

namespace VerbInputTests
{
bool FScenario::StageRally()
{
	if (!ExerciseGroundRallyAndBox())
		return true;
	PC->SelectActor(nullptr);
	Camera->FocusOn(FVector::ZeroVector);
	CenterCursor();
	CameraBefore = Camera->GetActorLocation();
	Key(EKeys::Up, IE_Pressed);
	++Stage;
	return false;
}

bool FScenario::ExerciseMinimapRally()
{
	SaveSerials();
	PC->SelectActorWithModifiers(Producer, false, false);
	const FVector2D Point = Minimap(HostileRegionPoint);
	const FOrderInputPreview Preview = PC->GetOrderPreview(Point);
	if (!Check(PC->GetSelectedForces().IsEmpty() && PC->GetSelectedBuilding() == Producer
				&& Preview.IsAllowed() && Preview.Resolution == ForceOrderInput::EResolution::Rally
				&& Preview.RegionIndex == EnemyHome,
			TEXT("Producer without selected forces previews minimap region rally"))
		|| !Check(PC->HandleOrderClick(Point) && Producer->RallyRegionIndex == EnemyHome && Unchanged(),
			TEXT("Producer minimap right-click sets rally without ordering existing forces")))
		return false;
	Camera->FocusOn(State->GetRegionAnchor(Target));
	return true;
}

bool FScenario::ExerciseGroundRallyAndBox()
{
	FVector2D Ground = FVector2D::ZeroVector;
	bool bFound = false;
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const float Angle = Index * PI / 8.f;
		const FVector Location = State->GetRegionAnchor(Target)
			+ FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 350.f;
		if (RegionAt(State, Location) == Target && PC->ProjectWorldLocationToScreen(Location, Ground)
			&& !HUD->IsPanelPoint(Ground))
		{
			bFound = true;
			break;
		}
	}
	if (!Check(bFound, TEXT("Target polygon supplies an uncovered ground rally point with the producer deck and feedback visible")))
		return false;
	const FOrderInputPreview GroundPreview = PC->GetOrderPreview(Ground);
	if (!Check(GroundPreview.IsAllowed() && GroundPreview.Resolution == ForceOrderInput::EResolution::Rally
				&& GroundPreview.RegionIndex == Target,
			TEXT("Producer without selected forces previews ground region rally"))
		|| !Check(PC->HandleOrderClick(Ground) && Producer->RallyRegionIndex == Target && Unchanged(),
			TEXT("Producer ground right-click replaces minimap rally without ordering existing forces")))
		return false;
	SelectBoth();
	int32 Width = 0, Height = 0;
	PC->GetViewportSize(Width, Height);
	PC->SelectForceBox(FVector2D(Width, Height), FVector2D::ZeroVector, true);
	return Check(Unchanged(), TEXT("Reverse selection drag never creates or changes force orders"));
}
}
#endif
