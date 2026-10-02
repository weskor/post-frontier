#include "Headquarters.h"

#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CoopAudioSubsystem.h"
#include "ObjectiveAnnouncer.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AHeadquarters::AHeadquarters()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	// Same 300x300x200 cm volume and collision settings the scaled cube root used to provide.
	HitBox = CreateDefaultSubobject<UBoxComponent>(TEXT("HQ Hit Box"));
	SetRootComponent(HitBox);
	HitBox->InitBoxExtent(FVector(150.f, 150.f, 100.f));
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitBox->SetCanEverAffectNavigation(false);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HQ Body"));
	Body->SetupAttachment(HitBox);
	Body->SetRelativeScale3D(FVector(3.f, 3.f, 2.f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
		Body->SetStaticMesh(Cube.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_CommandUnit.M_CommandUnit"));
	if (Material.Succeeded())
		Body->SetMaterial(0, Material.Object);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Human(TEXT("/Game/Art/Units/SM_Human_HQ.SM_Human_HQ"));
	if (Human.Succeeded())
		HumanMesh = Human.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Machine(TEXT("/Game/Art/Units/SM_Machine_HQ.SM_Machine_HQ"));
	if (Machine.Succeeded())
		MachineMesh = Machine.Object;
}

void AHeadquarters::BeginPlay()
{
	Super::BeginPlay();
	LastAudioHealth = Health;
	bDestroyedAudioPlayed = Health <= 0;
	bAudioStateInitialized = true;
	OnRep_Appearance();
}

void AHeadquarters::ReceiveAttack(int32 Damage, AArmyUnit* Attacker)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsAlive() || !IsValid(Attacker) || !Attacker->IsAlive()
		|| Attacker->GetTeamIndex() == TeamIndex || Damage <= 0 || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	const int32 PreviousHealth = Health;
	Health = FMath::Max(0, Health - Damage);
	OnRep_Appearance();
	ForceNetUpdate();
	if (UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this))
	{
		static const FName OwnIds[] = { TEXT("own_hq_under_attack"), TEXT("own_hq_half"), TEXT("own_hq_critical"), TEXT("own_hq_offline") };
		static const FName EnemyIds[] = { TEXT("enemy_hq_under_attack"), TEXT("enemy_hq_half"), TEXT("enemy_hq_critical"), TEXT("enemy_hq_offline") };
		const FName* Ids = TeamIndex == 0 ? OwnIds : EnemyIds;
		const int32 Tier = Health * 4 <= MaxHealth() ? 2 : Health * 2 <= MaxHealth() ? 1 : 0;
		Announcer->RaiseFromUnit(Ids[0], this, TeamIndex, GetActorLocation(), Attacker, Tier);
		if (PreviousHealth * 2 > MaxHealth() && Health * 2 <= MaxHealth())
			Announcer->RaiseFromUnit(Ids[1], this, TeamIndex, GetActorLocation(), Attacker, 1);
		if (PreviousHealth * 4 > MaxHealth() && Health * 4 <= MaxHealth())
			Announcer->RaiseFromUnit(Ids[2], this, TeamIndex, GetActorLocation(), Attacker, 2);
		if (Health == 0)
			Announcer->RaiseFromUnit(Ids[3], this, TeamIndex, GetActorLocation(), Attacker, 2);
	}
	if (!Health)
	{
		UE_LOG(LogTemp, Display, TEXT("Headquarters destroyed team=%d attacker=%s"), TeamIndex, *Attacker->GetName());
	}
}

void AHeadquarters::OnRep_Appearance()
{
	const bool bTookDamage = bAudioStateInitialized && Health < LastAudioHealth;
	const bool bDestroyed = bTookDamage && LastAudioHealth > 0 && Health <= 0 && !bDestroyedAudioPlayed;
	LastAudioHealth = Health;
	if (bDestroyed)
		bDestroyedAudioPlayed = true;
	if (bTookDamage)
	{
		if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		{
			Audio->PlayUnit(ECoopAudioEvent::Impact, TeamIndex, EUnitRole::Siege, GetActorLocation(), this);
			if (bDestroyed)
				Audio->PlayStructure(ECoopAudioEvent::HQDestroyed, TeamIndex, GetActorLocation(), this);
		}
	}

	if (GetNetMode() == NM_DedicatedServer)
		return;
	UStaticMesh* Themed = (TeamIndex == 5 ? MachineMesh : HumanMesh).Get();
	if (Themed && Body->GetStaticMesh() != Themed)
	{
		Body->SetStaticMesh(Themed);
		// SetStaticMesh keeps the cube's override (and any dynamic instance); drop them so the Team slot starts from the mesh default.
		Body->EmptyOverrideMaterials();
		Body->SetRelativeScale3D(FVector::OneVector);
	}
	const int32 TeamSlot = Themed ? FMath::Max(0, Body->GetMaterialIndex(TEXT("Team"))) : 0;
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(TeamSlot));
	if (!Material)
		Material = Body->CreateAndSetMaterialInstanceDynamic(TeamSlot);
	if (Material)
		Material->SetVectorParameterValue(TEXT("TeamColor"), !IsAlive() ? FLinearColor(.08f, .08f, .08f) : TeamIndex == 5 ? FLinearColor(1.f, .08f, .08f)
																														  : FLinearColor(.04f, .50f, 1.f));
}

void AHeadquarters::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHeadquarters, Health);
	DOREPLIFETIME(AHeadquarters, TeamIndex);
}
