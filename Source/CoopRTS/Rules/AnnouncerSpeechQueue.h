#pragma once

#include "CoreMinimal.h"
#include "AnnouncerPolicy.h"

namespace AnnouncerSpeechQueue
{
constexpr int32 Capacity = 16;
constexpr double MaxAgeSeconds = AnnouncerPolicy::RepeatSeconds;

struct FLine
{
	FName Id;
	float ServerTime = 0.f;
};

// Reliable announcer events arrive in server order. Only pending lines live here;
// a playing line has already been removed and cannot be interrupted by overflow.
class FQueue
{
public:
	// Ping speech is best-effort: it may start only when nothing is playing or pending.
	bool Enqueue(FName Id, float ServerTime, double Now, bool bSpeechPlaying = false);
	bool Dequeue(double Now, FLine& OutLine);
	void Reset();
	int32 Num() const { return Count; }
	bool IsEmpty() const { return Count == 0; }

private:
	FLine Lines[Capacity];
	int32 Head = 0;
	int32 Count = 0;

	void DropExpired(double Now);
	void DropOldest();
};
}
