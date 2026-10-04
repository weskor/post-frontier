#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "PlanningFixture.h"

#include "DepositSite.h"
#include "HAL/PlatformTime.h"

namespace PlanningFixture
{
FScenario::FScenario(FAutomationTestBase* InTest, double InTimeoutSeconds)
	: Test(InTest), Started(FPlatformTime::Seconds()), Timeout(InTimeoutSeconds)
{
	ACommandGameState::bPlanningHeldByTest = true;
}

FScenario::~FScenario()
{
	ACommandGameState::bPlanningHeldByTest = false;
}

void FScenario::Refresh()
{
	World = ArmyTestSetup::PlanningWorld();
	State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	PC = World ? ArmyTestSetup::Controller(World) : nullptr;
	Host = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
}

bool FScenario::Update()
{
	Refresh();
	if (bFailed)
		return Done();
	if (FPlatformTime::Seconds() - Started > Timeout)
	{
		Check(false, TEXT("Planning scenario timed out"));
		return Done();
	}
	if (!bPrepared)
	{
		bPrepared = Prepare();
		return bFailed;
	}
	return Step();
}

bool FScenario::Check(bool bOk, const FString& Message)
{
	if (!bOk)
	{
		Test->AddError(Message);
		bFailed = true;
	}
	return bOk;
}

bool FScenario::Done()
{
	Cleanup();
	// Leave the world running for whatever follows, and the roster as it was.
	if (State && State->IsPlanning())
	{
		ACommandGameState::bPlanningHeldByTest = false;
		State->CompletePlanningForHarness(false);
	}
	if (ACommandPlayerState* Guest = GuestPtr.Get())
		RemoveCommander(Guest);
	ACommandGameState::bPlanningHeldByTest = false;
	bFailed = true;
	return true;
}

void FScenario::Enter(int32 Next)
{
	Stage = Next;
	StageRealStart = World->GetRealTimeSeconds();
	StageGameStart = World->GetTimeSeconds();
}

bool FScenario::Prepare()
{
	if (!State || !PC || !Host || Host->CommanderIndex < 0 || !State->HasAuthority() || !ArmyTestSetup::MapReady(State)
		|| !IsValid(State->EnemyCommander) || !IsValid(State->Content) || !ArmyTestSetup::NavigationReady(World))
		return false;
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		It->Destroy();
	TArray<ACommandBuilding*> Buildings;
	for (ACommandBuilding* Building : State->Buildings)
		if (IsValid(Building))
			Buildings.Add(Building);
	for (ACommandBuilding* Building : Buildings)
		Building->Destroy();
	GuestPtr = SpawnCommander();
	State->BeginPlanning();
	Enter(0);
	return Check(State->IsPlanning() && World->IsPaused() && GuestPtr.IsValid(), TEXT("A fresh planning phase opens and freezes the world"));
}

ACommandPlayerState* FScenario::SpawnCommander()
{
	TSet<int32> Taken;
	for (APlayerState* Player : State->PlayerArray)
		if (const ACommandPlayerState* Commander = Cast<ACommandPlayerState>(Player))
			Taken.Add(Commander->CommanderIndex);
	int32 Slot = 0;
	while (Taken.Contains(Slot))
		++Slot;
	ACommandPlayerState* Player = World->SpawnActor<ACommandPlayerState>();
	if (!Player || Slot >= 5)
		return nullptr;
	Player->TeamIndex = 0;
	Player->CommanderIndex = Slot;
	Player->SetPlayerName(FString::Printf(TEXT("Planning commander %d"), Slot + 1));
	State->AddPlayerState(Player);
	Player->ResetForNewMatch();
	return Player;
}

void FScenario::RemoveCommander(ACommandPlayerState* Commander)
{
	if (!IsValid(Commander))
		return;
	// What ACommandGameMode::Logout does for a departing commander.
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		if (It->GetOwningPlayerState() == Commander)
			It->Destroy();
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		if (It->OwningPlayerState == Commander)
			It->Destroy();
	Commander->CommanderIndex = -1;
	State->RemovePlayerState(Commander);
	Commander->Destroy();
}

bool FScenario::BarracksSpot(int32 Nth, FVector& OutLocation) const
{
	// The Nth legal spot among well-separated ones, clear of every deposit, so spots never overlap each other.
	const FVector Center = State->FriendlyHeadquarters->GetActorLocation();
	TArray<FVector> Accepted;
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			const FVector Candidate = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			const FVector Point = State->ResolveBuildingLocation(ArmyTestSetup::BarracksIndex, Candidate);
			FString Reason;
			bool bClear = true;
			for (const ADepositSite* Deposit : State->Deposits)
				bClear &= !IsValid(Deposit) || FVector::Dist2D(Point, Deposit->GetActorLocation()) >= 400.;
			for (const FVector& Other : Accepted)
				bClear &= FVector::Dist2D(Point, Other) >= 400.;
			if (!bClear || State->FindRegionAt(Point) != State->FindRegionAt(Center)
				|| !State->ValidateBuildingPlacement(ArmyTestSetup::BarracksIndex, 0, Point, Reason))
				continue;
			if (Accepted.Num() == Nth)
			{
				OutLocation = Point;
				return true;
			}
			Accepted.Add(Point);
		}
	return false;
}

TArray<ADepositSite*> FScenario::OwnFreeDeposits() const
{
	TArray<ADepositSite*> Sites;
	for (ADepositSite* Deposit : State->Deposits)
		if (IsValid(Deposit) && !IsValid(Deposit->Extractor) && Deposit->Remaining > 0
			&& State->GetRegionController(Deposit->RegionIndex) == 0 && !State->IsRegionContested(Deposit->RegionIndex, 0))
			Sites.Add(Deposit);
	const FVector Home = State->FriendlyHeadquarters->GetActorLocation();
	Sites.StableSort([&Home](const ADepositSite& A, const ADepositSite& B) {
		return FVector::DistSquared2D(A.GetActorLocation(), Home) < FVector::DistSquared2D(B.GetActorLocation(), Home);
	});
	return Sites;
}

bool FScenario::JevKitStands(int32 Count) const
{
	if (State->Planning.JevKits.Num() != Count)
		return false;
	for (const FPlanningKit& Kit : State->Planning.JevKits)
		if (!IsValid(Kit.Barracks) || !IsValid(Kit.Rig) || !Kit.Barracks->IsComplete() || !Kit.Rig->IsComplete())
			return false;
	return true;
}
}
#endif
