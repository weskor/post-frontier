#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "VerbInputFixture.h"

namespace VerbInputTests
{
bool FScenario::Initialize(UWorld* World)
{
	PC = ArmyTestSetup::Controller(World);
	State = World->GetGameState<ACommandGameState>();
	if (!PC || !MapReady(State) || !State->Content
		|| !PC->GetPlayerState<ACommandPlayerState>() || PC->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0)
		return false;
	// Automation startup can resize the offscreen window after -ResX/-ResY.
	// Establish each supported HUD surface before projecting world clicks.
	int32 Width = 0, Height = 0;
	PC->GetViewportSize(Width, Height);
	if (Width != TargetSurface.X || Height != TargetSurface.Y)
	{
		if (!bRequestedViewport)
		{
			Test->AddInfo(FString::Printf(TEXT("Setting input fixture viewport from %dx%d to %dx%d"), Width, Height, TargetSurface.X, TargetSurface.Y));
			PC->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"), TargetSurface.X, TargetSurface.Y));
			bRequestedViewport = true;
		}
		return false;
	}
	HUD = PC->GetHUD<ACommandHUD>();
	Camera = Cast<ACommandCamera>(PC->GetPawn());
	FVector2D Origin;
	float Size = 0.f;
	if (!HUD || !Camera || !HUD->GetMinimapScreenRect(Origin, Size))
		return false;
	if (!Setup(World))
		return true;
	bRestoreCursor = PC->GetMousePosition(OriginalMouseX, OriginalMouseY);
	CenterCursor();
	SelectBoth();
	// The two selected forces are two cards: the deck sits beside them at 1600x900 and starts collapsed at 1280x720.
	if (!Check(!HUD->IsPanelPoint(FVector2D(Width, Height) * .5f), TEXT("The middle of the screen is world in the default state")))
		return true;
	Camera->FocusOn(State->GetRegionAnchor(Target));
	++Stage;
	return false;
}

ACommandBuilding* FScenario::Building(UWorld* World, ACommandPlayerState* Owner, const FVector& Location)
{
	const FTransform Transform(Location);
	ACommandBuilding* Result = World->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
		Owner == PC->GetPlayerState<ACommandPlayerState>() ? PC : nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Result)
		return nullptr;
	Result->BuildingIndex = BarracksIndex;
	Result->OwningPlayerState = Owner;
	Result->TeamIndex = Owner->TeamIndex;
	Result->ConstructionProgress = 1.f;
	Result->FinishSpawning(Transform);
	Result->bProductionEnabled = false;
	Result->SetActorTickEnabled(false);
	return Result;
}

bool FScenario::Setup(UWorld* World)
{
	return IsolateWorld(World) && FindRegions() && SpawnFixtures(World) && FindHostileGround() && SpawnForces(World);
}

bool FScenario::IsolateWorld(UWorld* World)
{
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		It->Destroy();
	State->bVerificationIncomePaused = true;
	PC->GetPlayerState<ACommandPlayerState>()->Resources = 0;
	if (!Check(IsValid(State->EnemyCommander), TEXT("Planner isolation preserves enemy ownership")))
		return false;
	State->EnemyCommander->Resources = 0;
	return true;
}

bool FScenario::FindRegions()
{
	for (const AMapRegion* Region : State->Regions)
	{
		if (IsValid(Region) && Region->HomeTeam == State->EnemyCommander->TeamIndex)
			EnemyHome = Region->RegionIndex;
		if (IsValid(Region) && Region->HomeTeam < 0 && Target == INDEX_NONE)
			Target = Region->RegionIndex;
	}
	return Check(Target != INDEX_NONE && EnemyHome != INDEX_NONE, TEXT("Generated map supplies neutral and hostile region targets"));
}

bool FScenario::SpawnFixtures(UWorld* World)
{
	ACommandPlayerState* Wallet = PC->GetPlayerState<ACommandPlayerState>();
	Producer = Building(World, Wallet, FromFriendlyHQ(State, 700.f, -600.f, 5.f));
	Hostile = Building(World, State->EnemyCommander.Get(), FromEnemyHQ(State, -700.f, 600.f, 5.f));
	return Check(IsValid(Producer) && IsValid(Hostile), TEXT("Completed friendly producer and hostile structure fixtures spawn"));
}

bool FScenario::FindHostileGround()
{
	bool bFoundHostileGround = false;
	for (int32 Direction = 0; Direction < 16; ++Direction)
	{
		const float Angle = Direction * PI / 8.f;
		const FVector Candidate = State->GetRegionAnchor(EnemyHome)
			+ FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 650.f;
		const FVector2D HQOffset = Minimap(Candidate) - Minimap(State->EnemyHeadquarters->GetActorLocation());
		const FVector2D BuildingOffset = Minimap(Candidate) - Minimap(Hostile->GetActorLocation());
		if (RegionAt(State, Candidate) == EnemyHome
			&& (FMath::Abs(HQOffset.X) > 7.f || FMath::Abs(HQOffset.Y) > 7.f)
			&& (FMath::Abs(BuildingOffset.X) > 4.f || FMath::Abs(BuildingOffset.Y) > 4.f))
		{
			HostileRegionPoint = Candidate;
			bFoundHostileGround = true;
			break;
		}
	}
	return Check(bFoundHostileGround, TEXT("Hostile polygon supplies bare region ground away from structure markers"));
}

bool FScenario::SpawnForces(UWorld* World)
{
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Forces[Index] = SpawnGroup(World, PC, Index, FromFriendlyHQ(State, 1000.f + Index * 350.f, -700.f, 100.f));
		if (!Check(IsValid(Forces[Index]), TEXT("Explicit selected force spawns with real members")))
			return false;
		Forces[Index]->ForceNumber = Index + 1;
		Forces[Index]->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Forces[Index]->GetUnits())
		{
			Unit->SetActorTickEnabled(false);
			Unit->GetCharacterMovement()->DisableMovement();
		}
	}
	return true;
}
}
#endif
