#include "FailoverNode.h"

#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CoopAudioSubsystem.h"
#include "EngineUtils.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "ObjectiveAnnouncer.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Rules/DamagePolicy.h"
#include "Rules/HqHoldPolicy.h"
#include "UObject/ConstructorHelpers.h"

AFailoverNode::AFailoverNode()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	HitBox = CreateDefaultSubobject<UBoxComponent>(TEXT("Node Hit Box"));
	SetRootComponent(HitBox);
	HitBox->InitBoxExtent(FVector(HitBoxHalfSize));
	HitBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	HitBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	HitBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitBox->SetCanEverAffectNavigation(false);
	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Node Body"));
	Body->SetupAttachment(HitBox);
	Body->SetRelativeScale3D(FVector(2.5f, 2.5f, 3.f));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
		Body->SetStaticMesh(Cube.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_CommandUnit.M_CommandUnit"));
	if (Material.Succeeded())
		Body->SetMaterial(0, Material.Object);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Human(TEXT("/Game/Art/Buildings/SM_Human_FailoverNode.SM_Human_FailoverNode"));
	if (Human.Succeeded())
		HumanMesh = Human.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Machine(TEXT("/Game/Art/Buildings/SM_Machine_FailoverNode.SM_Machine_FailoverNode"));
	if (Machine.Succeeded())
		MachineMesh = Machine.Object;
}

int32 AFailoverNode::MaxHealth() const
{
	return HqHoldPolicy::NodeHealth;
}

void AFailoverNode::BeginPlay()
{
	Super::BeginPlay();
	LastAudioHealth = Health;
	bAudioStateInitialized = true;
	if (AHeadquarters* Guarded = GetHome())
		Guarded->RegisterNode(this);
	OnRep_Appearance();
}

void AFailoverNode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (AHeadquarters* Guarded = Home.Get())
		Guarded->UnregisterNode(this);
	Super::EndPlay(Reason);
}

AHeadquarters* AFailoverNode::GetHome() const
{
	if (!Home.IsValid() && GetWorld())
		for (TActorIterator<AHeadquarters> It(GetWorld()); It; ++It)
			if (It->TeamIndex == TeamIndex)
			{
				Home = *It;
				break;
			}
	return Home.Get();
}

float AFailoverNode::GetBattleSeconds() const
{
	if (!Clock.IsValid())
		if (TActorIterator<AEnemyCommander> It(GetWorld()); It)
			Clock = *It;
	return Clock.IsValid() ? Clock->GetMatchSeconds() : 0.f;
}

bool AFailoverNode::IsPlated() const
{
	return HqHoldPolicy::NodePlated(GetBattleSeconds());
}

void AFailoverNode::ReceiveAttack(int32 Damage, AArmyUnit* Attacker)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsAlive() || !IsValid(Attacker) || !Attacker->IsAlive()
		|| Attacker->GetTeamIndex() == TeamIndex || Damage <= 0 || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	State->NotifyRegionDamage(this, TeamIndex, Attacker);
	TArray<float, TInlineAllocator<2>> Incoming;
	if (const float Fortify = AMapRegion::FortifyIncomingAt(*State, GetActorLocation(), TeamIndex); Fortify != 1.f)
		Incoming.Add(Fortify);
	if (IsPlated())
		Incoming.Add(HqHoldPolicy::PlatingIncomingMultiplier);
	const DamagePolicy::FResult Result = DamagePolicy::Resolve(Damage, Attacker->GetDamageType(), 0, Incoming);
	if (!Result.Any())
		return;
	Health = FMath::Max(0, Health - Result.HealthLoss);
	OnRep_Appearance();
	ForceNetUpdate();
	if (!Health)
		AnnounceLoss(Attacker);
}

void AFailoverNode::AnnounceLoss(AArmyUnit* Attacker) const
{
	UE_LOG(LogTemp, Display, TEXT("Failover Node destroyed team=%d attacker=%s"), TeamIndex, *Attacker->GetName());
	if (UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this))
	{
		const AHeadquarters* Guarded = GetHome();
		const int32 Left = Guarded ? Guarded->NodesStanding() : 0;
		Announcer->RaiseFromUnit(TeamIndex == 0 ? TEXT("own_node_lost") : TEXT("enemy_node_lost"), this, TeamIndex,
			GetActorLocation(), Attacker, Left);
	}
}

void AFailoverNode::OnRep_Appearance()
{
	const bool bTookDamage = bAudioStateInitialized && Health < LastAudioHealth;
	LastAudioHealth = Health;
	if (bTookDamage)
		if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
		{
			Audio->PlayUnit(ECoopAudioEvent::Impact, TeamIndex, EUnitRole::Siege, GetActorLocation(), this);
			if (!IsAlive())
				Audio->PlayStructure(ECoopAudioEvent::HQDestroyed, TeamIndex, GetActorLocation(), this);
		}
	if (!IsAlive())
		HitBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (GetNetMode() == NM_DedicatedServer)
		return;
	UStaticMesh* Themed = (TeamIndex == 5 ? MachineMesh : HumanMesh).Get();
	if (Themed && Body->GetStaticMesh() != Themed)
	{
		Body->SetStaticMesh(Themed);
		// SetStaticMesh keeps the cube's override (and any dynamic instance); drop them so the Team slot starts from the mesh default.
		Body->EmptyOverrideMaterials();
		Body->SetRelativeScale3D(FVector::OneVector);
		Body->SetRelativeLocation(FVector(0.f, 0.f, -HitBoxHalfSize));
	}
	const int32 TeamSlot = Themed ? FMath::Max(0, Body->GetMaterialIndex(TEXT("Team"))) : 0;
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(TeamSlot));
	if (!Material)
		Material = Body->CreateAndSetMaterialInstanceDynamic(TeamSlot);
	if (Material)
		Material->SetVectorParameterValue(TEXT("TeamColor"), !IsAlive() ? FLinearColor(.08f, .08f, .08f) : TeamIndex == 5 ? FLinearColor(1.f, .08f, .08f)
																														  : FLinearColor(.04f, .50f, 1.f));
}

void AFailoverNode::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFailoverNode, Health);
	DOREPLIFETIME(AFailoverNode, TeamIndex);
}
