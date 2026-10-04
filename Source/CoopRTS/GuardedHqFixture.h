#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "EnemyCommander.h"
#include "EngineUtils.h"
#include "FailoverNode.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "ObjectiveAnnouncer.h"
#include "TeamEconomyFixture.h"

// Shared by the guarded-HQ world scenarios: the two HQs of the fixture's map, nodes beside them, units standing
// in a main, and the clean state every scenario starts and ends with.
namespace GuardedHqWorld
{
inline constexpr int32 Teams[] = { 0, 5 };

inline AHeadquarters& Hq(const FTeamEconomyFixture& F, int32 Team)
{
	return *(Team == 0 ? F.State->FriendlyHeadquarters : F.State->EnemyHeadquarters);
}

// The main region of each side on the fixture's map: 0 the humans', 1 JEV's.
inline int32 Main(int32 Team)
{
	return Team == 0 ? 0 : 1;
}

inline int32 Opponent(int32 Team)
{
	return Team == 0 ? 5 : 0;
}

inline AFailoverNode* SpawnNode(FTeamEconomyFixture& F, int32 Team, int32 Slot)
{
	const FTransform Transform(Hq(F, Team).GetActorLocation() + FVector(0.f, 600.f + Slot * 400.f, 0.f));
	AFailoverNode* Node = F.World->SpawnActorDeferred<AFailoverNode>(AFailoverNode::StaticClass(), Transform, nullptr,
		nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Node)
		return nullptr;
	Node->TeamIndex = Team;
	Node->FinishSpawning(Transform);
	return Node;
}

inline void Freeze(AArmyGroup& Group)
{
	Group.SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Group.GetUnits())
		Unit->SetActorTickEnabled(false);
}

// A living unit of Team standing at the anchor of a region, frozen so it neither fights nor walks.
inline AArmyUnit* Stand(FTeamEconomyFixture& F, int32 Team, int32 Region)
{
	static int32 Serial = 60;
	if (Team == 5)
		return F.SpawnHostileIn(Region);
	AArmyGroup* Group = ArmyTestSetup::SpawnGroup(F.World, F.Controller, Serial++, F.State->GetRegionAnchor(Region) + FVector(0.f, 0.f, 100.f));
	if (!Group || Group->GetUnits().IsEmpty())
		return nullptr;
	Freeze(*Group);
	return Group->GetUnits()[0];
}

// A unit of the side opposing Victim that can deal damage and stands in neither main.
inline AArmyUnit* Striker(FTeamEconomyFixture& F, int32 Victim)
{
	return Victim == 0 ? F.SpawnHostileIn(F.Neck) : Stand(F, 0, F.Neck);
}

inline void Remove(AArmyUnit* Unit)
{
	if (IsValid(Unit) && IsValid(Unit->GetGroup()))
		Unit->GetGroup()->Destroy();
}

inline void ClearGroups(const FTeamEconomyFixture& F, int32 Team)
{
	for (TActorIterator<AArmyGroup> It(F.World); It; ++It)
		if (It->GetTeamIndex() == Team)
			It->Destroy();
}

// The map's HQs are shared by every scenario: each starts and ends with both online, unhurt, unguarded and
// with their emergency forces unspent.
inline void Clean(FTeamEconomyFixture& F)
{
	for (AMapRegion* Region : F.State->Regions)
		if (IsValid(Region))
		{
			Region->FortifyTeam = -1;
			Region->FortifyCaster = -1;
			Region->FortifyExpiresAt = 0.f;
		}
	for (ACommandPlayerState* Wallet : F.Wallets)
		Wallet->FortifyReadyAt = 0.f;
	for (TActorIterator<AFailoverNode> It(F.World); It; ++It)
		It->Destroy();
	for (TActorIterator<AEnemyCommander> It(F.World); It; ++It)
		It->Destroy();
	for (const int32 Team : Teams)
	{
		Hq(F, Team).ResetForTest(Hq(F, Team).MaxHealth());
		ClearGroups(F, Team);
	}
}

class FGuardedScenario : public FTeamEconomyScenario
{
public:
	FGuardedScenario(FAutomationTestBase* InTest, int32 Commanders, TFunction<void(FTeamEconomyFixture&)> Body)
		: FTeamEconomyScenario(InTest, Commanders, [Body](FTeamEconomyFixture& F) {
			  Clean(F);
			  Body(F);
			  Clean(F);
		  })
	{
	}
};

// The announcer's feed is shared by every scenario: a scenario marks it first and counts only what follows.
inline int32 Mark(const FTeamEconomyFixture& F)
{
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(F.State);
	return Announcer && !Announcer->GetEvents().IsEmpty() ? Announcer->GetEvents().Last().Sequence : 0;
}

inline int32 Events(const FTeamEconomyFixture& F, FName Id, int32 Since)
{
	const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(F.State);
	int32 Count = 0;
	if (Announcer)
		for (const FObjectiveEvent& Event : Announcer->GetEvents())
			Count += Event.Id == Id && Event.Sequence > Since ? 1 : 0;
	return Count;
}
}
#endif
