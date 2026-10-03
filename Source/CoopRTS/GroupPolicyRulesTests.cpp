#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ArmyGroupPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyGroupFormationRulesTest, "CoopRTS.Rules.ArmyGroup.Formation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FArmyGroupFormationRulesTest::RunTest(const FString& Parameters)
{
	using namespace ArmyGroupPolicy;
	const FFormation Produced{ true, 6, false };
	TestEqual(TEXT("Produced slot 0 leads on the left"), FormationOffset(Produced, 0), FVector(110.f, -55.f, 0.f));
	TestEqual(TEXT("Produced slot 1 leads on the right"), FormationOffset(Produced, 1), FVector(110.f, 55.f, 0.f));
	TestEqual(TEXT("Produced slot 4 trails"), FormationOffset(Produced, 4), FVector(-110.f, -55.f, 0.f));
	TestEqual(TEXT("A smaller produced capacity shifts the anchor row"), FormationOffset({ true, 2, false }, 0), FVector(0.f, -55.f, 0.f));
	TestEqual(TEXT("Produced forces ignore the opposing flag"), FormationOffset({ true, 6, true }, 3), FormationOffset(Produced, 3));

	TestEqual(TEXT("Friendly encounter slot 0 is ahead"), FormationOffset({ false, 6, false }, 0), FVector(220.f, -140.f, 0.f));
	TestEqual(TEXT("Friendly encounter slot 3 is on the anchor row"), FormationOffset({ false, 6, false }, 3), FVector(0.f, 140.f, 0.f));
	TestEqual(TEXT("Friendly encounter slot 5 trails"), FormationOffset({ false, 6, false }, 5), FVector(-220.f, 140.f, 0.f));
	TestEqual(TEXT("An opposing encounter mirrors the rows"), FormationOffset({ false, 6, true }, 0), FVector(-220.f, -140.f, 0.f));
	TestEqual(TEXT("Encounter capacity does not move the layout"), FormationOffset({ false, 2, false }, 4), FormationOffset({ false, 6, false }, 4));

	TestEqual(TEXT("An empty mask takes slot 0"), FirstVacantSlot(0, 6), 0);
	TestEqual(TEXT("The lowest vacancy wins over later ones"), FirstVacantSlot(0b0101, 6), 1);
	TestEqual(TEXT("A hole below the top is filled"), FirstVacantSlot(0b110111, 6), 3);
	TestEqual(TEXT("A full mask has no slot"), FirstVacantSlot(0b111111, 6), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Bits at or above capacity are not slots"), FirstVacantSlot(0b111, 3), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("A zero capacity has no slot"), FirstVacantSlot(0, 0), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyGroupOwnerRulesTest, "CoopRTS.Rules.ArmyGroup.Owner",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FArmyGroupOwnerRulesTest::RunTest(const FString& Parameters)
{
	using namespace ArmyGroupPolicy;
	for (int32 Commander = 0; Commander < 5; ++Commander)
		TestTrue(TEXT("Each human commander owns team 0"), OwnerPermitted(0, 0, Commander, false));
	TestFalse(TEXT("A negative commander owns no team 0 force"), OwnerPermitted(0, 0, -1, false));
	TestFalse(TEXT("A sixth commander owns no team 0 force"), OwnerPermitted(0, 0, 5, false));
	TestFalse(TEXT("Team 0 does not accept the enemy commander"), OwnerPermitted(0, 0, -1, true));
	TestFalse(TEXT("An owner on another team never matches"), OwnerPermitted(0, 5, 0, false));
	TestTrue(TEXT("The enemy commander owns team 5"), OwnerPermitted(5, 5, -1, true));
	TestFalse(TEXT("A non-enemy owner on team 5 is rejected"), OwnerPermitted(5, 5, 0, false));
	TestFalse(TEXT("Team 5 owners must share the team"), OwnerPermitted(5, 0, -1, true));
	TestFalse(TEXT("Other teams are never owned"), OwnerPermitted(3, 3, 0, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyGroupEngagementRulesTest, "CoopRTS.Rules.ArmyGroup.Engagement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FArmyGroupEngagementRulesTest::RunTest(const FString& Parameters)
{
	using namespace ArmyGroupPolicy;
	FEngagement Engagement;
	Engagement.WeaponRange = 500.f;
	Engagement.PursuitRadius = 1000.f;
	Engagement.UnitToEnemy = FMath::Square(500.);
	Engagement.EnemyToAnchor = FMath::Square(5000.);
	Engagement.UnitToAnchor = FMath::Square(5000.);
	TestTrue(TEXT("Without an Attack order, weapon range is enough"), EngagementPermitted(Engagement));
	Engagement.UnitToEnemy = FMath::Square(501.);
	TestFalse(TEXT("Without an Attack order, nothing beyond range is engaged"), EngagementPermitted(Engagement));

	Engagement.bAttackOrder = true;
	Engagement.UnitToEnemy = FMath::Square(1400.);
	Engagement.EnemyToAnchor = FMath::Square(900.);
	Engagement.UnitToAnchor = FMath::Square(900.);
	TestTrue(TEXT("Near the anchor an Attack order reaches beyond weapon range"), EngagementPermitted(Engagement));
	Engagement.UnitToEnemy = FMath::Square(1451.);
	TestFalse(TEXT("The acquire distance caps anchor engagement"), EngagementPermitted(Engagement));
	Engagement.UnitToEnemy = FMath::Square(1400.);
	Engagement.EnemyToAnchor = FMath::Square(1001.);
	TestFalse(TEXT("An enemy outside the pursuit radius is not near the anchor"), EngagementPermitted(Engagement));
	Engagement.EnemyToAnchor = FMath::Square(900.);
	Engagement.UnitToAnchor = FMath::Square(1001.);
	TestFalse(TEXT("A unit outside the pursuit radius is not near the anchor"), EngagementPermitted(Engagement));

	Engagement.bMarching = true;
	TestFalse(TEXT("En route, enemies beyond weapon range are ignored"), EngagementPermitted(Engagement));
	Engagement.UnitToEnemy = FMath::Square(500.);
	TestTrue(TEXT("En route, enemies in weapon range are engaged"), EngagementPermitted(Engagement));
	Engagement.bMarching = false;
	TestFalse(TEXT("Only a marching force engages en route"), EngagementPermitted(Engagement));
	return true;
}
#endif
