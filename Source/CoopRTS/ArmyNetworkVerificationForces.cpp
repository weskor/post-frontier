// Host-only probe fixtures for force, objective and match-end scenarios: JEV plans, isolation, capture,
// casualties and HQ damage. The caller has already checked the authority switch.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Commands/OrderGraph.h"
#include "EnemyCommander.h"
#include "EngineUtils.h"
#include "ForceOrders.h"
#include "Headquarters.h"
#include "JevIntentFixture.h"
#include "Json.h"
#include "MapRegion.h"
#include "RouteIntentVerification.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
FString ForcesJevPlans(const FProbeRequest& Probe)
{
	FString Stage;
	Probe.Request->TryGetStringField(TEXT("stage"), Stage);
	if (Stage == TEXT("create"))
		return JevIntentFixture::Publish(Probe.World, *Probe.State, JevIntentFixture::EStage::Create);
	if (Stage == TEXT("escalate"))
		return JevIntentFixture::Publish(Probe.World, *Probe.State, JevIntentFixture::EStage::Escalate);
	if (Stage == TEXT("replace"))
		return JevIntentFixture::Publish(Probe.World, *Probe.State, JevIntentFixture::EStage::Replace);
	return TEXT("unknown JEV plan stage");
}

FString ForcesIsolate(const FProbeRequest& Probe)
{
	UWorld* World = Probe.World;
	ACommandGameState* State = Probe.State;
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->SetActorTickEnabled(false);
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		if (It->GetTeamIndex() == 5)
		{
			const AMapRegion* PhysicalRegion = State->FindRegionAt(It->GetCenter());
			if (PhysicalRegion)
				FCommandService::IssueForceOrder(It->GetOwningPlayerState(), *It, EForceVerb::MoveHold, PhysicalRegion->RegionIndex);
		}
	for (ACommandBuilding* Building : State->Buildings)
		if (IsValid(Building) && Building->TeamIndex == 5 && Building->IsProducer())
			FCommandService::ConfigureProduction(State->EnemyCommander, Building,
				Building->bForceConfigured ? Building->ProductionRole : static_cast<EUnitRole>(255), false);
	return FString();
}

FString ForcesHqDamage(const FProbeRequest& Probe, AArmyGroup& Army)
{
	const int32 Damage = static_cast<int32>(Probe.Request->GetIntegerField(TEXT("damage")));
	AHeadquarters* Target = Probe.State->EnemyHeadquarters;
	if (!IsValid(Target) || Damage <= 0 || Damage >= Target->Health || Army.GetUnits().IsEmpty())
		return TEXT("nonlethal HQ damage fixture unavailable");
	Target->ReceiveAttack(Damage, Army.GetUnits()[0]);
	return FString();
}

FString ForcesCapture(const FProbeRequest& Probe, AArmyGroup& Army)
{
	const int32 SiteIndex = static_cast<int32>(Probe.Request->GetIntegerField(TEXT("site")));
	ACapturePoint* Site = nullptr;
	for (ACapturePoint* Candidate : Probe.State->CaptureSites)
		if (IsValid(Candidate) && Candidate->SiteIndex == SiteIndex)
			Site = Candidate;
	if (!Site || Army.GetUnits().IsEmpty())
		return TEXT("capture site or unit unavailable");
	Army.GetUnits()[0]->SetActorLocation(Site->GetActorLocation() + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
	return FString();
}

FString ForcesOccupant(const FProbeRequest& Probe, AArmyGroup& Army)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	const int32 SiteIndex = static_cast<int32>(Request->GetIntegerField(TEXT("site")));
	const int32 Slot = static_cast<int32>(Request->GetIntegerField(TEXT("slot")));
	const bool bEnemy = Request->GetBoolField(TEXT("enemy"));
	const bool bPresent = Request->GetBoolField(TEXT("present"));
	ACapturePoint* Site = nullptr;
	for (ACapturePoint* Candidate : Probe.State->CaptureSites)
		if (IsValid(Candidate) && Candidate->SiteIndex == SiteIndex
			&& Candidate->SiteKind == ECaptureSiteKind::Resource)
			Site = Candidate;
	AArmyGroup* SelectedGroup = &Army;
	if (bEnemy)
		for (TActorIterator<AArmyGroup> It(Probe.World); It; ++It)
			if (It->GetTeamIndex() == 5)
			{
				SelectedGroup = *It;
				break;
			}
	if (!Site || !IsValid(SelectedGroup) || SelectedGroup->GetTeamIndex() != (bEnemy ? 5 : 0))
		return TEXT("objective resource site or army unavailable");
	AArmyUnit* Unit = nullptr;
	for (AArmyUnit* Candidate : SelectedGroup->GetUnits())
		if (IsValid(Candidate) && Candidate->IsAlive() && Candidate->GetCompositionSlot() == Slot)
			Unit = Candidate;
	if (!Unit)
		return TEXT("objective live unit slot unavailable");
	// Only real units are moved; AdvanceCapture samples them on the normal server tick.
	const FVector Location = bPresent
		? Site->GetActorLocation() + FVector(bEnemy ? 370.f : -30.f, 0.f, 95.f)
		: SelectedGroup->GetHomeLocation() + FVector(0.f, 0.f, 95.f);
	Unit->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	Unit->ForceNetUpdate();
	return FString();
}

// Finds a living enemy shooter, or stages a hostile force in front of the enemy HQ; Fixture is the staged force.
FString ForcesKillShooter(const FProbeRequest& Probe, const AArmyUnit& Victim, AArmyUnit*& Shooter, AArmyGroup*& Fixture)
{
	UWorld* World = Probe.World;
	ACommandGameState* State = Probe.State;
	for (TActorIterator<AArmyGroup> It(World); It && !Shooter; ++It)
		if (It->GetTeamIndex() == 5 && It->GetTeamIndex() != Victim.GetTeamIndex())
			for (AArmyUnit* Candidate : It->GetUnits())
				if (IsValid(Candidate) && Candidate->IsAlive() && Candidate->GetTeamIndex() != Victim.GetTeamIndex())
				{
					Shooter = Candidate;
					break;
				}
	if (IsValid(Shooter))
		return FString();
	if (!IsValid(State->EnemyHeadquarters))
		return TEXT("enemy HQ unavailable for hostile staging");
	// Hostile fixtures stage in front of the enemy HQ, never at a literal map coordinate.
	const FTransform Transform(FRotator::ZeroRotator,
		State->EnemyHeadquarters->GetActorLocation() + FVector(-1400.f, 0.f, -10.f));
	AArmyGroup* Hostile = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Hostile)
		return TEXT("hostile casualty fixture allocation failed");
	Hostile->Initialize(FArmyGroupSpawn{ 5, State->EnemyCommander, -1, nullptr, Transform.GetLocation() });
	Hostile->FinishSpawning(Transform);
	Fixture = Hostile;
	if (!Hostile->SpawnUnits())
	{
		Hostile->Destroy();
		return TEXT("hostile casualty fixture spawn failed");
	}
	FCommandService::IssueForceOrder(State->EnemyCommander, Hostile, EForceVerb::MoveHold, ForceOrderGraph::TeamMain(*State, 5));
	Hostile->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Hostile->GetUnits())
		Unit->SetActorTickEnabled(false);
	Shooter = Hostile->GetUnits()[0];
	return FString();
}

FString ForcesKill(const FProbeRequest& Probe, AArmyGroup& Army)
{
	const int32 Slot = static_cast<int32>(Probe.Request->GetIntegerField(TEXT("slot")));
	AArmyUnit* Victim = nullptr;
	for (AArmyUnit* Unit : Army.GetUnits())
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetCompositionSlot() == Slot)
			Victim = Unit;
	if (!Victim)
		return TEXT("live casualty or hostile shooter unavailable");
	AArmyUnit* Shooter = nullptr;
	AArmyGroup* CasualtyFixture = nullptr;
	const FString StageError = ForcesKillShooter(Probe, *Victim, Shooter, CasualtyFixture);
	if (!StageError.IsEmpty())
		return StageError;
	if (!IsValid(Shooter) || !Shooter->IsAlive() || Shooter->GetTeamIndex() == Victim->GetTeamIndex())
		return TEXT("live casualty or hostile shooter unavailable");
	// Twice the remaining health stays lethal through entrenched-frontline damage reduction.
	Victim->ReceiveAttack(Victim->GetHealth() * 2, Shooter);
	if (CasualtyFixture)
		CasualtyFixture->Destroy();
	return !Victim->IsAlive() ? FString() : TEXT("hostile damage did not kill casualty");
}

FString ForcesFinish(const FProbeRequest& Probe, AArmyGroup& Army)
{
	const ACommandGameState* State = Probe.State;
	const bool bWin = Probe.Request->GetBoolField(TEXT("win"));
	AHeadquarters* Target = bWin ? State->EnemyHeadquarters : State->FriendlyHeadquarters;
	AArmyUnit* Shooter = bWin && !Army.GetUnits().IsEmpty() ? Army.GetUnits()[0] : nullptr;
	if (!bWin)
		for (TActorIterator<AArmyGroup> It(Probe.World); It; ++It)
			if (It->GetTeamIndex() == 5 && !It->GetUnits().IsEmpty())
			{
				Shooter = It->GetUnits()[0];
				break;
			}
	if (!IsValid(Target) || !IsValid(Shooter))
		return TEXT("HQ or shooter unavailable");
	Target->Health = 1;
	Target->ForceNetUpdate();
	const FVector Previous = Shooter->GetActorLocation();
	Shooter->SetActorLocation(Target->GetActorLocation() + FVector(90.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	Shooter->NextAttackTime = 0.f;
	Shooter->FireAt(Target);
	Shooter->SetActorLocation(Previous, false, nullptr, ETeleportType::TeleportPhysics);
	return Target->Health == 0 ? FString() : TEXT("weapon did not destroy HQ");
}
}

bool HandleScenarioFixture(const FProbeRequest& Probe, FString& Error)
{
	if (Probe.Action == TEXT("jevPlans"))
		Error = ForcesJevPlans(Probe);
	else if (Probe.Action == TEXT("routeTeammate"))
		Error = Probe.PC ? RouteIntentVerification::TeammateFixture(*Probe.PC,
							   Probe.Request->GetIntegerField(TEXT("targetRegionIndex")), Probe.Request->GetBoolField(TEXT("enabled")))
						 : TEXT("route fixture controller unavailable");
	else if (Probe.Action == TEXT("isolate"))
		Error = ForcesIsolate(Probe);
	else
		return false;
	return true;
}

bool HandleForceFixture(const FProbeRequest& Probe, AArmyGroup& Army, FString& Error)
{
	const FString& Action = Probe.Action;
	if (Action == TEXT("hqDamage"))
		Error = ForcesHqDamage(Probe, Army);
	else if (Action == TEXT("capture"))
		Error = ForcesCapture(Probe, Army);
	else if (Action == TEXT("occupant"))
		Error = ForcesOccupant(Probe, Army);
	else if (Action == TEXT("kill"))
		Error = ForcesKill(Probe, Army);
	else if (Action == TEXT("finish"))
		Error = ForcesFinish(Probe, Army);
	else
		return false;
	return true;
}
}
#endif
