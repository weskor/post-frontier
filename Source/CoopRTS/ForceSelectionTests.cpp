#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CommandCamera.h"
#include "CommandHUD.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceSelectionWorldTest, "CoopRTS.Selection.Forces",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ForceSelectionScenarioTests
{
using namespace ArmyTestSetup;

// Fresh standalone map; explicit army/building fixtures isolate selection from
// JEV, automatic fronts and paid production (proved by construction scenarios).
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 45.)
			return Fail(TEXT("Force selection scenario exceeded 45 seconds"));
		UWorld* World = ArmyTestSetup::World();
		if (!World || World->GetTimeSeconds() < 3.f)
			return false;
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		if (Stage == EStage::Setup)
		{
			PC = ArmyTestSetup::Controller(World);
			if (!PC.IsValid() || !MapReady(State) || !State->Content
				|| !PC->GetPlayerState<ACommandPlayerState>() || PC->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0)
				return false;
			Camera = Cast<ACommandCamera>(PC->GetPawn());
			if (!Check(Camera.IsValid(), TEXT("Selection scenario has the local command camera")) || !Setup(World, State))
				return true;
			if (!ExerciseActorSelection())
				return true;
			Stage = EStage::BuildingClicks;
			return false;
		}
		if (!Check(PC.IsValid() && Camera.IsValid() && State, TEXT("Selection controller, camera and match survive")))
			return true;
		switch (Stage)
		{
		case EStage::BuildingClicks:
			BeforeCamera = Camera->GetActorLocation();
			PC->SelectActor(Buildings[0].Get());
			if (!Check(PC->GetSelectedBuilding() == Buildings[0].Get() && PC->GetSelectedForces().IsEmpty()
						&& PC->IsForceHighlighted(Owned[0].Get()) && !PC->IsForceSelected(Owned[0].Get()),
					TEXT("First building click opens only its panel and highlights its force")))
				return true;
			// Both clicks use the cursor's world-hit entry in the same world frame.
			PC->SelectActor(Buildings[0].Get());
			if (!Check(Only(Owned[0].Get()), TEXT("Immediate second click on the same producer selects its force and closes its panel")))
				return true;
			PC->SelectActor(Buildings[0].Get());
			if (!Check(PC->GetSelectedBuilding() == Buildings[0].Get() && PC->GetSelectedForces().IsEmpty(),
					TEXT("A third building click starts a fresh single-click window")))
				return true;
			BuildingClickStarted = World->GetRealTimeSeconds();
			Stage = EStage::ExpiredBuildingClick;
			break;
		case EStage::ExpiredBuildingClick:
			if (World->GetRealTimeSeconds() - BuildingClickStarted <= .35)
				return false;
			PC->SelectActor(Buildings[0].Get());
			if (!Check(PC->GetSelectedBuilding() == Buildings[0].Get() && PC->GetSelectedForces().IsEmpty(),
					TEXT("Same-building click after the 0.3-second real-time window opens its panel instead of selecting its force")))
				return true;
			PC->SelectActor(Buildings[1].Get());
			if (!Check(PC->GetSelectedBuilding() == Buildings[1].Get() && PC->GetSelectedForces().IsEmpty(),
					TEXT("Clicking a distinct producer opens that building's panel")))
				return true;
			PC->SelectActor(Buildings[0].Get());
			if (!Check(PC->GetSelectedBuilding() == Buildings[0].Get() && PC->GetSelectedForces().IsEmpty(),
					TEXT("Intervening distinct-building click prevents a double-click on the original producer"))
				|| !Check(Camera->GetActorLocation().Equals(BeforeCamera, .01),
					TEXT("Short, expired and interrupted building click windows never move the camera")))
				return true;
			NumberIndex = 0;
			Stage = EStage::PressNumber;
			break;
		case EStage::PressNumber:
			PC->SelectActor(nullptr);
			BeforeCamera = Camera->GetActorLocation();
			Key(NumberKeys[NumberIndex], IE_Pressed);
			Stage = EStage::CheckNumber;
			break;
		case EStage::CheckNumber:
			Key(NumberKeys[NumberIndex], IE_Released);
			if (!Check(Only(Owned[NumberIndex].Get()), FString::Printf(TEXT("%s key %d selects force number %d, not army array index"), bSoloNumberPass ? TEXT("Solo") : TEXT("Co-op"), NumberIndex + 1, NumberIndex + 1))
				|| !Check(Camera->GetActorLocation().Equals(BeforeCamera, .01), TEXT("Single number key does not move camera")))
				return true;
			if (++NumberIndex < (bSoloNumberPass ? 5 : 4))
				Stage = EStage::PressNumber;
			else if (!bSoloNumberPass)
			{
				PC->SelectActor(Owned[0].Get());
				BeforeCamera = Camera->GetActorLocation();
				Key(EKeys::Five, IE_Pressed);
				Stage = EStage::CheckCoopFive;
			}
			else
			{
				State->AddPlayerState(ForeignWallet.Get());
				PC->SelectActor(Owned[0].Get());
				PC->SelectForce(Owned[1].Get(), true);
				Camera->FocusOn(FromFriendlyHQ(State, 0.f, -1800.f, 0.f));
				BeforeCamera = Camera->GetActorLocation();
				Key(EKeys::F, IE_Pressed);
				Stage = EStage::CheckFocus;
			}
			break;
		case EStage::CheckCoopFive:
			Key(EKeys::Five, IE_Released);
			if (!Check(Only(Owned[0].Get()), TEXT("Two-human roster rejects key five without changing the existing selection"))
				|| !Check(Camera->GetActorLocation().Equals(BeforeCamera, .01), TEXT("Rejected co-op key five does not move camera")))
				return true;
			State->RemovePlayerState(ForeignWallet.Get());
			bSoloNumberPass = true;
			NumberIndex = 0;
			Stage = EStage::PressNumber;
			break;
		case EStage::CheckFocus:
			Key(EKeys::F, IE_Released);
			if (!Check(FVector::Dist2D(Camera->GetActorLocation(), (Owned[0]->GetCenter() + Owned[1]->GetCenter()) * .5) < 1.,
					TEXT("F centres on the midpoint of the selected forces"))
				|| !Check(!Camera->GetActorLocation().Equals(BeforeCamera, 1.), TEXT("F actually moves the displaced camera")))
				return true;
			// Two dispatches in one frame make the timing boundary deterministic;
			// real number-key mappings have already been exercised above.
			PC->SelectForceNumber(2);
			Camera->FocusOn(FromFriendlyHQ(State, 0.f, -1800.f, 0.f));
			PC->SelectForceNumber(2);
			if (!Check(Only(Owned[1].Get()) && FVector::Dist2D(Camera->GetActorLocation(), Owned[1]->GetCenter()) < 1.,
					TEXT("Double-tapping a force number centres on that force")))
				return true;
			Camera->FocusOn((Owned[0]->GetCenter() + Owned[1]->GetCenter()) * .5);
			Stage = EStage::HUD;
			break;
		case EStage::HUD:
			if (!ExerciseHUD())
				return true;
			if (!ExerciseOrphan())
				return true;
			PC->SelectActor(nullptr);
			Test->AddInfo(TEXT("Force selection proof: living/orphan unit selection, Shift add/remove, co-op keys 1–4 and key-5 rejection, one-human roster keys 1–5, no selection camera jump, F multi-force focus, number double-tap focus, building panel/highlight and real-time short/expired/interrupted double-click windows, enemy/dead exclusions and teammate read-only inspection."));
			return true;
		default:
			break;
		}
		return false;
	}

private:
	enum class EStage : uint8
	{
		Setup,
		BuildingClicks,
		ExpiredBuildingClick,
		PressNumber,
		CheckNumber,
		CheckCoopFive,
		CheckFocus,
		HUD
	};

	bool Check(bool bValue, const FString& Message)
	{
		if (!bValue)
			Test->AddError(Message);
		return bValue;
	}
	bool Fail(const TCHAR* Message)
	{
		for (const FKey& NumberKey : NumberKeys)
			Key(NumberKey, IE_Released);
		Key(EKeys::F, IE_Released);
		Test->AddError(Message);
		return true;
	}
	void Key(const FKey& Value, EInputEvent Event)
	{
		if (PC.IsValid())
		{
			FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
			PC->InputKey(FInputKeyEventArgs(Viewport, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),
				Value, Event, FPlatformTime::Cycles64()));
		}
	}
	bool Only(const AArmyGroup* Force) const
	{
		return PC->GetSelectedForces().Num() == 1 && PC->IsForceSelected(Force)
			&& PC->GetInspectedForce() == Force && !PC->GetSelectedBuilding();
	}
	ACommandBuilding* Producer(UWorld* World, ACommandPlayerState* Wallet, int32 Number, const FVector& Location)
	{
		const FTransform Transform(Location);
		ACommandBuilding* Building = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			PC.Get(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Building)
			return nullptr;
		Building->BuildingIndex = BarracksIndex;
		Building->OwningPlayerState = Wallet;
		Building->TeamIndex = Wallet->TeamIndex;
		Building->ConstructionProgress = 1.f;
		Building->bForceConfigured = true;
		Building->ProductionUnitIndex = UnitIndex(World->GetGameState<ACommandGameState>(), EUnitRole::Frontline);
		Building->ForceNumber = Number;
		Building->FinishSpawning(Transform);
		Building->bProductionEnabled = false;
		Building->SetActorTickEnabled(false);
		return Building;
	}
	AArmyGroup* Force(UWorld* World, ACommandPlayerState* Wallet, int32 Number, const FVector& Location, ACommandBuilding* Building = nullptr)
	{
		const FTransform Transform(Location);
		AArmyGroup* Group = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			Wallet->TeamIndex == 0 ? PC.Get() : nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Group)
			return nullptr;
		// ArmyIndex deliberately differs from force number: input addresses ForceNumber.
		Group->Initialize({ Wallet->TeamIndex, Wallet, 20 + Number, Building, Location });
		Group->ForceNumber = Number;
		Group->FinishSpawning(Transform);
		if (!Group->SpawnUnits())
		{
			Group->Destroy();
			return nullptr;
		}
		if (Building)
			Building->ForceGroup = Group;
		Group->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Group->GetUnits())
		{
			Unit->SetActorTickEnabled(false);
			Unit->GetCharacterMovement()->DisableMovement();
		}
		return Group;
	}
	bool Setup(UWorld* World, ACommandGameState* State)
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
		if (!Check(IsValid(State->EnemyCommander), TEXT("Map provides enemy ownership for selection exclusions")))
			return false;
		State->EnemyCommander->Resources = 0;
		ForeignWallet = World->SpawnActor<ACommandPlayerState>();
		if (!Check(ForeignWallet.IsValid(), TEXT("Teammate wallet fixture spawns")))
			return false;
		ForeignWallet->CommanderIndex = Wallet->CommanderIndex == 0 ? 1 : 0;
		ForeignWallet->TeamIndex = Wallet->TeamIndex;
		ForeignWallet->Resources = 0;
		State->AddPlayerState(ForeignWallet.Get());
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const FVector Location = FromFriendlyHQ(State, 900.f + Index * 700.f, -900.f, 100.f);
			Buildings[Index] = Producer(World, Wallet, Index + 1, Location + FVector(0.f, -450.f, -95.f));
			if (!Check(Buildings[Index].IsValid(), TEXT("Isolated completed producer spawns")))
				return false;
			Owned[Index] = Force(World, Wallet, Index + 1, Location, Buildings[Index].Get());
			if (!Check(Owned[Index].IsValid(), TEXT("Isolated numbered force spawns")))
				return false;
		}
		Foreign = Force(World, ForeignWallet.Get(), 1, FromFriendlyHQ(State, 1250.f, -450.f, 100.f));
		Enemy = Force(World, State->EnemyCommander.Get(), 1, HostileStaging(State));
		return Check(Foreign.IsValid() && Enemy.IsValid(), TEXT("Teammate and enemy forces share own force number without sharing command ownership"));
	}
	bool ExerciseActorSelection()
	{
		Camera->FocusOn(Buildings[4]->GetActorLocation());
		const FVector CameraBefore = Camera->GetActorLocation();
		PC->SelectActor(Owned[0]->GetUnits()[0]);
		if (!Check(Only(Owned[0].Get()), TEXT("Clicking own unit selects its force, not its producer")))
			return false;
		PC->SelectActor(Owned[1]->GetUnits()[0], true);
		if (!Check(PC->GetSelectedForces().Num() == 2 && PC->IsForceSelected(Owned[0].Get()) && PC->IsForceSelected(Owned[1].Get()),
				TEXT("Shift-click adds another force without removing the first")))
			return false;
		PC->SelectActor(Owned[0]->GetUnits()[1], true);
		if (!Check(Only(Owned[1].Get()), TEXT("Shift-clicking another unit of a selected force removes that force")))
			return false;
		PC->SelectActor(Owned[1]->GetUnits()[1], true);
		if (!Check(PC->GetSelectedForces().IsEmpty() && !PC->GetInspectedForce(), TEXT("Removing the last selected force leaves no stale inspection")))
			return false;
		PC->SelectForce(Owned[0].Get());
		PC->SelectActor(Foreign->GetUnits()[0], true);
		if (!Check(PC->GetSelectedForces().Num() == 1 && PC->IsForceSelected(Owned[0].Get()) && !PC->IsForceSelected(Foreign.Get())
					&& PC->GetInspectedForce() == Foreign.Get(),
				TEXT("Shift-clicking teammate inspects read-only without adding it to command selection")))
			return false;
		PC->SelectActor(Foreign->GetUnits()[0]);
		if (!Check(PC->GetSelectedForces().IsEmpty() && !PC->GetSelectedBuilding() && PC->GetInspectedForce() == Foreign.Get(),
				TEXT("Clicking teammate replaces command selection with read-only inspection")))
			return false;
		PC->SelectActor(nullptr);
		PC->SelectActor(Enemy->GetUnits()[0]);
		if (!Check(PC->GetSelectedForces().IsEmpty() && !PC->GetInspectedForce(), TEXT("Enemy unit cannot select or inspect a same-number force")))
			return false;
		AArmyUnit* Dead = Owned[4]->GetUnits().Last();
		Dead->ReceiveAttack(Dead->MaxHealth(), Enemy->GetUnits()[0]);
		PC->SelectActor(Dead);
		return Check(!Dead->IsAlive() && PC->GetSelectedForces().IsEmpty(), TEXT("Dead own unit cannot select its living force"))
			&& Check(Camera->GetActorLocation().Equals(CameraBefore, .01), TEXT("Unit, Shift and teammate selection never move camera"));
	}
	bool ExerciseHUD()
	{
		ACommandHUD* HUD = PC->GetHUD<ACommandHUD>();
		int32 Width = 0, Height = 0;
		PC->GetViewportSize(Width, Height);
		if (!HUD || Width <= 0 || Height <= 0)
		{
			Test->AddInfo(TEXT("HUD badge/box/panel checks unavailable in this world; run HUD verification with a viewport."));
			return true;
		}
		PC->SelectActor(nullptr);
		FVector2D First, Second;
		if (!Check(HUD->FindForceScreenPosition(Owned[0].Get(), First) && HUD->FindForceScreenPosition(Owned[1].Get(), Second),
				TEXT("Own forces expose visible clickable badges"))
			|| !Check(HUD->GetForceAtScreenPosition(First) == Owned[0].Get(), TEXT("Badge centre targets the visible force")))
			return false;
		const FVector CameraBefore = Camera->GetActorLocation();
		if (!Check(PC->HandleHUDClick(First) && Only(Owned[0].Get()), TEXT("Clicking map badge selects its force")))
			return false;
		const FVector2D Start(FMath::Min(First.X, Second.X) - 2., FMath::Min(First.Y, Second.Y) - 2.);
		const FVector2D End(FMath::Max(First.X, Second.X) + 2., FMath::Max(First.Y, Second.Y) + 2.);
		PC->SelectForceBox(End, Start);
		if (!Check(PC->GetSelectedForces().Num() == 2 && PC->IsForceSelected(Owned[0].Get()) && PC->IsForceSelected(Owned[1].Get()),
				TEXT("Reverse-drag box selects both included force badges")))
			return false;
		PC->SelectForce(Owned[4].Get());
		PC->SelectForceBox(Start, End, true);
		PC->SelectForceBox(Start, End, true);
		if (!Check(PC->GetSelectedForces().Num() == 3 && PC->IsForceSelected(Owned[0].Get()) && PC->IsForceSelected(Owned[1].Get())
					&& PC->IsForceSelected(Owned[4].Get()),
				TEXT("Additive box preserves prior force and never duplicates included forces")))
			return false;
		FVector2D TeammateBadge;
		if (!Check(HUD->FindForceScreenPosition(Foreign.Get(), TeammateBadge), TEXT("Teammate badge is visible inside viewport exclusion probe")))
			return false;
		PC->SelectForceBox(FVector2D::ZeroVector, FVector2D(Width, Height));
		if (!Check(!PC->IsForceSelected(Foreign.Get()) && !PC->IsForceSelected(Enemy.Get()), TEXT("Viewport box never adds teammate or enemy forces")))
			return false;
		PC->SelectActorWithModifiers(Buildings[0].Get(), false, false);
		FVector2D Action;
		if (!Check(HUD->FindActionScreenPosition(EHUDAction::SelectForce, Action) && PC->HandleHUDClick(Action) && Only(Owned[0].Get()),
				TEXT("Building panel Select force button switches to its force")))
			return false;
		PC->SelectActorWithModifiers(Buildings[0].Get(), false, false);
		if (!Check(HUD->FindActionScreenPosition(EHUDAction::GoalExpand, Action) && PC->HandleHUDClick(Action) && PC->IsAssigningGoal(),
				TEXT("Existing building goal flow still opens region targeting")))
			return false;
		PC->SelectActor(Foreign->GetUnits()[0]);
		if (!Check(!PC->IsAssigningGoal() && !PC->IsPlacingBuilding() && PC->GetSelectedForces().IsEmpty(),
				TEXT("Read-only teammate inspection cancels pending building goal"))
			|| !Check(!HUD->FindActionScreenPosition(EHUDAction::GoalExpand, Action)
					&& !HUD->FindActionScreenPosition(EHUDAction::ToggleProduction, Action),
				TEXT("Read-only teammate exposes no building commands")))
			return false;
		if (!Check(HUD->FindActionScreenPosition(EHUDAction::BuildSlot0, Action) && PC->HandleHUDClick(Action) && PC->IsPlacingBuilding(),
				TEXT("Existing construction bar can enter placement mode")))
			return false;
		PC->SelectActor(Owned[0]->GetUnits()[0]);
		return Check(!PC->IsPlacingBuilding() && !PC->IsAssigningGoal() && Only(Owned[0].Get()), TEXT("Own force selection cancels building placement"))
			&& Check(Camera->GetActorLocation().Equals(CameraBefore, .01), TEXT("Badge, box and panel selection never move camera"));
	}
	bool ExerciseOrphan()
	{
		AArmyUnit* Survivor = Owned[0]->GetUnits()[0];
		const int32 Number = Owned[0]->ForceNumber;
		Buildings[0]->ReceiveAttack(Buildings[0]->MaxHealth(), Enemy->GetUnits()[0]);
		if (!Check(!Buildings[0].IsValid() && Owned[0].IsValid() && Survivor->IsAlive() && !IsValid(Owned[0]->GetProductionBuilding()),
				TEXT("Real producer destruction leaves living orphan survivors")))
			return false;
		const FVector CameraBefore = Camera->GetActorLocation();
		PC->SelectActor(Buildings[1].Get());
		PC->SelectActor(Survivor);
		if (!Check(Only(Owned[0].Get()) && Owned[0]->ForceNumber == Number, TEXT("Orphan unit remains selectable with retained force number")))
			return false;
		PC->SelectForceNumber(1);
		return Check(Only(Owned[0].Get()), TEXT("Number key also resolves the orphan force"))
			&& Check(Camera->GetActorLocation().Equals(CameraBefore, .01), TEXT("Selecting orphan does not move camera"));
	}

	FAutomationTestBase* Test;
	double Started;
	double BuildingClickStarted = 0.;
	EStage Stage = EStage::Setup;
	int32 NumberIndex = 0;
	bool bSoloNumberPass = false;
	const FKey NumberKeys[5] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five };
	TWeakObjectPtr<ACommandPlayerController> PC;
	TWeakObjectPtr<ACommandCamera> Camera;
	TWeakObjectPtr<ACommandPlayerState> ForeignWallet;
	TWeakObjectPtr<ACommandBuilding> Buildings[5];
	TWeakObjectPtr<AArmyGroup> Owned[5], Foreign, Enemy;
	FVector BeforeCamera;
};
}

bool FForceSelectionWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ForceSelectionScenarioTests::FScenario(this));
	return true;
}
#endif
