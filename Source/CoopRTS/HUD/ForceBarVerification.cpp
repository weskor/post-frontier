#include "ForceBarVerification.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ForceBar.h"
#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "ArmyTestSetup.h"
#include "CommandGameState.h"
#include "CommandBuilding.h"
#include "CommandPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CommandPlayerController.h"
#include "EngineUtils.h"
#include "Json.h"

namespace ForceBarVerification
{
void Snapshot(const ACommandPlayerController& Controller, const AArmyGroup& Force, const TSharedPtr<FJsonObject>& Entry)
{
	const ACommandHUD* HUD = Cast<ACommandHUD>(Controller.GetHUD());
	FVector2D Position;
	if (!HUD || !HUD->FindForceCardScreenPosition(&Force, EHUDAction::None, Position))
		return;
	using namespace CommandHUDPanels;
	int32 Width, Height;
	Controller.GetViewportSize(Width, Height);
	const FContext Context = MakeContext(&Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	FForceCard Card;
	const int32 ETA = HUD->GetForceETA(Force);
	ReadForceCard(Context, Force, ETA, Card);
	auto Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("joined"), Card.Joined);
	Result->SetNumberField(TEXT("travelling"), Card.Travelling);
	Result->SetNumberField(TEXT("capacity"), Card.Capacity);
	Result->SetNumberField(TEXT("presentationState"), static_cast<int32>(Card.State));
	Result->SetNumberField(TEXT("etaSeconds"), ETA);
	Result->SetNumberField(TEXT("productionProgress"), Card.ProductionProgress);
	Result->SetBoolField(TEXT("owned"), Card.bOwned);
	Result->SetBoolField(TEXT("highlighted"), Card.bHighlighted);
	Result->SetStringField(TEXT("statusText"), Card.Status.ToString());
	Result->SetStringField(TEXT("orderText"), Card.Order.ToString());
	Result->SetStringField(TEXT("productionText"), Card.Production.ToString());
	ForEachForceCard(Context, Layout, [&](AArmyGroup* Candidate, const FRect& Rect) {
		if (Candidate != &Force)
			return;
		Result->SetBoolField(TEXT("clearsPanels"), !Rect.Intersects(Layout.Build) && !Rect.Intersects(Layout.Minimap) && (!Card.bOwned || !Rect.Intersects(Layout.Bottom)));
		Result->SetArrayField(TEXT("rect"), { MakeShared<FJsonValueNumber>(Rect.X * Layout.Scale), MakeShared<FJsonValueNumber>(Rect.Y * Layout.Scale), MakeShared<FJsonValueNumber>(Rect.W * Layout.Scale), MakeShared<FJsonValueNumber>(Rect.H * Layout.Scale) });
	});
	Entry->SetObjectField(TEXT("forceCard"), Result);
}

static bool ControlAction(FStringView Control, EHUDAction& Action)
{
	struct FControl
	{
		FStringView Name;
		EHUDAction Action;
	};
	static const FControl Controls[] = {
		{ TEXT("select"), EHUDAction::None },
		{ TEXT("attack"), EHUDAction::ForceCardAttack },
		{ TEXT("retreat"), EHUDAction::ForceCardRetreat },
		{ TEXT("production"), EHUDAction::ForceCardProduction },
		{ TEXT("never"), EHUDAction::ForceCardNever },
		{ TEXT("25"), EHUDAction::ForceCard25 },
		{ TEXT("40"), EHUDAction::ForceCard40 },
		{ TEXT("60"), EHUDAction::ForceCard60 },
		{ TEXT("ping"), EHUDAction::PingTeammateForce }
	};
	for (const FControl& Item : Controls)
		if (Control == Item.Name)
		{
			Action = Item.Action;
			return true;
		}
	return false;
}

static FString InspectTeammateFixture(UWorld& World, ACommandPlayerController& Controller)
{
	ACommandGameState* State = World.GetGameState<ACommandGameState>();
	ACommandPlayerState* Own = Controller.GetPlayerState<ACommandPlayerState>();
	if (!State || !Own || World.GetNetMode() == NM_Client)
		return TEXT("teammate fixture requires an authoritative game");
	ACommandPlayerState* Teammate = World.SpawnActor<ACommandPlayerState>();
	if (!Teammate)
		return TEXT("teammate fixture player spawn failed");
	Teammate->CommanderIndex = Own->CommanderIndex == 0 ? 1 : 0;
	Teammate->TeamIndex = Own->TeamIndex;
	Teammate->SetPlayerName(TEXT("Fixture teammate"));
	State->AddPlayerState(Teammate);
	const FVector Location = ArmyTestSetup::FromFriendlyHQ(State, 900.f, -900.f, 100.f);
	const FTransform ProducerTransform(Location + FVector(0.f, -450.f, -95.f));
	ACommandBuilding* Producer = World.SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), ProducerTransform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Producer)
		return TEXT("teammate fixture producer spawn failed");
	Producer->BuildingIndex = ArmyTestSetup::BarracksIndex;
	Producer->OwningPlayerState = Teammate;
	Producer->ConstructionProgress = 1.f;
	Producer->bForceConfigured = true;
	Producer->ProductionUnitIndex = ArmyTestSetup::UnitIndex(State, EUnitRole::Frontline);
	Producer->ForceNumber = 1;
	Producer->FinishSpawning(ProducerTransform);
	Producer->SetActorTickEnabled(false);
	const FTransform Transform(Location);
	AArmyGroup* Force = World.SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Force)
		return TEXT("teammate fixture force spawn failed");
	Force->Initialize({ Teammate->TeamIndex, Teammate, 21, Producer, Location });
	Force->ForceNumber = 1;
	Force->FinishSpawning(Transform);
	if (!Force->SpawnUnits())
		return TEXT("teammate fixture member spawn failed");
	Producer->ForceGroup = Force;
	Force->Status = EForceStatus::Holding;
	Force->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Force->GetUnits())
	{
		Unit->SetActorTickEnabled(false);
		Unit->GetCharacterMovement()->DisableMovement();
	}
	Controller.SelectForce(Force);
	return FString();
}

bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error)
{
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (Action != TEXT("forceCard") && Action != TEXT("forceCardTeammate"))
		return false;
	ACommandPlayerController* Controller = nullptr;
	for (TActorIterator<ACommandPlayerController> It(&World); It; ++It)
		if (It->IsLocalController())
		{
			Controller = *It;
			break;
		}
	const ACommandHUD* HUD = Controller ? Cast<ACommandHUD>(Controller->GetHUD()) : nullptr;
	if (!HUD)
	{
		Error = TEXT("local force card HUD unavailable");
		return true;
	}
	if (Action == TEXT("forceCardTeammate"))
	{
		Error = InspectTeammateFixture(World, *Controller);
		return true;
	}
	const int32 Owner = Request->GetIntegerField(TEXT("owner"));
	const int32 Number = Request->GetIntegerField(TEXT("number"));
	AArmyGroup* Force = nullptr;
	for (TActorIterator<AArmyGroup> It(&World); It; ++It)
		if (IsValid(It->GetOwningPlayerState()) && It->GetOwningPlayerState()->CommanderIndex == Owner && It->ForceNumber == Number)
		{
			Force = *It;
			break;
		}
	const FString Control = Request->GetStringField(TEXT("control"));
	EHUDAction HUDAction = EHUDAction::None;
	if (!ControlAction(Control, HUDAction))
	{
		Error = TEXT("unknown force card control");
		return true;
	}
	FVector2D Position;
	EHUDAction Hit;
	if (!Force || !HUD->FindForceCardScreenPosition(Force, HUDAction, Position)
		|| HUD->GetForceCardAtScreenPosition(Position, Hit) != Force || Hit != HUDAction)
		Error = TEXT("force card control not visible or hit geometry disagrees");
	else if (!Controller->HandleHUDClick(Position))
		Error = TEXT("force card click rejected");
	return true;
}
}
#endif
