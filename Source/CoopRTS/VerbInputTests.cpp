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
		if (!World || World->GetTimeSeconds() < 3.f)
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
			if (!Check(PC->IsAssigningOrder() && PC->GetPendingVerb() == EForceVerb::Attack, TEXT("Real A key enters selected-force Attack mode"))
				|| !ConfirmAttack(false))
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
			if (!Check(PC->IsAssigningOrder(), TEXT("A can reopen targeting after Retreat")))
				return true;
			SaveSerials();
			Key(EKeys::Escape, IE_Pressed);
			++Stage;
			break;
		case 6:
			Key(EKeys::Escape, IE_Released);
			if (!Check(!PC->IsAssigningOrder() && PC->GetUIScreen() == ECommandScreen::Game && Unchanged(),
					TEXT("Real Esc cancels A without ordering or opening pause")))
				return true;
			Key(EKeys::A, IE_Pressed);
			++Stage;
			break;
		case 7:
			Key(EKeys::A, IE_Released);
			if (!Check(PC->IsAssigningOrder(), TEXT("A reopens targeting before right-click cancellation")))
				return true;
			Key(EKeys::RightMouseButton, IE_Pressed);
			++Stage;
			break;
		case 8:
			Key(EKeys::RightMouseButton, IE_Released);
			if (!Check(!PC->IsAssigningOrder() && Unchanged(), TEXT("Real right-click cancels A without giving a smart order"))
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
			if (!Check(PC->IsAssigningOrder(), TEXT("A opens even when the existing queue is full")))
				return true;
			const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
			const FOrderInputPreview Preview = PC->GetOrderPreview(Point, true);
			if (!Rejection(Preview))
				return true;
			PC->ConfirmAttackAtScreenPosition(Point, true);
			if (!RejectedUnchanged(Preview) || !Check(PC->IsAssigningOrder(), TEXT("Rejected A keeps Attack mode open")))
				return true;
			Key(EKeys::LeftShift, IE_Released);
			if (!ConfirmAttack(false) || !ExerciseRallyAndBox())
				return true;
			PC->SelectActor(nullptr);
			Test->AddInfo(TEXT("Verb input: ground/minimap smart orders, multi-force region and hostile structure targets, A/R/Esc/right-click keys, Shift bounded queue, preview/order agreement, rejection retention and explanation, producer rally and selection-only drag."));
			return true;
		}
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
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		for (const FKey& Value : { EKeys::A, EKeys::R, EKeys::Escape, EKeys::RightMouseButton, EKeys::LeftShift })
			Key(Value, IE_Released);
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
		return Check(Preview.IsAllowed() && Preview.Resolution == Resolution && Preview.RegionIndex == Region
					   && Preview.Structure == Structure,
				   TEXT("Smart preview resolves expected verb and target"))
			&& Check(PC->HandleOrderClick(Point, bQueue), TEXT("Shared smart right-click entry consumes the order"))
			&& OrdersMatch(Verb, Region, Structure, bQueue);
	}
	bool ConfirmAttack(bool bQueue)
	{
		const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
		const FOrderInputPreview Preview = PC->GetOrderPreview(Point, bQueue);
		if (!Check(Preview.IsAllowed() && Preview.Resolution == ForceOrderInput::EResolution::Attack
					&& Preview.RegionIndex == Target && !Preview.Structure,
				TEXT("Pending A previews Attack on a region rather than smart MoveHold")))
			return false;
		PC->ConfirmAttackAtScreenPosition(Point, bQueue);
		return OrdersMatch(EForceVerb::Attack, Target, nullptr, bQueue)
			&& Check(!PC->IsAssigningOrder(), TEXT("Accepted A ends targeting mode"));
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
	bool ExerciseRallyAndBox()
	{
		SaveSerials();
		PC->SelectActorWithModifiers(Producer, false, false);
		const FVector2D Point = Minimap(State->GetRegionAnchor(Target));
		const FOrderInputPreview Preview = PC->GetOrderPreview(Point);
		if (!Check(PC->GetSelectedForces().IsEmpty() && PC->GetSelectedBuilding() == Producer
					&& Preview.IsAllowed() && Preview.Resolution == ForceOrderInput::EResolution::Rally
					&& Preview.RegionIndex == Target,
				TEXT("Producer without selected forces previews region rally"))
			|| !Check(PC->HandleOrderClick(Point) && Producer->RallyRegionIndex == Target && Unchanged(),
				TEXT("Producer right-click sets rally without ordering existing forces")))
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
