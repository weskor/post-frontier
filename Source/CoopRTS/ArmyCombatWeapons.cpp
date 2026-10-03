#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyCombatScenario.h"

namespace ArmyCombatScenarioPrivate
{
bool FArmyCombatScenario::CheckWeapons(const ACommandGameState* State)
{
	AArmyUnit* Front = Army->GetUnits()[0];
	AArmyUnit* Ranged = Army->GetUnits()[2];
	AArmyUnit* Siege = Army->GetUnits()[4];
	if (!Check(Front->GetUnitRole() == EUnitRole::Frontline && Ranged->GetUnitRole() == EUnitRole::Ranged && Siege->GetUnitRole() == EUnitRole::Siege
				&& Front->WeaponRange() < Ranged->WeaponRange() && Ranged->WeaponRange() < Siege->WeaponRange(),
			TEXT("Frontline, ranged, siege spawn with strictly increasing weapon ranges")))
		return false;
	Victim = Enemy->GetUnits()[0];
	const FVector OriginalPosition = Victim->GetActorLocation();
	// FireAt is the authoritative weapon path. Probe just outside and inside each
	// role's effective range on a living hostile; one update prevents AI ticks
	// or cooldowns from contaminating the boundary observations.
	for (AArmyUnit* Shooter : { Front, Ranged, Siege })
		if (!ProbeWeapon(Shooter, Front, Ranged, Siege))
			return false;
	if (!CheckHeadquartersSplash(State, Siege))
		return false;
	Victim->SetActorLocation(OriginalPosition, false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

bool FArmyCombatScenario::ProbeWeapon(AArmyUnit* Shooter, AArmyUnit* Front, AArmyUnit* Ranged, AArmyUnit* Siege)
{
	const int32 Health = Victim->GetHealth();
	const uint32 Shots = Shooter->AttackCount;
	Victim->SetActorLocation(Shooter->GetActorLocation() + FVector(Shooter->WeaponRange() + 100.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	Shooter->FireAt(Victim.Get());
	if (!Check(Victim->GetHealth() == Health && Shooter->AttackCount == Shots,
			TEXT("Weapon cannot hit beyond its own role range")))
		return false;
	Victim->SetActorLocation(Shooter->GetActorLocation() + FVector(Shooter->WeaponRange() - 75.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	FVector SplashPositions[3];
	int32 SplashHealth[3];
	const FVector FriendlyPosition = Front->GetActorLocation();
	const int32 FriendlyHealth = Front->GetHealth();
	if (Shooter == Siege)
		PrepareSplash(Front, SplashPositions, SplashHealth);
	Shooter->FireAt(Victim.Get());
	const int32 ExpectedDamage = Shooter == Ranged
		? Shooter->GetDefinition()->AttackDamage * 3 / 2
		: Shooter->GetDefinition()->AttackDamage;
	if (!Check(Victim->GetHealth() == Health - ExpectedDamage && Shooter->AttackCount == Shots + 1,
			TEXT("Heavy target takes Piercing bonus and neutral Kinetic/Demolition damage within range")))
		return false;
	if (Shooter == Siege && !CheckSplash(Shooter, Front, SplashPositions, SplashHealth, FriendlyHealth, FriendlyPosition))
		return false;
	return true;
}

void FArmyCombatScenario::PrepareSplash(AArmyUnit* Front, FVector (&SplashPositions)[3], int32 (&SplashHealth)[3])
{
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AArmyUnit* Neighbour = Enemy->GetUnits()[Index + 2];
		SplashPositions[Index] = Neighbour->GetActorLocation();
		SplashHealth[Index] = Neighbour->GetHealth();
		const float Distance = Index == 0 ? 100.f : Index == 1 ? 200.f
															   : 201.f;
		Neighbour->SetActorLocation(Victim->GetActorLocation() + FVector(0.f, Distance, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
	}
	Front->SetActorLocation(Victim->GetActorLocation() + FVector(0.f, -100.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
}

bool FArmyCombatScenario::CheckSplash(AArmyUnit* Shooter, AArmyUnit* Front, const FVector (&SplashPositions)[3],
	const int32 (&SplashHealth)[3], int32 FriendlyHealth, const FVector& FriendlyPosition)
{
	const int32 SplashExpected[] = {
		Shooter->GetDefinition()->AttackDamage * 3 / 4,
		Shooter->GetDefinition()->AttackDamage / 2,
		0
	};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		AArmyUnit* Neighbour = Enemy->GetUnits()[Index + 2];
		if (!Check(Neighbour->GetHealth() == SplashHealth[Index] - SplashExpected[Index],
				TEXT("Artillery hits hostile neighbours with falloff, includes the edge and excludes outside")))
			return false;
		Neighbour->SetActorLocation(SplashPositions[Index], false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (!Check(Front->GetHealth() == FriendlyHealth, TEXT("Artillery splash never damages an ally inside the blast")))
		return false;
	Front->SetActorLocation(FriendlyPosition, false, nullptr, ETeleportType::TeleportPhysics);
	return true;
}

bool FArmyCombatScenario::CheckHeadquartersSplash(const ACommandGameState* State, AArmyUnit* Siege)
{
	AHeadquarters* HQ = State->EnemyHeadquarters;
	if (!Check(IsValid(HQ) && HQ->IsAlive(), TEXT("A live hostile HQ is available for non-primary splash")))
		return false;
	const FVector SiegePosition = Siege->GetActorLocation();
	Victim->SetActorLocation(HQ->GetActorLocation() + FVector(0.f, 100.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	Siege->SetActorLocation(Victim->GetActorLocation() + FVector(600.f, 0.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	const int32 HQHealth = HQ->Health;
	Siege->NextAttackTime = 0.f;
	Siege->FireAt(Victim.Get());
	Siege->SetActorLocation(SiegePosition, false, nullptr, ETeleportType::TeleportPhysics);
	const int32 StructureSplash = (Siege->GetDefinition()->AttackDamage * 3 / 2) * 3 / 4;
	if (!Check(HQ->Health == HQHealth - StructureSplash && HQ->IsAlive(),
			TEXT("A non-primary hostile HQ receives its Structure bonus then midpoint splash falloff")))
		return false;
	return true;
}
}

#endif
