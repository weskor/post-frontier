#include "CommandPlayerController.h"

#include "ArenaBounds.h"
#include "CombatTarget.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "Commands/OrderCommandComponent.h"
#include "Commands/OrderGraph.h"
#include "GroundHeight.h"
#include "Headquarters.h"
#include "InputCoreTypes.h"
#include "MapRegion.h"
#include "Rules/ControllerInputPolicy.h"

using namespace ForceOrderInput;

AActor* ACommandPlayerController::PickMinimapStructure(const ACommandHUD& HUD, const FVector2D& Position, const ACommandGameState& State, int32 Team) const
{
	// Use the drawn minimap symbol bounds, in screen pixels. HQ is drawn last.
	FVector2D Origin;
	float Size;
	const AArenaBounds* Arena = AArenaBounds::Find(GetWorld());
	if (!Arena || !HUD.GetMinimapScreenRect(Origin, Size))
		return nullptr;
	AActor* Picked = nullptr;
	const auto Pick = [&](AActor* Actor, double Radius) {
		if (!CombatTarget::IsAliveHostile(Actor, Team))
			return;
		const FVector World = Actor->GetActorLocation();
		const FVector2D Point = ControllerInputPolicy::MinimapPoint(FVector2D(World.X, World.Y), Arena->HalfExtent, Origin, Size);
		if (ControllerInputPolicy::IsWithinMarker(Position, Point, Radius))
			Picked = Actor;
	};
	for (ACommandBuilding* Building : State.Buildings)
		Pick(Building, 3.);
	Pick(State.FriendlyHeadquarters, 6.);
	Pick(State.EnemyHeadquarters, 6.);
	return Picked;
}

bool ACommandPlayerController::PickOrderTarget(const FVector2D& Position, const ACommandGameState& State, AActor*& Structure, FVector& Location) const
{
	const ACommandHUD* HUD = Cast<ACommandHUD>(GetHUD());
	const ACommandPlayerState* Commander = GetPlayerState<ACommandPlayerState>();
	const int32 Team = Commander ? Commander->TeamIndex : 0;
	if (HUD && HUD->GetMinimapWorldPosition(Position, Location))
	{
		Structure = PickMinimapStructure(*HUD, Position, State, Team);
		return true;
	}
	if (HUD && HUD->IsPanelPoint(Position))
		return false;
	FHitResult Hit;
	if (GetHitResultAtScreenPosition(Position, ECC_Visibility, false, Hit)
		&& (Cast<ACommandBuilding>(Hit.GetActor()) || Cast<AHeadquarters>(Hit.GetActor()))
		&& CombatTarget::IsAliveHostile(Hit.GetActor(), Team))
		Structure = Hit.GetActor();
	FVector RayOrigin, Direction;
	return DeprojectScreenPositionToWorld(Position.X, Position.Y, RayOrigin, Direction)
		&& (GroundHeight::Ray(*GetWorld(), RayOrigin, Direction, Location)
			|| ControllerInputPolicy::GroundPoint(RayOrigin, Direction, Location));
}

FOrderInputPreview ACommandPlayerController::GetOrderPreview(const FVector2D& Position, bool bQueue) const
{
	FOrderInputPreview Preview;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Commander = GetPlayerState<ACommandPlayerState>();
	FContext Context;
	Context.bAvailable = State && State->MatchResult == EMatchResult::Ongoing && IsValid(Commander)
		&& Commander->TeamIndex == 0 && Commander->CommanderIndex >= 0 && Commander->CommanderIndex < 5
		&& GetUIScreen() == ECommandScreen::Game && !bPlacingBuilding && !IsBuildHotkeyPending();
	Context.bAttack = bAssigningOrder;
	Context.bQueue = bQueue;
	Context.bProducerSelected = IsOwnedBuilding(SelectedBuilding) && SelectedBuilding->IsProducer();
	uint64 Graph[ForceOrders::MaxRegions];
	TArray<FForce, TInlineAllocator<5>> Forces;
	if (State)
	{
		Context.Graph = MakeArrayView(Graph, ForceOrderGraph::ReadGraph(*State, Graph));
		for (const AArmyGroup* Force : SelectedForces)
			Forces.Add({ IsOwnedForce(Force), IsValid(Force) ? ForceOrderGraph::SourceRegion(*Force, *State) : INDEX_NONE,
				IsValid(Force) ? Force->Orders.Num() : 0 });
		Context.Forces = Forces;
		if (Context.bProducerSelected)
		{
			const AMapRegion* Region = State->FindRegionAt(SelectedBuilding->GetActorLocation());
			Context.ProducerRegion = Region ? Region->RegionIndex : INDEX_NONE;
		}
		FVector Location;
		bool bGround = PickOrderTarget(Position, *State, Preview.Structure, Location);
		// A is explicitly a region order, including when the region contains a structure.
		if (Context.bAttack || Forces.IsEmpty())
			Preview.Structure = nullptr;
		if (Preview.Structure)
		{
			Location = Preview.Structure->GetActorLocation();
			bGround = true;
		}
		const AMapRegion* Region = bGround ? State->FindRegionAt(Location) : nullptr;
		Preview.RegionIndex = Region ? Region->RegionIndex : INDEX_NONE;
		Context.TargetRegion = Preview.RegionIndex;
		Context.bHostileStructure = Preview.Structure != nullptr;
	}
	static_cast<FResult&>(Preview) = Resolve(Context);
	return Preview;
}

void ACommandPlayerController::SendResolvedOrder(const FOrderInputPreview& Preview, bool bQueue)
{
	if (!Preview.IsAllowed())
	{
		SetCommandFeedback(Preview.Label(), false);
		return;
	}
	if (Preview.Resolution == EResolution::Rally)
	{
		OrderCommands->ServerSetRallyPoint(SelectedBuilding, Preview.RegionIndex);
		return;
	}
	TArray<AArmyGroup*> Forces;
	Forces.Reserve(SelectedForces.Num());
	for (AArmyGroup* Force : SelectedForces)
		Forces.Add(Force);
	SetFeedback(TEXT("Order sent; awaiting server."));
	OrderCommands->ServerIssueForceOrder(Forces,
		Preview.Resolution == EResolution::Attack ? EForceVerb::Attack : EForceVerb::MoveHold,
		Preview.RegionIndex, Preview.Structure, bQueue, bAssigningOrder ? AttackInputId : 0);
}

bool ACommandPlayerController::HandleOrderClick(const FVector2D& Position, bool bQueue)
{
	if (GetUIScreen() != ECommandScreen::Game)
		return false;
	if (bAssigningOrder || bPlacingBuilding || bBuildHotkeyPending || bSelectionDragging)
	{
		CancelPointerMode();
		return true;
	}
	SendResolvedOrder(GetOrderPreview(Position, bQueue), bQueue);
	return true;
}

void ACommandPlayerController::RightClickAtCursor()
{
	if (bAssigningOrder || bPlacingBuilding || bBuildHotkeyPending || bSelectionDragging)
	{
		CancelPointerMode();
		return;
	}
	float X, Y;
	if (GetMousePosition(X, Y))
		HandleOrderClick(FVector2D(X, Y), IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
}

void ACommandPlayerController::BeginForceAttack()
{
	if (GetUIScreen() != ECommandScreen::Game || !CanIssueGameplayCommand())
		return;
	if (SelectedForces.IsEmpty())
	{
		SetCommandFeedback(TEXT("Select your forces first."), false);
		return;
	}
	CancelMode();
	bAssigningOrder = true;
	if (++AttackInputId == 0)
		++AttackInputId;
	PendingVerb = EForceVerb::Attack;
	bHUDExpanded = false;
	SetFeedback(TEXT("Attack: LMB a region on ground or minimap; Shift queues; RMB/Esc cancels."));
}

void ACommandPlayerController::ConfirmAttackAtScreenPosition(const FVector2D& Position, bool bQueue)
{
	if (bAssigningOrder)
		SendResolvedOrder(GetOrderPreview(Position, bQueue), bQueue);
}

bool ACommandPlayerController::HandleAttackTargetClick(const FVector2D& Position)
{
	if (!bAssigningOrder)
		return false;
	ConfirmAttackAtScreenPosition(Position, IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
	return true;
}

void ACommandPlayerController::RetreatSelectedForces(bool bQueue)
{
	if (GetUIScreen() != ECommandScreen::Game || !CanIssueGameplayCommand())
		return;
	if (SelectedForces.IsEmpty())
	{
		SetCommandFeedback(TEXT("Select your forces first."), false);
		return;
	}
	CancelMode();
	TArray<AArmyGroup*> Forces;
	Forces.Reserve(SelectedForces.Num());
	for (AArmyGroup* Force : SelectedForces)
		Forces.Add(Force);
	SetFeedback(TEXT("Retreat sent; awaiting server."));
	OrderCommands->ServerIssueForceOrder(Forces, EForceVerb::Retreat, INDEX_NONE, nullptr, bQueue, 0);
}

void ACommandPlayerController::CompleteOrderInput(const FString& Message, bool bAccepted, uint32 InAttackInputId)
{
	if (bAccepted && bAssigningOrder && InAttackInputId != 0 && InAttackInputId == AttackInputId)
	{
		bAssigningOrder = false;
		bHUDExpanded = true;
	}
	SetCommandFeedback(Message, bAccepted);
}
