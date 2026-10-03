#include "GameState/GameStateEconomy.h"

#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "DepositSite.h"
#include "Rules/EconomyPolicy.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SimulationSettings.h"
#endif

namespace
{
constexpr float PaymentInterval = 2.f;

bool IsPayingExtractor(const ACommandBuilding* Building, const ADepositSite* Deposit)
{
	return IsValid(Building) && Building->IsAlive() && Building->IsComplete()
		&& Building->Kind == EBuildingKind::Extractor && Building->Deposit == Deposit;
}

// The wallet must live in this world and belong to this match's roster (or be JEV's commander).
bool IsRosterWallet(const ACommandGameState& State, const ACommandBuilding& Building, const ACommandPlayerState* Wallet)
{
	if (!IsValid(Wallet) || Wallet->GetWorld() != State.GetWorld())
		return false;
	if (Building.TeamIndex == 5 && Wallet != State.EnemyCommander)
		return false;
	return Building.TeamIndex != 0
		|| State.PlayerArray.ContainsByPredicate(
			[Wallet](const TObjectPtr<APlayerState>& Player) { return Player.Get() == Wallet; });
}

void PayExtractor(ACommandGameState& State, ADepositSite& Deposit)
{
	ACommandBuilding* Building = Deposit.Extractor;
	if (!IsPayingExtractor(Building, &Deposit))
		return;
	ACommandPlayerState* Wallet = Building->OwningPlayerState;
	if (!IsRosterWallet(State, *Building, Wallet))
		return;
	const FExtractorPayment Payment = EconomyPolicy::ExtractorPayment({ Deposit.RatePerSecond(), Deposit.Remaining,
		2, Building->TeamIndex, Wallet->CommanderIndex, Wallet->TeamIndex, Wallet->CommanderIndex, Building->IsAlive(),
		Building->IsComplete() });
	if (Payment.Amount == 0)
		return;
	Wallet->AddResources(Payment.Amount);
	Deposit.Remaining = Payment.Remaining;
	Deposit.ForceNetUpdate();
}
}

void FGameStateEconomy::Tick(ACommandGameState& State, float DeltaSeconds)
{
	Elapsed += DeltaSeconds;
	while (Elapsed >= PaymentInterval)
	{
		Elapsed -= PaymentInterval;
		PayInterval(State);
	}
}

void FGameStateEconomy::PayInterval(ACommandGameState& State)
{
	PayHumanBaseline(State);
	PayEnemyBaseline(State);
	PayExtractors(State);
}

void FGameStateEconomy::PayHumanBaseline(ACommandGameState& State)
{
	for (APlayerState* Player : State.PlayerArray)
		if (ACommandPlayerState* Wallet = Cast<ACommandPlayerState>(Player))
			if (Wallet->TeamIndex == 0 && Wallet->CommanderIndex >= 0 && Wallet->CommanderIndex < 5)
				Wallet->AddResources(State.GetBaselineIncomePerSecond() * 2);
}

void FGameStateEconomy::PayEnemyBaseline(ACommandGameState& State)
{
	if (!IsValid(State.EnemyCommander))
		return;
	const int32 PaymentTenths = FMath::RoundToInt(EnemyBaselineIncomePerSecond(State) * 20.) + EnemyRemainderTenths;
	State.EnemyCommander->AddResources(PaymentTenths / 10);
	EnemyRemainderTenths = PaymentTenths % 10;
}

void FGameStateEconomy::PayExtractors(ACommandGameState& State)
{
	for (ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit))
			PayExtractor(State, *Deposit);
}

int32 FGameStateEconomy::BaselineIncomePerSecond(const UWorld* World)
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	return FSimulationSettings::ForWorld(World).BaselineIncome;
#else
	return EconomyPolicy::BaselineIncome;
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
	return State.GetBaselineIncomePerSecond() * EconomyPolicy::JevPlayerCountFactor(HumanCommanders);
}

int32 FGameStateEconomy::IncomePerSecond(const ACommandGameState& State, const ACommandPlayerState* Commander)
{
	int32 Income = State.GetBaselineIncomePerSecond();
	if (!IsValid(Commander))
		return Income;
	// Existing integer estimates floor only JEV's fractional baseline; extractor rates remain unscaled.
	if (Commander == State.EnemyCommander)
		Income = FMath::FloorToInt(EnemyBaselineIncomePerSecond(State));
	for (const ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && Deposit->Remaining > 0 && IsPayingExtractor(Deposit->Extractor, Deposit)
			&& Deposit->Extractor->OwningPlayerState == Commander
			&& Deposit->Extractor->TeamIndex == Commander->TeamIndex)
			Income += Deposit->RatePerSecond();
	return Income;
}
