#pragma once

#include "CoreMinimal.h"
#include "Commands/CommandService.h"

class AActor;

// Authoritative planning commands (battle.md "Opening"). Each names the issuing commander, never a target's owner.
// Every edit needs an active phase and a commander who is not Ready; un-Ready unlocks. Ready never needs a lock.
struct COOPRTS_API FPlanningCommands
{
	// Ready ends planning for good once every human commander is Ready; un-Ready takes it back until then.
	static FCommandResult SetReady(ACommandPlayerState* Commander, bool bReady);
	// Places the kit's Barracks (EBuildingKind::Barracks) or Drill Rig (EBuildingKind::Extractor) with the normal
	// placement rules, or moves it. It is free and stands finished; a refused move leaves the piece where it was.
	static FCommandResult PlaceKit(ACommandPlayerState* Commander, EBuildingKind Piece, const FVector& Location);
	// The unit type the Barracks produces from 0:00.
	static FCommandResult SetUnitType(ACommandPlayerState* Commander, EUnitRole Role);
	// The first order for the Barracks' force, issued at 0:00. Queueing appends (three orders at most, as for any
	// force); otherwise it replaces the list. Move & Hold and Attack only.
	static FCommandResult SetFirstOrder(ACommandPlayerState* Commander, EForceVerb Verb, int32 RegionIndex,
		AActor* Structure = nullptr, bool bQueue = false);
	static FCommandResult ClearFirstOrders(ACommandPlayerState* Commander);
	// The region the Barracks' force starts from: the Barracks' own, or the team main while it is unplaced. The first
	// order's reachability starts here, on the server and in the controller's preview alike.
	static int32 SourceRegion(const class ACommandGameState& State, const struct FPlanningKit& Kit);
};
