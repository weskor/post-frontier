#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "TeamEconomyFixture.h"

#include "Commands/OrderGraph.h"
#include "Rules/EconomyPolicy.h"

using namespace ArmyTestSetup;

namespace
{
void FreezeGroup(AArmyGroup& Group)
{
	Group.SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Group.GetUnits())
		Unit->SetActorTickEnabled(false);
}
}

TUniquePtr<FTeamEconomyFixture> FTeamEconomyFixture::Create(UWorld* World, FAutomationTestBase* Test)
{
	ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	ACommandPlayerController* Controller = World ? ArmyTestSetup::Controller(World) : nullptr;
	ACommandPlayerState* Wallet = Controller ? Controller->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!MapReady(State) || !Wallet || Wallet->CommanderIndex < 0 || !IsValid(State->EnemyCommander) || !NavigationReady(World))
		return nullptr;
	TUniquePtr<FTeamEconomyFixture> Fixture = MakeUnique<FTeamEconomyFixture>();
	Fixture->Test = Test;
	Fixture->World = World;
	Fixture->State = State;
	Fixture->Controller = Controller;
	Fixture->Wallets.Add(Wallet);
	return Fixture;
}

ACommandPlayerState* FTeamEconomyFixture::Spawn(int32 CommanderIndex, bool bRoster)
{
	ACommandPlayerState* Wallet = World->SpawnActor<ACommandPlayerState>();
	if (!Wallet)
		return nullptr;
	Wallet->TeamIndex = 0;
	Wallet->CommanderIndex = CommanderIndex;
	Wallet->Resources = 0;
	// A spawned player state joins the roster by itself; an outsider must leave it again.
	if (bRoster)
		State->AddPlayerState(Wallet);
	else
		State->RemovePlayerState(Wallet);
	return Wallet;
}

bool FTeamEconomyFixture::Reset(int32 Commanders)
{
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		It->Destroy();
	Teardown();
	State->bVerificationIncomePaused = true;
	State->GiftLog.Reset();
	for (ADepositSite* Deposit : State->Deposits)
		if (IsValid(Deposit))
		{
			Deposit->Extractor = nullptr;
			Deposit->Remaining = Deposit->bRich ? EconomyPolicy::RichDepositAmount : EconomyPolicy::NormalDepositAmount;
		}
	for (const int32 Index : { Neck, Far, Alternate })
	{
		const AMapRegion* Region = ForceOrderGraph::Region(*State, Index);
		if (!Region || !IsValid(Region->Anchor) || !DepositIn(Index))
			return false;
		SetRole(Index, ERegionRole::Tactical);
		SetController(Index, 0);
	}
	SetTopology(false);
	int32 Slot = 0;
	for (int32 Index = 1; Index < Commanders; ++Index)
	{
		while (Slot == Wallets[0]->CommanderIndex)
			++Slot;
		ACommandPlayerState* Extra = Spawn(Slot++, true);
		if (!Extra)
			return false;
		Wallets.Add(Extra);
	}
	Step(); // Settle destroyed JEV buildings before any balance is read.
	for (ACommandPlayerState* Wallet : Wallets)
	{
		Wallet->Resources = Wallet->Data = Wallet->PowerCarry = Wallet->DataCarry = 0;
	}
	State->EnemyCommander->Resources = 0;
	State->EnemyCommander->Data = 0;
	return true;
}

void FTeamEconomyFixture::Teardown()
{
	for (int32 Index = Wallets.Num() - 1; Index > 0; --Index)
	{
		State->RemovePlayerState(Wallets[Index]);
		Wallets[Index]->Destroy();
	}
	Wallets.SetNum(FMath::Min(Wallets.Num(), 1));
}

namespace
{
AMapRegion* Mutable(ACommandGameState& State, int32 Index)
{
	for (AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex == Index)
			return Region;
	return nullptr;
}
}

void FTeamEconomyFixture::SetTopology(bool bAlternatePath)
{
	const auto Link = [&](int32 From, std::initializer_list<int32> To) {
		if (AMapRegion* Region = Mutable(*State, From))
			Region->Neighbours = TArray<int32>(To);
	};
	if (bAlternatePath)
	{
		Link(0, { Neck, Alternate });
		Link(Neck, { 0, Far });
		Link(Far, { Neck, Alternate });
		Link(Alternate, { 0, Far });
	}
	else
	{
		Link(0, { Neck });
		Link(Neck, { 0, Far });
		Link(Far, { Neck });
		Link(Alternate, {});
	}
}

void FTeamEconomyFixture::SetController(int32 RegionIndex, int32 Team)
{
	if (AMapRegion* Region = Mutable(*State, RegionIndex); Region && IsValid(Region->Anchor))
		Region->Anchor->ControllingTeam = Team;
}

void FTeamEconomyFixture::SetRole(int32 RegionIndex, ERegionRole Role)
{
	if (AMapRegion* Region = Mutable(*State, RegionIndex))
		Region->RegionRole = Role;
}

ADepositSite* FTeamEconomyFixture::DepositIn(int32 RegionIndex) const
{
	for (ADepositSite* Deposit : State->Deposits)
		if (IsValid(Deposit) && Deposit->RegionIndex == RegionIndex)
			return Deposit;
	return nullptr;
}

ACommandBuilding* FTeamEconomyFixture::SpawnRig(int32 RegionIndex, ACommandPlayerState* Builder, int32 Team, float Progress)
{
	ADepositSite* Deposit = DepositIn(RegionIndex);
	if (!Deposit || IsValid(Deposit->Extractor))
		return nullptr;
	const FTransform Transform(Deposit->GetActorLocation());
	ACommandBuilding* Rig = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Rig)
		return nullptr;
	Rig->BuildingIndex = ExtractorIndex;
	Rig->TeamIndex = Team;
	Rig->OwningPlayerState = Team == 5 ? State->EnemyCommander.Get() : Builder;
	Rig->Deposit = Deposit;
	Rig->ConstructionProgress = Progress;
	Deposit->Extractor = Rig;
	Rig->FinishSpawning(Transform);
	return Rig;
}

ACommandBuilding* FTeamEconomyFixture::SpawnBarracks(int32 Team, float Progress, int32 Slot)
{
	const FVector Home = Team == 5 ? FromEnemyHQ(State, -900.f, Slot * 500.f, 5.f) : FromFriendlyHQ(State, 900.f, Slot * 500.f, 5.f);
	const FTransform Transform(Home);
	ACommandBuilding* Barracks = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Barracks)
		return nullptr;
	Barracks->BuildingIndex = BarracksIndex;
	Barracks->TeamIndex = Team;
	Barracks->OwningPlayerState = Team == 5 ? State->EnemyCommander.Get() : Wallets[0];
	Barracks->ConstructionProgress = Progress;
	Barracks->FinishSpawning(Transform);
	return Barracks;
}

AArmyUnit* FTeamEconomyFixture::SpawnAttacker()
{
	AArmyGroup* Group = SpawnGroup(World, Controller, 10, FromFriendlyHQ(State, 0.f, 0.f, 100.f));
	if (!Group || Group->GetUnits().IsEmpty())
		return nullptr;
	FreezeGroup(*Group);
	return Group->GetUnits()[0];
}

AArmyUnit* FTeamEconomyFixture::SpawnHostileIn(int32 RegionIndex)
{
	AArmyGroup* Group = SpawnGroup(World, nullptr, -1, State->GetRegionAnchor(RegionIndex) + FVector(0.f, 0.f, 100.f));
	if (!Group || Group->GetUnits().IsEmpty())
		return nullptr;
	FreezeGroup(*Group);
	return Group->GetUnits()[0];
}

void FTeamEconomyFixture::Pay(int32 Payments)
{
	State->bVerificationIncomePaused = false;
	for (int32 Index = 0; Index < Payments; ++Index)
		State->Tick(2.f);
	State->bVerificationIncomePaused = true;
}

void FTeamEconomyFixture::Step()
{
	State->bVerificationIncomePaused = false;
	State->Tick(0.f);
	State->bVerificationIncomePaused = true;
}

bool FTeamEconomyScenario::Update()
{
	if (FPlatformTime::Seconds() - Started > 60.)
	{
		Test->AddError(TEXT("Team economy scenario exceeded 60 seconds"));
		return true;
	}
	UWorld* World = ArmyTestSetup::World();
	TUniquePtr<FTeamEconomyFixture> Fixture = FTeamEconomyFixture::Create(World, Test);
	if (!Fixture)
		return false;
	if (!Fixture->Reset(Commanders))
	{
		Test->AddError(TEXT("Fixture needs regions 2, 3 and 4 with anchors and deposits"));
		return true;
	}
	Body(*Fixture);
	Fixture->Teardown();
	return true;
}
#endif
