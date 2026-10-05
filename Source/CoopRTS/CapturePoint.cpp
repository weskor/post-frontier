#include "CapturePoint.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CoopAudioSubsystem.h"
#include "ObjectiveAnnouncer.h"
#include "MapRegion.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
const AArmyUnit* FindCaptureContributor(const AArmyGroup& Group, const ACapturePoint& Point, bool& bFriendly, bool& bEnemy)
{
	const AArmyUnit* PresentUnit = nullptr;
	for (const AArmyUnit* Unit : Group.GetUnits())
	{
		if (!IsValid(Unit) || !Unit->IsAlive()
			|| FVector::DistSquared2D(Unit->GetActorLocation(), Point.GetActorLocation()) > FMath::Square(Point.CaptureRadius))
			continue;
		if (Group.GetTeamIndex() == 0)
			bFriendly = true;
		else if (Group.GetTeamIndex() == 5)
			bEnemy = true;
		PresentUnit = Unit;
	}
	return PresentUnit;
}
}

ACapturePoint::ACapturePoint()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	SetRootComponent(Marker);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetCanEverAffectNavigation(false);
	Marker->SetWorldScale3D(FVector(2.5f, 2.5f, .04f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
		Marker->SetStaticMesh(Cylinder.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_CommandUnit.M_CommandUnit"));
	if (Material.Succeeded())
		Marker->SetMaterial(0, Material.Object);
}

void ACapturePoint::BeginPlay()
{
	Super::BeginPlay();
	LastAudioCaptureProgress = CaptureProgress;
	LastAudioControllingTeam = ControllingTeam;
	bCaptureAudioInitialized = true;
	OnRep_Capture();
}

void ACapturePoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority())
	{
		CaptureElapsed += DeltaSeconds;
		if (CaptureElapsed >= .2f)
		{
			const float Elapsed = CaptureElapsed;
			CaptureElapsed = 0.f;
			AdvanceCapture(Elapsed);
		}
	}
}

bool ACapturePoint::IsFortifyFrozen() const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return false;
	for (const AMapRegion* Region : State->Regions)
		if (IsValid(Region) && Region->Anchor == this)
			return Region->IsCaptureFrozen();
	return false;
}

void ACapturePoint::AdvanceCapture(float Seconds)
{
	if (!HasAuthority() || Seconds < 0.f)
		return;
	const ACommandGameState* MatchState = GetWorld()->GetGameState<ACommandGameState>();
	if (MatchState && MatchState->MatchResult != EMatchResult::Ongoing)
		return;
	bool bFriendly = false;
	bool bEnemy = false;
	TArray<const AArmyUnit*, TInlineAllocator<8>> ContributingUnits;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		const AArmyUnit* PresentUnit = FindCaptureContributor(**It, *this, bFriendly, bEnemy);
		if (PresentUnit)
			ContributingUnits.Add(PresentUnit);
	}
	const bool bOccupancyChanged = bFriendlyPresent != bFriendly || bEnemyPresent != bEnemy;
	if (bOccupancyChanged)
	{
		bFriendlyPresent = bFriendly;
		bEnemyPresent = bEnemy;
		ForceNetUpdate();
	}
	// Occupancy stays published above; a Fortified region keeps its progress for both sides.
	if (bFriendly == bEnemy || (ControllingTeam == 0 && bFriendly && !bEnemy)
		|| (ControllingTeam == 5 && bEnemy && !bFriendly) || IsFortifyFrozen())
		return;
	const float Previous = CaptureProgress;
	const int32 PreviousOwner = ControllingTeam;
	CaptureProgress = FMath::Clamp(CaptureProgress + (bFriendly ? 1.f : -1.f) * .125f * Seconds, -1.f, 1.f);
	if ((Previous > 0.f && CaptureProgress <= 0.f) || (Previous < 0.f && CaptureProgress >= 0.f))
		ControllingTeam = -1;
	if (CaptureProgress >= 1.f)
		ControllingTeam = 0;
	else if (CaptureProgress <= -1.f)
		ControllingTeam = 5;
	if (Previous != CaptureProgress || PreviousOwner != ControllingTeam)
	{
		OnRep_Capture();
		ForceNetUpdate();
	}
	if (PreviousOwner != ControllingTeam)
	{
		if (UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(this))
		{
			TArray<FObjectiveForce> Contributors;
			Contributors.Reserve(ContributingUnits.Num());
			for (const AArmyUnit* Unit : ContributingUnits)
				if (Unit->GetTeamIndex() == (bFriendly ? 0 : 5))
					Contributors.Add(UObjectiveAnnouncer::DescribeForce(Unit));
			if (PreviousOwner == 0)
				Announcer->Raise(TEXT("region_lost"), 0, GetActorLocation(), Contributors);
			if (ControllingTeam == 0)
				Announcer->Raise(TEXT("region_captured"), 0, GetActorLocation(), Contributors);
		}
		UE_LOG(LogTemp, Display, TEXT("Capture site=%d kind=%d owner=%d progress=%.2f"), SiteIndex,
			static_cast<int32>(SiteKind), ControllingTeam, CaptureProgress);
	}
}

void ACapturePoint::OnRep_Capture()
{
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Marker->GetMaterial(0));
	if (!Material)
		Material = Marker->CreateAndSetMaterialInstanceDynamic(0);
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("TeamColor"), ControllingTeam == 0 ? FLinearColor::Green : ControllingTeam == 5 ? FLinearColor::Red
																															   : FLinearColor(1.f, .7f, .05f));
	}
	if (!bCaptureAudioInitialized)
		return;
	const float PreviousProgress = LastAudioCaptureProgress;
	const int32 PreviousOwner = LastAudioControllingTeam;
	LastAudioCaptureProgress = CaptureProgress;
	LastAudioControllingTeam = ControllingTeam;
	if (UCoopAudioSubsystem* Audio = UCoopAudioSubsystem::Get(this))
	{
		if (PreviousOwner != 0 && ControllingTeam == 0)
			Audio->PlayCapture(ECoopAudioEvent::SectorCaptured, GetActorLocation());
		else if (PreviousOwner == 0 && ControllingTeam != 0)
			Audio->PlayCapture(ECoopAudioEvent::SectorLost, GetActorLocation());
		for (int32 Milestone = 2; Milestone >= 0; --Milestone)
		{
			const float Threshold = .25f * (Milestone + 1);
			if (PreviousProgress < Threshold && CaptureProgress >= Threshold)
			{
				Audio->PlayCapture(ECoopAudioEvent::CaptureTick, GetActorLocation(), Milestone);
				break;
			}
		}
	}
}

void ACapturePoint::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACapturePoint, ControllingTeam);
	DOREPLIFETIME(ACapturePoint, CaptureProgress);
	DOREPLIFETIME(ACapturePoint, bFriendlyPresent);
	DOREPLIFETIME(ACapturePoint, bEnemyPresent);
	DOREPLIFETIME(ACapturePoint, SiteKind);
	DOREPLIFETIME(ACapturePoint, SiteIndex);
}
