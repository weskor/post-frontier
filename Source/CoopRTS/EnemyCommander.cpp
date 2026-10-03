#include "EnemyCommander.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Commands/OrderGraph.h"
#include "DepositSite.h"
#include "MapRegion.h"
#include "Content/MatchContent.h"
#include "Headquarters.h"
#include "Rules/ForceOrderPolicy.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
int32 FirstBuildingWith(const UMatchContent& Content, bool UBuildingDefinition::* Capability)
{
	for (int32 Index = 0; Index < Content.Buildings.Num(); ++Index)
		if (const UBuildingDefinition* Building = Content.Building(Index); Building && Building->*Capability)
			return Index;
	return INDEX_NONE;
}

bool HasCapability(const ACommandBuilding& Building, bool UBuildingDefinition::* Capability)
{
	const UBuildingDefinition* Definition = Building.GetDefinition();
	return Definition && Definition->*Capability;
}

bool ValidRegion(int32 Index) { return Index >= 0 && Index < ForceOrders::MaxRegions; }
EForceVerb OrderVerb(JevPlanner::EVerb Verb)
{
	return Verb == JevPlanner::EVerb::Attack ? EForceVerb::Attack
		: Verb == JevPlanner::EVerb::Retreat ? EForceVerb::Retreat
											 : EForceVerb::MoveHold;
}

}

AEnemyCommander::AEnemyCommander()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

void AEnemyCommander::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || (EvaluateElapsed += DeltaSeconds) < 2.f)
		return;
	EvaluateElapsed = FMath::Fmod(EvaluateElapsed, 2.f);
	EvaluatePlan();
}

ACommandBuilding* AEnemyCommander::BuildNear(ACommandGameState* State, int32 BuildingIndex, const FVector& Center)
{
	const UBuildingDefinition* Definition = State->Content->Building(BuildingIndex);
	if (!Definition || !IsValid(Commander) || Commander->Resources < Definition->BuildCost)
		return nullptr;
	// Preserve the complete force's rally footprint, not just its empty-force centre.
	// 200 cm covers the six-unit formation extent plus the navigation agent margin.
	const float ClearanceSquared = FMath::Square(Definition->FootprintRadius * UE_SQRT_2 + 200.f);
	const auto KeepsRalliesClear = [&](const FVector& Location) {
		for (const ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->IsAlive() && Building->OwningPlayerState == Commander
				&& IsValid(Building->ForceGroup)
				&& FVector::DistSquared2D(Location, Building->ForceGroup->GetHomeLocation()) < ClearanceSquared)
				return false;
		return true;
	};
	if (Definition->bRequiresDeposit)
	{
		if (!KeepsRalliesClear(Center))
			return nullptr;
		return FCommandService::PlaceBuilding(Commander, BuildingIndex, Center).Building;
	}
	const AMapRegion* Region = State->FindRegionAt(Center);
	if (!Region)
		return nullptr;
	// Mirror the candidate set with the side, rather than giving team 0 a different search bias.
	const float Orientation = TeamIndex == 5 ? -1.f : 1.f;
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Location = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * ((380.f + Ring * 160.f) * Orientation);
			Location.Z = 5.f;
			Location = State->ResolveBuildingLocation(BuildingIndex, Location, TeamIndex);
			if (State->FindRegionAt(Location) != Region)
				continue;
			if (!KeepsRalliesClear(Location))
				continue;
			bool bBlocksDeposit = false;
			for (const ADepositSite* Deposit : State->Deposits)
				if (IsValid(Deposit) && Deposit->Remaining > 0 && !IsValid(Deposit->Extractor)
					&& FVector::DistSquared2D(Location, Deposit->GetActorLocation()) < ClearanceSquared)
				{
					bBlocksDeposit = true;
					break;
				}
			if (bBlocksDeposit)
				continue;
			if (ACommandBuilding* Building = FCommandService::PlaceBuilding(Commander, BuildingIndex, Location).Building)
				return Building;
		}
	return nullptr;
}

void AEnemyCommander::EvaluatePlan()
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !State || !State->Content || State->MatchResult != EMatchResult::Ongoing
		|| (TeamIndex != 0 && TeamIndex != 5) || !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters))
		return;
	if (!IsValid(Commander) && TeamIndex == 5)
		Commander = State->EnemyCommander;
	if (!IsValid(Commander) || Commander->TeamIndex != TeamIndex)
		return;
	if (!bMemoLoadAttempted)
	{
		bMemoLoadAttempted = true;
		bMemosLoaded = MemoTemplates.Load();
	}
	if (!bMemosLoaded)
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	CommittedForces.RemoveAllSwap([&](const FCommittedForce& Entry) {
		return !Entry.Force.IsValid() || Entry.Force->GetOwningPlayerState() != Commander
			|| Entry.Force->GetAliveCount() == 0;
	});
	if (TeamIndex == 5)
		State->EnemyPlans.RemoveAll([&](const FJevPublishedPlan& Entry) {
			return !IsValid(Entry.Force) || Entry.Force->GetOwningPlayerState() != Commander
				|| Entry.Force->GetAliveCount() == 0;
		});
	const UMatchContent& Content = *State->Content;
	const int32 ProducerIndex = FirstBuildingWith(Content, &UBuildingDefinition::bProducesForces);
	const int32 ExtractorIndex = FirstBuildingWith(Content, &UBuildingDefinition::bRequiresDeposit);
	const int32 WorkshopIndex = FirstBuildingWith(Content, &UBuildingDefinition::bOffersResearch);
	const int32 FrontlineIndex = Content.UnitIndexForRole(EUnitRole::Frontline);
	const int32 RangedIndex = Content.UnitIndexForRole(EUnitRole::Ranged);
	const int32 SiegeIndex = Content.UnitIndexForRole(EUnitRole::Siege);
	if (ProducerIndex < 0 || FrontlineIndex < 0 || RangedIndex < 0 || SiegeIndex < 0)
		return;
	const UArmyUnitDefinition& Infantry = *Content.Unit(FrontlineIndex);
	const FVector Home = (TeamIndex == 5 ? State->EnemyHeadquarters : State->FriendlyHeadquarters)->GetActorLocation();
	const FVector EnemyHome = (TeamIndex == 5 ? State->FriendlyHeadquarters : State->EnemyHeadquarters)->GetActorLocation();
	const AMapRegion* Regions[ForceOrders::MaxRegions] = {};
	int32 Controllers[ForceOrders::MaxRegions];
	int32 Hostiles[ForceOrders::MaxRegions] = {};
	int32 DepositValue[ForceOrders::MaxRegions] = {};
	JevPlanner::FWorld Summary;
	Summary.Team = TeamIndex;
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
		Controllers[Index] = -1;
	for (const AMapRegion* Region : State->Regions)
		if (IsValid(Region) && ValidRegion(Region->RegionIndex))
		{
			Regions[Region->RegionIndex] = Region;
			Controllers[Region->RegionIndex] = State->GetRegionController(Region->RegionIndex);
			JevPlanner::FRegion& Out = Summary.Regions[Region->RegionIndex];
			Out.bExists = true;
			Out.bMain = Region->RegionRole == ERegionRole::Main;
			Out.Controller = Controllers[Region->RegionIndex];
			Out.Position = State->GetRegionAnchor(Region->RegionIndex);
			for (int32 Neighbour : Region->Neighbours)
				if (ValidRegion(Neighbour))
					Out.Neighbours |= uint64(1) << Neighbour;
		}
	const AMapRegion* HomeRegion = State->FindRegionAt(Home);
	if (!HomeRegion || !ValidRegion(HomeRegion->RegionIndex))
		return;
	const AMapRegion* EnemyMain = State->FindRegionAt(EnemyHome);
	Summary.EnemyHome = EnemyMain ? EnemyMain->RegionIndex : INDEX_NONE;
	TArray<ACommandBuilding*, TInlineAllocator<8>> Barracks;
	ACommandBuilding* Workshop = nullptr;
	int32 Roles[3] = {};
	for (ACommandBuilding* Building : State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->OwningPlayerState != Commander)
			continue;
		if (Building->IsProducer())
		{
			Barracks.Add(Building);
			if (Building->bForceConfigured)
				++Roles[Building->ProductionRole == EUnitRole::Frontline ? 0 : Building->ProductionRole == EUnitRole::Ranged ? 1
																															 : 2];
		}
		if (HasCapability(*Building, &UBuildingDefinition::bOffersResearch))
			Workshop = Building;
	}
	Barracks.Sort([](const ACommandBuilding& A, const ACommandBuilding& B) { return A.ForceNumber < B.ForceNumber; });
	if (Barracks.IsEmpty())
	{
		BuildNear(State, ProducerIndex, Home);
		if (TeamIndex == 5)
			State->ForceNetUpdate();
	}
	TArray<AArmyGroup*, TInlineAllocator<8>> Forces;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		if (It->GetOwningPlayerState() == Commander && It->GetAliveCount() > 0)
			Forces.Add(*It);
	Forces.Sort([](const AArmyGroup& A, const AArmyGroup& B) { return A.ForceNumber < B.ForceNumber; });
	uint64 ForceRegions = 0;
	for (const AArmyGroup* Force : Forces)
	{
		const int32 Source = ForceOrderGraph::SourceRegion(*Force, *State);
		if (ValidRegion(Source))
			ForceRegions |= uint64(1) << Source;
	}
	int32 FriendlyStrength = 0, EnemyStrength = 0;
	for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive())
			continue;
		if (It->GetTeamIndex() == TeamIndex)
			++FriendlyStrength;
		else
		{
			++EnemyStrength;
			const AMapRegion* Region = State->FindRegionAt(It->GetActorLocation());
			if (Region && ValidRegion(Region->RegionIndex))
				++Hostiles[Region->RegionIndex];
			for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
				if ((ForceRegions & (uint64(1) << Index)) && State->IsDamagingRegion(**It, Index, TeamIndex))
					Summary.Regions[Index].bAttacked = true;
		}
	}
	int32 Established = 0;
	struct FDepositCandidate
	{
		ADepositSite* Deposit;
		float Score;
	};
	TArray<FDepositCandidate, TInlineAllocator<16>> EligibleDeposits;
	for (ADepositSite* Deposit : State->Deposits)
	{
		if (!IsValid(Deposit) || !ValidRegion(Deposit->RegionIndex) || Deposit->Remaining <= 0)
			continue;
		if (IsValid(Deposit->Extractor))
		{
			if (Deposit->Extractor->IsAlive() && Deposit->Extractor->IsComplete() && Deposit->Extractor->OwningPlayerState == Commander)
				++Established;
			continue;
		}
		DepositValue[Deposit->RegionIndex] += Deposit->RatePerSecond();
		if (Controllers[Deposit->RegionIndex] != TeamIndex || State->IsRegionContested(Deposit->RegionIndex, TeamIndex))
			continue;
		const float Score = Deposit->RatePerSecond() * 3.f - FVector::Dist2D(Home, Deposit->GetActorLocation()) / 2000.f;
		EligibleDeposits.Add({ Deposit, Score });
	}
	const int32 Reserve = Infantry.UnitCost * Infantry.Capacity;
	const UBuildingDefinition* ExtractorDefinition = Content.Building(ExtractorIndex);
	if (ExtractorDefinition && FriendlyStrength > 0 && Commander->Resources >= ExtractorDefinition->BuildCost + Reserve)
	{
		EligibleDeposits.Sort([](const FDepositCandidate& A, const FDepositCandidate& B) { return A.Score > B.Score; });
		// A free deposit can still be blocked by another building. Try the next legal site,
		// rather than letting an unbuildable preferred deposit deadlock the entire economy.
		for (const FDepositCandidate& Candidate : EligibleDeposits)
			if (BuildNear(State, ExtractorIndex, Candidate.Deposit->GetActorLocation()))
				break;
	}

	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
	{
		Summary.Regions[Index].Hostiles = Hostiles[Index];
		Summary.Regions[Index].DepositValue = DepositValue[Index];
		if (Controllers[Index] == TeamIndex && (Hostiles[Index] || Summary.Regions[Index].bAttacked))
			Summary.bThreatened = true;
	}
	const bool bThreatened = Summary.bThreatened;
	TArray<JevPlanner::FTarget, TInlineAllocator<32>> Targets;
	TArray<AActor*, TInlineAllocator<32>> TargetActors;
	const auto AddTarget = [&](AActor* Actor) {
		const AMapRegion* Region = State->FindRegionAt(Actor->GetActorLocation());
		if (Region && ValidRegion(Region->RegionIndex))
		{
			Targets.Add({ Actor->GetUniqueID(), Region->RegionIndex, true });
			TargetActors.Add(Actor);
		}
	};
	for (ACommandBuilding* Building : State->Buildings)
		if (IsValid(Building) && Building->IsAlive() && Building->TeamIndex != TeamIndex)
			AddTarget(Building);
	AddTarget(TeamIndex == 5 ? State->FriendlyHeadquarters.Get() : State->EnemyHeadquarters.Get());
	Summary.Targets = Targets;
	const int32 EnemyTeam = TeamIndex == 5 ? 0 : 5;
	int32 EnemyIncome = 0;
	if (EnemyTeam == 5)
		EnemyIncome = State->GetEnemyIncomePerSecond();
	else
		for (APlayerState* Player : State->PlayerArray)
			if (const ACommandPlayerState* Human = Cast<ACommandPlayerState>(Player); Human && Human->TeamIndex == EnemyTeam)
				EnemyIncome += State->GetIncomePerSecond(Human);
	const bool bAdvantage = FriendlyStrength >= Infantry.Capacity && FriendlyStrength * 4 >= FMath::Max(1, EnemyStrength) * 5
		&& State->GetIncomePerSecond(Commander) >= EnemyIncome;
	Summary.bAdvantage = bAdvantage;
	for (ACommandBuilding* Building : Barracks)
	{
		if (!Building->IsComplete())
			continue;
		const EUnitRole Role = Building->bForceConfigured ? Building->ProductionRole
			: Roles[0] == 0                               ? EUnitRole::Frontline
			: Roles[1] == 0                               ? EUnitRole::Ranged
			: Roles[2] == 0                               ? EUnitRole::Siege
			: Roles[0] <= Roles[1]                        ? EUnitRole::Frontline
														  : EUnitRole::Ranged;
		const bool bWasConfigured = Building->bForceConfigured;
		if (!Building->bProductionEnabled)
			FCommandService::ConfigureProduction(Commander, Building, Role, true);
		if (!bWasConfigured && Building->bForceConfigured)
			++Roles[Role == EUnitRole::Frontline ? 0 : Role == EUnitRole::Ranged ? 1
																				 : 2];
	}
	for (AArmyGroup* Force : Forces)
	{
		FCommittedForce* Current = CommittedForces.FindByPredicate(
			[&](const FCommittedForce& Entry) { return Entry.Force == Force; });
		JevPlanner::FForce Snapshot;
		Snapshot.Source = ForceOrderGraph::SourceRegion(*Force, *State);
		Snapshot.Home = HomeRegion->RegionIndex;
		Snapshot.Position = Force->GetCenter();
		Snapshot.UnitCount = Force->GetAliveCount();
		Snapshot.bRecovering = Current && Current->bRecovering;
		float Health = 0.f;
		int32 Joined = 0;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
			{
				Health += float(Unit->GetHealth()) / Unit->MaxHealth();
				++Joined;
			}
		Snapshot.HealthFraction = Joined ? Health / Joined : 1.f;
		const bool bRecovering = Snapshot.HealthFraction < .35f
			|| (Snapshot.bRecovering && Snapshot.HealthFraction < .8f);
		Snapshot.bAtRecovery = Snapshot.bRecovering && Force->Verb == EForceVerb::MoveHold
			&& Force->IsHoldingRegion() && Force->HoldRegionIndex == Snapshot.Source;
		const float Speed[] = { Force->GetBaseMarchSpeed() };
		Snapshot.ClassSpeeds = Speed;
		JevPlanner::FPlan Next;
		if (!JevPlanner::Decide(Summary, Snapshot, Now, Current ? &Current->Plan : nullptr, Next))
		{
			if (TeamIndex == 5)
				State->EnemyPlans.RemoveAll([&](const FJevPublishedPlan& Entry) { return Entry.Force == Force; });
			continue;
		}
		AActor* Structure = nullptr;
		for (int32 Index = 0; Index < Targets.Num(); ++Index)
			if (Targets[Index].Identity == Next.TargetIdentity)
				Structure = TargetActors[Index];
#if !UE_BUILD_SHIPPING
		const bool bEscalation = Current && !Current->Plan.bEscalated && Next.bEscalated;
#endif
		const bool bNewCommitment = !Current || Next.CommittedUntil != Current->Plan.CommittedUntil;
		const bool bDecisionChanged = !Current || Next.Verb != Current->Plan.Verb
			|| Next.Target != Current->Plan.Target || Next.TargetIdentity != Current->Plan.TargetIdentity;
		const bool bActualChanged = Force->Verb != OrderVerb(Next.Verb)
			|| (Next.Verb != JevPlanner::EVerb::Retreat && Force->TargetRegionIndex != Next.Target)
			|| Force->TargetStructure != Structure || Force->Orders.IsEmpty();
		const bool bChanged = bActualChanged && (bDecisionChanged || bNewCommitment);
		if (bChanged && !FCommandService::IssueForceOrder(Commander, Force, OrderVerb(Next.Verb), Next.Verb == JevPlanner::EVerb::Retreat ? INDEX_NONE : Next.Target, Structure))
		{
			if (TeamIndex == 5)
				State->EnemyPlans.RemoveAll([&](const FJevPublishedPlan& Entry) { return Entry.Force == Force; });
			continue;
		}
		if (bChanged && Next.Verb == JevPlanner::EVerb::Retreat && ValidRegion(Force->GetRetreatRegion()))
		{
			Next.Target = Force->GetRetreatRegion();
			Next.EtaSeconds = JevPlanner::TravelSeconds(Summary, Snapshot, Next.Target) / 1.25f;
		}
		if (Next.Verb == JevPlanner::EVerb::Retreat && Force->Verb == EForceVerb::Retreat
			&& ValidRegion(Force->GetRetreatRegion()))
			if (ACommandBuilding* Producer = Force->GetProductionBuilding(); IsValid(Producer)
				&& Producer->RallyRegionIndex != Force->GetRetreatRegion())
				FCommandService::SetRallyPoint(Commander, Producer, Force->GetRetreatRegion());
		if (!Current)
		{
			Current = &CommittedForces.AddDefaulted_GetRef();
			Current->Force = Force;
		}
		if (bNewCommitment)
			Current->TicketNumber = NextTicketNumber++;
		Current->Plan = Next;
		Current->bRecovering = bRecovering;
		if (ValidRegion(Next.Target) && Next.bRequiresUnownedTarget)
			Summary.Regions[Next.Target].bClaimed = true;
		JevPlanner::FPlan DisplayPlan = Next;
		if (Next.Verb == JevPlanner::EVerb::Retreat)
		{
			// Execution can finish before the next strategic decision; completion does not reset commitment.
			if (Force->Verb == EForceVerb::MoveHold)
			{
				DisplayPlan.Verb = JevPlanner::EVerb::MoveAndHold;
				DisplayPlan.Target = Force->TargetRegionIndex;
				DisplayPlan.EtaSeconds = 0.f;
			}
			else if (ValidRegion(Force->GetRetreatRegion()) && Force->GetRetreatRegion() != Next.Target)
			{
				DisplayPlan.Target = Force->GetRetreatRegion();
				DisplayPlan.EtaSeconds = JevPlanner::TravelSeconds(Summary, Snapshot, DisplayPlan.Target) / 1.25f;
			}
		}
		if (TeamIndex == 5)
		{
			FJevPublishedPlan* Existing = State->EnemyPlans.FindByPredicate(
				[&](const FJevPublishedPlan& Entry) { return Entry.Force == Force; });
			const bool bMemoChanged = !Existing || Existing->TicketNumber != Current->TicketNumber
				|| Existing->Verb != OrderVerb(DisplayPlan.Verb) || Existing->TargetRegionIndex != DisplayPlan.Target
				|| Existing->SizeBand != DisplayPlan.SizeBand || Existing->EtaSeconds != DisplayPlan.EtaSeconds
				|| Existing->bEscalated != DisplayPlan.bEscalated;
			FJevPublishedPlan& Published = Existing ? *Existing : State->EnemyPlans.AddDefaulted_GetRef();
			Published.TicketNumber = Current->TicketNumber;
			Published.Force = Force;
			Published.ForceNumber = Force->ForceNumber;
			Published.Verb = OrderVerb(DisplayPlan.Verb);
			Published.SourceRegionIndex = DisplayPlan.Source;
			Published.TargetRegionIndex = DisplayPlan.Target;
			Published.TargetStructure = Structure;
			Published.SizeBand = DisplayPlan.SizeBand;
			Published.EtaSeconds = DisplayPlan.EtaSeconds;
			Published.CommittedUntil = Next.CommittedUntil;
			Published.RemainingCommitment = JevPlanner::Remaining(Next, Now);
			Published.bEscalated = Next.bEscalated;
			if (bMemoChanged)
				Published.Memo = MemoTemplates.Format(DisplayPlan, Published.TicketNumber,
					Regions[DisplayPlan.Target]->DisplayName.ToString());
#if !UE_BUILD_SHIPPING
			if (bNewCommitment || bEscalation)
			{
				FJevPlanHistoryEntry& History = State->EnemyPlanHistory.AddDefaulted_GetRef();
				History.Plan = Published;
				History.TimeSeconds = Now;
				History.bEscalation = bEscalation && !bNewCommitment;
				History.ForceNumber = Force->ForceNumber;
				History.TargetStructureName = Structure ? Structure->GetName() : FString();
			}
#endif
		}
	}

	// Prefer the safest controlled forward anchor. Never construct on a contested region.
	FVector BuildCenter = Home;
	float BestForward = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
	{
		if (!Regions[Index] || Regions[Index]->RegionRole == ERegionRole::Main || Controllers[Index] != TeamIndex
			|| State->IsRegionContested(Index, TeamIndex))
			continue;
		bool bHasProducer = false;
		for (const ACommandBuilding* Building : Barracks)
			if (Regions[Index]->Contains(Building->GetActorLocation()))
			{
				bHasProducer = true;
				break;
			}
		if (bHasProducer)
			continue;
		const FVector Anchor = State->GetRegionAnchor(Index);
		const float Score = -FVector::Dist2D(Anchor, EnemyHome);
		if (Score > BestForward)
		{
			BestForward = Score;
			BuildCenter = Anchor;
		}
	}
	const UBuildingDefinition* WorkshopDefinition = Content.Building(WorkshopIndex);
	if (!bThreatened && Established > 0 && Barracks.Num() < 3 && BestForward > -TNumericLimits<float>::Max()
		&& Commander->Resources >= Content.Building(ProducerIndex)->BuildCost + Reserve)
		BuildNear(State, ProducerIndex, BuildCenter);
	else if (!Workshop && Established > 0 && WorkshopDefinition && Commander->Resources >= WorkshopDefinition->BuildCost + Reserve)
		BuildNear(State, WorkshopIndex, Home);
	else if (Workshop && Workshop->IsComplete() && Commander->Doctrine == EArmyDoctrine::None
		&& Commander->Resources >= ACommandBuilding::ResearchCost + Reserve)
		FCommandService::Research(Commander, Workshop, EArmyDoctrine::FieldRepairs);
	if (TeamIndex == 5)
		State->ForceNetUpdate();
}
