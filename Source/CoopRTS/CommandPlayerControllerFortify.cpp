#include "CommandPlayerController.h"

#include "CommandGameState.h"
#include "Commands/AbilityCommandComponent.h"
#include "InputCoreTypes.h"
#include "MapRegion.h"
#include "Rules/ControllerInputPolicy.h"
#include "Rules/FortifyPolicy.h"
#include "WorldOverlay.h"

namespace
{
constexpr float RingSeconds = 1.2f;
constexpr float RingRadius = 3.f * GameplayConstants::CaptureRadius;
constexpr float DashLength = 140.f;
constexpr float DashGap = 90.f;

// The region outline at the height of the order overlay; Dashed breaks each edge into dashes.
void Outline(AWorldOverlay& Overlay, const AMapRegion& Region, FColor Color, float Width, bool bDashed)
{
	const int32 Count = Region.Polygon.Num();
	if (Count < 3)
		return;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D& A = Region.Polygon[Index];
		const FVector2D& B = Region.Polygon[(Index + 1) % Count];
		const FVector Start(A.X, A.Y, 13.f);
		const FVector End(B.X, B.Y, 13.f);
		if (!bDashed)
		{
			Overlay.Line(Start, End, Color, Width);
			continue;
		}
		const float Length = FVector::Dist(Start, End);
		const FVector Direction = (End - Start).GetSafeNormal();
		for (float At = 0.f; At < Length; At += DashLength + DashGap)
			Overlay.Line(Start + Direction * At, Start + Direction * FMath::Min(At + DashLength, Length), Color, Width);
	}
}
}

void ACommandPlayerController::ToggleFortifyTargeting()
{
	if (GetUIScreen() != ECommandScreen::Game)
		return;
	if (ControllerInputPolicy::FortifyStep(bFortifyTargeting, ControllerInputPolicy::EFortifyInput::HKey)
		== ControllerInputPolicy::EFortifyStep::Cancel)
	{
		CancelPointerMode();
		return;
	}
	if (!CanIssueGameplayCommand())
		return;
	CancelMode();
	bFortifyTargeting = true;
	bHUDExpanded = false;
	SetFeedback(TEXT("Fortify: LMB a region on ground or minimap; RMB/Esc cancels."));
}

FFortifyPreview ACommandPlayerController::GetFortifyPreview(const FVector2D& Position) const
{
	FFortifyPreview Preview;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return Preview;
	AActor* Structure = nullptr;
	FVector Location;
	const AMapRegion* Region = PickOrderTarget(Position, *State, Structure, Location) ? State->FindRegionAt(Location) : nullptr;
	Preview.Decision = FortifyPolicy::Evaluate(
		UAbilityCommandComponent::MakeFortifyInput(*State, GetPlayerState<ACommandPlayerState>(), Region));
	if (Region)
	{
		Preview.RegionIndex = Region->RegionIndex;
		Preview.RegionName = Region->DisplayName.ToString();
	}
	return Preview;
}

bool ACommandPlayerController::HandleFortifyClick(const FVector2D& Position)
{
	if (ControllerInputPolicy::FortifyStep(bFortifyTargeting, ControllerInputPolicy::EFortifyInput::LeftClick)
		!= ControllerInputPolicy::EFortifyStep::Cast)
		return false;
	const FFortifyPreview Preview = GetFortifyPreview(Position);
	if (!Preview.IsAllowed())
	{
		TStringBuilder<96> Reason;
		FortifyPolicy::AppendReason(Reason, Preview.Decision, Preview.RegionName);
		SetCommandFeedback(FString(Reason.ToView()), false);
		return true;
	}
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	for (AMapRegion* Region : State->Regions)
		if (IsValid(Region) && Region->RegionIndex == Preview.RegionIndex)
		{
			SetFeedback(TEXT("Fortify sent; awaiting server."));
			AbilityCommands->ServerCastFortify(Region);
			break;
		}
	return true;
}

void ACommandPlayerController::CompleteFortifyInput(const FString& Message, bool bAccepted)
{
	if (!ControllerInputPolicy::FortifyStaysArmed(bFortifyTargeting, bAccepted) && bFortifyTargeting)
	{
		bFortifyTargeting = false;
		bHUDExpanded = true;
	}
	SetCommandFeedback(Message, bAccepted);
}

void ACommandPlayerController::DrawFortifyOverlay(AWorldOverlay& Overlay) const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Commander = GetPlayerState<ACommandPlayerState>();
	if (!State || !Commander)
		return;
	const float Now = State->GetServerWorldTimeSeconds();
	int32 Hovered = INDEX_NONE;
	FortifyPolicy::FDecision HoveredDecision;
	float MouseX, MouseY;
	if (bFortifyTargeting && GetMousePosition(MouseX, MouseY))
	{
		const FFortifyPreview Preview = GetFortifyPreview(FVector2D(MouseX, MouseY));
		Hovered = Preview.RegionIndex;
		HoveredDecision = Preview.Decision;
	}
	for (const AMapRegion* Region : State->Regions)
	{
		if (!IsValid(Region))
			continue;
		if (bFortifyTargeting)
		{
			const FortifyPolicy::FDecision Decision = FortifyPolicy::Evaluate(
				UAbilityCommandComponent::MakeFortifyInput(*State, Commander, Region));
			if (Region->RegionIndex == Hovered)
				Outline(Overlay, *Region, !HoveredDecision.IsAccepted() ? FColor::Red : HoveredDecision.bRefresh ? FColor::Orange
																												  : FColor::Green,
					4.f, false);
			else
				Outline(Overlay, *Region, FortifyPolicy::IsValidTarget(Decision.Verdict) ? FColor(60, 230, 90) : FColor(60, 66, 72),
					2.f, FortifyPolicy::IsValidTarget(Decision.Verdict));
		}
		// A teammate's cast: one expanding ring at the region; the camera never moves.
		const FortifyPolicy::FRegionState Fortify = Region->GetFortify();
		const float Age = Now - (Fortify.ExpiresAt - FortifyPolicy::DurationSeconds);
		if (FortifyPolicy::IsActive(Fortify, Now) && Region->FortifyCaster != Commander->CommanderIndex
			&& Age >= 0.f && Age < RingSeconds)
			Overlay.Ring(State->GetRegionAnchor(Region->RegionIndex) + FVector(0.f, 0.f, 9.f),
				RingRadius * Age / RingSeconds, FColor::Cyan);
	}
}
