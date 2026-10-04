#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "AIController.h"
#include "CombatTraitFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOpenSpeedWorldTest, "CoopRTS.Combat.Traits.OpenSpeed",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace CombatTraitFixture;

// Two-unit forces, one in Open ground and one on plain ground, walk the same real path on the real
// movement component. The Open force must be 15% faster, and only while the whole force is inside.
// Last, a real bulk order caps a fast Open force to the slower selected force's speed.
class FOpenSpeedScenario : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	bool Setup() override;
	bool Step(double Now) override;

private:
	static FVector At(const FVector& Ground, float X, float Y) { return Ground + FVector(X, Y, 65.f); }
	static float Speed(const AArmyUnit* Unit) { return Unit->GetCharacterMovement()->GetMaxSpeed(); }
	static void March(AArmyUnit* Unit, const FVector& Ground, float Y)
	{
		Cast<AAIController>(Unit->GetController())->MoveToLocation(Ground + FVector(400.f, Y, 0.f), 20.f);
	}
	bool OrderSelection();

	AMapRegion* Open = nullptr;
	AMapRegion* Plain = nullptr;
	FVector OpenGround = FVector::ZeroVector, PlainGround = FVector::ZeroVector;
	TWeakObjectPtr<AArmyUnit> OpenFirst, OpenSecond, PlainFirst, PlainSecond, FastFirst, FastSecond;
	float OpenPeak = 0.f, PlainPeak = 0.f;
};

bool FOpenSpeedScenario::Setup()
{
	if (!Check(Arena.PickRegions(Open, Plain) && Arena.Ground(*Open, OpenGround) && Arena.Ground(*Plain, PlainGround),
			TEXT("The map offers two roomy navigable regions for the speed fixtures")))
		return false;
	Arena.SetTrait(*Open, ERegionTrait::Open);
	FUnitSpec Walker;
	Walker.MoveSpeed = 400.f;
	FUnitSpec Runner;
	Runner.MoveSpeed = 500.f;
	const int32 Index = Arena.AddUnit(Walker), FastIndex = Arena.AddUnit(Runner);
	OpenFirst = Arena.Spawn(false, Index, OpenGround, At(OpenGround, -500.f, -100.f));
	OpenSecond = OpenFirst.IsValid() ? Arena.Join(OpenFirst.Get(), Index, OpenGround, At(OpenGround, -500.f, 100.f)) : nullptr;
	PlainFirst = Arena.Spawn(false, Index, PlainGround, At(PlainGround, -500.f, -100.f));
	PlainSecond = PlainFirst.IsValid() ? Arena.Join(PlainFirst.Get(), Index, PlainGround, At(PlainGround, -500.f, 100.f)) : nullptr;
	FastFirst = Arena.Spawn(false, FastIndex, OpenGround, At(OpenGround, 0.f, -250.f));
	FastSecond = FastFirst.IsValid() ? Arena.Join(FastFirst.Get(), FastIndex, OpenGround, At(OpenGround, 0.f, 250.f)) : nullptr;
	return Check(OpenFirst.IsValid() && OpenSecond.IsValid() && PlainFirst.IsValid() && PlainSecond.IsValid()
				&& FastFirst.IsValid() && FastSecond.IsValid(),
		TEXT("Three forces of two real units spawn"));
}

bool FOpenSpeedScenario::OrderSelection()
{
	AArmyGroup* Slow = OpenFirst->GetGroup();
	AArmyGroup* Fast = FastFirst->GetGroup();
	const int32 Region = ArmyTestSetup::TravelRegion(Slow, Arena.State->EnemyHeadquarters->GetActorLocation());
	// Real orders need the groups' own ticks, which the fixture switches off by default.
	Slow->SetActorTickEnabled(true);
	Fast->SetActorTickEnabled(true);
	const TArray<AArmyGroup*> Selection{ Slow, Fast };
	return Check(Region != INDEX_NONE && FCommandService::IssueForceOrder(Arena.Wallet.Get(), Selection, EForceVerb::MoveHold, Region).IsAccepted(),
		TEXT("A bulk MoveHold order for both Open forces is accepted"));
}

bool FOpenSpeedScenario::Step(double Now)
{
	switch (Stage)
	{
	case 0:
		if (!After(Now, .6))
			return false;
		if (!Check(FMath::IsNearlyEqual(Speed(OpenFirst.Get()), 460.f, .5f) && FMath::IsNearlyEqual(Speed(OpenSecond.Get()), 460.f, .5f)
					&& FMath::IsNearlyEqual(Speed(PlainFirst.Get()), 400.f, .5f) && FMath::IsNearlyEqual(Speed(PlainSecond.Get()), 400.f, .5f),
				TEXT("A force wholly in Open ground moves at 115% of its speed; plain ground is unchanged")))
			return true;
		Arena.Place(OpenSecond.Get(), At(PlainGround, 0.f, 300.f));
		Next(1, Now);
		return false;
	case 1:
		if (!After(Now, .6))
			return false;
		if (!Check(FMath::IsNearlyEqual(Speed(OpenFirst.Get()), 400.f, .5f) && FMath::IsNearlyEqual(Speed(OpenSecond.Get()), 400.f, .5f),
				TEXT("A force with one member off Open ground keeps a single speed, so the formation holds together")))
			return true;
		Arena.Place(OpenSecond.Get(), At(OpenGround, -500.f, 100.f));
		Next(2, Now);
		return false;
	case 2:
		if (!After(Now, .6))
			return false;
		if (!Check(FMath::IsNearlyEqual(Speed(OpenSecond.Get()), 460.f, .5f), TEXT("The bonus returns once the whole force is back in Open ground")))
			return true;
		March(OpenFirst.Get(), OpenGround, -100.f);
		March(OpenSecond.Get(), OpenGround, 100.f);
		March(PlainFirst.Get(), PlainGround, -100.f);
		March(PlainSecond.Get(), PlainGround, 100.f);
		Next(3, Now);
		return false;
	case 3:
		OpenPeak = FMath::Max3(OpenPeak, static_cast<float>(OpenFirst->GetVelocity().Size2D()), static_cast<float>(OpenSecond->GetVelocity().Size2D()));
		PlainPeak = FMath::Max3(PlainPeak, static_cast<float>(PlainFirst->GetVelocity().Size2D()), static_cast<float>(PlainSecond->GetVelocity().Size2D()));
		if (!After(Now, 1.5))
			return false;
		if (!Check(PlainPeak > 350.f && OpenPeak / PlainPeak > 1.13f && OpenPeak / PlainPeak < 1.17f,
				TEXT("A moving force in Open ground walks 15% faster than the same force on plain ground")))
			return true;
		if (!OrderSelection())
			return true;
		Next(4, Now);
		return false;
	default:
		if (!After(Now, .15))
			return false;
		// The selection runs at its slowest member's 400; Open then adds 15% to both forces (460),
		// not to the faster force's own 500 (575).
		Check(FMath::IsNearlyEqual(Speed(OpenFirst.Get()), 460.f, .5f) && FMath::IsNearlyEqual(Speed(FastFirst.Get()), 460.f, .5f)
				&& FMath::IsNearlyEqual(Speed(FastSecond.Get()), 460.f, .5f),
			TEXT("Open composes with the selection speed cap of a real bulk order"));
		return true;
	}
}
}

bool FOpenSpeedWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FOpenSpeedScenario(this));
	return true;
}

#endif
