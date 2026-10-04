#include "JevProductionVerification.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyTestSetup.h"
#include "CommandBuilding.h"
#include "CommandCamera.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Engine/World.h"
#include "Json.h"
#include "MapRegion.h"

namespace JevProductionVerification
{
using namespace CoopRTSNetworkVerification::Probe;

namespace
{
ACommandCamera* CameraOf(UWorld& World)
{
	ACommandPlayerController* Controller = LocalController(&World);
	return Controller ? Cast<ACommandCamera>(Controller->GetPawn()) : nullptr;
}

FString Focus(UWorld& World, const TSharedPtr<FJsonObject>& Request)
{
	ACommandCamera* Camera = CameraOf(World);
	if (!Camera)
		return TEXT("JEV production fixture camera unavailable");
	Camera->FocusOn(FVector(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")), 0.));
	return FString();
}

FString Produce(UWorld& World, const TSharedPtr<FJsonObject>& Request)
{
	ACommandGameState* State = World.GetGameState<ACommandGameState>();
	ACommandCamera* Camera = CameraOf(World);
	if (!State || !Camera || !IsValid(State->EnemyCommander) || !IsValid(State->EnemyHeadquarters))
		return TEXT("JEV production fixture world unavailable");
	int32 Role = INDEX_NONE;
	if (!Request->TryGetNumberField(TEXT("role"), Role) || Role < 0 || Role >= static_cast<int32>(EUnitRole::Unset))
		return TEXT("JEV production fixture needs a role in range");
	// Enough for the barracks, the configuration fee and a full squad; the isolated planner spends nothing.
	State->EnemyCommander->Resources = 5000;
	ACommandBuilding* Barracks = PlaceBarracks(*State);
	if (!Barracks)
		return TEXT("no legal JEV barracks site beside its HQ");
	Barracks->Tick(60.f); // Accelerated construction fixture, not natural completion proof.
	if (!Barracks->IsComplete()
		|| !FCommandService::ConfigureProduction(State->EnemyCommander, Barracks, static_cast<EUnitRole>(Role), true).IsAccepted())
		return TEXT("JEV barracks did not complete and accept its production role");
	State->EnemyCommander->ForceNetUpdate();
	Camera->FocusOn(Barracks->GetActorLocation());
	return FString();
}
}

// Rings of sites around JEV's HQ, as its own economy searches; the first legal placement wins.
ACommandBuilding* PlaceBarracks(ACommandGameState& State)
{
	const FVector Center = State.EnemyHeadquarters->GetActorLocation();
	const AMapRegion* Main = State.FindRegionAt(Center);
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			Point.Z = 5.f;
			Point = State.ResolveBuildingLocation(ArmyTestSetup::BarracksIndex, Point, 5);
			FString Reason;
			if (State.FindRegionAt(Point) != Main || !State.ValidateBuildingPlacement(ArmyTestSetup::BarracksIndex, 5, Point, Reason))
				continue;
			if (ACommandBuilding* Building = FCommandService::PlaceBuilding(State.EnemyCommander, ArmyTestSetup::BarracksIndex, Point).Building)
				return Building;
		}
	return nullptr;
}

bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error)
{
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (Action != TEXT("jevProduction") && Action != TEXT("jevFocus"))
		return false;
	Error = !bAuthorityFixtures || World.GetNetMode() != NM_ListenServer || !World.GetAuthGameMode()
		? FString(TEXT("JEV production fixture requires opted-in authority host"))
		: Action == TEXT("jevFocus") ? Focus(World, Request)
									 : Produce(World, Request);
	return true;
}
}
#endif
