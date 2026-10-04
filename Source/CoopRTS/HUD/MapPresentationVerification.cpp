#include "MapPresentationVerification.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyGroup.h"
#include "ArmyNetworkVerification.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandCamera.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "DepositSite.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GroundHeight.h"
#include "Json.h"
#include "MapRegion.h"

namespace MapPresentationVerification
{
using namespace CoopRTSNetworkVerification::Probe;

namespace
{
TArray<TWeakObjectPtr<AArmyGroup>> Spawned;
int32 ScramblerIndex = INDEX_NONE;
int32 ShieldedIndex = INDEX_NONE;

AMapRegion* FindRegion(const ACommandGameState& State, int32 Index)
{
	for (AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex == Index)
			return Region;
	return nullptr;
}

FString Control(AMapRegion& Region, int32 Team)
{
	if (!IsValid(Region.Anchor) || (Team != -1 && Team != 0 && Team != 5))
		return TEXT("map presentation control fixture needs an anchored region and team -1, 0 or 5");
	Region.Anchor->ControllingTeam = Team;
	Region.Anchor->ForceNetUpdate();
	return FString();
}

// A finished Drill Rig of the humans on the region's first free deposit.
FString Rig(UWorld& World, ACommandGameState& State, ACommandPlayerState* Builder, const AMapRegion& Region)
{
	ADepositSite* Deposit = nullptr;
	for (ADepositSite* Candidate : State.Deposits)
		if (IsValid(Candidate) && Candidate->RegionIndex == Region.RegionIndex && !IsValid(Candidate->Extractor))
		{
			Deposit = Candidate;
			break;
		}
	if (!Deposit || !Builder)
		return TEXT("map presentation rig fixture needs a free deposit and a commander");
	const FTransform Transform(Deposit->GetActorLocation());
	ACommandBuilding* Building = World.SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform, nullptr,
		nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Building)
		return TEXT("map presentation rig fixture allocation failed");
	Building->BuildingIndex = ArmyTestSetup::ExtractorIndex;
	Building->TeamIndex = 0;
	Building->OwningPlayerState = Builder;
	Building->Deposit = Deposit;
	Building->ConstructionProgress = 1.f;
	Deposit->Extractor = Building;
	Building->FinishSpawning(Transform);
	return FString();
}

int32 AddUnit(ACommandGameState& State, int32 Shield, float PulseRadius)
{
	UArmyUnitDefinition* Definition = NewObject<UArmyUnitDefinition>(State.Content);
	Definition->Id = FName(*FString::Printf(TEXT("map-presentation-%d"), State.Content->Units.Num()));
	Definition->Role = PulseRadius > 0.f ? EUnitRole::Support : EUnitRole::Frontline;
	Definition->ArmorClass = PulseRadius > 0.f ? EArmorClass::Light : EArmorClass::Heavy;
	Definition->DamageType = PulseRadius > 0.f ? EDamageType::EMP : EDamageType::Kinetic;
	Definition->MaxHealth = 100000;
	Definition->MaxShield = Shield;
	Definition->AttackDamage = 0;
	Definition->Range = 550.f;
	Definition->Interval = 1000.f;
	Definition->MoveSpeed = 400.f;
	Definition->PulseInterval = PulseRadius > 0.f ? 10.f : 0.f;
	Definition->PulseRadius = PulseRadius;
	Definition->PulseBuildingStunSeconds = PulseRadius > 0.f ? 3.f : 0.f;
	Definition->UnitCost = 1;
	Definition->Capacity = 6;
	Definition->UnitDuration = 1.f;
	return State.Content->Units.Add(Definition);
}

AArmyUnit* SpawnOne(UWorld& World, ACommandGameState& State, ACommandPlayerController* Owner, bool bHostile, int32 UnitIndex,
	const FVector& Where)
{
	const FTransform Transform(Where);
	AArmyGroup* Group = World.SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform, bHostile ? nullptr : Owner,
		nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Group)
		return nullptr;
	Group->Initialize({ bHostile ? 5 : 0, bHostile ? State.EnemyCommander.Get() : Owner->GetPlayerState<ACommandPlayerState>(),
		bHostile ? -1 : 0, nullptr, Where });
	Group->FinishSpawning(Transform);
	Group->SetActorTickEnabled(false);
	Spawned.Add(Group);
	AArmyUnit* Unit = Group->SpawnMember(UnitIndex, Where, 0);
	if (!Unit)
		Group->Destroy();
	else
		Unit->SetActorLocation(Where, false, nullptr, ETeleportType::TeleportPhysics);
	return Unit;
}

// A Scrambler beside a hostile shielded unit in the region: the real unit tick casts the pulse on its own.
FString Pulse(UWorld& World, ACommandGameState& State, ACommandPlayerController& Owner, const AMapRegion& Region)
{
	if (ScramblerIndex == INDEX_NONE)
	{
		ScramblerIndex = AddUnit(State, 0, 400.f);
		ShieldedIndex = AddUnit(State, 80, 0.f);
	}
	const FVector Anchor = State.GetRegionAnchor(Region.RegionIndex);
	const FVector Ground = GroundHeight::Snap(World, Anchor) + FVector(0.f, 0.f, 95.f);
	AArmyUnit* Scrambler = SpawnOne(World, State, &Owner, false, ScramblerIndex, Ground);
	AArmyUnit* Shielded = SpawnOne(World, State, &Owner, true, ShieldedIndex, Ground + FVector(260.f, 0.f, 0.f));
	return Scrambler && Shielded ? FString() : TEXT("map presentation pulse fixture spawn failed");
}

// Centres the camera Weight of the way from the region's anchor to its Drill Rig, so plate, rig and labels clear the panels.
FString FocusRig(ACommandPlayerController& Controller, const ACommandGameState& State, const AMapRegion& Region, double Weight)
{
	ACommandCamera* Camera = Cast<ACommandCamera>(Controller.GetPawn());
	if (!Camera)
		return TEXT("map presentation camera unavailable");
	FVector Target = State.GetRegionAnchor(Region.RegionIndex);
	for (const ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && Deposit->RegionIndex == Region.RegionIndex && IsValid(Deposit->Extractor))
		{
			Target = FMath::Lerp(Target, Deposit->GetActorLocation(), FMath::Clamp(Weight, 0., 1.));
			break;
		}
	Camera->FocusOn(Target);
	return FString();
}

// Centres the camera where the line between two regions' anchors leaves the first: where a snapped cable breaks.
FString FocusEdge(ACommandPlayerController& Controller, const ACommandGameState& State, const AMapRegion& Region,
	const AMapRegion* Other)
{
	ACommandCamera* Camera = Cast<ACommandCamera>(Controller.GetPawn());
	if (!Camera || !Other)
		return TEXT("map presentation camera or second region unavailable");
	// Where the straight line between the anchors leaves Region: the same break point the cable uses.
	const FVector From = State.GetRegionAnchor(Region.RegionIndex), To = State.GetRegionAnchor(Other->RegionIndex);
	double Inside = 0., Outside = 1.;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		const double Middle = (Inside + Outside) * .5;
		(Region.Contains(FMath::Lerp(From, To, Middle)) ? Inside : Outside) = Middle;
	}
	Camera->FocusOn(FMath::Lerp(From, To, Outside));
	return FString();
}

void Clear(UWorld& World)
{
	for (const TWeakObjectPtr<AArmyGroup>& Group : Spawned)
		if (Group.IsValid())
			Group->Destroy();
	Spawned.Reset();
	if (AWorldSettings* Settings = World.GetWorldSettings())
		Settings->SetTimeDilation(1.f);
}
}

bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error)
{
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (!Action.StartsWith(TEXT("mapPres")))
		return false;
	ACommandPlayerController* Controller = LocalController(&World);
	ACommandGameState* State = World.GetGameState<ACommandGameState>();
	if (!Controller || !State || !State->Content)
		Error = TEXT("map presentation world unavailable");
	else if (!bAuthorityFixtures || World.GetNetMode() != NM_ListenServer || !World.GetAuthGameMode())
		Error = TEXT("map presentation fixture requires opted-in authority host");
	else if (Action == TEXT("mapPresClear"))
		Clear(World);
	else if (Action == TEXT("mapPresZoom"))
	{
		// Wheel steps: negative zooms out; the camera clamps at the arena.
		ACommandCamera* Camera = Cast<ACommandCamera>(Controller->GetPawn());
		if (Camera)
			Camera->Zoom(static_cast<float>(Request->GetNumberField(TEXT("steps"))));
		else
			Error = TEXT("map presentation camera unavailable");
	}
	else if (Action == TEXT("mapPresDilation"))
	{
		const double Factor = Request->GetNumberField(TEXT("factor"));
		if (Factor < .01 || Factor > 1.)
			Error = TEXT("map presentation time dilation out of bounds");
		else
			World.GetWorldSettings()->SetTimeDilation(static_cast<float>(Factor));
	}
	else
	{
		AMapRegion* Region = FindRegion(*State, static_cast<int32>(Request->GetIntegerField(TEXT("region"))));
		if (!Region)
			Error = TEXT("map presentation region unavailable");
		else if (Action == TEXT("mapPresControl"))
			Error = Control(*Region, static_cast<int32>(Request->GetIntegerField(TEXT("team"))));
		else if (Action == TEXT("mapPresRig"))
			Error = Rig(World, *State, Controller->GetPlayerState<ACommandPlayerState>(), *Region);
		else if (Action == TEXT("mapPresFocusRig"))
			Error = FocusRig(*Controller, *State, *Region, Request->GetNumberField(TEXT("weight")));
		else if (Action == TEXT("mapPresFocusEdge"))
			Error = FocusEdge(*Controller, *State, *Region, FindRegion(*State, static_cast<int32>(Request->GetIntegerField(TEXT("other")))));
		else if (Action == TEXT("mapPresPulse"))
			Error = Pulse(World, *State, *Controller, *Region);
		else
			Error = TEXT("unknown map presentation action");
	}
	return true;
}
}
#endif
