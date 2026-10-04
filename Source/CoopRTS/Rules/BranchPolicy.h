#pragma once

#include "CoreMinimal.h"
#include "BranchPolicy.generated.h"

// Where a production building stands in its tier-2 branch purchase. The purchase is once per battle per
// building and replicates with the building, so a new battle starts every building at None.
UENUM(BlueprintType)
enum class EBranchPhase : uint8
{
	None,
	Upgrading,
	Done
};

// The purchase as it replicates with the building: one property, so a peer never sees a phase without its progress.
USTRUCT(BlueprintType)
struct FBranchState
{
	GENERATED_BODY()

	UPROPERTY()
	EBranchPhase Phase = EBranchPhase::None;
	// Seconds of upgrade done, 0 outside Upgrading; BranchPolicy::UpgradeSeconds when Done.
	UPROPERTY()
	float ProgressSeconds = 0.f;
};

// Tier-2 branches (forces.md "Barracks upgrades", decisions B1 and B2). Rules only: the purchase command, the
// building, the force's refit queue and the HUD read these, never copies.
namespace BranchPolicy
{
inline constexpr int32 PowerCost = 100;
inline constexpr int32 DataCost = 50;
inline constexpr float UpgradeSeconds = 20.f;

enum class EVerdict : uint8
{
	Accepted,
	NoBattle,
	NotOwner,
	NotProducer,
	NotBuilt,
	TypeNotLocked,
	NoBranch,
	Upgrading,
	AlreadyBought,
	Stunned,
	NeedResources
};

struct FInput
{
	bool bBattleLive = false;
	// The buyer is the commander who owns the building.
	bool bOwner = false;
	bool bProducer = false;
	// Finished and alive.
	bool bBuilt = false;
	// The building's force type is locked (its first Start has happened).
	bool bTypeLocked = false;
	// The catalogue has a branch of the locked type.
	bool bBranchExists = false;
	EBranchPhase Phase = EBranchPhase::None;
	bool bStunned = false;
	int32 Power = 0;
	int32 Data = 0;
};

struct FDecision
{
	EVerdict Verdict = EVerdict::NoBattle;
	// Resources still missing, for NeedResources.
	int32 PowerShort = 0;
	int32 DataShort = 0;
	bool IsAccepted() const { return Verdict == EVerdict::Accepted; }
};

// The first failing rule wins, in the order of EVerdict.
FDecision Evaluate(const FInput& In);
// "Lock a type first", "Need 50 more Data": what the button and the rejection read.
void AppendReason(FStringBuilderBase& Out, const FDecision& Decision);

// Production at the building waits for the whole upgrade.
bool PausesProduction(EBranchPhase Phase);

struct FUpgradeStep
{
	float Progress = 0.f;
	bool bCompleted = false;
};
// One tick of the upgrade timer: frozen while the building is stunned, completes at UpgradeSeconds.
FUpgradeStep Advance(float Progress, float DeltaSeconds, bool bStunned);

// A living member of a force, as the refit queue sees it.
struct FMember
{
	int32 Slot = INDEX_NONE;
	int32 UnitIndex = INDEX_NONE;
};
// The member refitted next: the lowest composition slot still in the base form. INDEX_NONE when none is left.
int32 NextRefit(TConstArrayView<FMember> Living, int32 BaseIndex);

struct FRefitProgress
{
	int32 Branched = 0;
	int32 Living = 0;
	bool IsComplete() const { return Branched >= Living; }
};
FRefitProgress RefitProgress(TConstArrayView<FMember> Living, int32 BranchIndex);

// A refit keeps the fraction of the maximum: HP and shield scale to the new maximum, rounded to the nearest
// point. A unit that is alive stays alive.
int32 ScaleDurability(int32 Current, int32 OldMax, int32 NewMax);

// Text of the panel and the force card. Names and summaries come from the unit data.
// "100 Power + 50 Data · 20 s".
void AppendPrice(FStringBuilderBase& Out);
// "MARKSMAN +20% range · 100 Power + 50 Data · 20 s".
void AppendButtonText(FStringBuilderBase& Out, FStringView BranchName, FStringView Summary);
// "Upgrading to Marksman 12 / 20 s".
void AppendUpgradeText(FStringBuilderBase& Out, FStringView BranchName, float Progress);
// "✓ MARKSMAN +20% range".
void AppendDoneText(FStringBuilderBase& Out, FStringView BranchName, FStringView Summary);
// "REFIT 2/5".
void AppendRefitChip(FStringBuilderBase& Out, const FRefitProgress& Progress);
// "Refit 2/5 → Marksman · cut-off members keep old form".
void AppendRefitLine(FStringBuilderBase& Out, const FRefitProgress& Progress, FStringView BranchName);
}
