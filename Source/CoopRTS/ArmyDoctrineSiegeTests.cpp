#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyDoctrineFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoctrineSiegeTest, "CoopRTS.Doctrine.SiegeOptics",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ArmyDoctrineSiegeTests
{
using namespace ArmyDoctrineFixture;

class FSiegeScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool Step(double Now) override
	{
		AArmyUnit* Siege = Actors.Armies[0]->GetUnits()[4];
		AArmyUnit* OtherSiege = Actors.Armies[1]->GetUnits()[4];
		AArmyUnit* EnemySiege = Actors.Enemy->GetUnits()[4];
		AArmyUnit* Victim = Actors.Enemy->GetUnits()[0];
		const float BaseRange = Siege->GetDefinition()->Range;
		FVector Original;
		int32 WalletBefore;
		float ProbeRange;
		int32 BaseDamage;
		int32 OpticsDamage;
		if (RunInitialProbe(Siege, OtherSiege, EnemySiege, Victim, BaseRange,
				Original, WalletBefore, ProbeRange, BaseDamage))
			return true;
		if (RunTeammateProbe(Siege, Victim, BaseRange, Original, WalletBefore, ProbeRange))
			return true;
		if (RunOpticsProbe(Siege, OtherSiege, Victim, Original, ProbeRange, BaseDamage, OpticsDamage))
			return true;
		return FinishProbe(Siege, Victim, BaseRange, ProbeRange, OpticsDamage);
	}

	bool RunInitialProbe(AArmyUnit* Siege, AArmyUnit* OtherSiege, AArmyUnit* EnemySiege,
		AArmyUnit* Victim, float BaseRange, FVector& Original, int32& WalletBefore,
		float& ProbeRange, int32& BaseDamage)
	{
		if (!Check(Siege->GetDefinition() == OtherSiege->GetDefinition()
					&& Siege->GetDefinition() == EnemySiege->GetDefinition(),
				TEXT("All siege roles share the unmodified base asset")))
			return true;
		Original = Victim->GetActorLocation();
		WalletBefore = Actors.Wallet->Resources;
		ProbeRange = BaseRange * 1.125f;
		Victim->SetActorLocation(Siege->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		const int32 Outside = Victim->GetHealth();
		const uint32 BeforeShots = Siege->AttackCount;
		Siege->NextAttackTime = 0.f;
		Siege->FireAt(Victim);
		if (!Check(Victim->GetHealth() == Outside && Siege->AttackCount == BeforeShots,
				TEXT("Unchosen siege cannot hit at the future optics-only range")))
			return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		BaseDamage = WeaponHitFresh(Siege, Victim);
		if (!Check(BaseDamage > 0, TEXT("Unchosen siege inflicts actual weapon damage")))
			return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::None);
		ArmyTestSetup::Research(Actors.Controller.Get(), static_cast<EArmyDoctrine>(255));
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::None && Actors.Wallet->Resources == WalletBefore,
				TEXT("None and invalid enum requests reject without spending or selecting")))
			return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::SiegeOptics);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost,
				TEXT("Owned workshop purchase selects SiegeOptics and pays once")))
			return true;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::FieldRepairs);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics
					&& Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost,
				TEXT("A purchased specialization cannot be replaced or charged twice")))
			return true;
		if (!Check(FMath::IsNearlyEqual(Siege->WeaponRange(), BaseRange * 1.25f)
					&& FMath::IsNearlyEqual(OtherSiege->WeaponRange(), BaseRange * 1.25f)
					&& FMath::IsNearlyEqual(EnemySiege->WeaponRange(), BaseRange)
					&& FMath::IsNearlyEqual(Siege->GetDefinition()->Range, BaseRange),
				TEXT("Both owned armies inherit optics; hostile siege and shared asset remain unchanged")))
			return true;
		return false;
	}

	bool RunTeammateProbe(AArmyUnit* Siege, AArmyUnit* Victim, float BaseRange,
		const FVector& Original, int32 WalletBefore, float ProbeRange)
	{
		// A second controller has its own PlayerState and an actual six-unit group.
		// Sharing team and DataAsset must not share this player's doctrine.
		ACommandPlayerController* Teammate = Actors.World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* TeammateWallet = Actors.World->SpawnActor<ACommandPlayerState>();
		if (!Check(Teammate && TeammateWallet, TEXT("Second player controller and wallet spawn")))
			return true;
		Teammate->SetPlayerState(TeammateWallet);
		Actors.State->AddPlayerState(TeammateWallet);
		TeammateWallet->CommanderIndex = 1;
		const FTransform AllyTransform(FRotator::ZeroRotator,
			ArmyTestSetup::FromFriendlyHQ(Actors.State.Get(), 1700.f, -600.f, 100.f));
		AArmyGroup* Ally = Actors.World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(),
			AllyTransform, Teammate, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Check(Ally, TEXT("Second player's owned group spawns")))
			return true;
		Ally->Initialize({ 0, TeammateWallet, 0, nullptr, AllyTransform.GetLocation() });
		Ally->FinishSpawning(AllyTransform);
		if (!Check(Ally->SpawnUnits() && Ally->GetUnits().Num() == 6 && TeammateWallet->Doctrine == EArmyDoctrine::None
					&& FMath::IsNearlyEqual(Ally->GetUnits()[4]->WeaponRange(), BaseRange),
				TEXT("Other player's same-team siege does not inherit optics")))
			return true;
		const int32 TeammateBalance = TeammateWallet->Resources;
		ArmyTestSetup::Research(Teammate, EArmyDoctrine::FieldRepairs);
		if (!Check(TeammateWallet->Doctrine == EArmyDoctrine::FieldRepairs
					&& Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics
					&& TeammateWallet->Resources == TeammateBalance - ACommandBuilding::ResearchCost
					&& Actors.Wallet->Resources == WalletBefore - ACommandBuilding::ResearchCost
					&& FMath::IsNearlyEqual(Ally->GetUnits()[4]->WeaponRange(), BaseRange)
					&& FMath::IsNearlyEqual(Siege->WeaponRange(), BaseRange * 1.25f),
				TEXT("Another player's choice and wallet remain independent on a shared team")))
			return true;
		Victim->SetActorLocation(Ally->GetUnits()[4]->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		const uint32 AllyShots = Ally->GetUnits()[4]->AttackCount;
		Ally->GetUnits()[4]->NextAttackTime = 0.f;
		Ally->GetUnits()[4]->FireAt(Victim);
		if (!Check(Ally->GetUnits()[4]->AttackCount == AllyShots,
				TEXT("Unchosen teammate cannot land an optics-only siege hit")))
			return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		return false;
	}

	bool RunOpticsProbe(AArmyUnit* Siege, AArmyUnit* OtherSiege, AArmyUnit* Victim,
		const FVector& Original, float ProbeRange, int32 BaseDamage, int32& OpticsDamage)
	{
		Victim->SetActorLocation(Siege->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		AArmyUnit* SplashVictim = Actors.Enemy->GetUnits()[2];
		if (!Check(IsValid(SplashVictim) && SplashVictim->IsAlive(), TEXT("A live hostile Light neighbour is available for optics splash")))
			return true;
		const FVector SplashPosition = SplashVictim->GetActorLocation();
		const int32 SplashHealth = SplashVictim->GetHealth();
		// At 144 cm, base damage 40 truncates to 25, then optics gives 18.
		// Applying optics first would instead truncate 30 * .64 to 19.
		SplashVictim->SetActorLocation(Victim->GetActorLocation() + FVector(0.f, 144.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->NextAttackTime = 0.f;
		const uint32 Shots = Siege->AttackCount;
		const int32 Before = Victim->GetHealth();
		Siege->FireAt(Victim);
		SplashVictim->SetActorLocation(SplashPosition, false, nullptr, ETeleportType::TeleportPhysics);
		const int32 SplashOpticsDamage = static_cast<int32>(BaseDamage * .64f) * 3 / 4;
		if (!Check(SplashVictim->GetHealth() == SplashHealth - SplashOpticsDamage,
				TEXT("Optics splash truncates distance falloff before applying the outgoing Workshop modifier")))
			return true;
		if (!Check(Siege->AttackCount == Shots + 1 && Victim->GetHealth() < Before,
				TEXT("Optics siege lands a real hit beyond its former range")))
			return true;
		Victim->SetActorLocation(Original, false, nullptr, ETeleportType::TeleportPhysics);
		OpticsDamage = WeaponHitFresh(Siege, Victim);
		if (!Check(OpticsDamage == BaseDamage * 3 / 4 && OpticsDamage > 0,
				TEXT("Optics trades actual outgoing siege weapon damage for range")))
			return true;
		if (!Check(WeaponHitFresh(OtherSiege, Victim) == OpticsDamage,
				TEXT("Second owned army applies the same outgoing damage tradeoff")))
			return true;
		return false;
	}

	bool FinishProbe(AArmyUnit* Siege, AArmyUnit* Victim, float BaseRange, float ProbeRange, int32 OpticsDamage)
	{
		AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
		if (!Check(IsValid(HQ) && HQ->IsAlive(), TEXT("Live hostile HQ exists for optics hit")))
			return true;
		const FVector SiegeHome = Siege->GetActorLocation();
		Siege->SetActorLocation(HQ->GetActorLocation() + FVector(ProbeRange, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->NextAttackTime = 0.f;
		const int32 HQBefore = HQ->Health;
		Siege->NextAttackTime = 0.f;
		Siege->FireAt(HQ);
		Siege->SetActorLocation(SiegeHome, false, nullptr, ETeleportType::TeleportPhysics);
		const int32 StructureOpticsDamage = (Siege->GetDefinition()->AttackDamage * 3 / 2) * 3 / 4;
		if (!Check(HQ->Health == HQBefore - StructureOpticsDamage && HQ->IsAlive(),
				TEXT("Optics siege composes the Structure bonus with reduced damage at extended HQ range")))
			return true;
		AArmyGroup* Later = ArmyTestSetup::SpawnGroup(Actors.World.Get(), Actors.Controller.Get(), 2,
			ArmyTestSetup::FromFriendlyHQ(Actors.State.Get(), 1700.f, 1700.f, 100.f));
		AArmyUnit* Replacement = Later ? Later->GetUnits()[4].Get() : nullptr;
		if (!Check(Replacement && Replacement->IsAlive()
					&& FMath::IsNearlyEqual(Replacement->WeaponRange(), BaseRange * 1.25f),
				TEXT("Units created after research inherit the owner effect without changing the asset")))
			return true;
		if (!Check(WeaponHitFresh(Replacement, Victim) == OpticsDamage,
				TEXT("Replacement siege fires with the same reduced real weapon damage")))
			return true;
		Test->AddInfo(TEXT("Optics: real extended unit/HQ hits, outgoing damage tradeoff, existing and later units, paid irreversible research, owner isolation."));
		return true;
	}
};
}

bool FDoctrineSiegeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ArmyDoctrineSiegeTests::FSiegeScenario(this));
	return true;
}

#endif
