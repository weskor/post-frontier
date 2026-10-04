#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class AHeadquarters;
struct FJevPublishedPlan;

// Telemetry JSON helpers shared by the lifecycle, match, duel and report files.
namespace MatchSimulationJson
{
// Json's number storage is protected; expose mutation for encounter-owned values
// so live telemetry observations do not allocate replacement fields each frame.
class FDuelNumber : public FJsonValueNumber
{
public:
	explicit FDuelNumber(double Number) : FJsonValueNumber(Number) {}
	void Set(double Number) { Value = Number; }
};

inline TArray<TSharedPtr<FJsonValue>> Position(const FVector& P)
{
	return { MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z) };
}

inline void Append(FJsonObject& Object, const TCHAR* Field, const TSharedRef<FJsonObject>& Item)
{
	TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	Object.GetField<EJson::Array>(Field)->TryGetArray(Array);
	check(Array);
	Array->Add(MakeShared<FJsonValueObject>(Item));
}

inline TArray<TSharedPtr<FJsonValue>> Numbers(double Left, double Right)
{
	return { MakeShared<FDuelNumber>(Left), MakeShared<FDuelNumber>(Right) };
}

inline void UpdateNumbers(FJsonObject& Row, const FString& Field, double Left, double Right)
{
	const TArray<TSharedPtr<FJsonValue>>& Values = Row.GetArrayField(Field);
	StaticCastSharedPtr<FDuelNumber>(Values[0])->Set(Left);
	StaticCastSharedPtr<FDuelNumber>(Values[1])->Set(Right);
}

// One plan row, shared by plan-history events and snapshot plan listings.
void PlanFields(FJsonObject& Row, const FJevPublishedPlan& Plan, int32 ForceNumber,
	const FString& TargetStructureName, double StartWorldTime, float RemainingCommitment);

// An HQ's lifecycle for a team's snapshot: `hq_state` (online, offline, lost) and `hq_hold_seconds`, the progress
// of the hold on its main.
void HqFields(FJsonObject& Row, const AHeadquarters& HQ);
}
#endif
