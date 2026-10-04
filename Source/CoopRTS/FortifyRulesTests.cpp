#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ControllerInputPolicy.h"
#include "Rules/DamagePolicy.h"
#include "Rules/FortifyPolicy.h"
#include "Rules/JevPlanner.h"
#include "Rules/RegionTraitPolicy.h"

// Pure rule tests: no world, no actors. The cast verdicts, refresh, expiry, early end, damage order and targeting.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyValidationTest, "CoopRTS.Rules.Fortify.Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyRefreshTest, "CoopRTS.Rules.Fortify.Refresh",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyLifetimeTest, "CoopRTS.Rules.Fortify.Lifetime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyDamageOrderTest, "CoopRTS.Rules.Fortify.DamageOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyTextTest, "CoopRTS.Rules.Fortify.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyTargetingTest, "CoopRTS.Rules.Fortify.Targeting",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFortifyJevScoreTest, "CoopRTS.Rules.Fortify.JevScore",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace FortifyPolicy;

// A ready commander of team 0 with enough Data, casting on a region its team controls.
FCastInput Ready()
{
	FCastInput In;
	In.bBattleLive = true;
	In.bCommander = true;
	In.bRegionExists = true;
	In.CasterTeam = 0;
	In.RegionController = 0;
	In.Data = DataCost;
	In.Now = 100.f;
	In.ReadyAt = 0.f;
	return In;
}

EVerdict Verdict(const FCastInput& In)
{
	return Evaluate(In).Verdict;
}

FString Reason(const FDecision& Decision, const TCHAR* Region = TEXT("Fusion Works"))
{
	TStringBuilder<96> Text;
	AppendReason(Text, Decision, Region);
	return FString(Text.ToView());
}
}

bool FFortifyValidationTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A ready commander on a controlled region is accepted"), Verdict(Ready()), EVerdict::Accepted);

	FCastInput Contested = Ready();
	Contested.RegionController = 0;
	TestEqual(TEXT("A contested region keeps its controller, so it is allowed"), Verdict(Contested), EVerdict::Accepted);

	// The main is controlled by its owner through the same field.
	FCastInput Main = Ready();
	Main.RegionController = 0;
	TestTrue(TEXT("Your own main is allowed"), Evaluate(Main).IsAccepted());

	FCastInput Neutral = Ready();
	Neutral.RegionController = -1;
	TestEqual(TEXT("A neutral region is refused"), Verdict(Neutral), EVerdict::Neutral);
	FCastInput Enemy = Ready();
	Enemy.RegionController = 5;
	TestEqual(TEXT("A region JEV holds is refused"), Verdict(Enemy), EVerdict::HeldByEnemy);

	FCastInput Short = Ready();
	Short.Data = DataCost - 1;
	const FDecision ShortDecision = Evaluate(Short);
	TestEqual(TEXT("One Data short is refused"), ShortDecision.Verdict, EVerdict::NeedData);
	TestEqual(TEXT("It names the exact shortfall"), ShortDecision.DataShort, 1);
	FCastInput Exact = Ready();
	Exact.Data = DataCost;
	TestTrue(TEXT("The exact price is enough"), Evaluate(Exact).IsAccepted());

	FCastInput Cooling = Ready();
	Cooling.ReadyAt = Cooling.Now + 47.f;
	const FDecision CoolingDecision = Evaluate(Cooling);
	TestEqual(TEXT("A cooldown refuses"), CoolingDecision.Verdict, EVerdict::Cooldown);
	TestEqual(TEXT("It reports the seconds left"), CoolingDecision.CooldownLeft, 47.f);
	Cooling.ReadyAt = Cooling.Now;
	TestTrue(TEXT("The cooldown ends exactly at its end time"), Evaluate(Cooling).IsAccepted());

	FCastInput Both = Ready();
	Both.RegionController = 5;
	Both.Data = 0;
	Both.ReadyAt = Both.Now + 10.f;
	TestEqual(TEXT("The region rule fails before the cooldown and Data"), Verdict(Both), EVerdict::HeldByEnemy);
	Both.RegionController = 0;
	TestEqual(TEXT("The cooldown fails before the Data shortfall"), Verdict(Both), EVerdict::Cooldown);

	FCastInput Idle = Ready();
	Idle.bBattleLive = false;
	TestEqual(TEXT("No live battle"), Verdict(Idle), EVerdict::NoBattle);
	FCastInput Outsider = Ready();
	Outsider.bCommander = false;
	TestEqual(TEXT("Only roster commanders cast"), Verdict(Outsider), EVerdict::NotCommander);
	FCastInput Nowhere = Ready();
	Nowhere.bRegionExists = false;
	TestEqual(TEXT("A click outside every region has no target"), Verdict(Nowhere), EVerdict::NoRegion);

	TestTrue(TEXT("Cooldown and Data do not make a region invalid"), IsValidTarget(EVerdict::Cooldown) && IsValidTarget(EVerdict::NeedData));
	TestFalse(TEXT("Neutral and enemy regions are not targets"), IsValidTarget(EVerdict::Neutral) || IsValidTarget(EVerdict::HeldByEnemy));
	return true;
}

bool FFortifyRefreshTest::RunTest(const FString& Parameters)
{
	FCastInput In = Ready();
	TestFalse(TEXT("A fresh region is not a refresh"), Evaluate(In).bRefresh);

	In.Region = Cast(0, In.Now - 20.f);
	const FDecision Held = Evaluate(In);
	TestTrue(TEXT("A Fortify the team holds is a refresh, still accepted"), Held.IsAccepted() && Held.bRefresh);
	TestEqual(TEXT("It reports the seconds left"), Held.RefreshLeft, DurationSeconds - 20.f);

	const FRegionState Recast = Cast(0, In.Now);
	TestEqual(TEXT("A recast ends 60 s from that cast, not from the first"), Recast.ExpiresAt - In.Now, DurationSeconds);
	TestTrue(TEXT("The recast outlives the first cast's expiry"), Recast.ExpiresAt > In.Region.ExpiresAt);
	TestEqual(TEXT("It protects the same team and stays a single state, so effects never stack"), Recast.Team, In.Region.Team);

	In.Region = Cast(0, In.Now - DurationSeconds);
	TestFalse(TEXT("An expired Fortify is no refresh"), Evaluate(In).bRefresh);
	return true;
}

bool FFortifyLifetimeTest::RunTest(const FString& Parameters)
{
	const FRegionState Region = Cast(0, 100.f);
	TestEqual(TEXT("The effect lasts 60 s"), Region.ExpiresAt, 160.f);
	TestEqual(TEXT("The commander's cooldown is 90 s from the cast"), CooldownEnd(100.f), 190.f);

	TestTrue(TEXT("Active just before expiry"), IsActive(Region, 159.9f));
	TestFalse(TEXT("Inactive at expiry"), IsActive(Region, 160.f));
	TestEqual(TEXT("Nothing is left when inactive"), SecondsLeft(Region, 200.f), 0.f);
	TestFalse(TEXT("An empty state is never active"), IsActive(FRegionState(), 0.f));

	TestEqual(TEXT("Running out reports Expired"), Review(Region, 0, 160.f), EEnd::Expired);
	TestEqual(TEXT("Held and in time reports nothing"), Review(Region, 0, 130.f), EEnd::None);
	TestEqual(TEXT("Losing control ends it early"), Review(Region, 5, 130.f), EEnd::RegionLost);
	TestEqual(TEXT("A region gone neutral ends it early"), Review(Region, -1, 130.f), EEnd::RegionLost);
	TestEqual(TEXT("An empty state has nothing to review"), Review(FRegionState(), 5, 130.f), EEnd::None);

	TestTrue(TEXT("Capture is frozen while active"), FreezesCapture(Region, 130.f));
	TestFalse(TEXT("Capture resumes at expiry"), FreezesCapture(Region, 160.f));
	TestEqual(TEXT("Only the Fortified team takes less damage"), IncomingFor(Region, 5, 130.f), 1.f);
	TestEqual(TEXT("The casting team takes x0.75"), IncomingFor(Region, 0, 130.f), IncomingMultiplier);
	TestEqual(TEXT("Nothing after expiry"), IncomingFor(Region, 0, 160.f), 1.f);

	TestEqual(TEXT("JEV weighs a Fortified hostile region x1.33"), DefenceFor(Region, 5, 130.f), JevDefenceMultiplier);
	TestEqual(TEXT("Not its own region"), DefenceFor(FRegionState{ 5, 160.f }, 5, 130.f), 1.f);
	TestEqual(TEXT("Not after expiry"), DefenceFor(Region, 5, 160.f), 1.f);

	TestEqual(TEXT("The dock is ready with the Data and no cooldown"), Dock(100.f, 90.f, DataCost).State, EDockState::Ready);
	const FDock Cooling = Dock(100.f, 147.f, 0);
	TestEqual(TEXT("The dock shows the cooldown before the Data shortfall"), Cooling.State, EDockState::Cooldown);
	TestEqual(TEXT("with the seconds left"), Cooling.CooldownLeft, 47.f);
	const FDock Short = Dock(100.f, 90.f, 28);
	TestEqual(TEXT("The dock shows the Data shortfall"), Short.State, EDockState::NeedData);
	TestEqual(TEXT("as the 12 missing"), Short.DataShort, 12);
	return true;
}

bool FFortifyDamageOrderTest::RunTest(const FString& Parameters)
{
	const float Cover = RegionTraitPolicy::CoverDamageMultiplier;
	const float Entrenched = DamagePolicy::EntrenchedIncomingMultiplier;
	const TArray<float> CoverFortify{ Cover, IncomingMultiplier };
	const TArray<float> FortifyCover{ IncomingMultiplier, Cover };
	TestEqual(TEXT("Fortify alone"), DamagePolicy::Incoming(100, { IncomingMultiplier }), 75);
	TestEqual(TEXT("Cover and Fortify multiply to x0.6"), DamagePolicy::Incoming(100, CoverFortify), 60);
	TestEqual(TEXT("The order of the list does not matter"), DamagePolicy::Incoming(100, FortifyCover), 60);
	TestEqual(TEXT("Entrenched and Fortify multiply and truncate once"), DamagePolicy::Incoming(100, { Entrenched, IncomingMultiplier }), 56);
	TestEqual(TEXT("Truncation happens once, not per step"), DamagePolicy::Incoming(7, { Cover, IncomingMultiplier }), 4);

	// Shields come after every incoming multiplier: 60 of 100 lands on a 30-point shield first.
	const DamagePolicy::FResult Shielded = DamagePolicy::Resolve(100, EDamageType::Kinetic, 30, CoverFortify);
	TestEqual(TEXT("The shield absorbs the reduced damage"), Shielded.ShieldLoss, 30);
	TestEqual(TEXT("The rest reaches health"), Shielded.HealthLoss, 30);
	// Hazard ignores Cover and Fortify alike.
	TestEqual(TEXT("Hazard takes no multiplier"), DamagePolicy::Environmental(4, 0).HealthLoss, 4);
	return true;
}

bool FFortifyTextTest::RunTest(const FString& Parameters)
{
	FDecision Decision;
	Decision.Verdict = EVerdict::HeldByEnemy;
	TestEqual(TEXT("Enemy"), Reason(Decision), FString(TEXT("Fusion Works is held by JEV")));
	Decision.Verdict = EVerdict::Neutral;
	TestEqual(TEXT("Neutral"), Reason(Decision), FString(TEXT("Fusion Works is neutral")));
	Decision.Verdict = EVerdict::NeedData;
	Decision.DataShort = 12;
	TestEqual(TEXT("Data"), Reason(Decision), FString(TEXT("Need 12 more Data")));
	Decision.Verdict = EVerdict::Cooldown;
	Decision.CooldownLeft = 46.2f;
	TestEqual(TEXT("A cooldown rounds up to the second shown"), Reason(Decision), FString(TEXT("Cooldown 0:47")));
	Decision.CooldownLeft = 90.f;
	TestEqual(TEXT("Minutes"), Reason(Decision), FString(TEXT("Cooldown 1:30")));
	Decision.Verdict = EVerdict::Accepted;
	TestEqual(TEXT("An accepted cast has no reason"), Reason(Decision), FString());

	TStringBuilder<96> Cast;
	AppendCastFeedText(Cast, 1, TEXT("Fusion Works"));
	TestEqual(TEXT("A teammate's cast row names the one-based commander"), FString(Cast.ToView()), FString(TEXT("Commander 2 fortified Fusion Works")));
	TStringBuilder<96> Ended;
	AppendEndedFeedText(Ended, TEXT("Fusion Works"));
	TestEqual(TEXT("The early end row"), FString(Ended.ToView()), FString(TEXT("Fortify at Fusion Works ended: region lost")));
	return true;
}

bool FFortifyTargetingTest::RunTest(const FString& Parameters)
{
	using namespace ControllerInputPolicy;
	TestEqual(TEXT("H arms"), FortifyStep(false, false, EFortifyInput::HKey), EFortifyStep::Arm);
	TestEqual(TEXT("H again cancels"), FortifyStep(true, false, EFortifyInput::HKey), EFortifyStep::Cancel);
	TestEqual(TEXT("H cancels even while a cast is pending"), FortifyStep(true, true, EFortifyInput::HKey), EFortifyStep::Cancel);
	TestEqual(TEXT("LMB casts"), FortifyStep(true, false, EFortifyInput::LeftClick), EFortifyStep::Cast);
	TestEqual(TEXT("A second LMB waits for the first cast's verdict"), FortifyStep(true, true, EFortifyInput::LeftClick), EFortifyStep::Wait);
	TestEqual(TEXT("LMB does nothing when not armed"), FortifyStep(false, false, EFortifyInput::LeftClick), EFortifyStep::Ignore);
	TestFalse(TEXT("The mode ends on acceptance"), FortifyStaysArmed(true, true));
	TestTrue(TEXT("It stays open on rejection"), FortifyStaysArmed(true, false));
	TestFalse(TEXT("A closed mode never reopens by itself"), FortifyStaysArmed(false, false));
	return true;
}

bool FFortifyJevScoreTest::RunTest(const FString& Parameters)
{
	using namespace JevPlanner;
	// JEV's main 0 touches two player-held regions 1 and 2, equally far and equally defended; 3 is the players' main.
	FWorld World;
	for (int32 Index = 0; Index < 4; ++Index)
		World.Regions[Index].bExists = true;
	World.Regions[0].Neighbours = (uint64(1) << 1) | (uint64(1) << 2);
	World.Regions[1].Neighbours = (uint64(1) << 0) | (uint64(1) << 3);
	World.Regions[2].Neighbours = (uint64(1) << 0) | (uint64(1) << 3);
	World.Regions[3].Neighbours = (uint64(1) << 1) | (uint64(1) << 2);
	World.Regions[0].Position = FVector(0.f, 0.f, 0.f);
	World.Regions[1].Position = FVector(1000.f, 1000.f, 0.f);
	World.Regions[2].Position = FVector(1000.f, -1000.f, 0.f);
	World.Regions[3].Position = FVector(2000.f, 0.f, 0.f);
	World.Regions[0].Controller = 5;
	World.Regions[0].bMain = true;
	World.Regions[3].Controller = 0;
	World.Regions[3].bMain = true;
	World.Regions[1].Controller = World.Regions[2].Controller = 0;
	World.Regions[1].Hostiles = World.Regions[2].Hostiles = 4;
	World.Home = 0;
	World.EnemyHome = 3;
	const float Speeds[] = { 200.f };
	FForce Force;
	Force.Source = Force.Home = 0;
	Force.UnitCount = 5;
	Force.ClassSpeeds = Speeds;

	const auto Pick = [&]() { return Choose(Propose(World, Force))->Plan.Target; };
	TestEqual(TEXT("Equal regions tie on the lower index"), Pick(), 1);
	World.Regions[1].DefenceMultiplier = FortifyPolicy::JevDefenceMultiplier;
	TestEqual(TEXT("A Fortified region scores worse, so JEV picks the other"), Pick(), 2);
	World.Regions[1].DefenceMultiplier = 1.f;
	World.Regions[2].DefenceMultiplier = FortifyPolicy::JevDefenceMultiplier;
	TestEqual(TEXT("and not merely the higher index"), Pick(), 1);

	// The multiplier scales defenders: with none, Fortify changes nothing.
	World.Regions[1].Hostiles = World.Regions[2].Hostiles = 0;
	TestEqual(TEXT("An empty Fortified region keeps the tie order"), Pick(), 1);
	return true;
}
#endif
