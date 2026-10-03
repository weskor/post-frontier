#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "MapRegion.h"
#include "NavigationSystem.h"
#include "Commands/ForceCapState.h"
#include "HUD/HUDPanels.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceCapWorldTest, "CoopRTS.Construction.ForceCap",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ForceCapScenario
{
using namespace ArmyTestSetup;
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		UWorld* World = ArmyTestSetup::World();
		if (!World || World->GetTimeSeconds() < 3.f)
			return false;
		ACommandPlayerController* PC = Controller(World);
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!Wallet || Wallet->CommanderIndex < 0 || !MapReady(State) || !State->Content || !IsValid(State->EnemyCommander))
			return false;
		if (Stage == 0)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				It->Destroy();
			State->bVerificationIncomePaused = true;
			Wallet->Resources = State->EnemyCommander->Resources = 10000;
			Foreign = World->SpawnActor<ACommandPlayerState>();
			if (!Foreign.IsValid())
				return Fail(TEXT("Second human roster fixture could not spawn"));
			Foreign->CommanderIndex = Wallet->CommanderIndex == 0 ? 1 : 0;
			Foreign->Resources = 10000;
			State->AddPlayerState(Foreign.Get());
			if (!Place(State, Foreign.Get(), BarracksIndex) || !Place(State, Wallet, WorkshopIndex))
				return true;
			for (int32 Index = 0; Index < 4; ++Index)
			{
				ACommandBuilding* Producer = Place(State, Wallet, BarracksIndex);
				if (!Producer)
					return true;
				if (Index == 0)
					First = Producer;
				if (!Check(!Producer->IsComplete(), TEXT("Unfinished producers reserve force slots before recruitment")))
					return true;
			}
			const CommandForceCap::FOccupancy Coop = CommandForceCap::Read(*State, *Wallet);
			if (!Check(Coop.Count == 4 && Coop.Limit == 4, TEXT("Co-op counts only this commander's living producers, not workshop or teammate"))
				|| !Rejected(State, Wallet, 4) || !HUDState(PC, 4, 4, true))
				return true;
			for (int32 Index = 0; Index < 6; ++Index)
				if (!Place(State, State->EnemyCommander, BarracksIndex))
					return true;
			const CommandForceCap::FOccupancy Enemy = CommandForceCap::Read(*State, *State->EnemyCommander);
			if (!Check(Enemy.Count == 6 && !Enemy.IsFull(), TEXT("JEV places six paid producers without the human force cap")))
				return true;
			Stage = 1;
			return false;
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav || Nav->IsNavigationBuildInProgress())
			return false;
		if (Stage == 1)
		{
			if (!First.IsValid())
				return Fail(TEXT("First producer disappeared before orphan proof"));
			First->Tick(60.f);
			if (!Check(FCommandService::ConfigureProduction(Wallet, First.Get(), EUnitRole::Frontline, true).IsAccepted(),
					TEXT("Completed paid producer configures its force")))
				return true;
			First->TickProduction(First->GetProductionDuration());
			FCommandService::ConfigureProduction(Wallet, First.Get(), EUnitRole::Frontline, false);
			Orphan = First->ForceGroup;
			if (!Check(Orphan.IsValid() && Orphan->GetUnits().Num() == 1, TEXT("Paid production creates a real survivor before destroying its producer")))
				return true;
			Orphan->SetActorTickEnabled(false);
			Survivor = Orphan->GetUnits()[0];
			Survivor->SetActorTickEnabled(false);
			AArmyGroup* Enemy = SpawnGroup(World, nullptr, -1, HostileStaging(State));
			if (!Enemy || Enemy->GetUnits().IsEmpty())
				return Fail(TEXT("Isolated hostile damage fixture could not spawn"));
			Enemy->SetActorTickEnabled(false);
			for (AArmyUnit* Unit : Enemy->GetUnits())
				Unit->SetActorTickEnabled(false);
			First->ReceiveAttack(First->MaxHealth(), Enemy->GetUnits()[0]);
			if (!Check(!First.IsValid() && Orphan.IsValid() && Survivor->IsAlive() && !Orphan->GetProductionBuilding()
						&& CommandForceCap::Read(*State, *Wallet).Count == 3,
					TEXT("Lethal producer damage frees a slot while its living orphan does not count")))
				return true;
			if (!HUDState(PC, 3, 4, false))
				return true;
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			if (!Place(State, Wallet, BarracksIndex))
				return true;
			if (!Check(Orphan.IsValid() && Survivor.IsValid() && Survivor->IsAlive()
						&& CommandForceCap::Read(*State, *Wallet).Count == 4,
					TEXT("Paid replacement fills the freed slot without counting the surviving orphan"))
				|| !Rejected(State, Wallet, 4))
				return true;
			State->RemovePlayerState(Foreign.Get());
			Foreign->Destroy();
			if (!Check(CommandForceCap::Read(*State, *Wallet).Limit == 5, TEXT("One human roster entry restores the solo cap"))
				|| !HUDState(PC, 4, 5, false))
				return true;
			if (!Place(State, Wallet, BarracksIndex))
				return true;
			if (!Check(CommandForceCap::Read(*State, *Wallet).Count == 5, TEXT("Solo can place its fifth producer"))
				|| !Rejected(State, Wallet, 5) || !HUDState(PC, 5, 5, true))
				return true;
			Test->AddInfo(TEXT("Force cap: unfinished co-op 4, foreign/non-producer exclusion, no debit or spawn on rejection, lethal destruction frees a slot, live orphan excluded, solo fifth accepted/sixth rejected, six paid JEV producers accepted."));
			return true;
		}
		return Fail(TEXT("Unexpected force-cap scenario stage"));
	}
private:
	bool HUDState(ACommandPlayerController* PC, int32 Count, int32 Limit, bool bCapped)
	{
		using namespace CommandHUDPanels;
		const FContext Context = MakeContext(PC);
		const FLayout Layout = MakeLayout(Context, 1280.f, 720.f);
		if (!Check(Context.ForceSlots.Count == Count && Context.ForceSlots.Limit == Limit,
				TEXT("Build bar occupancy follows construction, destruction and the live human roster")))
			return false;
		bool bProducer = false, bWorkshop = false;
		FRect ProducerRect;
		ForEachButton(Context, Layout, [&](const FButton& Button) {
			if (Button.Action == EHUDAction::BuildSlot0)
			{
				bProducer = Button.Available() == !bCapped && Button.Block == (bCapped ? EBlock::ForceCap : EBlock::None);
				ProducerRect = Button.Rect;
			}
			if (Button.Action == EHUDAction::BuildSlot2)
				bWorkshop = Button.Available();
		});
		if (!Check(bProducer && bWorkshop, TEXT("Cap disables only producer buttons and frees them when a slot opens")))
			return false;
		if (!bCapped)
			return true;
		if (!Check(HitTest(Context, Layout, ProducerRect.Center()) == EHUDAction::BuildSlot0,
				TEXT("Capped producer button remains a hit target for its explanation")))
			return false;
		const int32 Balance = Context.Wallet->Resources;
		// Null-RHI has no pixel viewport; B Q enters the same blocked-action handler as a build-card click.
		const FInputDeviceId Device = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		PC->InputKey(FInputKeyEventArgs(nullptr, Device, EKeys::B, IE_Pressed, FPlatformTime::Cycles64()));
		PC->InputKey(FInputKeyEventArgs(nullptr, Device, EKeys::Q, IE_Pressed, FPlatformTime::Cycles64()));
		return Check(!PC->IsPlacingBuilding() && Context.Wallet->Resources == Balance
				&& PC->GetOrderFeedback().Contains(TEXT("Force cap")) && PC->GetFeedbackOpacity() > 0.f,
			TEXT("Capped build action explains rejection without entering placement or paying"));
	}
	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
			Test->AddError(Message);
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	bool Location(ACommandGameState* State, ACommandPlayerState* Wallet, int32 BuildingIndex, FVector& Result)
	{
		const FVector Center = (Wallet->TeamIndex == 5 ? State->EnemyHeadquarters : State->FriendlyHeadquarters)->GetActorLocation();
		for (int32 Ring = 0; Ring < 9; ++Ring)
			for (int32 Direction = 0; Direction < 32; ++Direction)
			{
				const float Angle = Direction * PI / 16.f;
				FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
				Point.Z = 5.f;
				Point = State->ResolveBuildingLocation(BuildingIndex, Point, Wallet->TeamIndex);
				if (State->FindRegionAt(Point) != State->FindRegionAt(Center))
					continue;
				FString Reason;
				if (State->ValidateBuildingPlacement(BuildingIndex, Wallet->TeamIndex, Point, Reason))
				{
					Result = Point;
					return true;
				}
			}
		return false;
	}
	ACommandBuilding* Place(ACommandGameState* State, ACommandPlayerState* Wallet, int32 BuildingIndex)
	{
		FVector Point;
		if (!Location(State, Wallet, BuildingIndex, Point))
		{
			Fail(TEXT("No legal force-cap fixture footprint"));
			return nullptr;
		}
		const int32 Before = Wallet->Resources;
		ACommandBuilding* Building = FCommandService::PlaceBuilding(Wallet, BuildingIndex, Point).Building;
		if (!Check(IsValid(Building) && Wallet->Resources == Before - State->Content->Building(BuildingIndex)->BuildCost,
				TEXT("Force-cap scenario placement creates a paid building")))
			return nullptr;
		Building->SetActorTickEnabled(false); // Construction is controlled; under-construction occupancy is the assertion.
		return Building;
	}
	bool Rejected(ACommandGameState* State, ACommandPlayerState* Wallet, int32 Cap)
	{
		FVector Point;
		if (!Location(State, Wallet, BarracksIndex, Point))
		{
			Fail(TEXT("No legal footprint for cap rejection"));
			return false;
		}
		const int32 Balance = Wallet->Resources;
		const int32 Buildings = State->Buildings.Num();
		const FCommandResult Result = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Point);
		return Check(!Result.IsAccepted() && !Result.Building && Wallet->Resources == Balance
				&& State->Buildings.Num() == Buildings && Result.Message.Contains(FString::Printf(TEXT("%d/%d"), Cap, Cap)),
			TEXT("A geometrically valid capped placement explains the cap without spending or spawning"));
	}
	FAutomationTestBase* Test;
	int32 Stage = 0;
	TWeakObjectPtr<ACommandPlayerState> Foreign;
	TWeakObjectPtr<ACommandBuilding> First;
	TWeakObjectPtr<AArmyGroup> Orphan;
	TWeakObjectPtr<AArmyUnit> Survivor;
};
}

bool FForceCapWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ForceCapScenario::FScenario(this));
	return true;
}
#endif
