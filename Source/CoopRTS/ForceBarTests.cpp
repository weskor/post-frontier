#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "CommandCamera.h"
#include "HUD/ForceBar.h"
#include "HUD/HUDPanels.h"
#include "HUD/ForceETA.h"
#include "Commands/OrderCommandComponent.h"
#include "Commands/PingCommandComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceBarWorldTest, "CoopRTS.HUD.ForceBar",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ForceBarTests
{
using namespace ArmyTestSetup;
using namespace CommandHUDPanels;

class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 45.)
		{
			Test->AddError(FString::Printf(TEXT("Force bar scenario timed out at stage %d"), Stage));
			return true;
		}
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		if (!PC.IsValid())
			PC = World ? Controller(World) : nullptr;
		if (!PC.IsValid() || !MapReady(State) || !State->Content || !PC->GetPlayerState<ACommandPlayerState>()
			|| PC->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0)
			return false;
		if (Stage == 0)
		{
			if (!ArmyTestSetup::NavigationReady(World))
				return false;
			if (!Prepare(World, State) || !States(State) || !SelectionAndLayout(State))
				return true;
			PC->SelectForce(Own.Get());
			Key(EKeys::A);
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			Key(EKeys::A, IE_Released);
			Check(PC->IsAssigningOrder() && PC->GetPendingVerb() == EForceVerb::Attack, TEXT("A opens selected-force Attack"));
			Key(EKeys::Escape);
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			Key(EKeys::Escape, IE_Released);
			if (!Click(Own.Get(), EHUDAction::ForceCardAttack))
				return true;
			Check(PC->IsAssigningOrder() && PC->GetPendingVerb() == EForceVerb::Attack, TEXT("Attack card opens the same selected-force mode as A"));
			FVector2D Point;
			const ACommandHUD* HUD = Cast<ACommandHUD>(PC->GetHUD());
			FVector2D Origin;
			float Size;
			if (!Check(HUD && HUD->GetMinimapScreenRect(Origin, Size), TEXT("Attack card has a live minimap target surface")))
				return true;
			const FVector Anchor = State->GetRegionAnchor(Target);
			Point = Origin + FVector2D((Anchor.Y + State->Arena->HalfExtent.Y) / (2.f * State->Arena->HalfExtent.Y), (State->Arena->HalfExtent.X - Anchor.X) / (2.f * State->Arena->HalfExtent.X)) * Size;
			PC->HandleHUDClick(Point);
			Check(Own->Verb == EForceVerb::Attack && Own->TargetRegionIndex == Target && !PC->IsAssigningOrder(),
				TEXT("Card Attack confirms a real owned Attack through the shared target input"));
			AttackSerial = Own->OrderSerial;
			Key(EKeys::R);
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			Key(EKeys::R, IE_Released);
			Check(Own->Verb == EForceVerb::Retreat && Own->OrderSerial != AttackSerial, TEXT("R commits a real immediate Retreat"));
			const uint32 KeySerial = Own->OrderSerial;
			Click(Own.Get(), EHUDAction::ForceCardRetreat);
			Check(Own->Verb == EForceVerb::Retreat && Own->OrderSerial != KeySerial, TEXT("Retreat card commits the same verb as R"));
			Click(Own.Get(), EHUDAction::ForceCardNever);
			Check(Own->RetreatThreshold == ERetreatThreshold::Never, TEXT("Never threshold changes the owned force"));
			Click(Own.Get(), EHUDAction::ForceCard25);
			Check(Own->RetreatThreshold == ERetreatThreshold::Percent25, TEXT("25% threshold changes the owned force"));
			Click(Own.Get(), EHUDAction::ForceCard40);
			Check(Own->RetreatThreshold == ERetreatThreshold::Percent40, TEXT("40% threshold changes the owned force"));
			Click(Own.Get(), EHUDAction::ForceCard60);
			Check(Own->RetreatThreshold == ERetreatThreshold::Percent60, TEXT("60% threshold changes the owned force"));
			Click(Own.Get(), EHUDAction::ForceCardProduction);
			Check(Producer->bProductionEnabled, TEXT("Card resumes its actual producer without selecting the building"));
			Click(Own.Get(), EHUDAction::ForceCardProduction);
			Check(!Producer->bProductionEnabled, TEXT("Card pauses its producer without unlocking its unit type"));
			Teammate(State);
			return true;
		}
		return false;
	}
private:
	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
			Test->AddError(Message);
		return Value;
	}
	void Key(FKey Value, EInputEvent Event = IE_Pressed)
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Value, Event, FPlatformTime::Cycles64()));
	}
	bool Click(AArmyGroup* Force, EHUDAction Action)
	{
		const ACommandHUD* HUD = Cast<ACommandHUD>(PC->GetHUD());
		FVector2D Point;
		EHUDAction Hit;
		return Check(HUD && HUD->FindForceCardScreenPosition(Force, Action, Point)
				&& HUD->GetForceCardAtScreenPosition(Point, Hit) == Force && Hit == Action && PC->HandleHUDClick(Point),
			TEXT("Force card's drawn control uses shared screen geometry and the real click path"));
	}
	AArmyGroup* MakeForce(UWorld* World, ACommandPlayerState* Wallet, int32 Number, const FVector& Location, ACommandBuilding* Building = nullptr)
	{
		const FTransform Transform(Location);
		AArmyGroup* Force = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			Wallet->TeamIndex == 0 ? PC.Get() : nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Force)
			return nullptr;
		Force->Initialize({ Wallet->TeamIndex, Wallet, 20 + Number, Building, Location });
		Force->ForceNumber = Number;
		Force->FinishSpawning(Transform);
		if (!Force->SpawnUnits())
			return nullptr;
		Force->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Force->GetUnits())
		{
			Unit->SetActorTickEnabled(false);
			Unit->GetCharacterMovement()->DisableMovement();
		}
		return Force;
	}
	bool Prepare(UWorld* World, ACommandGameState* State)
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<ACommandBuilding> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		State->bVerificationIncomePaused = true;
		ACommandPlayerState* Wallet = PC->GetPlayerState<ACommandPlayerState>();
		Wallet->Resources = 1000;
		const FVector Home = FromFriendlyHQ(State, 900.f, -900.f, 100.f);
		const FTransform Transform(Home + FVector(0.f, -450.f, -95.f));
		Producer = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform, PC.Get(), nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Producer.IsValid())
			return false;
		Producer->BuildingIndex = BarracksIndex;
		Producer->OwningPlayerState = Wallet;
		Producer->ConstructionProgress = 1.f;
		Producer->bForceConfigured = true;
		Producer->ProductionUnitIndex = UnitIndex(State, EUnitRole::Frontline);
		Producer->ForceNumber = 1;
		Producer->FinishSpawning(Transform);
		Producer->SetActorTickEnabled(false);
		Own = MakeForce(World, Wallet, 1, Home, Producer.Get());
		Producer->ForceGroup = Own.Get();
		Second = MakeForce(World, Wallet, 2, Home + FVector(700.f, 0.f, 0.f));
		ForeignWallet = World->SpawnActor<ACommandPlayerState>();
		ForeignWallet->CommanderIndex = Wallet->CommanderIndex == 0 ? 1 : 0;
		ForeignWallet->TeamIndex = 0;
		State->AddPlayerState(ForeignWallet.Get());
		const FVector ForeignHome = Home + FVector(0.f, 800.f, 0.f);
		const FTransform ForeignTransform(ForeignHome + FVector(0.f, -450.f, -95.f));
		ForeignProducer = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), ForeignTransform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(ForeignProducer.IsValid(), TEXT("Teammate producer fixture exists")))
			return false;
		ForeignProducer->BuildingIndex = BarracksIndex;
		ForeignProducer->OwningPlayerState = ForeignWallet.Get();
		ForeignProducer->ConstructionProgress = 1.f;
		ForeignProducer->bForceConfigured = true;
		ForeignProducer->ProductionUnitIndex = UnitIndex(State, EUnitRole::Frontline);
		ForeignProducer->ForceNumber = 1;
		ForeignProducer->FinishSpawning(ForeignTransform);
		ForeignProducer->SetActorTickEnabled(false);
		Foreign = MakeForce(World, ForeignWallet.Get(), 1, ForeignHome, ForeignProducer.Get());
		ForeignProducer->ForceGroup = Foreign.Get();
		Hostile = MakeForce(World, State->EnemyCommander, 1, HostileStaging(State));
		if (!Check(Own.IsValid() && Second.IsValid() && Foreign.IsValid() && Hostile.IsValid(), TEXT("Isolated force card fixtures exist")))
			return false;
		Target = TravelRegion(Own.Get(), State->EnemyHeadquarters->GetActorLocation());
		return Check(Target != INDEX_NONE, TEXT("Force card has a reachable map-derived target"));
	}
	bool States(ACommandGameState* State)
	{
		const FContext Context = MakeContext(PC.Get());
		Own->Verb = EForceVerb::Attack;
		Own->TargetRegionIndex = Target;
		Own->WaypointRegionIndex = Target;
		Own->Destination = State->GetRegionAnchor(Target);
		Own->Status = EForceStatus::Marching;
		Own->MarchSpeed = 300.f;
		FForceCard March;
		ReadForceCard(Context, *Own, 20, March);
		Check(March.Joined == 6 && March.Capacity == 6 && March.Travelling == 0 && March.State == ForceCardPolicy::EState::Marching,
			TEXT("Initial card shows all six joined members and the active marching state"));
		Check(March.Status.ToView().Contains(TEXT("0:20")), TEXT("Marching card includes the supplied travel ETA"));
		float Slowest = TNumericLimits<float>::Max();
		for (const AArmyUnit* Unit : Own->GetUnits())
			Slowest = FMath::Min(Slowest, Unit->GetDefinition()->MoveSpeed);
		const double Distance = FVector::Dist2D(Own->GetCenter(), Own->Destination);
		const int32 ExpectedETA = FMath::CeilToInt(Distance / FMath::Min(Slowest, 300.f));
		Check(ForceTravelETA::Compute(*Own, *State) == ExpectedETA,
			TEXT("ETA uses the route length divided by the slowest authored living member, respecting the selection speed cap"));
		Own->MarchSpeed = Slowest * .5f;
		Check(ForceTravelETA::Compute(*Own, *State) == FMath::CeilToInt(Distance / (Slowest * .5f)),
			TEXT("A slower multi-selection cap cannot produce the individual force's faster ETA"));
		Own->MarchSpeed = 300.f;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			AArmyUnit* Unit = Own->GetUnits().Last();
			Unit->ReceiveAttack(Unit->GetHealth(), Hostile->GetUnits()[0]);
		}
		Own->Status = EForceStatus::Withdrawing;
		Own->ResumeCount = 5;
		FForceCard Withdrawal;
		ReadForceCard(Context, *Own, INDEX_NONE, Withdrawal);
		Check(Withdrawal.Joined == 3 && Withdrawal.Status.ToView().Contains(TEXT("3/6")) && Withdrawal.Status.ToView().Contains(TEXT("5/6")),
			TEXT("Automatic withdrawal reports joined casualties and the retained five-of-six resume threshold"));
		Own->Status = EForceStatus::Refilling;
		FForceCard Recovery;
		ReadForceCard(Context, *Own, INDEX_NONE, Recovery);
		Check(Recovery.State == ForceCardPolicy::EState::Withdrawing && Recovery.Status.ToView() == Withdrawal.Status.ToView(),
			TEXT("Attack recovery remains Withdrawing while refilling rather than pretending to be manual Retreat"));
		Own->Verb = EForceVerb::Retreat;
		FForceCard Manual;
		ReadForceCard(Context, *Own, INDEX_NONE, Manual);
		Check(Manual.State == ForceCardPolicy::EState::Refilling, TEXT("Manual Retreat arrival changes to Refilling with weapons enabled"));
		Own->Verb = EForceVerb::MoveHold;
		Own->Status = EForceStatus::Holding;
		Own->bHoldResponding = true;
		Own->HoldThreatKind = EHoldThreatKind::Headquarters;
		FForceCard Response;
		ReadForceCard(Context, *Own, INDEX_NONE, Response);
		Check(Response.State == ForceCardPolicy::EState::Responding && Response.Status.ToView().Contains(TEXT("HQ under attack")),
			TEXT("Hold alarm responding status takes precedence over Holding"));
		const FTransform RigTransform(FromFriendlyHQ(State, 500.f, -1600.f, 5.f));
		ACommandBuilding* Rig = State->GetWorld()->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), RigTransform,
			PC.Get(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Rig != nullptr, TEXT("Drill Rig threatened-asset fixture exists")))
			return false;
		Rig->BuildingIndex = ExtractorIndex;
		Rig->OwningPlayerState = PC->GetPlayerState<ACommandPlayerState>();
		Rig->ConstructionProgress = 1.f;
		Rig->FinishSpawning(RigTransform);
		Rig->SetActorTickEnabled(false);
		Own->HoldThreatKind = EHoldThreatKind::Building;
		Own->HoldThreatenedAsset = Rig;
		ReadForceCard(Context, *Own, INDEX_NONE, Response);
		Check(Response.Status.ToView() == TEXT("Responding \u00B7 Drill Rig under attack"),
			TEXT("Responding card names the threatened Drill Rig, including old authored display-name assets"));
		Own->HoldThreatenedAsset = nullptr;
		Rig->Destroy();
		Own->bHoldResponding = false;
		Own->ResumeCount = 0;
		return true;
	}
	bool SelectionAndLayout(ACommandGameState* State)
	{
		ACommandCamera* Camera = Cast<ACommandCamera>(PC->GetPawn());
		if (!Check(Camera != nullptr, TEXT("Force bar owns a live command camera")))
			return false;
		PC->SelectActor(Producer.Get());
		FForceCard Highlight;
		ReadForceCard(MakeContext(PC.Get()), *Own, INDEX_NONE, Highlight);
		Check(Highlight.bHighlighted && !PC->IsForceSelected(Own.Get()), TEXT("Producer inspection lights its force card without selecting its force"));
		Camera->FocusOn(FromFriendlyHQ(State, 0.f, -1800.f, 0.f));
		const FVector Before = Camera->GetActorLocation();
		Click(Own.Get(), EHUDAction::None);
		Check(PC->IsForceSelected(Own.Get()) && Camera->GetActorLocation().Equals(Before, .01), TEXT("A card click selects the force without moving the camera"));
		PC->HandleForceCardClick(Second.Get(), EHUDAction::None, true);
		Check(PC->IsForceSelected(Own.Get()) && PC->IsForceSelected(Second.Get()), TEXT("Shift card click adds a second force"));
		PC->HandleForceCardClick(Own.Get(), EHUDAction::None, false, true);
		Check(FVector::Dist2D(Camera->GetActorLocation(), Own->GetCenter()) < 1., TEXT("Double-click card centres on that force"));
		const FContext Context = MakeContext(PC.Get());
		for (const FVector2D& Resolution : { FVector2D(1600.f, 900.f), FVector2D(1280.f, 720.f) })
		{
			const FLayout Layout = MakeLayout(Context, Resolution.X, Resolution.Y);
			ForEachForceCard(Context, Layout, [&](AArmyGroup*, const FRect& Rect) {
				Check(!Rect.Intersects(Layout.Build) && !Rect.Intersects(Layout.Bottom) && !Rect.Intersects(Layout.Minimap),
					TEXT("Cards clear build bar, deck and minimap at both required resolutions"));
				Check(IsPanelPoint(Context, Layout, Rect.Center()), TEXT("Card panel absorbs clicks instead of box-selecting battlefield units"));
			});
		}
		return true;
	}
	void Teammate(ACommandGameState* State)
	{
		PC->SelectForce(Foreign.Get());
		const FContext Context = MakeContext(PC.Get());
		FForceCard Card;
		ReadForceCard(Context, *Foreign, INDEX_NONE, Card);
		Check(!Card.bOwned && PC->GetSelectedForces().IsEmpty(), TEXT("Teammate card is inspection only, not a command selection"));
		int32 Controls = 0;
		ForEachForceCardButton(Card, { 0.f, 0.f, 360.f, ForceBarHeight }, [&](const FButton& Button, FStringView) {
			++Controls;
			Check(Button.Action == EHUDAction::PingTeammateForce, TEXT("The only teammate control is Need help here"));
		});
		Check(Controls == 1, TEXT("Read-only teammate card exposes exactly one ping control"));
		const uint32 Serial = Foreign->OrderSerial;
		const ERetreatThreshold Threshold = Foreign->RetreatThreshold;
		const bool bProducing = ForeignProducer->bProductionEnabled;
		PC->HandleForceCardClick(Foreign.Get(), EHUDAction::ForceCardRetreat, false);
		PC->HandleForceCardClick(Foreign.Get(), EHUDAction::ForceCardNever, false);
		PC->HandleForceCardClick(Foreign.Get(), EHUDAction::ForceCardProduction, false);
		Check(Foreign->OrderSerial == Serial && Foreign->RetreatThreshold == Threshold && ForeignProducer->bProductionEnabled == bProducing,
			TEXT("Forged teammate controls cannot retreat, change threshold or toggle its actual producer"));
		const int32 Pings = PC->PingCommands->GetEvents().Num();
		Click(Foreign.Get(), EHUDAction::PingTeammateForce);
		Check(PC->PingCommands->GetEvents().Num() == Pings + 1, TEXT("Teammate card ping creates a real team event"));
	}
	FAutomationTestBase* Test;
	double Started;
	int32 Stage = 0;
	int32 Target = INDEX_NONE;
	uint32 AttackSerial = 0;
	TWeakObjectPtr<ACommandPlayerController> PC;
	TWeakObjectPtr<ACommandBuilding> Producer, ForeignProducer;
	TWeakObjectPtr<AArmyGroup> Own, Second, Foreign, Hostile;
	TWeakObjectPtr<ACommandPlayerState> ForeignWallet;
};
}

bool FForceBarWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ForceBarTests::FScenario(this));
	return true;
}
#endif
