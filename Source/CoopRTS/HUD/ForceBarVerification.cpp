#include "ForceBarVerification.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ForceBar.h"
#include "HUDPanels.h"
#include "ArmyGroup.h"
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
		Result->SetBoolField(TEXT("clearsPanels"), !Rect.Intersects(Layout.Build) && !Rect.Intersects(Layout.Minimap)
			&& (!Card.bOwned || !Rect.Intersects(Layout.Bottom)));
		Result->SetArrayField(TEXT("rect"), { MakeShared<FJsonValueNumber>(Rect.X * Layout.Scale), MakeShared<FJsonValueNumber>(Rect.Y * Layout.Scale),
			MakeShared<FJsonValueNumber>(Rect.W * Layout.Scale), MakeShared<FJsonValueNumber>(Rect.H * Layout.Scale) });
	});
	Entry->SetObjectField(TEXT("forceCard"), Result);
}

static bool ControlAction(FStringView Control, EHUDAction& Action)
{
	struct FControl { FStringView Name; EHUDAction Action; };
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

bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error)
{
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (Action != TEXT("forceCard"))
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
