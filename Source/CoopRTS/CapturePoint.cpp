#include "CapturePoint.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

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
	if (Cylinder.Succeeded()) Marker->SetStaticMesh(Cylinder.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/M_CommandUnit.M_CommandUnit"));
	if (Material.Succeeded()) Marker->SetMaterial(0, Material.Object);
}

void ACapturePoint::BeginPlay()
{
	Super::BeginPlay();
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
	if (GetNetMode() != NM_DedicatedServer)
	{
		const FColor Color = ControllingTeam == 0 ? FColor::Green : ControllingTeam == 5 ? FColor::Red : FColor::Yellow;
		DrawDebugCircle(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 12.f), TerritoryRadius, 72,
			Color, false, -1.f, 0, 1.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		DrawDebugCircle(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 9.f), CaptureRadius, 48,
			Color, false, -1.f, 0, 2.f, FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), false);
		DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 110.f),
			FString::Printf(TEXT("RESOURCE %d  %s%s  %.0f%%"), SiteIndex + 1,
				ControllingTeam == 0 ? TEXT("FRIENDLY") : ControllingTeam == 5 ? TEXT("ENEMY") : TEXT("NEUTRAL"),
				IsEstablishedForTeam(ControllingTeam) ? TEXT(" OUTPOST ACTIVE") : TEXT(" UNESTABLISHED"),
				FMath::Abs(CaptureProgress) * 100.f), nullptr, Color, 0.f, true);
	}
}

bool ACapturePoint::IsEstablishedForTeam(int32 Team) const
{
	if (Team != 0 && Team != 5) return false;
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!State || ControllingTeam != Team) return false;
	for (const ACommandBuilding* Building : State->Buildings)
		if (IsValid(Building) && Building->Kind == EBuildingKind::Outpost && Building->OutpostSite == this
			&& Building->TeamIndex == Team && Building->IsAlive() && Building->IsComplete()) return true;
	return false;
}

void ACapturePoint::AdvanceCapture(float Seconds)
{
	if (!HasAuthority() || Seconds < 0.f) return;
	const ACommandGameState* MatchState = GetWorld()->GetGameState<ACommandGameState>();
	if (MatchState && MatchState->MatchResult != EMatchResult::Ongoing) return;
	bool bFriendly = false;
	bool bEnemy = false;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		for (const AArmyUnit* Unit : It->Units)
		{
			if (!IsValid(Unit) || !Unit->IsAlive()
				|| FVector::DistSquared2D(Unit->GetActorLocation(), GetActorLocation()) > FMath::Square(CaptureRadius)) continue;
			if (It->TeamIndex == 0) bFriendly = true;
			else if (It->TeamIndex == 5) bEnemy = true;
		}
	}
	const bool bOccupancyChanged = bFriendlyPresent != bFriendly || bEnemyPresent != bEnemy;
	if (bOccupancyChanged)
	{
		bFriendlyPresent = bFriendly;
		bEnemyPresent = bEnemy;
		ForceNetUpdate();
	}
	if (bFriendly == bEnemy || (ControllingTeam == 0 && bFriendly && !bEnemy)
		|| (ControllingTeam == 5 && bEnemy && !bFriendly)
		|| IsEstablishedForTeam(ControllingTeam))
		return;
	const float Previous = CaptureProgress;
	const int32 PreviousOwner = ControllingTeam;
	CaptureProgress = FMath::Clamp(CaptureProgress + (bFriendly ? 1.f : -1.f) * .125f * Seconds, -1.f, 1.f);
	if ((Previous > 0.f && CaptureProgress <= 0.f) || (Previous < 0.f && CaptureProgress >= 0.f)) ControllingTeam = -1;
	if (CaptureProgress >= 1.f) ControllingTeam = 0;
	else if (CaptureProgress <= -1.f) ControllingTeam = 5;
	if (Previous != CaptureProgress) ForceNetUpdate();
	if (bOccupancyChanged || PreviousOwner != ControllingTeam)
		if (ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>()) State->RefreshTerritory();
	if (PreviousOwner != ControllingTeam)
	{
		OnRep_Capture();
		UE_LOG(LogTemp, Display, TEXT("Capture site=%d kind=%d owner=%d progress=%.2f"), SiteIndex,
			static_cast<int32>(SiteKind), ControllingTeam, CaptureProgress);
	}
}

void ACapturePoint::OnRep_Capture()
{
	UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Marker->GetMaterial(0));
	if (!Material) Material = Marker->CreateAndSetMaterialInstanceDynamic(0);
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("TeamColor"), ControllingTeam == 0 ? FLinearColor::Green
			: ControllingTeam == 5 ? FLinearColor::Red : FLinearColor(1.f, .7f, .05f));
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
