#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "MatchSimulationJson.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"

using namespace MatchSimulationJson;
using namespace SimulationDuel;

namespace
{
template <typename TPart>
TArray<TSharedPtr<FJsonValue>> SquadRows(const UMatchContent& Content, const TArray<TPart>& Squad)
{
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const TPart& Part : Squad)
	{
		const TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("id"), Content.Unit(Part.Definition)->Id.ToString());
		Row->SetNumberField(TEXT("count"), Part.Count);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	return Rows;
}
}

void FSimulationDuelRunner::BuildFights(const ACommandGameState& InState)
{
	const UMatchContent& Content = *InState.Content;
	Fights.Reset();
	for (const int32 Left : Definitions)
		for (const int32 Right : Definitions)
		{
			FFight& Fight = Fights.AddDefaulted_GetRef();
			Fight.Squads[0] = { { Left, DuelBudget / Content.Unit(Left)->UnitCost } };
			Fight.Squads[1] = { { Right, DuelBudget / Content.Unit(Right)->UnitCost } };
		}
	// A support unit is judged by what it adds to a partner against the unit it counters, never alone:
	// the first Ranged definition is the partner, the first Shielded definition is the target.
	int32 Partner = INDEX_NONE, Target = INDEX_NONE;
	for (const int32 Index : Definitions)
	{
		if (Partner == INDEX_NONE && Content.Unit(Index)->Role == EUnitRole::Ranged)
			Partner = Index;
		if (Target == INDEX_NONE && Content.Unit(Index)->ArmorClass == EArmorClass::Shielded)
			Target = Index;
	}
	if (Partner == INDEX_NONE || Target == INDEX_NONE)
		return;
	for (const int32 Support : Definitions)
		if (Content.Unit(Support)->Role == EUnitRole::Support)
			AddComposition(InState, Support, Partner, Target);
}

// The same partner squad with and without the support unit against the same target squad, on both sides
// of the field. With the support unit, one support unit takes its Power out of the partner's budget.
void FSimulationDuelRunner::AddComposition(const ACommandGameState& InState, int32 Support, int32 Partner, int32 Target)
{
	const UMatchContent& Content = *InState.Content;
	const int32 SupportCost = Content.Unit(Support)->UnitCost, PartnerCost = Content.Unit(Partner)->UnitCost;
	const TArray<FSquadPart> Baseline = { { Partner, DuelBudget / PartnerCost } };
	const TArray<FSquadPart> Supported = { { Support, 1 }, { Partner, (DuelBudget - SupportCost) / PartnerCost } };
	for (const bool bSupported : { false, true })
		for (const int32 SubjectSide : { 0, 1 })
		{
			FFight& Fight = Fights.AddDefaulted_GetRef();
			Fight.Squads[SubjectSide] = bSupported ? Supported : Baseline;
			Fight.Squads[1 - SubjectSide] = { { Target, DuelBudget / Content.Unit(Target)->UnitCost } };
			Fight.Kind = bSupported ? TEXT("with_support") : TEXT("baseline");
			Fight.Support = Content.Unit(Support)->Id;
			Fight.Partner = Content.Unit(Partner)->Id;
			Fight.Target = Content.Unit(Target)->Id;
			Fight.SubjectSide = SubjectSide;
		}
}

bool FSimulationDuelRunner::StartPair()
{
	Elapsed = 0.;
	LastDamageElapsed = -1.;
	Current = MakeShared<FJsonObject>();
	const FFight& Fight = Fights[FightIndex];
	const UMatchContent& Content = *State->Content;
	float Slowest = 0.f;
	for (const TArray<FSquadPart>& Squad : Fight.Squads)
		for (const FSquadPart& Part : Squad)
			Slowest = FMath::Max(Slowest, Content.Unit(Part.Definition)->Interval);
	StallTimeout = FMath::Max(30., 10. * Slowest);
	Current->SetNumberField(TEXT("stall_timeout_seconds"), StallTimeout);
	Current->SetField(TEXT("no_damage_seconds"), MakeShared<FDuelNumber>(0.));
	Current->SetNumberField(TEXT("spawn_first_team"), SpawnFirstSide == 0 ? 0 : 5);
	TArray<TSharedPtr<FJsonValue>> Spawns[2];
	const float Angle = Random.FRandRange(0.f, 2.f * PI);
	const FVector Forward(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
	const FVector Across(-Forward.Y, Forward.X, 0.f);
	Current->SetNumberField(TEXT("spawn_angle_radians"), Angle);
	for (int32 Order = 0; Order < 2; ++Order)
	{
		const int32 Side = (SpawnFirstSide + Order) % 2;
		if (!SpawnSide(Side, Fight.Squads[Side], Forward, Across, Spawns[Side]))
			return false;
	}
	if (Fight.IsComposition())
	{
		Current->SetStringField(TEXT("scenario"), Fight.Kind);
		Current->SetStringField(TEXT("support"), Fight.Support.ToString());
		Current->SetStringField(TEXT("partner"), Fight.Partner.ToString());
		Current->SetStringField(TEXT("target"), Fight.Target.ToString());
		Current->SetNumberField(TEXT("subject_team"), Fight.SubjectSide == 0 ? 0 : 5);
		Current->SetArrayField(TEXT("left_units"), SquadRows(Content, Fight.Squads[0]));
		Current->SetArrayField(TEXT("right_units"), SquadRows(Content, Fight.Squads[1]));
	}
	else
	{
		Current->SetStringField(TEXT("left"), Content.Unit(Fight.Squads[0][0].Definition)->Id.ToString());
		Current->SetStringField(TEXT("right"), Content.Unit(Fight.Squads[1][0].Definition)->Id.ToString());
	}
	if (!IssueAttackOrders())
		return false;
	PublishPair(Spawns);
	return true;
}

bool FSimulationDuelRunner::SpawnSide(int32 Side, const TArray<FSquadPart>& Squad, const FVector& Forward,
	const FVector& Across, TArray<TSharedPtr<FJsonValue>>& Spawns)
{
	const UMatchContent& Content = *State->Content;
	TArray<int32> Units;
	for (const FSquadPart& Part : Squad)
		for (int32 Count = 0; Count < Part.Count; ++Count)
			Units.Add(Part.Definition);
	Initial[Side] = Units.Num();
	Spent[Side] = 0;
	for (const int32 Definition : Units)
		Spent[Side] += Content.Unit(Definition)->UnitCost;
	Survivors[Side] = Initial[Side];
	SurvivorPower[Side] = Spent[Side];
	Damage[Side] = ShieldDamage[Side] = Attacks[Side] = 0;
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Initial[Side])));
	const int32 Rows = FMath::DivideAndRoundUp(Initial[Side], Columns);
	AArmyGroup* Group = nullptr;
	for (int32 Index = 0; Index < Initial[Side]; ++Index)
	{
		const float Sign = Side == 0 ? -1.f : 1.f;
		const float X = Sign * (500.f + ((Index % Columns) - (Columns - 1) * .5f) * DuelSpacing);
		const float Y = ((Index / Columns) - (Rows - 1) * .5f) * DuelSpacing;
		const FVector Spawn = Center + Forward * (X + Random.FRandRange(-DuelJitter, DuelJitter))
			+ Across * (Y + Random.FRandRange(-DuelJitter, DuelJitter));
		// Six-slot legacy formations are separate groups, never a squad cap.
		if (Index % 6 == 0)
		{
			const FTransform Transform(Spawn);
			Group = State->GetWorld()->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
				Wallets[Side]->GetOwner(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Group)
			{
				Error = TEXT("Could not spawn duel group");
				return false;
			}
			Group->Initialize({ Side == 0 ? 0 : 5, Wallets[Side].Get(), Index / 6, nullptr, Spawn });
			Group->FinishSpawning(Transform);
			Groups.Add(Group);
		}
		AArmyUnit* Unit = Group->SpawnMember(Units[Index], Spawn, Index % 6);
		if (!Unit)
		{
			Error = TEXT("Could not spawn a joined duel member on verified ground");
			return false;
		}
		Members.Add({ Unit, Side, Unit->GetHealth(), Unit->GetShield(), Content.Unit(Units[Index])->UnitCost, Unit->AttackCount });
		Spawns.Add(MakeShared<FJsonValueArray>(Position(Unit->GetActorLocation())));
	}
	return true;
}
#endif
