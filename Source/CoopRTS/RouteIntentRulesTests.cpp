#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/RouteIntent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRouteIntentRulesTest, "CoopRTS.Rules.RouteIntent",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRouteIntentRulesTest::RunTest(const FString& Parameters)
{
	const uint64 Graph[] = { 6, 9, 9, 6, 0 };
	const RouteIntent::FPath Path = RouteIntent::Path(Graph, 5, 0, 3);
	TestEqual(TEXT("Shortest tied route includes source, ascending first hop, target"), Path.Count, 3);
	TestEqual(TEXT("Source"), Path.Regions[0], 0);
	TestEqual(TEXT("Ascending tie break"), Path.Regions[1], 1);
	TestEqual(TEXT("Target"), Path.Regions[2], 3);
	for (int32 Index = 0; Index + 1 < Path.Count; ++Index)
		TestEqual(TEXT("Every successive segment agrees with executor BFS"),
			Path.Regions[Index + 1], ForceOrders::NextWaypoint(Graph, 5, Path.Regions[Index], 3));
	TestEqual(TEXT("Unreachable target does not fabricate a route"), RouteIntent::Path(Graph, 5, 0, 4).Count, 0);
	TestEqual(TEXT("Invalid source"), RouteIntent::Path(Graph, 5, -1, 3).Count, 0);
	TestEqual(TEXT("Oversized graph"), RouteIntent::Path(Graph, 65, 0, 3).Count, 0);
	const FVector Anchors[] = { FVector(0., 0., 0.), FVector(100., 0., 0.), FVector(0., 100., 0.), FVector(100., 100., 0.), FVector(500., 500., 0.) };
	const FVector Centre(10., 20., 0.);
	const RouteIntent::FPolyline Line = RouteIntent::Polyline(Centre, MakeArrayView(Path.Regions, Path.Count), MakeArrayView(Anchors));
	TestEqual(TEXT("Physical centre replaces source anchor"), Line.Count, 3);
	TestEqual(TEXT("Starts at force, not region centre"), Line.Points[0], Centre);
	TestEqual(TEXT("First march waypoint"), Line.Points[1], Anchors[1]);
	TestEqual(TEXT("Final target highlight position"), Line.Points[2], Anchors[3]);
	const RouteIntent::FPolyline Securing = RouteIntent::Polyline(Centre, MakeArrayView(Path.Regions, Path.Count), MakeArrayView(Anchors), true);
	TestEqual(TEXT("Securing current region includes its anchor"), Securing.Points[1], Anchors[0]);
	const auto Next = RouteIntent::Path(Graph, 5, 3, 2);
	const auto Queued = RouteIntent::Polyline(Line.Points[Line.Count - 1], MakeArrayView(Next.Regions, Next.Count), MakeArrayView(Anchors));
	TestEqual(TEXT("Queued segment starts at active target"), Queued.Points[0], Anchors[3]);
	TestEqual(TEXT("Queued segment ends at its target"), Queued.Points[Queued.Count - 1], Anchors[2]);
	const int32 Invalid[] = { 0, 8, 3 };
	TestEqual(TEXT("Missing middle region never bridges across invalid geometry"), RouteIntent::Polyline(Centre, MakeArrayView(Invalid), MakeArrayView(Anchors)).Count, 0);
	const int32 Same[] = { 1 };
	TestEqual(TEXT("Already at target has no zero-length segment"), RouteIntent::Polyline(Anchors[1], MakeArrayView(Same), MakeArrayView(Anchors)).Count, 1);
	return true;
}
#endif
