#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

// The authored terrain of Habitable Zone v2, read from the JSON the generator built the level from.
namespace TerrainScenarioTests
{
struct FRamp
{
	int32 Plateau = INDEX_NONE;
	int32 To = INDEX_NONE;
	FVector2D Centre = FVector2D::ZeroVector;
	int32 Yaw = 0;
};

struct FRoute
{
	FString Name;
	TArray<int32> Regions;
};

struct FTerrainData
{
	TArray<FRoute> Routes;
	TArray<FRamp> Ramps;
	FVector2D FirstProp = FVector2D::ZeroVector;
};

inline bool LoadTerrain(FTerrainData& Out, FString& Error)
{
	FString Text;
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Build/Maps/AvailabilityZoneV2.json"));
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
	{
		Error = TEXT("Cannot read ") + Path;
		return false;
	}
	const TSharedPtr<FJsonObject>* Terrain = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Routes = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Plateaus = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
	if (!Root->TryGetObjectField(TEXT("terrain"), Terrain) || !(*Terrain)->TryGetArrayField(TEXT("routes"), Routes)
		|| !(*Terrain)->TryGetArrayField(TEXT("plateaus"), Plateaus) || !(*Terrain)->TryGetArrayField(TEXT("props"), Props))
	{
		Error = TEXT("Map JSON lacks terrain routes, plateaus or props");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Routes)
	{
		FRoute& Route = Out.Routes.AddDefaulted_GetRef();
		Route.Name = Value->AsObject()->GetStringField(TEXT("name"));
		for (const TSharedPtr<FJsonValue>& Region : Value->AsObject()->GetArrayField(TEXT("regions")))
			Route.Regions.Add(static_cast<int32>(Region->AsNumber()));
	}
	for (const TSharedPtr<FJsonValue>& Value : *Plateaus)
		for (const TSharedPtr<FJsonValue>& RampValue : Value->AsObject()->GetArrayField(TEXT("ramps")))
		{
			const TSharedPtr<FJsonObject> Ramp = RampValue->AsObject();
			const TArray<TSharedPtr<FJsonValue>>& Centre = Ramp->GetArrayField(TEXT("centre"));
			Out.Ramps.Add({ static_cast<int32>(Value->AsObject()->GetNumberField(TEXT("region"))),
				static_cast<int32>(Ramp->GetNumberField(TEXT("to"))), FVector2D(Centre[0]->AsNumber(), Centre[1]->AsNumber()),
				static_cast<int32>(Ramp->GetNumberField(TEXT("yaw"))) });
		}
	if (Out.Routes.IsEmpty() || Out.Ramps.IsEmpty() || Props->IsEmpty())
	{
		Error = TEXT("Map JSON terrain is empty");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Position = (*Props)[0]->AsObject()->GetArrayField(TEXT("pos"));
	Out.FirstProp = FVector2D(Position[0]->AsNumber(), Position[1]->AsNumber());
	return true;
}

inline FVector2D Downhill(const FRamp& Ramp)
{
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Ramp.Yaw));
	return FVector2D(FMath::RoundToDouble(FMath::Cos(Radians)), FMath::RoundToDouble(FMath::Sin(Radians)));
}

}

#endif
