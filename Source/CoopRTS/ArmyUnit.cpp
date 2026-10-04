#include "ArmyUnit.h"

#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CombatTarget.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "CoopAudioSubsystem.h"
#include "WorldOverlay.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "DetourCrowdAIController.h"
#include "EngineUtils.h"
#include "Headquarters.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

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
}

void AArmyUnit::Initialize(AArmyGroup* InGroup, int32 InTeamIndex, int32 InCommanderIndex,
	int32 InArmyIndex, int32 InCompositionSlot, int32 InUnitIndex,
	UArmyUnitDefinition* InDefinition, bool bInReinforcing)
{
	Group = InGroup;
	TeamIndex = InTeamIndex;
	CommanderIndex = InCommanderIndex;
	ArmyIndex = InArmyIndex;
	CompositionSlot = InCompositionSlot;
	UnitIndex = InUnitIndex;
	Definition = InDefinition;
	UnitRole = Definition->Role;
	GetCharacterMovement()->MaxWalkSpeed = Definition->MoveSpeed;
	Health = MaxHealth();
	Shield = MaxShield();
	bReinforcing = bInReinforcing;
}

void AArmyUnit::BeginPlay()
{
	Super::BeginPlay();
	LastAudioAttackCount = AttackCount;
	LastAudioHealth = Health;
	bDeathAudioPlayed = Health <= 0;
	bAudioStateInitialized = true;
	OnRep_Appearance();
	if (HasAuthority())
		RefreshRegion();
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
					OnRep_Appearance();
					ForceNetUpdate();
				}
			}
			if (Health >= MaxHealth())
				HealAccumulator = 0.f;
		}
		TickShield(DeltaSeconds);
		TickRegion(DeltaSeconds);
		TickPulse();
	}
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
		? Colors[InCommanderIndex]
		: FLinearColor(1.f, .08f, .08f);
}

void AArmyUnit::OnRep_Appearance()
{
	if (Definition)
	{
		const float OrderSpeed = IsValid(Group) ? Group->GetMarchSpeed() : 0.f;
		GetCharacterMovement()->MaxWalkSpeed = OrderSpeed > 0.f ? OrderSpeed : Definition->MoveSpeed;
	}
	const bool bTookDamage = bAudioStateInitialized && Health < LastAudioHealth;
	const bool bDied = bTookDamage && LastAudioHealth > 0 && Health <= 0 && !bDeathAudioPlayed;
	LastAudioHealth = Health;
	if (bDied)
		bDeathAudioPlayed = true;
	if (bTookDamage)
	{
		if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		{
			Audio->PlayUnit(ECoopAudioEvent::Impact, TeamIndex, UnitRole, GetActorLocation(), this);
			if (bDied)
				Audio->PlayUnit(ECoopAudioEvent::Death, TeamIndex, UnitRole, GetActorLocation(), this);
		}
	}

	const FLinearColor CommanderColor = TeamIndex == 5
		? FLinearColor(1.f, .08f, .08f)
		: GetCommanderColor(CommanderIndex);
	UStaticMesh* Themed = Definition
		? (TeamIndex == 5 ? Definition->MachineMesh : Definition->HumanMesh).LoadSynchronous()
		: nullptr;
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
		const FLinearColor ArmyColor = ArmyIndex == 1
			? FMath::Lerp(CommanderColor, FLinearColor::White, 0.45f)
			: CommanderColor;
		Material->SetVectorParameterValue(TEXT("TeamColor"), Health > 0 ? ArmyColor : FLinearColor(0.08f, 0.08f, 0.08f));
	}
	if (Themed)
		Body->SetRelativeScale3D(FVector::OneVector);
}

int32 AArmyUnit::MaxHealth() const
{
	return Definition ? Definition->MaxHealth : 0;
}

int32 AArmyUnit::MaxShield() const
{
	return Definition ? Definition->MaxShield : 0;
}

void AArmyUnit::StripShield()
{
	if (!HasAuthority() || !IsAlive())
		return;
	Shield = 0;
	ShieldPolicy::RestartRegen(ShieldClock);
	ForceNetUpdate();
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
	if (!Definition)
		return 0.f;
	const float Doctrine = UnitRole == EUnitRole::Siege && GetDoctrine() == EArmyDoctrine::SiegeOptics ? 1.25f : 1.f;
	return Definition->Range * Doctrine * RegionTraitPolicy::RangeMultiplier(GetRegionTrait());
}

float AArmyUnit::AttackInterval() const
{
	return Definition ? Definition->Interval : 0.f;
}

void AArmyUnit::OnRep_Attack()
{
	const bool bNewAttack = bAudioStateInitialized && AttackCount != LastAudioAttackCount;
	LastAudioAttackCount = AttackCount;
	if (bNewAttack)
	{
		if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		{
			Audio->PlayUnit(ECoopAudioEvent::Attack, TeamIndex, UnitRole, GetActorLocation(), this);
		}
	}
	if (!CombatTarget::IsAliveHostile(Target.Get(), TeamIndex) || GetNetMode() == NM_DedicatedServer)
		return;
	const FColor Color = UnitRole == EUnitRole::Siege ? FColor::Purple
		: UnitRole == EUnitRole::Ranged               ? FColor::Cyan
													  : FColor::Yellow;
	if (AWorldOverlay* Overlay = AWorldOverlay::Get(this))
		Overlay->Attack(GetActorLocation() + FVector(0.f, 0.f, 90.f),
			Target->GetActorLocation() + FVector(0.f, 0.f, 75.f), Color, UnitRole == EUnitRole::Siege);
}

void AArmyUnit::FireAt(AActor* Victim)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsAlive() || bReinforcing || !IsValid(Victim) || Victim->GetWorld() != GetWorld()
		|| (State && State->MatchResult != EMatchResult::Ongoing)
		|| (IsValid(Group) && Group->Status == EForceStatus::Retreating)
		|| !CombatTarget::IsAliveHostile(Victim, TeamIndex)
		|| FVector::Dist2D(GetActorLocation(), Victim->GetActorLocation()) > WeaponRange())
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextAttackTime)
		return;
	NextAttackTime = Now + AttackInterval();
	ResetRepairTimer();
	Target = Victim;
	++AttackCount;
	OnRep_Attack();
	ForceNetUpdate();
	const FVector Impact = Victim->GetActorLocation();
	const auto Hit = [&](AActor* Other) {
		if (!CombatTarget::IsAliveHostile(Other, TeamIndex))
			return;
		const float DistanceSquared = FVector::DistSquared2D(Impact, Other->GetActorLocation());
		if (UnitRole == EUnitRole::Siege && DistanceSquared > FMath::Square(CombatPolicy::ArtillerySplashRadius))
			return;
		DamagePolicy::FOutgoing Outgoing;
		Outgoing.Base = Definition->AttackDamage;
		Outgoing.Type = GetDamageType();
		Outgoing.Armor = CombatTarget::ArmorClass(Other);
		if (UnitRole == EUnitRole::Siege)
		{
			Outgoing.SplashDistance = FMath::Sqrt(DistanceSquared);
			// Each victim's class bonus and falloff precede the Workshop tradeoff.
			if (GetDoctrine() == EArmyDoctrine::SiegeOptics)
				Outgoing.WorkshopMultiplier = DamagePolicy::SiegeOpticsOutgoingMultiplier;
		}
		CombatTarget::ReceiveAttack(Other, DamagePolicy::Outgoing(Outgoing), this);
	};
	Hit(Victim);
	if (UnitRole != EUnitRole::Siege)
		return;
	// Actor iteration survives casualties removing members/buildings from registries.
	for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
		if (*It != Victim)
			Hit(*It);
	for (TActorIterator<ACommandBuilding> It(GetWorld()); It; ++It)
		if (*It != Victim)
			Hit(*It);
	if (State)
	{
		if (State->FriendlyHeadquarters != Victim)
			Hit(State->FriendlyHeadquarters);
		if (State->EnemyHeadquarters != Victim)
			Hit(State->EnemyHeadquarters);
	}
}

void AArmyUnit::ReceiveAttack(int32 Damage, AArmyUnit* Attacker)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsAlive() || !IsValid(Attacker) || !Attacker->IsAlive()
		|| (State && State->MatchResult != EMatchResult::Ongoing)
		|| Attacker->TeamIndex == TeamIndex || Damage <= 0)
		return;
	ResetRepairTimer();
	TArray<float, TInlineAllocator<4>> Incoming;
	if (!bReinforcing && UnitRole == EUnitRole::Frontline
		&& GetDoctrine() == EArmyDoctrine::EntrenchedFrontline
		&& IsValid(Group) && Group->Verb == EForceVerb::MoveHold && Group->Status == EForceStatus::Holding
		&& GetVelocity().SizeSquared2D() <= FMath::Square(1.f))
		Incoming.Add(DamagePolicy::EntrenchedIncomingMultiplier);
	if (const float Cover = RegionTraitPolicy::IncomingMultiplier(GetRegionTrait()); Cover != 1.f)
		Incoming.Add(Cover);
	const DamagePolicy::FResult Result = DamagePolicy::Resolve(Damage, Attacker->GetDamageType(), Shield, Incoming);
	if (State && Result.Any())
		State->NotifyRegionDamage(this, TeamIndex, Attacker);
	ApplyDurabilityLoss(Result, Attacker);
}

void AArmyUnit::ReceiveEnvironmentalDamage(int32 Damage)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsAlive() || (State && State->MatchResult != EMatchResult::Ongoing) || Damage <= 0)
		return;
	ResetRepairTimer();
	ApplyDurabilityLoss(DamagePolicy::Environmental(Damage, Shield), nullptr);
}

void AArmyUnit::ApplyDurabilityLoss(const DamagePolicy::FResult& Result, const AArmyUnit* Killer)
{
	if (Result.Any())
		ShieldPolicy::RestartRegen(ShieldClock);
	Shield = FMath::Max(0, Shield - Result.ShieldLoss);
	Health = FMath::Max(0, Health - Result.HealthLoss);
	OnRep_Appearance();
	ForceNetUpdate();
	if (Health == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("Combat death %s role=%d killer=%s"), *GetName(), static_cast<int32>(UnitRole),
			Killer ? *Killer->GetName() : TEXT("environment"));
		if (AAIController* AI = Cast<AAIController>(GetController()))
			AI->StopMovement();
		if (AController* Controller = GetController())
			Controller->Destroy();
		GetCharacterMovement()->StopMovementImmediately();
		GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Target = nullptr;
		if (IsValid(Group))
		{
			Group->OnMemberDied(this);
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
	DOREPLIFETIME(AArmyUnit, UnitIndex);
	DOREPLIFETIME(AArmyUnit, UnitRole);
	DOREPLIFETIME(AArmyUnit, CompositionSlot);
	DOREPLIFETIME(AArmyUnit, bReinforcing);
	DOREPLIFETIME(AArmyUnit, Health);
	DOREPLIFETIME(AArmyUnit, Shield);
	DOREPLIFETIME(AArmyUnit, Target);
	DOREPLIFETIME(AArmyUnit, AttackCount);
}
