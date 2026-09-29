#include "ArmyUnit.h"

#include "ArmyGroup.h"
#include "CombatTarget.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "DetourCrowdAIController.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Constructor-only: FObjectFinder must run while a CDO is being constructed.
	void FindThemedMeshes(const TCHAR* Faction, TArray<TObjectPtr<UStaticMesh>>& OutMeshes)
	{
		static const TCHAR* const Roles[] = { TEXT("Frontline"), TEXT("Ranged"), TEXT("Siege") };
		for (const TCHAR* Role : Roles)
		{
			const FString Path = FString::Printf(TEXT("/Game/Art/Units/SM_%s_%s.SM_%s_%s"), Faction, Role, Faction, Role);
			ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(*Path);
			OutMeshes.Add(Finder.Succeeded() ? Finder.Object : nullptr);
		}
	}
}

AArmyUnit::AArmyUnit()
{
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = true;
	bAlwaysRelevant = true;
	AIControllerClass = ADetourCrowdAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;

	GetCapsuleComponent()->InitCapsuleSize(34.0f, 60.0f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetMesh()->SetCanEverAffectNavigation(false);

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = 420.0f;
	Movement->MaxAcceleration = 1600.0f;
	Movement->BrakingDecelerationWalking = 2000.0f;
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	// Detour owns corridor-aware local avoidance; stacking Character RVO on
	// top can divert agents from reachable final slots and terminate as blocked.
	Movement->bUseRVOAvoidance = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(GetRootComponent());
	Body->SetRelativeScale3D(FVector(0.60f, 0.60f, 1.0f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Body->SetStaticMesh(Cube.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TeamMaterial(TEXT("/Game/Materials/M_CommandUnit.M_CommandUnit"));
	if (TeamMaterial.Succeeded())
	{
		Body->SetMaterial(0, TeamMaterial.Object);
	}
	FindThemedMeshes(TEXT("Human"), HumanMeshes);
	FindThemedMeshes(TEXT("Machine"), MachineMeshes);
}

void AArmyUnit::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Appearance();
}
void AArmyUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
		if (!IsAlive() || !State || State->MatchResult != EMatchResult::Ongoing
			|| GetDoctrine() != EArmyDoctrine::FieldRepairs
			|| GetVelocity().SizeSquared2D() > FMath::Square(1.f))
		{
			ResetRepairTimer();
		}
		else
		{
			const float HealTime = QuietSeconds >= 5.f ? DeltaSeconds
				: FMath::Max(0.f, QuietSeconds + DeltaSeconds - 5.f);
			QuietSeconds += DeltaSeconds;
			if (Health < MaxHealth())
			{
				HealAccumulator += HealTime * 5.f;
				const int32 Recovered = FMath::Min(FMath::FloorToInt(HealAccumulator), MaxHealth() - Health);
				if (Recovered > 0)
				{
					Health += Recovered;
					HealAccumulator -= Recovered;
					ForceNetUpdate();
				}
			}
			if (Health >= MaxHealth()) HealAccumulator = 0.f;
		}
	}
	if (!IsAlive() || MaxHealth() <= 0 || GetNetMode() == NM_DedicatedServer) return;
	const FVector Start = GetActorLocation() + FVector(-55.f, 0.f, 135.f);
	const FVector End = Start + FVector(110.f, 0.f, 0.f);
	DrawDebugLine(GetWorld(), Start, End, FColor(25, 25, 25), false, -1.f, 0, 9.f);
	DrawDebugLine(GetWorld(), Start, FMath::Lerp(Start, End,
		FMath::Clamp(static_cast<float>(Health) / MaxHealth(), 0.f, 1.f)),
		TeamIndex == 5 ? FColor::Red : FColor::Green, false, -1.f, 0, 5.f);
}


FLinearColor AArmyUnit::GetCommanderColor(int32 InCommanderIndex)
{
	static const FLinearColor Colors[] = {
		FLinearColor(.04f, .50f, 1.f),
		FLinearColor(1.f, .72f, .04f),
		FLinearColor(.16f, .85f, .25f),
		FLinearColor(.65f, .25f, 1.f),
		FLinearColor(.04f, .85f, .80f)
	};
	return InCommanderIndex >= 0 && InCommanderIndex < UE_ARRAY_COUNT(Colors)
		? Colors[InCommanderIndex] : FLinearColor(1.f, .08f, .08f);
}

void AArmyUnit::OnRep_Appearance()
{
	const FLinearColor CommanderColor = TeamIndex == 5
		? FLinearColor(1.f, .08f, .08f) : GetCommanderColor(CommanderIndex);
	const TArray<TObjectPtr<UStaticMesh>>& Meshes = TeamIndex == 5 ? MachineMeshes : HumanMeshes;
	UStaticMesh* Themed = Meshes.IsValidIndex(static_cast<int32>(UnitRole)) ? Meshes[static_cast<int32>(UnitRole)].Get() : nullptr;
	if (Themed && Body->GetStaticMesh() != Themed)
	{
		Body->SetStaticMesh(Themed);
		// SetStaticMesh keeps the cube's override (and any dynamic instance); drop them so the Team slot starts from the mesh default.
		Body->EmptyOverrideMaterials();
	}
	const int32 TeamSlot = Themed ? FMath::Max(0, Body->GetMaterialIndex(TEXT("Team"))) : 0;
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(TeamSlot));
	if (!Material)
	{
		Material = Body->CreateAndSetMaterialInstanceDynamic(TeamSlot);
	}
	if (Material)
	{
		const FLinearColor TeamColor = CommanderColor;
		const FLinearColor ArmyColor = ArmyIndex == 1
			? FMath::Lerp(TeamColor, FLinearColor::White, 0.45f) : TeamColor;
		// Role whitening only tells roles apart on identical cubes; themed silhouettes do that already.
		const float RoleWhitening = Themed ? 0.f
			: UnitRole == EUnitRole::Siege ? .38f : UnitRole == EUnitRole::Ranged ? .16f : 0.f;
		Material->SetVectorParameterValue(TEXT("TeamColor"), Health > 0
			? FMath::Lerp(ArmyColor, FLinearColor::White, RoleWhitening)
			: FLinearColor(0.08f, 0.08f, 0.08f));
	}
	Body->SetRelativeScale3D(Themed ? FVector::OneVector
		: UnitRole == EUnitRole::Frontline ? FVector(.72f, .68f, 1.12f)
		: UnitRole == EUnitRole::Ranged ? FVector(.48f, .48f, .85f) : FVector(.86f, .78f, .52f));
}

int32 AArmyUnit::MaxHealth() const
{
	return Definition ? Definition->MaxHealth : 0;
}

EArmyDoctrine AArmyUnit::GetDoctrine() const
{
	return IsValid(Group) ? Group->GetDoctrine() : EArmyDoctrine::None;
}

void AArmyUnit::ResetRepairTimer()
{
	QuietSeconds = 0.f;
	HealAccumulator = 0.f;
}

float AArmyUnit::WeaponRange() const
{
	return Definition ? Definition->Range * (UnitRole == EUnitRole::Siege
		&& GetDoctrine() == EArmyDoctrine::SiegeOptics ? 1.25f : 1.f) : 0.f;
}

float AArmyUnit::AttackInterval() const
{
	return Definition ? Definition->Interval : 0.f;
}

void AArmyUnit::OnRep_Attack()
{
	if (!CombatTarget::IsAliveHostile(Target.Get(), TeamIndex) || GetNetMode() == NM_DedicatedServer) return;
	const FColor Color = UnitRole == EUnitRole::Siege ? FColor::Purple
		: UnitRole == EUnitRole::Ranged ? FColor::Cyan : FColor::Yellow;
	DrawDebugLine(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 90.f),
		Target->GetActorLocation() + FVector(0.f, 0.f, 75.f), Color, false, .32f, 0,
		UnitRole == EUnitRole::Siege ? 6.f : 3.f);
	DrawDebugSphere(GetWorld(), Target->GetActorLocation() + FVector(0.f, 0.f, 75.f),
		UnitRole == EUnitRole::Siege ? 32.f : 17.f, 8, Color, false, .32f, 0, 2.f);
}

void AArmyUnit::FireAt(AActor* Victim)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsAlive() || !IsValid(Victim) || Victim->GetWorld() != GetWorld()
		|| (State && State->MatchResult != EMatchResult::Ongoing)
		|| !CombatTarget::IsAliveHostile(Victim, TeamIndex)
		|| FVector::Dist2D(GetActorLocation(), Victim->GetActorLocation()) > WeaponRange())
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextAttackTime) return;
	NextAttackTime = Now + AttackInterval();
	ResetRepairTimer();
	Target = Victim;
	++AttackCount;
	OnRep_Attack();
	ForceNetUpdate();
	// Integer tradeoffs truncate toward zero for both units and HQ.
	const int32 Damage = UnitRole == EUnitRole::Siege && GetDoctrine() == EArmyDoctrine::SiegeOptics
		? Definition->AttackDamage * 3 / 4 : Definition->AttackDamage;
	CombatTarget::ReceiveAttack(Victim, Damage, this);
}

void AArmyUnit::ReceiveAttack(int32 Damage, AArmyUnit* Attacker)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsAlive() || !IsValid(Attacker) || !Attacker->IsAlive()
		|| (State && State->MatchResult != EMatchResult::Ongoing)
		|| Attacker->TeamIndex == TeamIndex || Damage <= 0) return;
	ResetRepairTimer();
	const int32 AppliedDamage = UnitRole == EUnitRole::Frontline
		&& GetDoctrine() == EArmyDoctrine::EntrenchedFrontline
		&& IsValid(Group) && Group->Order == EArmyOrder::Hold
		&& GetVelocity().SizeSquared2D() <= FMath::Square(1.f)
		? Damage * 3 / 4 : Damage;
	Health = FMath::Max(0, Health - AppliedDamage);
	ForceNetUpdate();
	if (Health == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("Combat death %s role=%d killer=%s"), *GetName(), static_cast<int32>(UnitRole), *Attacker->GetName());
		if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
		if (AController* Controller = GetController()) Controller->Destroy();
		GetCharacterMovement()->StopMovementImmediately();
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Target = nullptr;
		OnRep_Appearance();
		if (IsValid(Group))
		{
			Group->Units.Remove(this);
			Group->ForceNetUpdate();
		}
		SetLifeSpan(2.f);
	}
}

void AArmyUnit::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AArmyUnit, Group);
	DOREPLIFETIME(AArmyUnit, TeamIndex);
	DOREPLIFETIME(AArmyUnit, CommanderIndex);
	DOREPLIFETIME(AArmyUnit, ArmyIndex);
	DOREPLIFETIME(AArmyUnit, Definition);
	DOREPLIFETIME(AArmyUnit, UnitRole);
	DOREPLIFETIME(AArmyUnit, CompositionSlot);
	DOREPLIFETIME(AArmyUnit, Health);
	DOREPLIFETIME(AArmyUnit, Target);
	DOREPLIFETIME(AArmyUnit, AttackCount);
}
