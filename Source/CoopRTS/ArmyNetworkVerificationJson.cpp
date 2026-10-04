// JSON and lookup helpers shared by the network probe's snapshot and action files.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Content/MatchContent.h"
#include "EngineUtils.h"
#include "Json.h"
#include "UObject/UObjectArray.h"

namespace CoopRTSNetworkVerification::Probe
{
TSharedPtr<FJsonObject> Object() { return MakeShared<FJsonObject>(); }
int32 LifetimeId(const UObject* Value)
{
	// Object indices can be reused after travel; weak-object serials distinguish lifetimes.
	return GUObjectArray.AllocateSerialNumber(Value->GetUniqueID());
}
void Number(const TSharedPtr<FJsonObject>& ObjectValue, const TCHAR* Key, double Value)
{
	ObjectValue->SetNumberField(Key, Value);
}
void Vector(const TSharedPtr<FJsonObject>& ObjectValue, const TCHAR* Key, FVector Value)
{
	TArray<TSharedPtr<FJsonValue>> Coordinates;
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.X));
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.Y));
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.Z));
	ObjectValue->SetArrayField(Key, Coordinates);
}
// Unit definition currently selected by a producer; production rules read the same definition.
const UArmyUnitDefinition* ProductionDefinition(const ACommandGameState& State, const ACommandBuilding& Building)
{
	if (!State.Content)
		return nullptr;
	for (int32 Index = 0; const UArmyUnitDefinition* Definition = State.Content->Unit(Index); ++Index)
		if (Definition->Role == Building.ProductionRole)
			return Definition;
	return nullptr;
}
AArmyGroup* FindArmy(UWorld* World, int32 Owner, int32 Index)
{
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		if (It->GetArmyIndex() == Index && IsValid(It->GetOwningPlayerState())
			&& It->GetOwningPlayerState()->CommanderIndex == Owner)
			return *It;
	return nullptr;
}
ACommandPlayerController* LocalController(UWorld* World)
{
	for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
		if (It->IsLocalController())
			return *It;
	return nullptr;
}
}
#endif
