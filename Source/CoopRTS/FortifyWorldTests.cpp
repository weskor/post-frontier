#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Commands/AbilityCommandComponent.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/FortifyPolicy.h"
#include "Rules/RegionTraitPolicy.h"
#include "TeamEconomyFixture.h"

// Fortify on the real authority: the cast and its rejections, the capture freeze, the damage multiplier on units,
// buildings and the HQ, a teammate's refresh and the early end. Server time cannot be skipped in a world test, so
// a test moves the published expiry and cooldown instead; the rules tests pin the arithmetic itself.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyCastTest, "CoopRTS.Fortify.Cast",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyRegionsTest, "CoopRTS.Fortify.MainAndContested",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyCaptureTest, "CoopRTS.Fortify.CaptureFreeze",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyDamageTest, "CoopRTS.Fortify.Damage",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyRefreshWorldTest, "CoopRTS.Fortify.Refresh",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyEarlyEndTest, "CoopRTS.Fortify.EarlyEnd",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
AMapRegion* Region(FTeamEconomyFixture& F, int32 Index)
{
	for (AMapRegion* Candidate : F.State->Regions)
		if (IsValid(Candidate) && Candidate->RegionIndex == Index)
			return Candidate;
	return nullptr;
}

float Now(const FTeamEconomyFixture& F)
{
	return F.State->GetServerWorldTimeSeconds();
}

FCommandResult Cast(FTeamEconomyFixture& F, ACommandPlayerState* Caster, int32 RegionIndex)
{
	return FCommandService::CastFortify(Caster, Region(F, RegionIndex));
}

// A living unit of the local team standing at the region's anchor, with its region read.
AArmyUnit* FriendlyIn(FTeamEconomyFixture& F, int32 RegionIndex)
{
	AArmyGroup* Group = ArmyTestSetup::SpawnGroup(F.World, F.Controller, 11, F.State->GetRegionAnchor(RegionIndex) + FVector(0.f, 0.f, 100.f));
	if (!Group || Group->GetUnits().IsEmpty())
		return nullptr;
	Group->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Group->GetUnits())
		Unit->SetActorTickEnabled(false);
	AArmyUnit* Unit = Group->GetUnits()[0];
	Unit->RefreshRegion();
	return Unit;
}

int32 Durability(const AArmyUnit& Unit)
{
	return Unit.GetHealth() + Unit.GetShield();
}

// What one hit of Damage costs the victim, reading the real receive path.
int32 UnitHit(AArmyUnit& Victim, AArmyUnit& Attacker, int32 Damage)
{
	const int32 Before = Durability(Victim);
	Victim.ReceiveAttack(Damage, &Attacker);
	return Before - Durability(Victim);
}

bool Starts(const FString& Text, const TCHAR* Prefix)
{
	return Text.StartsWith(Prefix);
}

// The map, the controller's wallet and its feed are shared by every scenario, so each starts and ends with
// no Fortify on any region, no cooldown and an empty team feed.
void Clean(FTeamEconomyFixture& F)
{
	for (AMapRegion* Region : F.State->Regions)
		if (IsValid(Region))
		{
			Region->FortifyTeam = -1;
			Region->FortifyCaster = -1;
			Region->FortifyExpiresAt = 0.f;
		}
	for (ACommandPlayerState* Wallet : F.Wallets)
		Wallet->FortifyReadyAt = 0.f;
	F.Controller->AbilityCommands->ResetForMatch();
}

class FFortifyScenario : public FTeamEconomyScenario
{
public:
	FFortifyScenario(FAutomationTestBase* InTest, int32 Commanders, TFunction<void(FTeamEconomyFixture&)> Body)
		: FTeamEconomyScenario(InTest, Commanders, [Body](FTeamEconomyFixture& F) {
			  Clean(F);
			  Body(F);
			  Clean(F);
		  })
	{
	}
};

// Refusals by cost, roster and target, then the repeat cast after the cooldown.
void CheckCastLimits(FTeamEconomyFixture& F, ACommandPlayerState* First, ACommandPlayerState* Second)
{
	FAutomationTestBase& T = *F.Test;
	Second->Data = FortifyPolicy::DataCost - 1;
	const FCommandResult Short = Cast(F, Second, F.Neck);
	T.TestFalse(TEXT("One Data short is refused"), Short.IsAccepted());
	T.TestEqual(TEXT("with the shortfall"), Short.Message, FString(TEXT("Need 1 more Data")));
	T.TestEqual(TEXT("keeping the Data"), Second->Data, FortifyPolicy::DataCost - 1);

	int32 Slot = 0;
	while (F.Wallets.ContainsByPredicate([Slot](const ACommandPlayerState* Wallet) { return Wallet->CommanderIndex == Slot; }))
		++Slot;
	ACommandPlayerState* Outsider = F.Spawn(Slot, false);
	Outsider->Data = 100;
	T.TestFalse(TEXT("A player state outside the roster cannot cast"), Cast(F, Outsider, F.Far).IsAccepted());
	T.TestEqual(TEXT("and pays nothing"), Outsider->Data, 100);
	Outsider->Destroy();
	T.TestFalse(TEXT("JEV has no Fortify"), Cast(F, F.State->EnemyCommander, F.Far).IsAccepted());
	T.TestFalse(TEXT("A missing region is refused"), FCommandService::CastFortify(Second, nullptr).IsAccepted());
	T.TestEqual(TEXT("Nothing above protected the neck"), Region(F, F.Neck)->FortifyTeam, -1);

	// After the cooldown the same commander pays again.
	First->FortifyReadyAt = Now(F) - 1.f;
	T.TestTrue(TEXT("A cooled-down commander casts again"), Cast(F, First, F.Neck).IsAccepted());
	T.TestEqual(TEXT("and pays 40 Data again"), First->Data, 20);
	T.TestEqual(TEXT("A new cooldown starts"), First->FortifyReadyAt > Now(F) + 80.f, true);
}
}

bool FFortifyCastTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFortifyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ACommandPlayerState* First = F.Wallets[0];
		ACommandPlayerState* Second = F.Wallets[1];
		AMapRegion* Far = Region(F, F.Far);
		First->Data = 100;
		Second->Data = 100;
		const float Start = Now(F);

		const FCommandResult Cast1 = Cast(F, First, F.Far);
		T.TestTrue(TEXT("A controlled region is Fortified"), Cast1.IsAccepted());
		T.TestEqual(TEXT("The cast spends 40 Data once"), First->Data, 60);
		T.TestEqual(TEXT("and no Power"), First->Resources, 0);
		T.TestEqual(TEXT("The region protects the casting team"), Far->FortifyTeam, 0);
		T.TestEqual(TEXT("and remembers the caster's slot"), Far->FortifyCaster, First->CommanderIndex);
		T.TestTrue(TEXT("The effect lasts 60 s from the cast"), FMath::IsNearlyEqual(Far->FortifyExpiresAt - Start, 60.f, .5f));
		T.TestTrue(TEXT("The caster's cooldown is 90 s from the cast"), FMath::IsNearlyEqual(First->FortifyReadyAt - Start, 90.f, .5f));
		T.TestEqual(TEXT("A teammate's cooldown is untouched"), Second->FortifyReadyAt, 0.f);

		// Every rejection carries the chip's reason text and spends nothing.
		const FCommandResult Cooling = Cast(F, First, F.Far);
		T.TestFalse(TEXT("The cooldown refuses a second cast"), Cooling.IsAccepted());
		T.TestTrue(TEXT("with its reason"), Starts(Cooling.Message, TEXT("Cooldown 1:")) || Starts(Cooling.Message, TEXT("Cooldown 0:")));
		T.TestEqual(TEXT("and spends nothing"), First->Data, 60);

		F.SetController(F.Alternate, -1);
		const FCommandResult Neutral = Cast(F, Second, F.Alternate);
		T.TestFalse(TEXT("A neutral region is refused"), Neutral.IsAccepted());
		T.TestTrue(TEXT("naming it"), Neutral.Message.EndsWith(TEXT(" is neutral")));
		F.SetController(F.Alternate, 5);
		const FCommandResult Enemy = Cast(F, Second, F.Alternate);
		T.TestFalse(TEXT("A region JEV holds is refused"), Enemy.IsAccepted());
		T.TestTrue(TEXT("naming JEV"), Enemy.Message.EndsWith(TEXT(" is held by JEV")));
		T.TestEqual(TEXT("Neither spent Data"), Second->Data, 100);
		T.TestEqual(TEXT("or started a cooldown"), Second->FortifyReadyAt, 0.f);
		T.TestEqual(TEXT("The refused regions stay unprotected"), Region(F, F.Alternate)->FortifyTeam, -1);
		CheckCastLimits(F, First, Second);
	}));
	return true;
}

bool FFortifyRegionsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFortifyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		F.Wallets[0]->Data = 100;
		F.Wallets[1]->Data = 100;
		T.TestTrue(TEXT("Your own main is allowed"), Cast(F, F.Wallets[0], 0).IsAccepted());
		T.TestEqual(TEXT("The main is protected"), Region(F, 0)->FortifyTeam, 0);
		F.SpawnHostileIn(F.Neck);
		T.TestTrue(TEXT("The neck is contested"), F.State->IsRegionContested(F.Neck, 0));
		T.TestTrue(TEXT("A contested region is allowed"), Cast(F, F.Wallets[1], F.Neck).IsAccepted());
		T.TestEqual(TEXT("and protected"), Region(F, F.Neck)->FortifyTeam, 0);
		T.TestEqual(TEXT("Both casts paid"), F.Wallets[0]->Data + F.Wallets[1]->Data, 120);
	}));
	return true;
}

bool FFortifyCaptureTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFortifyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		AMapRegion* Far = Region(F, F.Far);
		ACapturePoint* Anchor = Far->Anchor;
		Anchor->CaptureProgress = 1.f;
		F.SpawnHostileIn(F.Far);
		F.Wallets[0]->Data = 100;

		Anchor->AdvanceCapture(4.f);
		T.TestTrue(TEXT("Without Fortify the raid drains the capture"), FMath::IsNearlyEqual(Anchor->CaptureProgress, .5f, .001f));
		Anchor->CaptureProgress = 1.f;
		T.TestTrue(TEXT("The Fortify is cast"), Cast(F, F.Wallets[0], F.Far).IsAccepted());
		Anchor->AdvanceCapture(4.f);
		T.TestEqual(TEXT("A Fortified anchor keeps its progress"), Anchor->CaptureProgress, 1.f);
		T.TestEqual(TEXT("and its owner"), Anchor->ControllingTeam, 0);
		T.TestTrue(TEXT("while occupancy is still published"), Anchor->bEnemyPresent);
		T.TestFalse(TEXT("no friendly is there"), Anchor->bFriendlyPresent);
		Anchor->AdvanceCapture(60.f);
		T.TestEqual(TEXT("Even a minute of raiding changes nothing"), Anchor->CaptureProgress, 1.f);

		// Sixty seconds later: capture resumes from the frozen progress.
		Far->FortifyExpiresAt = Now(F) - 1.f;
		Anchor->AdvanceCapture(4.f);
		T.TestTrue(TEXT("At expiry capture resumes from the frozen progress"), FMath::IsNearlyEqual(Anchor->CaptureProgress, .5f, .001f));
		Anchor->AdvanceCapture(12.f);
		T.TestEqual(TEXT("and the raid takes the region"), Anchor->ControllingTeam, 5);
	}));
	return true;
}

bool FFortifyDamageTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFortifyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		AMapRegion* Far = Region(F, F.Far);
		AArmyUnit* Raider = F.SpawnHostileIn(F.Neck);
		AArmyUnit* Ally = FriendlyIn(F, F.Far);
		AArmyUnit* Outside = FriendlyIn(F, F.Neck);
		AArmyUnit* Foe = F.SpawnHostileIn(F.Far);
		if (Foe)
			Foe->RefreshRegion();
		AArmyUnit* Striker = F.SpawnAttacker();
		if (!Raider || !Ally || !Outside || !Foe || !Striker)
		{
			T.AddError(TEXT("Fixture units unavailable"));
			return;
		}
		F.Wallets[0]->Data = 100;
		T.TestEqual(TEXT("A hit costs its full damage without Fortify"), UnitHit(*Ally, *Raider, 40), 40);

		T.TestTrue(TEXT("The Fortify is cast"), Cast(F, F.Wallets[0], F.Far).IsAccepted());
		T.TestEqual(TEXT("Casting-team units in the region take x0.75"), UnitHit(*Ally, *Raider, 40), 30);
		T.TestEqual(TEXT("Units of the same team outside it do not"), UnitHit(*Outside, *Raider, 40), 40);
		T.TestEqual(TEXT("Hostile units in the region do not"), UnitHit(*Foe, *Striker, 40), 40);

		const ERegionTrait Trait = Far->Trait;
		Far->Trait = ERegionTrait::Cover;
		T.TestEqual(TEXT("With Cover the multipliers stack to x0.6"), UnitHit(*Ally, *Raider, 100), 60);
		Far->Trait = Trait;

		// A Drill Rig is a building in the region; the HQ is the main's building.
		ACommandBuilding* Rig = F.SpawnRig(F.Far, F.Wallets[0], 0);
		AHeadquarters* Hq = F.State->FriendlyHeadquarters;
		if (Rig)
		{
			const int32 Before = Rig->Health;
			Rig->ReceiveAttack(80, Raider);
			T.TestEqual(TEXT("A building in the region takes x0.75"), Before - Rig->Health, 60);
		}
		else
			T.AddError(TEXT("Rig fixture unavailable"));
		F.Wallets[0]->FortifyReadyAt = 0.f;
		T.TestTrue(TEXT("The main is Fortified too"), Cast(F, F.Wallets[0], 0).IsAccepted());
		const int32 Health = Hq->Health;
		Hq->ReceiveAttack(80, Raider);
		T.TestEqual(TEXT("The HQ takes x0.75"), Health - Hq->Health, 60);
		Hq->Health = Health;

		Far->FortifyExpiresAt = Now(F) - 1.f;
		T.TestEqual(TEXT("After expiry the hit is full again"), UnitHit(*Ally, *Raider, 40), 40);
	}));
	return true;
}

bool FFortifyRefreshWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFortifyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ACommandPlayerState* First = F.Wallets[0];
		ACommandPlayerState* Second = F.Wallets[1];
		AMapRegion* Far = Region(F, F.Far);
		AArmyUnit* Raider = F.SpawnHostileIn(F.Neck);
		AArmyUnit* Ally = FriendlyIn(F, F.Far);
		First->Data = 100;
		Second->Data = 100;
		T.TestTrue(TEXT("The first commander casts"), Cast(F, First, F.Far).IsAccepted());

		// Thirty seconds pass, then the teammate recasts.
		Far->FortifyExpiresAt -= 30.f;
		const float Before = Far->FortifyExpiresAt;
		const FCommandResult Recast = Cast(F, Second, F.Far);
		T.TestTrue(TEXT("A teammate refreshes a Fortified region"), Recast.IsAccepted());
		T.TestTrue(TEXT("and the message says so"), Recast.Message.StartsWith(TEXT("Fortify refreshed")));
		T.TestTrue(TEXT("Expiry is 60 s from the recast"), FMath::IsNearlyEqual(Far->FortifyExpiresAt - Now(F), 60.f, .5f));
		T.TestTrue(TEXT("later than before"), Far->FortifyExpiresAt > Before + 20.f);
		T.TestEqual(TEXT("The badge shows the latest caster"), Far->FortifyCaster, Second->CommanderIndex);
		T.TestEqual(TEXT("Each caster paid 40 once"), First->Data + Second->Data, 120);
		T.TestEqual(TEXT("The recast was the teammate's own, so the first caster's cooldown is unchanged"),
			First->FortifyReadyAt > Now(F) + 80.f, true);
		T.TestEqual(TEXT("Effects never stack"), UnitHit(*Ally, *Raider, 40), 30);
		T.TestEqual(TEXT("A recast by the same commander is still on cooldown"), Cast(F, Second, F.Far).IsAccepted(), false);

		// The teammate's cast reaches the local commander's feed; its own cast does not.
		const FObjectiveEventView Events = F.Controller->AbilityCommands->GetEvents();
		T.TestEqual(TEXT("One team row: the second commander's cast"), Events.Num(), 1);
		if (!Events.IsEmpty())
		{
			T.TestEqual(TEXT("It is a cast row"), Events.Last().Id, FName(FortifyPolicy::CastEventId));
			T.TestEqual(TEXT("naming the caster"), Events.Last().Forces[0].CommanderIndex, Second->CommanderIndex);
			T.TestEqual(TEXT("and the region"), Events.Last().RegionIndex, F.Far);
			T.TestTrue(TEXT("in the team ring"), UAbilityCommandComponent::IsAbilitySequence(Events.Last().Sequence));
		}
	}));
	return true;
}

bool FFortifyEarlyEndTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFortifyScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		AMapRegion* Far = Region(F, F.Far);
		AArmyUnit* Raider = F.SpawnHostileIn(F.Neck);
		AArmyUnit* Ally = FriendlyIn(F, F.Far);
		F.Wallets[0]->Data = 100;
		T.TestTrue(TEXT("The Fortify is cast"), Cast(F, F.Wallets[0], F.Far).IsAccepted());
		Far->Tick(0.f);
		T.TestEqual(TEXT("It holds while the team controls the region"), Far->FortifyTeam, 0);
		T.TestEqual(TEXT("and posts nothing"), F.Controller->AbilityCommands->GetEvents().Num(), 0);

		F.SetController(F.Far, 5);
		Far->Tick(0.f);
		T.TestEqual(TEXT("Losing control ends it early"), Far->FortifyTeam, -1);
		T.TestEqual(TEXT("and clears the expiry"), Far->FortifyExpiresAt, 0.f);
		T.TestEqual(TEXT("so the hit is full"), UnitHit(*Ally, *Raider, 40), 40);
		T.TestEqual(TEXT("The cooldown is not refunded"), F.Wallets[0]->FortifyReadyAt > Now(F) + 80.f, true);
		const FObjectiveEventView Events = F.Controller->AbilityCommands->GetEvents();
		T.TestEqual(TEXT("The whole team gets one row"), Events.Num(), 1);
		if (!Events.IsEmpty())
			T.TestEqual(TEXT("saying the region was lost"), Events.Last().Id, FName(FortifyPolicy::EndedEventId));

		// A run-out Fortify clears silently.
		F.SetController(F.Far, 0);
		F.Wallets[0]->FortifyReadyAt = 0.f;
		T.TestTrue(TEXT("It can be cast again"), Cast(F, F.Wallets[0], F.Far).IsAccepted());
		Far->FortifyExpiresAt = Now(F) - 1.f;
		Far->Tick(0.f);
		T.TestEqual(TEXT("Expiry clears the region"), Far->FortifyTeam, -1);
		T.TestEqual(TEXT("without a feed row"), F.Controller->AbilityCommands->GetEvents().Num(), 1);
	}));
	return true;
}
#endif
