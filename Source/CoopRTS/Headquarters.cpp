#include "Headquarters.h"

#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AHeadquarters::AHeadquarters()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
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
	if (Cube.Succeeded()) Body->SetStaticMesh(Cube.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_CommandUnit.M_CommandUnit"));
	if (Material.Succeeded()) Body->SetMaterial(0, Material.Object);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Human(TEXT("/Game/Art/Units/SM_Human_HQ.SM_Human_HQ"));
	if (Human.Succeeded()) HumanMesh = Human.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Machine(TEXT("/Game/Art/Units/SM_Machine_HQ.SM_Machine_HQ"));
	if (Machine.Succeeded()) MachineMesh = Machine.Object;
}

void AHeadquarters::BeginPlay()
{
	Super::BeginPlay();
	OnRep_Appearance();
}

void AHeadquarters::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer) return;
	const FVector Start = GetActorLocation() + FVector(-160.f, 0.f, 250.f);
	const FVector End = Start + FVector(320.f, 0.f, 0.f);
	DrawDebugLine(GetWorld(), Start, End, FColor::Black, false, -1.f, 0, 16.f);
	DrawDebugLine(GetWorld(), Start, FMath::Lerp(Start, End, static_cast<float>(Health) / MaxHealth()),
		TeamIndex == 5 ? FColor::Red : FColor::Green, false, -1.f, 0, 11.f);
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 300.f),
		HealthLabel, nullptr, TeamIndex == 5 ? FColor::Red : FColor::Green, 0.f, true);
}

void AHeadquarters::ReceiveAttack(int32 Damage, AArmyUnit* Attacker)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsAlive() || !IsValid(Attacker) || !Attacker->IsAlive()
		|| Attacker->GetTeamIndex() == TeamIndex || Damage <= 0 || !State || State->MatchResult != EMatchResult::Ongoing) return;
	Health = FMath::Max(0, Health - Damage);
	OnRep_Appearance();
	ForceNetUpdate();
	if (!Health)
	{
		UE_LOG(LogTemp, Display, TEXT("Headquarters destroyed team=%d attacker=%s"), TeamIndex, *Attacker->GetName());
	}
}

void AHeadquarters::OnRep_Appearance()
{
	if (GetNetMode() == NM_DedicatedServer) return;
	HealthLabel = FString::Printf(TEXT("%s HQ %d/%d"),
		TeamIndex == 5 ? TEXT("ENEMY") : TEXT("FRIENDLY"), Health, MaxHealth());
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
	if (!Material) Material = Body->CreateAndSetMaterialInstanceDynamic(TeamSlot);
	if (Material) Material->SetVectorParameterValue(TEXT("TeamColor"), !IsAlive() ? FLinearColor(.08f, .08f, .08f)
		: TeamIndex == 5 ? FLinearColor(1.f, .08f, .08f) : FLinearColor(.04f, .50f, 1.f));
}

void AHeadquarters::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHeadquarters, Health);
	DOREPLIFETIME(AHeadquarters, TeamIndex);
}
