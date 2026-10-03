#include "AnnouncerSpeechQueue.h"

namespace AnnouncerSpeechQueue
{
bool FQueue::Enqueue(FName Id, float ServerTime, double Now, bool bSpeechPlaying)
{
	const AnnouncerPolicy::FDefinition* Definition = AnnouncerPolicy::Find(Id);
	if (Definition && Definition->bPingSpeech && (bSpeechPlaying || !IsEmpty()))
		return false;
	DropExpired(Now);
	if (Now - static_cast<double>(ServerTime) >= MaxAgeSeconds)
		return false;
	if (Count == Capacity)
		DropOldest();
	Lines[(Head + Count) % Capacity] = { Id, ServerTime };
	++Count;
	return true;
}

bool FQueue::Dequeue(double Now, FLine& OutLine)
{
	DropExpired(Now);
	if (IsEmpty())
		return false;
	OutLine = Lines[Head];
	DropOldest();
	return true;
}

void FQueue::Reset()
{
	Head = 0;
	Count = 0;
}

void FQueue::DropExpired(double Now)
{
	while (!IsEmpty() && Now - static_cast<double>(Lines[Head].ServerTime) >= MaxAgeSeconds)
		DropOldest();
}

void FQueue::DropOldest()
{
	Head = (Head + 1) % Capacity;
	--Count;
}
}
