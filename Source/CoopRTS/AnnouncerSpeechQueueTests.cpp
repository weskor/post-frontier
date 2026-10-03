#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/AnnouncerSpeechQueue.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerSpeechFreshnessTest, "CoopRTS.Rules.Announcer.SpeechFreshness",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerSpeechFreshnessTest::RunTest(const FString& Parameters)
{
	AnnouncerSpeechQueue::FQueue Queue;
	AnnouncerSpeechQueue::FLine Line;
	const FName Expired(TEXT("expired"));
	const FName Fresh(TEXT("fresh"));
	TestFalse(TEXT("Arrival at exactly twenty seconds is rejected"), Queue.Enqueue(Expired, 100.f, 120.));
	TestFalse(TEXT("An older arrival is rejected"), Queue.Enqueue(Expired, 99.f, 120.));
	TestTrue(TEXT("Rejected arrivals leave no pending speech"), Queue.IsEmpty());
	TestTrue(TEXT("Arrival just inside the age window is retained"), Queue.Enqueue(Fresh, 100.f, 119.999));
	TestTrue(TEXT("A retained line starts just inside the age window"), Queue.Dequeue(119.999, Line));
	TestEqual(TEXT("The retained line keeps its event identity"), Line.Id, Fresh);
	TestEqual(TEXT("The retained line keeps its authoritative timestamp"), Line.ServerTime, 100.f);

	Queue.Enqueue(Expired, 100.f, 100.);
	TestFalse(TEXT("A queued line is discarded at the exact expiry boundary"), Queue.Dequeue(120., Line));
	TestTrue(TEXT("Expired dequeue empties the queue"), Queue.IsEmpty());

	Queue.Enqueue(Expired, 100.f, 100.);
	Queue.Enqueue(Fresh, 110.f, 110.);
	TestTrue(TEXT("Dequeue skips an expired head and finds a fresh line"), Queue.Dequeue(125., Line));
	TestEqual(TEXT("Dequeue does not play expired speech before fresh speech"), Line.Id, Fresh);
	TestTrue(TEXT("Skipping the stale head preserves only the playable line"), Queue.IsEmpty());

	Queue.Enqueue(Expired, 100.f, 100.);
	Queue.Enqueue(Fresh, 110.f, 110.);
	TestTrue(TEXT("Fresh enqueue is accepted after older pending speech expires"), Queue.Enqueue(TEXT("newest"), 125.f, 125.));
	TestEqual(TEXT("Enqueue discards the expired pending head"), Queue.Num(), 2);
	TestFalse(TEXT("A stale arrival cannot displace existing fresh speech"), Queue.Enqueue(Expired, 100.f, 125.));
	TestEqual(TEXT("Rejecting an old arrival preserves pending count"), Queue.Num(), 2);
	TestTrue(TEXT("Existing fresh speech is still first"), Queue.Dequeue(125., Line));
	TestEqual(TEXT("Rejecting an old arrival preserves FIFO order"), Line.Id, Fresh);
	TestTrue(TEXT("New fresh speech remains next"), Queue.Dequeue(125., Line));
	TestEqual(TEXT("New fresh speech keeps its identity"), Line.Id, FName(TEXT("newest")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerSpeechRingTest, "CoopRTS.Rules.Announcer.SpeechRing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerSpeechRingTest::RunTest(const FString& Parameters)
{
	AnnouncerSpeechQueue::FQueue Queue;
	AnnouncerSpeechQueue::FLine Line;
	FName Ids[AnnouncerSpeechQueue::Capacity + 4];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ids); ++Index)
	{
		Ids[Index] = FName(*FString::Printf(TEXT("line_%d"), Index));
		Queue.Enqueue(Ids[Index], 100.f + Index * .25f, 100. + Index * .25);
	}
	TestEqual(TEXT("Overflow retains exactly sixteen pending lines"), Queue.Num(), 16);
	TestFalse(TEXT("A stale arrival cannot overflow a full fresh queue"), Queue.Enqueue(TEXT("stale"), 80.f, 105.));
	for (int32 Index = 4; Index < UE_ARRAY_COUNT(Ids); ++Index)
	{
		if (!TestTrue(TEXT("Every retained overflow line is available"), Queue.Dequeue(105., Line)))
			return false;
		TestEqual(TEXT("Overflow drops only the oldest lines and preserves exact FIFO order"), Line.Id, Ids[Index]);
		TestEqual(TEXT("Wrapped ring slots retain authoritative timestamps"), Line.ServerTime, 100.f + Index * .25f);
	}
	TestFalse(TEXT("Dropped overflow lines cannot reappear"), Queue.Dequeue(105., Line));

	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		for (int32 Index = 0; Index < 12; ++Index)
			Queue.Enqueue(Ids[Index], 110.f + Cycle, 110. + Cycle);
		for (int32 Index = 0; Index < 12; ++Index)
		{
			if (!TestTrue(TEXT("A reused ring slot remains available"), Queue.Dequeue(110. + Cycle, Line)))
				return false;
			TestEqual(TEXT("Repeated wraparound preserves FIFO order"), Line.Id, Ids[Index]);
		}
	}

	Queue.Enqueue(TEXT("old_world"), 200.f, 200.);
	Queue.Reset();
	TestFalse(TEXT("World reset removes all pending old-world speech"), Queue.Dequeue(0., Line));
	TestTrue(TEXT("A new world's lower server clock accepts new speech"), Queue.Enqueue(TEXT("new_world"), 1.f, 1.));
	TestTrue(TEXT("The new world starts its own speech"), Queue.Dequeue(1., Line));
	TestEqual(TEXT("Old world speech never survives reset"), Line.Id, FName(TEXT("new_world")));
	TestEqual(TEXT("New world speech has the new authoritative timestamp"), Line.ServerTime, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerSpeechPingPriorityTest, "CoopRTS.Rules.Announcer.SpeechPingPriority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerSpeechPingPriorityTest::RunTest(const FString& Parameters)
{
	AnnouncerSpeechQueue::FQueue Queue;
	AnnouncerSpeechQueue::FLine Line;
	const FName Pings[] = { TEXT("ping_look_here"), TEXT("ping_need_help") };
	const FName Objectives[] = { TEXT("own_hq_critical"), TEXT("enemy_hq_offline"), TEXT("region_lost") };

	for (FName Ping : Pings)
		TestFalse(TEXT("Playing speech drops pings even with no pending backlog"), Queue.Enqueue(Ping, 100.f, 100., true));
	TestFalse(TEXT("Dropped busy pings cannot play later"), Queue.Dequeue(100., Line));

	TestTrue(TEXT("An idle announcer can speak a ping"), Queue.Enqueue(Pings[0], 100.f, 100.));
	TestFalse(TEXT("A second ping cannot queue behind a pending ping"), Queue.Enqueue(Pings[1], 100.f, 100.));
	if (!TestTrue(TEXT("The idle ping starts immediately"), Queue.Dequeue(100., Line)))
		return false;
	TestEqual(TEXT("The admitted ping retains its identity"), Line.Id, Pings[0]);
	TestTrue(TEXT("A critical objective queues behind the one current ping"), Queue.Enqueue(Objectives[0], 101.f, 101., true));
	TestTrue(TEXT("A second objective retains normal admission during speech"), Queue.Enqueue(Objectives[1], 102.f, 102., true));
	for (FName Ping : Pings)
		TestFalse(TEXT("Ping traffic cannot extend the current ping's speech backlog"), Queue.Enqueue(Ping, 103.f, 103., true));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		if (!TestTrue(TEXT("An objective follows the current ping"), Queue.Dequeue(103., Line)))
			return false;
		TestEqual(TEXT("Objectives retain their FIFO order after the current ping"), Line.Id, Objectives[Index]);
	}
	TestFalse(TEXT("No ping is waiting behind the objectives"), Queue.Dequeue(103., Line));

	for (int32 Index = 0; Index < AnnouncerSpeechQueue::Capacity; ++Index)
		Queue.Enqueue(Objectives[Index % UE_ARRAY_COUNT(Objectives)], 110.f + Index * .25f, 114.);
	for (int32 Index = 0; Index < AnnouncerSpeechQueue::Capacity * 2; ++Index)
		TestFalse(TEXT("Ping traffic cannot enter a full objective backlog"), Queue.Enqueue(Pings[Index % UE_ARRAY_COUNT(Pings)], 114.f, 114.));
	for (int32 Index = 0; Index < AnnouncerSpeechQueue::Capacity; ++Index)
	{
		if (!TestTrue(TEXT("Ping traffic cannot evict any pending objective"), Queue.Dequeue(114., Line)))
			return false;
		TestEqual(TEXT("Every objective retains its exact order under ping traffic"), Line.Id, Objectives[Index % UE_ARRAY_COUNT(Objectives)]);
		TestEqual(TEXT("Every objective retains its authoritative timestamp"), Line.ServerTime, 110.f + Index * .25f);
	}
	TestFalse(TEXT("Rejected ping traffic leaves no speech backlog"), Queue.Dequeue(114., Line));
	TestTrue(TEXT("Ping speech resumes once objective speech is idle"), Queue.Enqueue(Pings[1], 115.f, 115.));
	if (!TestTrue(TEXT("The next idle ping is available"), Queue.Dequeue(115., Line)))
		return false;
	TestEqual(TEXT("Resumed speech plays the new ping rather than a dropped ping"), Line.Id, Pings[1]);
	return true;
}
#endif
