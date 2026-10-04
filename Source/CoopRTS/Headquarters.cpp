#include "Headquarters.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "EngineUtils.h"
#include "FailoverNode.h"
#include "MapRegion.h"
#include "CoopAudioSubsystem.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/DamagePolicy.h"
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
	PrimaryActorTick.bCanEverTick = true;
	// Ticks only while offline: the hold is the one thing that runs per frame.
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetNetUpdateFrequency(10.f);
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

void AHeadquarters::RegisterNode(AFailoverNode* Node)
{
	Nodes.AddUnique(Node);
}

void AHeadquarters::UnregisterNode(AFailoverNode* Node)
{
	Nodes.RemoveSingleSwap(Node);
}

int32 AHeadquarters::NodesStanding() const
{
	int32 Standing = 0;
	for (const TWeakObjectPtr<AFailoverNode>& Node : Nodes)
		Standing += Node.IsValid() && Node->IsAlive() ? 1 : 0;
	return Standing;
}

void AHeadquarters::ReceiveAttack(int32 Damage, AArmyUnit* Attacker)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsOnline() || Health <= 0 || IsImmune() || !IsValid(Attacker) || !Attacker->IsAlive()
		|| Attacker->GetTeamIndex() == TeamIndex || Damage <= 0 || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	const int32 PreviousHealth = Health;
	State->NotifyRegionDamage(this, TeamIndex, Attacker);
	TArray<float, TInlineAllocator<2>> Incoming;
	if (const float Fortify = AMapRegion::FortifyIncomingAt(*State, GetActorLocation(), TeamIndex); Fortify != 1.f)
		Incoming.Add(Fortify);
	const DamagePolicy::FResult Result = DamagePolicy::Resolve(Damage, Attacker->GetDamageType(), 0, Incoming);
	Health = FMath::Max(0, Health - Result.HealthLoss);
	OnRep_Appearance();
	ForceNetUpdate();
	if (UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this))
	{
		static const FName OwnIds[] = { TEXT("own_hq_under_attack"), TEXT("own_hq_half"), TEXT("own_hq_critical"), TEXT("own_hq_offline") };
		static const FName EnemyIds[] = { TEXT("enemy_hq_under_attack"), TEXT("enemy_hq_half"), TEXT("enemy_hq_critical"), TEXT("enemy_hq_offline") };
		const FName* Ids = TeamIndex == 0 ? OwnIds : EnemyIds;
		const int32 Tier = Health * 4 <= MaxHealth() ? 2 : Health * 2 <= MaxHealth() ? 1
																					 : 0;
		const int32 Announcement = Health == 0                              ? 3
			: PreviousHealth * 4 > MaxHealth() && Health * 4 <= MaxHealth() ? 2
			: PreviousHealth * 2 > MaxHealth() && Health * 2 <= MaxHealth() ? 1
																			: 0;
		Announcer->RaiseFromUnit(Ids[Announcement], this, TeamIndex, GetActorLocation(), Attacker, Tier);
	}
	if (!Health)
	{
		UE_LOG(LogTemp, Display, TEXT("Headquarters offline team=%d attacker=%s"), TeamIndex, *Attacker->GetName());
		GoOffline();
	}
}

void AHeadquarters::Announce(const TCHAR* OwnId, const TCHAR* EnemyId, int32 Tier) const
{
	if (UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this))
		Announcer->Raise(TeamIndex == 0 ? FName(OwnId) : FName(EnemyId), TeamIndex, GetActorLocation(), {}, Tier);
}

void AHeadquarters::GoOffline()
{
	Phase = static_cast<uint8>(HqHoldPolicy::EPhase::Offline);
	HoldState = static_cast<uint8>(HqHoldPolicy::EHoldState::Decaying);
	HoldProgress = 0.f;
	bHoldStarted = false;
	SetActorTickEnabled(true);
	OnRep_Appearance();
	ForceNetUpdate();
	if (HqHoldPolicy::ClaimEmergency(bEmergencyGranted))
		DeployEmergencyForces();
}

void AHeadquarters::ComeBackOnline()
{
	Phase = static_cast<uint8>(HqHoldPolicy::EPhase::Online);
	HoldState = static_cast<uint8>(HqHoldPolicy::EHoldState::None);
	HoldProgress = 0.f;
	bHoldStarted = false;
	Health = HqHoldPolicy::RestoredHealth(MaxHealth());
	bDestroyedAudioPlayed = false;
	SetActorTickEnabled(false);
	LastAudioHealth = Health;
	OnRep_Appearance();
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("Headquarters back online team=%d health=%d"), TeamIndex, Health);
	Announce(TEXT("own_hq_online"), TEXT("enemy_hq_online"), FMath::RoundToInt(HqHoldPolicy::RestoreFraction * 100.f));
}

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
void AHeadquarters::ResetForTest(int32 InHealth)
{
	Phase = static_cast<uint8>(HqHoldPolicy::EPhase::Online);
	HoldState = static_cast<uint8>(HqHoldPolicy::EHoldState::None);
	HoldProgress = 0.f;
	bHoldStarted = false;
	bEmergencyGranted = false;
	Health = InHealth;
	LastAudioHealth = InHealth;
	bDestroyedAudioPlayed = false;
	SetActorTickEnabled(false);
	OnRep_Appearance();
	ForceNetUpdate();
}
#endif

HqHoldPolicy::FPresence AHeadquarters::CountPresence(const ACommandGameState& State) const
{
	const AMapRegion* Main = nullptr;
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionRole == ERegionRole::Main && Region->HomeTeam == TeamIndex)
			Main = Region;
	if (!Main)
		Main = State.FindRegionAt(GetActorLocation());
	HqHoldPolicy::FPresence Presence;
	if (!Main)
		return Presence;
	for (TActorIterator<AArmyUnit> It(GetWorld()); It; ++It)
	{
		const AArmyGroup* Group = It->GetGroup();
		if (!It->IsAlive() || It->IsReinforcing() || !IsValid(Group) || !Group->GetUnits().Contains(*It)
			|| !Main->Contains(It->GetActorLocation()))
			continue;
		(It->GetTeamIndex() == TeamIndex ? Presence.Defenders : Presence.Attackers) += 1;
	}
	return Presence;
}

void AHeadquarters::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickHold(DeltaSeconds);
}

void AHeadquarters::TickHold(float DeltaSeconds)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !IsOffline() || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	const HqHoldPolicy::FStep Step = HqHoldPolicy::Advance(GetPhase(), GetHold(), CountPresence(*State), DeltaSeconds);
	HoldProgress = Step.Hold.Progress;
	bHoldStarted = Step.Hold.bStarted;
	HoldState = static_cast<uint8>(Step.State);
	if (Step.bRevived)
		ComeBackOnline();
	else if (Step.bCompleted)
	{
		Phase = static_cast<uint8>(Step.Phase);
		SetActorTickEnabled(false);
		OnRep_Appearance();
		ForceNetUpdate();
		UE_LOG(LogTemp, Display, TEXT("Headquarters lost team=%d after a completed hold"), TeamIndex);
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
		Material->SetVectorParameterValue(TEXT("TeamColor"), !IsOnline() ? FLinearColor(.08f, .08f, .08f) : TeamIndex == 5 ? FLinearColor(1.f, .08f, .08f)
																														   : FLinearColor(.04f, .50f, 1.f));
}

void AHeadquarters::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AHeadquarters, Health);
	DOREPLIFETIME(AHeadquarters, TeamIndex);
	DOREPLIFETIME(AHeadquarters, Phase);
	DOREPLIFETIME(AHeadquarters, HoldState);
	DOREPLIFETIME(AHeadquarters, HoldProgress);
}
