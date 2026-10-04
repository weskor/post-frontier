#pragma once

#include "CoreMinimal.h"
#include "Rules/FortifyPolicy.h"
#include "GameFramework/Actor.h"
#include "Rules/RegionTraitPolicy.h"
#include "MapRegion.generated.h"

class ACapturePoint;
class ACommandGameState;

UENUM(BlueprintType)
enum class ERegionRole : uint8
{
	Main,
	Natural,
	Reward,
	Tactical
};

UCLASS()
class COOPRTS_API AMapRegion : public AActor
{
	GENERATED_BODY()
public:
	AMapRegion();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool Contains(const FVector& WorldLocation) const;
	const TArray<FVector>& GetDefendPosts() const { return DefendPosts; }
	ERegionTrait GetTrait() const { return Trait; }

	// Fortify (FortifyPolicy): the published state and the one-place reads of it. Times are server world seconds.
	FortifyPolicy::FRegionState GetFortify() const { return { FortifyTeam, FortifyExpiresAt }; }
	float GetServerNow() const;
	bool IsFortifyActive() const;
	// Incoming-multiplier list entry for a unit or building of VictimTeam standing in this region.
	float FortifyIncomingMultiplier(int32 VictimTeam) const;
	// The same entry for a structure at Location: 1 outside every region.
	static float FortifyIncomingAt(const ACommandGameState& State, const FVector& Location, int32 VictimTeam);
	bool IsCaptureFrozen() const;
	// Authority only: protects Team for FortifyPolicy::DurationSeconds from now. A recast refreshes, never stacks.
	void StartFortify(int32 Team, int32 CasterCommander);
	// Authority only: clears the state; a lost region also tells the team through the feed.
	void EndFortify(FortifyPolicy::EEnd Reason);

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Fortify")
	int32 FortifyTeam = -1;
	// Commander slot of the latest caster: the badge shows its colour and name.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Fortify")
	int32 FortifyCaster = -1;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Fortify")
	float FortifyExpiresAt = 0.f;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	int32 RegionIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	FText DisplayName;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	ERegionRole RegionRole = ERegionRole::Tactical;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	int32 HomeTeam = -1;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<FVector2D> Polygon;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<FVector> DefendPosts;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<int32> Neighbours;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	TObjectPtr<ACapturePoint> Anchor;
	// What the region does to units standing inside it; the map generator sets it by name.
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	ERegionTrait Trait = ERegionTrait::None;
};
