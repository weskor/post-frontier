#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyDoctrineFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoctrineFrontlineTest, "CoopRTS.Doctrine.EntrenchedFrontline",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ArmyDoctrineFrontlineTests
{
using namespace ArmyDoctrineFixture;

class FFrontlineScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool Step(double Now) override
	{
		AArmyUnit* Held = Actors.Armies[0]->GetUnits()[0];
		AArmyUnit* Moving = Actors.Armies[1]->GetUnits()[0];
		AArmyUnit* Enemy = Actors.Enemy->GetUnits()[0];
		switch (Stage)
		{
		case 0:
			return Stage0(Now, Held, Moving, Enemy);
		case 1:
			return Stage1(Now, Moving, Enemy);
		case 2:
			return Stage2(Now, Moving, Enemy);
		case 3:
			return Stage3(Now, Enemy);
		default:
			return true;
		}
	}

	bool Stage0(double Now, AArmyUnit* Held, AArmyUnit* Moving, AArmyUnit* Enemy)
	{
		const int32 Baseline = WeaponHitFresh(Enemy, Held);
		if (!Check(Baseline > 0, TEXT("Hostile real weapon establishes unchosen frontline damage")))
			return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::EntrenchedFrontline);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::EntrenchedFrontline,
				TEXT("EntrenchedFrontline is the player's irreversible choice")))
			return true;
		for (const TWeakObjectPtr<AArmyGroup>& Group : Actors.Armies)
			if (!Check(Group->Verb == EForceVerb::MoveHold && Group->Status == EForceStatus::Holding,
					TEXT("Owned armies physically hold their generated region")))
				return true;
		const int32 Protected = WeaponHit(Enemy, Held);
		if (!Check(Protected == Baseline * 3 / 4 && Actors.Armies[0]->Status == EForceStatus::Holding
					&& Held->GetCharacterMovement()->Velocity.Size2D() <= 1.f,
				TEXT("Stationary held frontline takes exactly 25 percent less real weapon damage")))
			return true;
		const int32 OtherProtected = WeaponHitFresh(Enemy, Moving);
		if (!Check(OtherProtected == Protected && Actors.Armies[1]->Verb == EForceVerb::MoveHold,
				TEXT("Second owned held army gains identical protection")))
			return true;
		AArmyUnit* Piercing = Actors.Enemy->GetUnits()[2];
		const int32 PiercingProtected = (Piercing->GetDefinition()->AttackDamage * 3 / 2) * 3 / 4;
		if (!Check(WeaponHitFresh(Piercing, Held) == PiercingProtected,
				TEXT("Stationary held frontline mitigation composes with the Piercing bonus against Heavy")))
			return true;
		AArmyUnit* Ranged = Actors.Armies[0]->GetUnits()[2];
		if (!Check(Ranged->GetUnitRole() == EUnitRole::Ranged
					&& WeaponHit(Enemy, Ranged) == Baseline * 3 / 2,
				TEXT("Stationary held Light ranged takes full Kinetic bonus without frontline-only mitigation")))
			return true;
		Start = Moving->GetActorLocation();
		RetreatOrigin = Actors.Armies[1]->HoldPostLocation;
		RetreatRegion = Actors.Armies[1]->HoldRegionIndex;
		if (!Check(FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[1].Get(), EForceVerb::MoveHold,
					   ArmyTestSetup::TravelRegion(Actors.Armies[1].Get(), Actors.State->EnemyHeadquarters->GetActorLocation()))
					   .IsAccepted(),
				TEXT("Second army can travel to a neighbouring region")))
			return true;
		BaseDamage = Baseline;
		Next(1, Now);
		return false;
	}

	bool Stage1(double Now, AArmyUnit* Moving, AArmyUnit* Enemy)
	{
		if (!After(Now, .7) || ArmyTestSetup::CurrentRegion(Actors.Armies[1].Get()) == RetreatRegion
			|| FVector::Dist2D(Actors.Armies[1]->GetCenter(), RetreatOrigin) < 500.f)
			return false;
		if (!Check(FVector::Dist2D(Moving->GetActorLocation(), Start) > 30.f,
				TEXT("Frontline traveling to its MoveHold region actually changes position")))
			return true;
		if (!Check(WeaponHit(Enemy, Moving) == BaseDamage,
				TEXT("Moving frontline takes full real weapon damage despite chosen doctrine")))
			return true;
		if (!Check(FCommandService::IssueForceOrder(Actors.Wallet.Get(), Actors.Armies[1].Get(), EForceVerb::Retreat).IsAccepted(),
				TEXT("Retreat replaces the travelling MoveHold order")))
			return true;
		Start = Moving->GetActorLocation();
		Next(2, Now);
		return false;
	}

	bool Stage2(double Now, AArmyUnit* Moving, AArmyUnit* Enemy)
	{
		if (!Check(Actors.Armies[1]->Status == EForceStatus::Retreating,
				TEXT("Retreat encounter must remain en route before its weapon comparison")))
			return true;
		if (FVector::Dist2D(Moving->GetActorLocation(), Start) <= 30.f)
			return false;
		if (!Check(WeaponHit(Enemy, Moving) == BaseDamage,
				TEXT("Retreat cannot obtain stationary MoveHold protection")))
			return true;
		int32 LaterRegion = INDEX_NONE;
		float Closest = TNumericLimits<float>::Max();
		for (const AMapRegion* Region : Actors.State->Regions)
		{
			if (!IsValid(Region) || Region->RegionIndex == Actors.Armies[0]->TargetRegionIndex
				|| Region->RegionIndex == Actors.Armies[1]->WaypointRegionIndex
				|| (Region->RegionRole == ERegionRole::Main && Region->HomeTeam != Actors.Wallet->TeamIndex)
				|| Actors.State->IsRegionContested(Region->RegionIndex, Actors.Wallet->TeamIndex))
				continue;
			const float Distance = FVector::DistSquared2D(Actors.State->GetRegionAnchor(Region->RegionIndex),
				Actors.State->FriendlyHeadquarters->GetActorLocation());
			if (Distance < Closest)
			{
				Closest = Distance;
				LaterRegion = Region->RegionIndex;
			}
		}
		if (!Check(LaterRegion != INDEX_NONE, TEXT("Later frontline has a vacant nonhostile region anchor")))
			return true;
		AArmyGroup* Later = ArmyTestSetup::SpawnGroup(Actors.World.Get(), Actors.Controller.Get(), 2,
			Actors.State->GetRegionAnchor(LaterRegion) + FVector(0.f, 0.f, 100.f));
		LaterFrontline = Later ? Later->GetUnits()[0].Get() : nullptr;
		if (!Check(LaterFrontline.IsValid(), TEXT("New frontline exists after research")))
			return true;
		if (!Check(FCommandService::IssueForceOrder(Actors.Wallet.Get(), Later, EForceVerb::MoveHold, LaterRegion).IsAccepted(),
				TEXT("New squad adopts MoveHold after research")))
			return true;
		Next(3, Now);
		return false;
	}

	bool Stage3(double Now, AArmyUnit* Enemy)
	{
		if (!Settled(LaterFrontline->GetGroup()))
		{
			StageStarted = Now;
			return false;
		}
		if (!After(Now, .5))
			return false;
		AArmyUnit* Replacement = LaterFrontline.Get();
		if (!Check(Replacement && WeaponHitFresh(Enemy, Replacement) == BaseDamage * 3 / 4,
				TEXT("New stationary held frontline inherits protection against a real shot")))
			return true;
		if (!Check(FCommandService::IssueForceOrder(Actors.Wallet.Get(), Replacement->GetGroup(), EForceVerb::Attack, INDEX_NONE, Actors.State->EnemyHeadquarters).IsAccepted()
					&& WeaponHit(Enemy, Replacement) == BaseDamage,
				TEXT("Attack frontline does not receive stationary Hold mitigation")))
			return true;
		Test->AddInfo(TEXT("Entrenched: stationary Holding mitigates real hits; traveling, retreating and Attack members do not; later units inherit."));
		return true;
	}
	int32 BaseDamage = 0;
	int32 RetreatRegion = INDEX_NONE;
	FVector Start = FVector::ZeroVector;
	FVector RetreatOrigin = FVector::ZeroVector;
	TWeakObjectPtr<AArmyUnit> LaterFrontline;
};
}

bool FDoctrineFrontlineTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ArmyDoctrineFrontlineTests::FFrontlineScenario(this));
	return true;
}

#endif
