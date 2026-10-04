#include "GameState/GameStateEconomy.h"

#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "DepositSite.h"
#include "FailoverNode.h"
#include "Headquarters.h"
#include "GameState/GameStateTerritory.h"
#include "MapRegion.h"
#include "Rules/EconomyPolicy.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SimulationSettings.h"
#endif

namespace
{
constexpr int32 PaymentSeconds = 2;
constexpr float PaymentInterval = PaymentSeconds;

bool IsPayingExtractor(const ACommandBuilding* Building, const ADepositSite* Deposit)
{
	return IsValid(Building) && Building->IsAlive() && Building->IsComplete()
		&& Building->Kind == EBuildingKind::Extractor && Building->Deposit == Deposit;
}

bool IsConnected(uint64 Mask, int32 RegionIndex)
{
	return RegionIndex >= 0 && RegionIndex < ForceOrders::MaxRegions && (Mask & (uint64(1) << RegionIndex)) != 0;
}

FExtractorPaymentInput ExtractorInput(const ADepositSite& Deposit, uint64 TeamMask)
{
	const ACommandBuilding* Building = Deposit.Extractor;
	const bool bPaying = IsPayingExtractor(Building, &Deposit);
	return { Deposit.RatePerSecond(), Deposit.Remaining, PaymentSeconds, bPaying ? Building->TeamIndex : -1, bPaying,
		bPaying, bPaying && IsConnected(TeamMask, Deposit.RegionIndex) };
}

// Per-second rate of every extractor of the team that is paying now, given its connected regions.
int32 ExtractionPerSecond(const ACommandGameState& State, int32 Team, uint64 Mask)
{
	int32 Rate = 0;
	for (const ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit))
		{
			const FExtractorPaymentInput Input = ExtractorInput(*Deposit, Mask);
			if (Input.Team == Team && EconomyPolicy::ExtractorPayment(Input).Amount > 0)
				Rate += Deposit->RatePerSecond();
		}
	return Rate;
}

// Reward regions the human team controls and reaches from its main.
int32 ConnectedRewardRegions(const ACommandGameState& State, uint64 Mask)
{
	int32 Count = 0;
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionRole == ERegionRole::Reward && IsConnected(Mask, Region->RegionIndex))
			++Count;
	return Count;
}

// Pay the pool to the roster in slot order. Nothing is paid, and nothing depletes, without recipients.
void DistributePool(const TArray<ACommandPlayerState*>& Roster, int32 PowerTotal, int32 DataTotal)
{
	for (ACommandPlayerState* Wallet : Roster)
		Wallet->CreditPoolShare(PowerTotal, DataTotal, Roster.Num());
}
}

void FGameStateEconomy::Tick(ACommandGameState& State, float DeltaSeconds)
{
	TrackStructureKills(State);
	Elapsed += DeltaSeconds;
	while (Elapsed >= PaymentInterval)
	{
		Elapsed -= PaymentInterval;
		PayInterval(State);
	}
}

void FGameStateEconomy::PayInterval(ACommandGameState& State)
{
	PayHumanPool(State);
	PayEnemyBaseline(State);
	PayEnemyExtractors(State);
}

void FGameStateEconomy::PayHumanPool(ACommandGameState& State)
{
	const TArray<ACommandPlayerState*> Roster = FGameStateEconomy::Roster(State);
	if (Roster.IsEmpty())
		return;
	// Payment reads the live rule, never a copy that could lag a controller change.
	const uint64 Mask = GameStateTerritory::TeamConnectedMask(State, 0);
	int32 Power = Roster.Num() * State.GetHumanBaselineIncomePerSecond() * PaymentSeconds;
	for (ADepositSite* Deposit : State.Deposits)
	{
		if (!IsValid(Deposit))
			continue;
		const FExtractorPaymentInput Input = ExtractorInput(*Deposit, Mask);
		if (Input.Team != 0)
			continue;
		const FExtractorPayment Payment = EconomyPolicy::ExtractorPayment(Input);
		if (Payment.Amount == 0)
			continue;
		Power = EconomyPolicy::AddResources(Power, Payment.Amount);
		Deposit->Remaining = Payment.Remaining;
		Deposit->ForceNetUpdate();
	}
	const int32 Data = ConnectedRewardRegions(State, Mask) * EconomyPolicy::RewardRegionPoolData(Roster.Num(), PaymentSeconds);
	DistributePool(Roster, Power, Data);
}

void FGameStateEconomy::PayEnemyBaseline(ACommandGameState& State)
{
	if (!IsValid(State.EnemyCommander))
		return;
	const int32 PaymentTenths = FMath::RoundToInt(EnemyBaselineIncomePerSecond(State) * 10. * PaymentSeconds) + EnemyRemainderTenths;
	State.EnemyCommander->AddResources(PaymentTenths / 10);
	EnemyRemainderTenths = PaymentTenths % 10;
}

void FGameStateEconomy::PayEnemyExtractors(ACommandGameState& State)
{
	if (!IsValid(State.EnemyCommander) || State.EnemyCommander->GetWorld() != State.GetWorld())
		return;
	const uint64 Mask = GameStateTerritory::TeamConnectedMask(State, 5);
	for (ADepositSite* Deposit : State.Deposits)
	{
		if (!IsValid(Deposit))
			continue;
		const FExtractorPaymentInput Input = ExtractorInput(*Deposit, Mask);
		const FExtractorPayment Payment = EconomyPolicy::ExtractorPayment(Input);
		if (Input.Team != 5 || Payment.Amount == 0)
			continue;
		State.EnemyCommander->AddResources(Payment.Amount);
		Deposit->Remaining = Payment.Remaining;
		Deposit->ForceNetUpdate();
	}
}

void FGameStateEconomy::TrackStructureKills(ACommandGameState& State)
{
	int32 Kills = 0;
	for (int32 Index = LiveJevBuildings.Num() - 1; Index >= 0; --Index)
	{
		const ACommandBuilding* Building = LiveJevBuildings[Index].Get();
		if (IsValid(Building) && !Building->IsActorBeingDestroyed() && Building->IsAlive())
			continue;
		LiveJevBuildings.RemoveAtSwap(Index);
		++Kills;
	}
	for (int32 Index = LiveJevNodes.Num() - 1; Index >= 0; --Index)
	{
		const AFailoverNode* Node = LiveJevNodes[Index].Get();
		if (IsValid(Node) && !Node->IsActorBeingDestroyed() && Node->IsAlive())
			continue;
		LiveJevNodes.RemoveAtSwap(Index);
		++Kills;
	}
	if (Kills > 0)
	{
		const TArray<ACommandPlayerState*> Roster = FGameStateEconomy::Roster(State);
		DistributePool(Roster, 0, Kills * EconomyPolicy::StructureKillData);
	}
	for (const ACommandBuilding* Building : State.Buildings)
		if (IsValid(Building) && Building->TeamIndex == 5 && Building->IsAlive() && Building->IsComplete())
			LiveJevBuildings.AddUnique(Building);
	if (IsValid(State.EnemyHeadquarters))
		for (const TWeakObjectPtr<AFailoverNode>& Node : State.EnemyHeadquarters->GetNodes())
			if (Node.IsValid() && Node->IsAlive())
				LiveJevNodes.AddUnique(Node.Get());
}

TArray<ACommandPlayerState*> FGameStateEconomy::Roster(const ACommandGameState& State)
{
	TArray<ACommandPlayerState*> Result;
	for (APlayerState* Player : State.PlayerArray)
	{
		ACommandPlayerState* Commander = Cast<ACommandPlayerState>(Player);
		if (IsValid(Commander) && Commander->GetWorld() == State.GetWorld() && Commander->TeamIndex == 0
			&& Commander->CommanderIndex >= 0 && Commander->CommanderIndex < EconomyPolicy::MaxRecipients
			&& !Result.ContainsByPredicate([Commander](const ACommandPlayerState* Other) {
				   return Other->CommanderIndex == Commander->CommanderIndex;
			   }))
			Result.Add(Commander);
	}
	Result.Sort([](const ACommandPlayerState& A, const ACommandPlayerState& B) { return A.CommanderIndex < B.CommanderIndex; });
	return Result;
}

int32 FGameStateEconomy::HumanBaselineIncomePerSecond(const UWorld* World)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	return FSimulationSettings::ForWorld(World).HumanBaselineIncome;
#else
	return EconomyPolicy::HumanBaselineIncome;
#endif
}

int32 FGameStateEconomy::JevBaselineIncomePerSecond(const UWorld* World)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	return FSimulationSettings::ForWorld(World).JevBaselineIncome;
#else
	return EconomyPolicy::JevBaselineIncome;
#endif
}

double FGameStateEconomy::EnemyBaselineIncomePerSecond(const ACommandGameState& State)
{
	int32 HumanCommanders = 0;
	for (const APlayerState* Player : State.PlayerArray)
		if (const ACommandPlayerState* Commander = Cast<ACommandPlayerState>(Player))
			if (IsValid(Commander) && Commander->TeamIndex == 0
				&& Commander->CommanderIndex >= 0 && Commander->CommanderIndex < 5)
				++HumanCommanders;
	return EconomyPolicy::JevBaselineRate(State.GetJevBaselineIncomePerSecond(), HumanCommanders);
}

double FGameStateEconomy::PowerRate(const ACommandGameState& State, const ACommandPlayerState* Commander)
{
	if (!IsValid(Commander))
		return 0.;
	if (Commander == State.EnemyCommander)
		return EnemyBaselineIncomePerSecond(State) + ExtractionPerSecond(State, 5, State.GetConnectedMask(5));
	const TArray<ACommandPlayerState*> Recipients = Roster(State);
	if (!Recipients.Contains(Commander))
		return 0.;
	return State.GetHumanBaselineIncomePerSecond() + static_cast<double>(ExtractionPerSecond(State, 0, State.GetConnectedMask(0))) / Recipients.Num();
}

double FGameStateEconomy::DataRate(const ACommandGameState& State, const ACommandPlayerState* Commander)
{
	if (!IsValid(Commander) || !Roster(State).Contains(Commander))
		return 0.;
	return static_cast<double>(ConnectedRewardRegions(State, State.GetConnectedMask(0))) * EconomyPolicy::RewardRegionDataRate;
}

int32 FGameStateEconomy::IncomePerSecond(const ACommandGameState& State, const ACommandPlayerState* Commander)
{
	return FMath::FloorToInt(PowerRate(State, Commander));
}
