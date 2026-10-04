#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SupplyDeliveryFixture.h"

#include "HAL/PlatformTime.h"

namespace SupplyTests
{
bool FScenarioBase::Update()
{
	if (bFailed)
		return true;
	if (FPlatformTime::Seconds() - Started > 120.)
	{
		Test->AddError(FString::Printf(TEXT("Supply scenario timed out at stage %d (joined=%d in transit=%d waiting=%d)"), Stage,
			Force.IsValid() ? Joined() : -1, Force.IsValid() ? Force->RecruitsInTransit : -1, Force.IsValid() ? Force->RecruitsWaiting : -1));
		return true;
	}
	if (!bPrepared)
		return Prepare();
	return Run();
}

bool FScenarioBase::Check(bool bValue, const TCHAR* Message)
{
	if (!bValue)
	{
		Test->AddError(Message);
		bFailed = true;
	}
	return bValue;
}

void FScenarioBase::SetStage(int32 Next)
{
	Stage = Next;
	StageStarted = Now();
}

ACommandBuilding* FScenarioBase::SpawnProducer()
{
	return Fixture->SpawnBarracks(0, 1.f, 0);
}

// Returns true when the scenario must stop.
bool FScenarioBase::Prepare()
{
	GameWorld = ArmyTestSetup::World();
	if (!GameWorld || GameWorld->GetTimeSeconds() < 3.f || !ArmyTestSetup::NavigationReady(GameWorld))
		return false;
	Fixture = FTeamEconomyFixture::Create(GameWorld, Test);
	if (!Fixture)
		return false;
	State = Fixture->State;
	if (!Check(Fixture->Reset(1), TEXT("Fixture needs regions 2, 3 and 4 with anchors and deposits")))
		return true;
	Wallet = Fixture->Wallets[0];
	Wallet->Resources = 10000;
	Producer = SpawnProducer();
	if (!Check(Producer.IsValid(), TEXT("A finished Barracks stands in the scenario's producer region")))
		return true;
	// The first Start locks the unit type and creates the force; pause again before any work happens.
	if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted()
				&& IsValid(Producer->ForceGroup),
			TEXT("The Barracks locks a Frontline force")))
		return true;
	Force = Producer->ForceGroup;
	FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false);
	AArmyGroup* Hostile = ArmyTestSetup::SpawnGroup(GameWorld, nullptr, -1, ArmyTestSetup::HostileStaging(State));
	if (!Check(Hostile && !Hostile->GetUnits().IsEmpty(), TEXT("A hostile unit exists to deal lethal damage")))
		return true;
	Hostile->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Hostile->GetUnits())
	{
		Unit->SetActorTickEnabled(false);
		Unit->NextAttackTime = TNumericLimits<float>::Max();
	}
	Attacker = Hostile->GetUnits()[0];
	bPrepared = true;
	SetStage(0);
	return false;
}

int32 FScenarioBase::Produce()
{
	const int32 Before = Wallet->Resources;
	Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(),
		TEXT("The Barracks accepts production"));
	Producer->TickProduction(Producer->GetProductionDuration());
	FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false);
	return Before - Wallet->Resources;
}

AArmyUnit* FScenarioBase::FirstMember() const
{
	for (AArmyUnit* Unit : Force->GetUnits())
		if (IsValid(Unit) && Unit->IsAlive())
			return Unit;
	return nullptr;
}

int32 FScenarioBase::MemberActors() const
{
	int32 Count = 0;
	for (TActorIterator<AArmyUnit> It(GameWorld); It; ++It)
		Count += It->IsAlive() && It->GetGroup() == Force.Get();
	return Count;
}

bool FScenarioBase::InRegion(const AArmyUnit* Unit, int32 Region) const
{
	return Unit && ArmyTestSetup::RegionAt(State, Unit->GetActorLocation()) == Region;
}

bool FScenarioBase::PutForceInFar()
{
	if (Stage == 0)
	{
		Produce();
		const AArmyUnit* Recruit = FirstMember();
		if (!Check(Recruit && Joined() == 1 && Force->GetPendingRecruitCount() == 0
					&& FVector::Dist2D(Recruit->GetActorLocation(), Producer->GetActorLocation()) < 1200.f,
				TEXT("An empty force's first recruit spawns joined at the producer exit")))
			return false;
		const FVector Anchor = State->GetRegionAnchor(Far);
		FirstMember()->SetActorLocation(Anchor + FVector(0.f, 0.f, 100.f), false, nullptr, ETeleportType::TeleportPhysics);
		if (!Check(FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, Far).IsAccepted(),
				TEXT("The force accepts a hold order in Far")))
			return false;
		SetStage(1);
	}
	return Stage == 1 && StageSeconds() >= 1.5 && Force->Status == EForceStatus::Holding
		&& InRegion(FirstMember(), Far);
}
}
#endif
