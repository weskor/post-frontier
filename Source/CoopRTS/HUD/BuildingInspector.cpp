#include "HUDPanels.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "DepositSite.h"
#include "MapRegion.h"

namespace CommandHUDPanels
{
static void DrawConstructionInspector(const FPainter& Paint, const FContext& Context, const FRect& Inspector, const FLinearColor& Accent, FStringView Title, FStringView Subtitle, int32 Owner)
{
	const ACommandBuilding* Building = Context.Building;
	const UBuildingDefinition* Definition = Building->GetDefinition();
	const float Progress = FMath::Clamp(Building->ConstructionProgress, 0.f, 1.f);
	DrawInspectorHeader(Paint, Inspector, Accent, Title, Subtitle, Owner,
		Building->Health, Building->MaxHealth(), Context.bTerminal ? TEXT("HALTED") : TEXT("UNDER CONSTRUCTION"),
		Context.bTerminal ? Palette::Faint : Palette::Warn);
	const float Top = BodyTop(Inspector);
	const FRect Bar{ Inspector.X + Pad, Top + LabelHeight + 2.f, CancelButton(Inspector).X - Inspector.X - 2.f * Pad - 12.f, 14.f };
	Paint.Text(TEXT("CONSTRUCTION"), Bar.X, Top, 8.5f, Palette::Muted, true);
	Paint.Bar(Bar, Progress, Accent.CopyWithNewOpacity(.85f));
	TStringBuilder<64> Status;
	if (Context.bTerminal)
		Status.Appendf(TEXT("%d%%  \u00B7  halted: match over"), FMath::FloorToInt(Progress * 100.f));
	else
		Status.Appendf(TEXT("%d%%  \u00B7  %.0fs remaining"), FMath::FloorToInt(Progress * 100.f),
			FMath::CeilToFloat((1.f - Progress) * (Building->GetDefinition() ? Building->GetDefinition()->BuildDuration : 0.f)));
	Paint.Text(Status.ToView(), Bar.X, Bar.Bottom() + 7.f, 11.f, Palette::Text, true);
	Paint.Text(Building->IsProducer()                    ? TEXT("When complete: choose a permanent force type, Start and issue an order.")
			: Definition && Definition->bRequiresDeposit ? TEXT("When complete: extracts finite Power for your wallet only.")
			: Definition && Definition->bOffersResearch  ? TEXT("When complete: buy one specialization for your forces.")
														 : TEXT(""),
		Bar.X, Bar.Bottom() + 32.f, 9.f, Palette::Muted, false, EAlign::Left, Bar.W);
	Paint.Text(TEXT("Cancelling refunds the unbuilt share of the cost."), Bar.X, Bar.Bottom() + 48.f, 9.f, Palette::Faint,
		false, EAlign::Left, Bar.W);
}

static void DrawProductionRemedy(const FPainter& Paint, const FContext& Context, const FRect& Inspector, EProductionState ProductionState, const UArmyUnitDefinition* Recipe, int32 UnitCost, FStringView Status, const FLinearColor& StatusColor)
{
	TStringBuilder<128> Remedy;
	if (ProductionState == EProductionState::Unconfigured)
	{
		Remedy << TEXT("First Start permanently locks this building's force type");
		const int32 Fee = Recipe ? ACommandBuilding::GetConfigurationCost(*Recipe) : 0;
		if (Fee > 0)
			Remedy.Appendf(TEXT("; configuration costs %d resources once."), Fee);
		else
			Remedy << TEXT("; no configuration fee.");
	}
	else if (ProductionState == EProductionState::ForceComplete)
		Remedy << TEXT("Force full: no spending; casualties automatically open replacement slots.");
	else if (ProductionState == EProductionState::InsufficientResources)
		Remedy.Appendf(TEXT("Need %d more: resumes with income."),
			FMath::Max(0, UnitCost - Context.Balance));
	else if (ProductionState == EProductionState::Paused)
		Remedy << TEXT("Paused: click Resume.");
	else if (ProductionState == EProductionState::DeploymentBlocked)
		Remedy << TEXT("Deployment blocked: clear barracks exit; auto retry.");
	else if (ProductionState == EProductionState::Producing)
		Remedy << TEXT("Building one unit; pays at barracks deployment, then walks to this force.");
	else
		Remedy << Status;
	Paint.Text(Remedy.ToView(), Inspector.X + Pad, Inspector.Bottom() - 22.f, 10.f, StatusColor,
		true, EAlign::Left, Inspector.W - 2.f * Pad - (IsValid(Context.Building->ForceGroup) ? 150.f + Gap : 0.f));
}

static void DrawProductionInspector(const FPainter& Paint, const FContext& Context, const FRect& Inspector, const FLinearColor& Accent, FStringView Title, FStringView Subtitle, int32 Owner)
{
	const ACommandBuilding* Building = Context.Building;
	const EProductionState ProductionState = Building->GetProductionState();
	const FString Status = StatusText(ProductionState);
	const FLinearColor StatusColor = ProductionState == EProductionState::Producing || ProductionState == EProductionState::ForceComplete ? Palette::Good
		: ProductionState == EProductionState::Paused || ProductionState == EProductionState::MatchFinished                               ? Palette::Muted
																																		  : Palette::Warn;
	DrawInspectorHeader(Paint, Inspector, Accent, Title, Subtitle, Owner,
		Building->Health, Building->MaxHealth(), Status, StatusColor);
	const FRect Recipes = Column(Inspector, 0, 3);
	const FRect Production = Column(Inspector, 1, 3);
	const FRect Orders = Column(Inspector, 2, 3);
	ColumnLabel(Paint, Recipes, TEXT("FORCE TYPE"), Building->bForceConfigured ? TEXT("LOCKED") : TEXT("choose before Start"));
	int32 Joined = 0, Travelling = 0;
	Building->GetForceCounts(Joined, Travelling);
	const UArmyUnitDefinition* Recipe = ProductionDefinition(Context);
	const int32 Capacity = Recipe ? ACommandBuilding::GetForceCapacity(*Recipe) : 0;
	const int32 Vacancies = FMath::Max(0, Capacity - Joined - Travelling);
	TStringBuilder<32> ForceCounts;
	ForceCounts.Appendf(TEXT("joined %d/%d"), Joined, Capacity);
	ColumnLabel(Paint, Production, TEXT("FORCE"), ForceCounts.ToView());
	const AArmyGroup* Force = IsValid(Building->ForceGroup) ? Building->ForceGroup.Get() : nullptr;
	ColumnLabel(Paint, Orders, TEXT("ORDER"), Force ? ForceStatusTitle(Force->Status) : TEXT("UNCONFIGURED"),
		Force ? OrderColor(Force->Verb) : Palette::Muted);

	const FRect Progress = Row(Production, 0);
	const float Duration = FMath::Max(KINDA_SMALL_NUMBER, Recipe ? ACommandBuilding::GetUnitDuration(*Recipe) : 0.f);
	Paint.Bar({ Progress.X, Progress.Y, Progress.W, 5.f }, Building->ProductionProgressSeconds / Duration, StatusColor);
	TStringBuilder<64> Timer;
	const int32 BuildingCount = Building->bForceConfigured && (Building->ProductionProgressSeconds > 0.f || ProductionState == EProductionState::Producing || ProductionState == EProductionState::DeploymentBlocked) ? 1 : 0;
	Timer.Appendf(TEXT("Building %d: %.1f/%.1fs"), BuildingCount, Building->ProductionProgressSeconds, Duration);
	Paint.Text(Timer.ToView(), Progress.X, Progress.Y + 7.f, 9.f, Palette::Text, true, EAlign::Left, Progress.W);
	TStringBuilder<64> Recruits;
	Recruits.Appendf(TEXT("Travelling %d  \u00B7  Vacant %d"), Travelling, Vacancies);
	Paint.Text(Recruits.ToView(), Production.X, Row(Production, 2).Y, 9.f, Palette::Text, false, EAlign::Left, Production.W);
	TStringBuilder<48> UnitPrice;
	const int32 UnitCost = Recipe ? ACommandBuilding::GetUnitCost(*Recipe) : 0;
	UnitPrice.Appendf(TEXT("%d resources per unit"), UnitCost);
	Paint.Text(UnitPrice.ToView(), Production.X, Row(Production, 2).Y + 13.f, 8.f, Palette::Gold, false, EAlign::Left, Production.W);
	DrawProductionRemedy(Paint, Context, Inspector, ProductionState, Recipe, UnitCost, Status, StatusColor);
}

static void DrawResearchInspector(const FPainter& Paint, const FContext& Context, const FRect& Inspector, const FLinearColor& Accent, FStringView Title, FStringView Subtitle, int32 Owner)
{
	const ACommandBuilding* Building = Context.Building;
	const EArmyDoctrine Owned = Context.Wallet ? Context.Wallet->Doctrine : EArmyDoctrine::None;
	DrawInspectorHeader(Paint, Inspector, Accent, Title, Subtitle, Owner,
		Building->Health, Building->MaxHealth(), Owned == EArmyDoctrine::None ? TEXT("RESEARCH AVAILABLE") : TEXT("SPECIALIZED"),
		Owned == EArmyDoctrine::None ? Palette::Good : Palette::Muted);
	TStringBuilder<64> Detail;
	if (Owned == EArmyDoctrine::None)
		Detail.Appendf(TEXT("one per commander  \u00B7  %d each  \u00B7  applies to all your forces"), ACommandBuilding::ResearchCost);
	else
		Detail.Appendf(TEXT("%s is permanent for this match"), ResearchName(Owned));
	const FRect Area{ Inspector.X + Pad, BodyTop(Inspector), Inspector.W - 2.f * Pad, LabelHeight };
	ColumnLabel(Paint, Area, TEXT("SPECIALIZATION"), Detail.ToView());
}

static void DrawExtractorInspector(const FPainter& Paint, const FContext& Context, const FRect& Inspector, const FLinearColor& Accent, FStringView Title, FStringView Subtitle, int32 Owner)
{
	const ACommandBuilding* Building = Context.Building;
	const ADepositSite* Site = IsValid(Building->Deposit) ? Building->Deposit.Get() : nullptr;
	const bool bPaying = Site && Site->Remaining > 0;
	DrawInspectorHeader(Paint, Inspector, Accent, Title, Subtitle, Owner,
		Building->Health, Building->MaxHealth(), bPaying ? TEXT("EXTRACTING POWER") : TEXT("DEPOSIT EMPTY"),
		bPaying ? Palette::Good : Palette::Warn);
	const float X = Inspector.X + Pad;
	const float Top = BodyTop(Inspector);
	const float Width = Inspector.W - 2.f * Pad;
	TStringBuilder<96> Deposit;
	if (Site)
		Deposit.Appendf(TEXT("REGION %d  \u00B7  %s deposit  \u00B7  taken"),
			Site->RegionIndex + 1, Site->bRich ? TEXT("rich") : TEXT("normal"));
	else
		Deposit << TEXT("NO DEPOSIT");
	Paint.Text(Deposit.ToView(), X, Top, 8.5f, Palette::Muted, true);
	TStringBuilder<96> Income;
	Income.Appendf(TEXT("+%d Power/s to C%d only  \u00B7  %d remaining"),
		bPaying ? Site->RatePerSecond() : 0, Owner + 1, Site ? Site->Remaining : 0);
	Paint.Text(Income.ToView(), X, Top + 22.f, 10.5f, Palette::Text, false, EAlign::Left, Width);
	Paint.Text(TEXT("Region control grants build rights; extractors do not lock capture."),
		X, Top + 44.f, 10.f, Palette::Muted, false, EAlign::Left, Width);
	Paint.Text(TEXT("Depletion stops income. Destruction frees this deposit."),
		X, Top + 64.f, 10.f, Palette::Warn, false, EAlign::Left, Width);
}

void DrawBuildingInspector(const FPainter& Paint, const FContext& Context, const FRect& Inspector)
{
	const ACommandBuilding* Building = Context.Building;
	const int32 Owner = Context.Wallet ? Context.Wallet->CommanderIndex : -1;
	const UBuildingDefinition* Definition = Building->GetDefinition();
	const FLinearColor Accent = Definition ? Definition->Accent : Palette::Muted;
	FString Title = Definition ? Definition->DisplayName.ToString().ToUpper() : TEXT("BUILDING");
	if (Building->ForceNumber > 0)
		Title += FString::Printf(TEXT(" %d"), Building->ForceNumber);
	TStringBuilder<256> Subtitle;
	if (Building->IsProducer())
	{
		const AArmyGroup* Force = IsValid(Building->ForceGroup) ? Building->ForceGroup.Get() : nullptr;
		Subtitle.Appendf(TEXT("C%d  \u00B7  %s  \u00B7  "), Owner + 1, Force ? OrderTitle(Force->Verb) : TEXT("NO FORCE"));
		const AMapRegion* Target = nullptr;
		if (Context.State && Force)
			for (const AMapRegion* Region : Context.State->Regions)
				if (IsValid(Region) && Region->RegionIndex == Force->TargetRegionIndex)
				{
					Target = Region;
					break;
				}
		if (Target)
			Subtitle << Target->DisplayName.ToString();
		else
			Subtitle << TEXT("region unavailable");
	}
	else
		Subtitle.Appendf(TEXT("C%d  \u00B7  your building"), Owner + 1);
	if (!Building->IsComplete())
		DrawConstructionInspector(Paint, Context, Inspector, Accent, Title, Subtitle.ToView(), Owner);
	else if (Building->IsProducer())
		DrawProductionInspector(Paint, Context, Inspector, Accent, Title, Subtitle.ToView(), Owner);
	else if (Definition && Definition->bOffersResearch)
		DrawResearchInspector(Paint, Context, Inspector, Accent, Title, Subtitle.ToView(), Owner);
	else if (Definition && Definition->bRequiresDeposit)
		DrawExtractorInspector(Paint, Context, Inspector, Accent, Title, Subtitle.ToView(), Owner);
	else
		DrawInspectorHeader(Paint, Inspector, Accent, Title, Subtitle.ToView(), Owner,
			Building->Health, Building->MaxHealth(), FStringView(), Palette::Muted);
}


static void DrawForceSelection(const FPainter& Paint, const FContext& Context, const FRect& Selection, bool bOwned, int32 Owner)
{
	ColumnLabel(Paint, Selection, bOwned ? TEXT("SELECTION") : TEXT("OWNER"));
	TStringBuilder<128> Selected;
	if (bOwned)
	{
		const auto& Forces = Context.Controller->GetSelectedForces();
		Selected.Appendf(TEXT("%d selected: "), Forces.Num());
		bool bFirst = true;
		for (const AArmyGroup* Group : Forces)
		{
			if (!IsValid(Group))
				continue;
			if (!bFirst)
				Selected << TEXT(", ");
			Selected.Appendf(TEXT("%d"), Group->ForceNumber);
			bFirst = false;
		}
	}
	else
		Selected.Appendf(TEXT("Commander %d"), Owner + 1);
	Paint.Text(Selected.ToView(), Selection.X, Row(Selection, 0).Y, 10.f, Palette::Text, true, EAlign::Left, Selection.W);
	if (!CanPingInspectedForce(Context))
		Paint.Text(bOwned ? TEXT("Shift-click / box: select several") : TEXT("Teammate information only"),
			Selection.X, Row(Selection, 1).Y, 9.f, Palette::Muted, false, EAlign::Left, Selection.W);
	Paint.DrawKey(Selection.X, Row(Selection, 2).Y, TEXT("F"), TEXT("Centre selection"));
}

static void DrawForceStrength(const FPainter& Paint, const AArmyGroup* Force, const FRect& Strength)
{
	ColumnLabel(Paint, Strength, TEXT("STRENGTH"));
	int32 Joined = 0, Travelling = 0, Health = 0, MaxHealth = 0;
	for (const AArmyUnit* Unit : Force->GetUnits())
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		if (Unit->IsReinforcing())
			++Travelling;
		else
			++Joined;
		Health += Unit->GetHealth();
		MaxHealth += Unit->MaxHealth();
	}
	TStringBuilder<64> Counts;
	Counts.Appendf(TEXT("%d joined  \u00B7  %d travelling"), Joined, Travelling);
	Paint.Text(Counts.ToView(), Strength.X, Row(Strength, 0).Y, 10.f, Palette::Text, true, EAlign::Left, Strength.W);
	const FRect HealthRow = Row(Strength, 1);
	Paint.Bar({ HealthRow.X, HealthRow.Y, HealthRow.W, 8.f },
		MaxHealth > 0 ? static_cast<float>(Health) / MaxHealth : 0.f, Palette::Good);
	Counts.Reset();
	Counts.Appendf(TEXT("HP %d / %d"), Health, MaxHealth);
	Paint.Text(Counts.ToView(), Strength.X, HealthRow.Y + 10.f, 9.f, Palette::Muted, false, EAlign::Left, Strength.W);
}

void DrawForceInspector(const FPainter& Paint, const FContext& Context, const FRect& Inspector)
{
	const AArmyGroup* Force = Context.Force;
	if (!IsValid(Force) || !IsValid(Force->GetOwningPlayerState()))
		return;
	const int32 Owner = Force->GetOwningPlayerState()->CommanderIndex;
	const bool bOwned = Force->GetOwningPlayerState() == Context.Wallet;
	const ACommandBuilding* Producer = Force->GetProductionBuilding();
	const bool bHasProducer = IsValid(Producer) && Producer->IsAlive();
	TStringBuilder<64> Title;
	Title.Appendf(TEXT("FORCE %d"), Force->ForceNumber);
	TStringBuilder<96> Subtitle;
	Subtitle.Appendf(TEXT("C%d  \u00B7  %s  \u00B7  %s"), Owner + 1,
		bOwned ? TEXT("your force") : TEXT("teammate's force"),
		bHasProducer ? TEXT("reinforcements available") : TEXT("orphan: no reinforcements"));
	DrawInspectorHeader(Paint, Inspector, AArmyUnit::GetCommanderColor(Owner), Title.ToView(), Subtitle.ToView(),
		Owner, 0, 0, bOwned ? TEXT("SELECTED") : TEXT("READ ONLY"), bOwned ? Palette::Gold : Palette::Muted);

	const FRect Selection = Column(Inspector, 0, 3);
	const FRect Orders = Column(Inspector, 1, 3);
	const FRect Strength = Column(Inspector, 2, 3);
	DrawForceSelection(Paint, Context, Selection, bOwned, Owner);
	ColumnLabel(Paint, Orders, TEXT("ORDER"));

	Paint.Text(ForceStatusTitle(Force->Status), Orders.X, Row(Orders, 0).Y, 10.f, Palette::Text, true, EAlign::Left, Orders.W);
	Paint.Text(OrderTitle(Force->Verb), Orders.X, Row(Orders, 1).Y, 10.f,
		OrderColor(Force->Verb), true, EAlign::Left, Orders.W);
	if (Context.State)
		for (const AMapRegion* Region : Context.State->Regions)
			if (IsValid(Region) && Region->RegionIndex == Force->TargetRegionIndex)
			{
				Paint.Text(Region->DisplayName.ToString(), Orders.X, Row(Orders, 2).Y, 9.f,
					Palette::Muted, false, EAlign::Left, Orders.W);
				break;
			}

	DrawForceStrength(Paint, Force, Strength);
	Paint.Text(bHasProducer ? TEXT("Building panel keeps production and orders.") : TEXT("Survivors retain their order and remain selectable."),
		Inspector.X + Pad, Inspector.Bottom() - 22.f, 10.f, Palette::Muted, false, EAlign::Left, Inspector.W - 2.f * Pad);
}

}
