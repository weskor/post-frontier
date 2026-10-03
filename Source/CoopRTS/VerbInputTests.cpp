#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CommandCamera.h"
#include "CommandHUD.h"
#include "HUD/OrderInputPreview.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVerbInputWorldTest, "CoopRTS.Input.Verbs",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace VerbInputTests
{
using namespace ArmyTestSetup;

// Fresh standalone world: stationary explicit forces and completed producers,
// no JEV, paid production or income. Executor travel is covered by VerbOrderTests.
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 45.)
			return Fail(TEXT("Verb input scenario exceeded 45 seconds"));
		if (bFailed)
			return true;
		UWorld* World = ArmyTestSetup::World();
		if (!World || ArmyTestSetup::GameSeconds(World) < 3. || (Stage == 0 && !ArmyTestSetup::NavigationReady(World)))
			return false;
		if (Stage == 0)
		{
			PC = ArmyTestSetup::Controller(World);
			State = World->GetGameState<ACommandGameState>();
			if (!PC || !MapReady(State) || !State->Content
				|| !PC->GetPlayerState<ACommandPlayerState>() || PC->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0)
				return false;
			HUD = PC->GetHUD<ACommandHUD>();
			Camera = Cast<ACommandCamera>(PC->GetPawn());
			FVector2D Origin;
			float Size = 0.f;
			if (!HUD || !Camera || !HUD->GetMinimapScreenRect(Origin, Size))
				return false;
			if (!Setup(World))
				return true;
			bRestoreCursor = PC->GetMousePosition(OriginalMouseX, OriginalMouseY);
			CenterCursor();
			SelectBoth();
			Camera->FocusOn(State->GetRegionAnchor(Target));
			++Stage;
			return false;
		}
		if (!Check(IsValid(PC) && IsValid(HUD) && IsValid(Camera) && IsValid(State)
					&& IsValid(Forces[0]) && IsValid(Forces[1]) && IsValid(Hostile) && IsValid(Producer),
				TEXT("Isolated input fixtures survive")))
			return true;

		switch (Stage)
		{
		case 1: {
			FVector2D Ground;
			if (!Check(PC->ProjectWorldLocationToScreen(State->GetRegionAnchor(Target), Ground), TEXT("Map region projects onto ground viewport"))
				|| !Submit(Ground, ForceOrderInput::EResolution::MoveHold, EForceVerb::MoveHold, Target)
				|| !Submit(Minimap(HostileRegionPoint), ForceOrderInput::EResolution::MoveHold, EForceVerb::MoveHold, EnemyHome)
				|| !Submit(Minimap(Hostile->GetActorLocation()), ForceOrderInput::EResolution::Attack, EForceVerb::Attack,
					RegionAt(State, Hostile->GetActorLocation()), Hostile))
				return true;
			Camera->FocusOn(Hostile->GetActorLocation());
			++Stage;
			break;
		}
		case 2: {
			FVector2D Ground;
			if (!Check(PC->ProjectWorldLocationToScreen(Hostile->GetActorLocation() + FVector(0.f, 0.f, 40.f), Ground),
					TEXT("Hostile structure projects onto ground viewport"))
				|| !Submit(Ground, ForceOrderInput::EResolution::Attack, EForceVerb::Attack,
					RegionAt(State, Hostile->GetActorLocation()), Hostile))
				return true;
			Key(EKeys::A, IE_Pressed);
			++Stage;
			break;
		}
		case 3:
			Key(EKeys::A, IE_Released);
			if (!Check(PC->IsAssigningOrder() && PC->GetPendingVerb() == EForceVerb::Attack && !PC->IsHUDExpanded(),
					TEXT("Real A key enters selected-force Attack mode and collapses the deck"))
				|| !ExerciseAttackPanels() || !ConfirmMinimapAttack())
				return true;
			Key(EKeys::R, IE_Pressed);
			++Stage;
			break;
		case 4:
			Key(EKeys::R, IE_Released);
			for (AArmyGroup* Force : Forces)
				if (!Check(Force->Verb == EForceVerb::Retreat && Force->Orders.Num() == 1,
						TEXT("Real R key immediately replaces orders with Retreat for every selected force")))
					return true;
			if (!Check(!PC->IsAssigningOrder(), TEXT("Retreat leaves no pointer targeting mode")))
				return true;
			Key(EKeys::A, IE_Pressed);
			++Stage;
			break;
		case 5:
			Key(EKeys::A, IE_Released);
			if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A can reopen targeting after Retreat with the deck collapsed")))
				return true;
			SaveSerials();
			Key(EKeys::Escape, IE_Pressed);
			++Stage;
			break;
		case 6:
			Key(EKeys::Escape, IE_Released);
			if (!Check(!PC->IsAssigningOrder() && PC->IsHUDExpanded() && PC->GetUIScreen() == ECommandScreen::Game && Unchanged(),
					TEXT("Real Esc cancels A without ordering or opening pause and restores the deck")))
				return true;
			Key(EKeys::A, IE_Pressed);
			++Stage;
			break;
		case 7:
			Key(EKeys::A, IE_Released);
			if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A reopens targeting before right-click cancellation with the deck collapsed")))
				return true;
			Key(EKeys::RightMouseButton, IE_Pressed);
			++Stage;
			break;
		case 8:
			Key(EKeys::RightMouseButton, IE_Released);
			if (!Check(!PC->IsAssigningOrder() && PC->IsHUDExpanded() && Unchanged(),
					TEXT("Real right-click cancels A without giving a smart order and restores the deck"))
				|| !Submit(Minimap(State->GetRegionAnchor(Target)), ForceOrderInput::EResolution::MoveHold, EForceVerb::MoveHold, Target))
				return true;
			Key(EKeys::LeftShift, IE_Pressed);
			++Stage;
			break;
		case 9: {
			const bool bQueue = PC->IsInputKeyDown(EKeys::LeftShift);
			if (!Check(bQueue, TEXT("Real Shift key supplies the queue modifier"))
				|| !Submit(Minimap(HostileRegionPoint), ForceOrderInput::EResolution::MoveHold, EForceVerb::MoveHold, EnemyHome, nullptr, bQueue)
				|| !Submit(Minimap(Hostile->GetActorLocation()), ForceOrderInput::EResolution::Attack, EForceVerb::Attack,
					RegionAt(State, Hostile->GetActorLocation()), Hostile, bQueue))
				return true;
			for (AArmyGroup* Force : Forces)
				if (!Check(Force->Orders.Num() == 3 && Force->Orders[0].Verb == EForceVerb::MoveHold
							&& Force->Orders[0].RegionIndex == Target && Force->Orders[1].RegionIndex == EnemyHome
							&& Force->Orders[2].Structure == Hostile,
						TEXT("Shift preserves active order and appends through three total orders")))
					return true;
			SaveSerials();
			const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
			const FOrderInputPreview Preview = PC->GetOrderPreview(Point, true);
			if (!Rejection(Preview))
				return true;
			PC->HandleOrderClick(Point, true);
			if (!RejectedUnchanged(Preview))
				return true;
			Key(EKeys::A, IE_Pressed);
			++Stage;
			break;
		}
		case 10: {
			Key(EKeys::A, IE_Released);
			if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A opens with the deck collapsed even when the existing queue is full")))
				return true;
			const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
			const FOrderInputPreview Preview = PC->GetOrderPreview(Point, true);
			if (!Rejection(Preview))
				return true;
			if (!Check(PC->HandleHUDClick(Point), TEXT("Real minimap HUD click consumes rejected A target"))
				|| !RejectedUnchanged(Preview)
				|| !Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("Rejected A keeps Attack mode open and the deck collapsed")))
				return true;
			Key(EKeys::LeftShift, IE_Released);
			++Stage;
			break;
		}
		case 11: {
			if (!Check(!PC->IsInputKeyDown(EKeys::LeftShift), TEXT("Shift release reaches the input state before unshifted confirmation"))
				|| !ConfirmMinimapAttack())
				return true;
			Camera->FocusOn(State->GetRegionAnchor(Target));
			Key(EKeys::A, IE_Pressed);
			++Stage;
			break;
		}
		case 12: {
			Key(EKeys::A, IE_Released);
			FVector2D Ground;
			if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A opens ground targeting with the deck collapsed"))
				|| !Check(PC->ProjectWorldLocationToScreen(State->GetRegionAnchor(Target), Ground) && !HUD->IsPanelPoint(Ground),
					TEXT("Attack region projects onto the uncovered ground viewport")))
				return true;
			PC->SetMouseLocation(FMath::RoundToInt(Ground.X), FMath::RoundToInt(Ground.Y));
			float X, Y;
			if (!Check(PC->GetMousePosition(X, Y), TEXT("Native ground click has a viewport cursor"))
				|| !AttackPreview(FVector2D(X, Y), false))
				return true;
			SaveSerials();
			Key(EKeys::LeftMouseButton, IE_Pressed);
			++Stage;
			break;
		}
		case 13:
			Key(EKeys::LeftMouseButton, IE_Released);
			if (!Check(!Unchanged(), TEXT("Native LMB through the controller selection path submits the ground Attack"))
				|| !OrdersMatch(EForceVerb::Attack, Target, nullptr, false)
				|| !Check(!PC->IsAssigningOrder() && PC->IsHUDExpanded(), TEXT("Accepted native ground A ends targeting and restores the deck"))
				|| !ExerciseMinimapRally())
				return true;
			++Stage;
			break;
		case 14:
			if (!ExerciseGroundRallyAndBox())
				return true;
			PC->SelectActor(nullptr);
			Camera->FocusOn(FVector::ZeroVector);
			CenterCursor();
			CameraBefore = Camera->GetActorLocation();
			Key(EKeys::Up, IE_Pressed);
			++Stage;
			break;
		case 15:
			Key(EKeys::Up, IE_Released);
			if (!Check(Camera->GetActorLocation().X > CameraBefore.X + 1.,
					TEXT("Native Up arrow moves the camera forward through its input binding")))
				return true;
			if (!BeginEdgePan())
				return Finish();
			++Stage;
			break;
		case 16:
			if (!FocusedViewport())
			{
				Test->AddInfo(TEXT("Edge-pan movement proof unavailable: native viewport focus was lost."));
				return Finish();
			}
			if (ArmyTestSetup::GameSeconds(World) - EdgePanStarted < .05)
				return false;
			if (!Check(Camera->GetActorLocation().X < CameraBefore.X - 1.,
					bEdgeOverPanel ? TEXT("Focused viewport bottom edge pans the camera over a HUD panel")
								   : TEXT("Focused viewport bottom edge pans the camera")))
				return true;
			Test->AddInfo(bEdgeOverPanel ? TEXT("Edge-pan movement observed with native viewport focus over a HUD panel.")
										 : TEXT("Edge-pan movement observed with native viewport focus; HUD panels do not intersect this viewport's edge band."));
			return Finish();
		}
		return false;
	}

private:
	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
		{
			Test->AddError(Message);
			bFailed = true;
		}
		if (!Value)
			RestoreInput();
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Check(false, Message);
		return true;
	}
	void Key(const FKey& Value, EInputEvent Event)
	{
		if (IsValid(PC))
		{
			FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
			PC->InputKey(FInputKeyEventArgs(Viewport, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),
				Value, Event, FPlatformTime::Cycles64()));
		}
	}
	void RestoreInput()
	{
		for (const FKey& Value : { EKeys::A, EKeys::R, EKeys::Escape, EKeys::RightMouseButton,
				 EKeys::LeftMouseButton, EKeys::LeftShift, EKeys::Up })
			Key(Value, IE_Released);
		if (IsValid(PC) && bRestoreCursor)
			PC->SetMouseLocation(FMath::RoundToInt(OriginalMouseX), FMath::RoundToInt(OriginalMouseY));
	}
	bool Finish()
	{
		RestoreInput();
		Test->AddInfo(TEXT("Verb input: ground/minimap smart orders, multi-force region and hostile structure targets, real minimap HUD and native ground LMB A routing, Pause/Menu panel interactions, targeting/acceptance/cancellation deck state, A/R/Esc/right-click keys, Shift bounded queue, preview/order agreement, rejection retention and explanation, minimap and ground producer rally, selection-only drag and native arrow camera movement."));
		return true;
	}
	void CenterCursor()
	{
		int32 Width = 0, Height = 0;
		PC->GetViewportSize(Width, Height);
		PC->SetMouseLocation(Width / 2, Height / 2);
	}
	FViewport* FocusedViewport() const
	{
		UGameViewportClient* Client = PC->GetWorld()->GetGameViewport();
		FViewport* Viewport = Client ? Client->Viewport : nullptr;
		return Viewport && Viewport->HasFocus() && Viewport->IsForegroundWindow() ? Viewport : nullptr;
	}
	bool BeginEdgePan()
	{
		if (!FocusedViewport())
		{
			Test->AddInfo(TEXT("Edge-pan movement proof unavailable: viewport lacks native focus/foreground (offscreen runs do not establish native focus)."));
			return false;
		}
		int32 Width = 0, Height = 0;
		PC->GetViewportSize(Width, Height);
		PC->SetMouseLocation(Width / 2, Height - 8);
		float X, Y;
		if (!Check(PC->GetMousePosition(X, Y) && X >= 0.f && X < Width && Y >= Height - 8 && Y < Height,
				TEXT("Focused native viewport cursor reaches the inclusive bottom edge band")))
			return false;
		bEdgeOverPanel = HUD->IsPanelPoint(FVector2D(X, Y));
		Camera->FocusOn(FVector::ZeroVector);
		CameraBefore = Camera->GetActorLocation();
		EdgePanStarted = ArmyTestSetup::GameSeconds(PC->GetWorld());
		return true;
	}
	void SelectBoth()
	{
		PC->SelectForce(Forces[0]);
		PC->SelectForce(Forces[1], true);
	}
	FVector2D Minimap(const FVector& Point) const
	{
		FVector2D Origin;
		float Size = 0.f;
		HUD->GetMinimapScreenRect(Origin, Size);
		const FVector2D Extent = State->Arena->HalfExtent;
		return Origin + FVector2D((Point.Y + Extent.Y) / (2. * Extent.Y), (Extent.X - Point.X) / (2. * Extent.X)) * Size;
	}
	void SaveSerials()
	{
		for (int32 Index = 0; Index < 2; ++Index)
			Serials[Index] = Forces[Index]->OrderSerial;
	}
	bool Unchanged() const
	{
		return Forces[0]->OrderSerial == Serials[0] && Forces[1]->OrderSerial == Serials[1];
	}
	bool Rejection(const FOrderInputPreview& Preview)
	{
		return Check(!Preview.IsAllowed() && Preview.Resolution == ForceOrderInput::EResolution::Reject
				&& Preview.Rejection == ForceOrderInput::ERejection::QueueFull
				&& Preview.Label() && *Preview.Label(),
			TEXT("Fourth-order preview rejects with the queue-limit reason"));
	}
	bool RejectedUnchanged(const FOrderInputPreview& Preview)
	{
		return Check(Unchanged() && Forces[0]->Orders.Num() == 3 && Forces[1]->Orders.Num() == 3,
				   TEXT("Rejected fourth order changes neither selected force"))
			&& Check(!PC->GetOrderFeedback().IsEmpty() && PC->GetOrderFeedback().Contains(Preview.Label()) && PC->GetFeedbackOpacity() > 0.f,
				TEXT("Rejected input displays the same reason as its preview"));
	}
	bool OrdersMatch(EForceVerb Verb, int32 Region, AActor* Structure, bool bQueue)
	{
		for (AArmyGroup* Force : Forces)
		{
			if (!Check(!Force->Orders.IsEmpty(), TEXT("Selected force receives an order")))
				return false;
			const FForceOrder& Order = Force->Orders.Last();
			if (!Check(Order.Verb == Verb && Order.RegionIndex == Region && Order.Structure == Structure,
					TEXT("Every selected force's resulting order agrees with the hovered preview"))
				|| !Check(bQueue || (Force->Orders.Num() == 1 && Force->Verb == Verb && Force->TargetRegionIndex == Region && Force->TargetStructure == Structure), TEXT("Unshifted input replaces active order")))
				return false;
		}
		return true;
	}
	bool Submit(const FVector2D& Point, ForceOrderInput::EResolution Resolution, EForceVerb Verb,
		int32 Region, AActor* Structure = nullptr, bool bQueue = false)
	{
		const FOrderInputPreview Preview = PC->GetOrderPreview(Point, bQueue);
		if (!Preview.IsAllowed() || Preview.Resolution != Resolution || Preview.RegionIndex != Region
			|| Preview.Structure != Structure)
		{
			Test->AddInfo(FString::Printf(TEXT("Preview at (%.1f, %.1f): resolution=%d rejection=%d region=%d structure=%s panel=%d camera=%s"),
				Point.X, Point.Y, static_cast<int32>(Preview.Resolution), static_cast<int32>(Preview.Rejection),
				Preview.RegionIndex, *GetNameSafe(Preview.Structure), HUD->IsPanelPoint(Point), *Camera->GetActorLocation().ToString()));
			return Check(false, TEXT("Smart preview resolves expected verb and target"));
		}
		return Check(PC->HandleOrderClick(Point, bQueue), TEXT("Shared smart right-click entry consumes the order"))
			&& OrdersMatch(Verb, Region, Structure, bQueue);
	}
	bool ClickPanel(EHUDAction Action)
	{
		FVector2D Point;
		return Check(HUD->FindActionScreenPosition(Action, Point), TEXT("Regression panel action has a visible HUD hit target"))
			&& Check(PC->HandleHUDClick(Point), TEXT("Real HUD click dispatches the panel action during A"));
	}
	bool ExerciseAttackPanels()
	{
		SaveSerials();
		PC->CompleteOrderInput(TEXT("Earlier non-targeting order accepted."), true, 0);
		if (!Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded() && Unchanged(),
				TEXT("A late non-targeting acknowledgement cannot close A or reopen its deck")))
			return false;
		if (!Check(!State->IsActivePaused(), TEXT("Panel regression begins with the simulation running"))
			|| !ClickPanel(EHUDAction::ActivePause)
			|| !Check(State->IsActivePaused() && PC->GetWorld()->IsPaused()
					&& PC->IsAssigningOrder() && !PC->IsHUDExpanded() && Unchanged(),
				TEXT("Pause panel pauses simulation while preserving A targeting and collapsed deck without ordering"))
			|| !ClickPanel(EHUDAction::Menu)
			|| !Check(PC->GetUIScreen() == ECommandScreen::Pause && !PC->IsAssigningOrder()
					&& PC->IsHUDExpanded() && State->IsActivePaused() && Unchanged(),
				TEXT("Menu panel cancels A and restores the deck without changing active pause or force orders"))
			|| !ClickPanel(EHUDAction::Resume)
			|| !Check(PC->GetUIScreen() == ECommandScreen::Game && State->IsActivePaused() && PC->GetWorld()->IsPaused(),
				TEXT("Closing the menu does not resume a separately active-paused simulation")))
			return false;
		Key(EKeys::A, IE_Pressed);
		Key(EKeys::A, IE_Released);
		return Check(PC->IsAssigningOrder() && !PC->IsHUDExpanded(), TEXT("A reopens while actively paused"))
			&& ClickPanel(EHUDAction::ActivePause)
			&& Check(!State->IsActivePaused() && !PC->GetWorld()->IsPaused()
					&& PC->IsAssigningOrder() && !PC->IsHUDExpanded() && Unchanged(),
				TEXT("Pause panel resumes simulation while preserving A targeting and force orders"));
	}
	bool AttackPreview(const FVector2D& Point, bool bQueue)
	{
		const FOrderInputPreview Preview = PC->GetOrderPreview(Point, bQueue);
		return Check(Preview.IsAllowed() && Preview.Resolution == ForceOrderInput::EResolution::Attack
				&& Preview.RegionIndex == Target && !Preview.Structure,
			TEXT("Pending A previews Attack on a region rather than smart MoveHold"));
	}
	bool ConfirmMinimapAttack()
	{
		const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
		return AttackPreview(Point, false)
			&& Check(PC->HandleHUDClick(Point), TEXT("Real minimap HUD click confirms pending A"))
			&& OrdersMatch(EForceVerb::Attack, Target, nullptr, false)
			&& Check(!PC->IsAssigningOrder() && PC->IsHUDExpanded(), TEXT("Accepted minimap A ends targeting and restores the deck"));
	}
	ACommandBuilding* Building(UWorld* World, ACommandPlayerState* Owner, const FVector& Location)
	{
		const FTransform Transform(Location);
		ACommandBuilding* Result = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			Owner == PC->GetPlayerState<ACommandPlayerState>() ? PC : nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Result)
			return nullptr;
		Result->BuildingIndex = BarracksIndex;
		Result->OwningPlayerState = Owner;
		Result->TeamIndex = Owner->TeamIndex;
		Result->ConstructionProgress = 1.f;
		Result->FinishSpawning(Transform);
		Result->bProductionEnabled = false;
		Result->SetActorTickEnabled(false);
		return Result;
	}
	bool Setup(UWorld* World)
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<ACommandBuilding> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		State->bVerificationIncomePaused = true;
		ACommandPlayerState* Wallet = PC->GetPlayerState<ACommandPlayerState>();
		Wallet->Resources = 0;
		if (!Check(IsValid(State->EnemyCommander), TEXT("Planner isolation preserves enemy ownership")))
			return false;
		State->EnemyCommander->Resources = 0;
		for (const AMapRegion* Region : State->Regions)
		{
			if (IsValid(Region) && Region->HomeTeam == State->EnemyCommander->TeamIndex)
				EnemyHome = Region->RegionIndex;
			if (IsValid(Region) && Region->HomeTeam < 0 && Target == INDEX_NONE)
				Target = Region->RegionIndex;
		}
		if (!Check(Target != INDEX_NONE && EnemyHome != INDEX_NONE, TEXT("Generated map supplies neutral and hostile region targets")))
			return false;
		Producer = Building(World, Wallet, FromFriendlyHQ(State, 700.f, -600.f, 5.f));
		Hostile = Building(World, State->EnemyCommander.Get(), FromEnemyHQ(State, -700.f, 600.f, 5.f));
		if (!Check(IsValid(Producer) && IsValid(Hostile), TEXT("Completed friendly producer and hostile structure fixtures spawn")))
			return false;
		bool bFoundHostileGround = false;
		for (int32 Direction = 0; Direction < 16; ++Direction)
		{
			const float Angle = Direction * PI / 8.f;
			const FVector Candidate = State->GetRegionAnchor(EnemyHome)
				+ FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 650.f;
			const FVector2D HQOffset = Minimap(Candidate) - Minimap(State->EnemyHeadquarters->GetActorLocation());
			const FVector2D BuildingOffset = Minimap(Candidate) - Minimap(Hostile->GetActorLocation());
			if (RegionAt(State, Candidate) == EnemyHome
				&& (FMath::Abs(HQOffset.X) > 7.f || FMath::Abs(HQOffset.Y) > 7.f)
				&& (FMath::Abs(BuildingOffset.X) > 4.f || FMath::Abs(BuildingOffset.Y) > 4.f))
			{
				HostileRegionPoint = Candidate;
				bFoundHostileGround = true;
				break;
			}
		}
		if (!Check(bFoundHostileGround, TEXT("Hostile polygon supplies bare region ground away from structure markers")))
			return false;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			Forces[Index] = SpawnGroup(World, PC, Index, FromFriendlyHQ(State, 1000.f + Index * 350.f, -700.f, 100.f));
			if (!Check(IsValid(Forces[Index]), TEXT("Explicit selected force spawns with real members")))
				return false;
			Forces[Index]->ForceNumber = Index + 1;
			Forces[Index]->SetActorTickEnabled(false);
			for (AArmyUnit* Unit : Forces[Index]->GetUnits())
			{
				Unit->SetActorTickEnabled(false);
				Unit->GetCharacterMovement()->DisableMovement();
			}
		}
		return true;
	}
	bool ExerciseMinimapRally()
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
	bool ExerciseGroundRallyAndBox()
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

	FAutomationTestBase* Test;
	double Started;
	bool bFailed = false;
	int32 Stage = 0;
	int32 Target = INDEX_NONE, EnemyHome = INDEX_NONE;
	FVector HostileRegionPoint = FVector::ZeroVector;
	FVector CameraBefore = FVector::ZeroVector;
	double EdgePanStarted = 0.;
	float OriginalMouseX = 0.f, OriginalMouseY = 0.f;
	bool bRestoreCursor = false;
	bool bEdgeOverPanel = false;
	ACommandPlayerController* PC = nullptr;
	ACommandGameState* State = nullptr;
	ACommandHUD* HUD = nullptr;
	ACommandCamera* Camera = nullptr;
	ACommandBuilding* Producer = nullptr;
	ACommandBuilding* Hostile = nullptr;
	AArmyGroup* Forces[2] = {};
	uint32 Serials[2] = {};
};
}

bool FVerbInputWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(VerbInputTests::FScenario(this));
	return true;
}
#endif
