#pragma once

#include "CoreMinimal.h"

// Replacements travel along the supply chain instead of walking (forces.md). Pure: no world, no actors.
namespace SupplyDelivery
{
constexpr double BaseDelaySeconds = 4.;
constexpr double HopDelaySeconds = 2.;

// Seconds from a finished recruit to its arrival at a force Hops regions from the producer.
double Delay(int32 Hops);

// Region hops from From to To through regions in Connected only (ForceOrders::ConnectedMask of the team).
// INDEX_NONE when either end is outside the mask or out of range, or the graph is invalid.
int32 Hops(const uint64* Neighbours, int32 RegionCount, uint64 Connected, int32 From, int32 To);

enum class ERoute : uint8
{
	Empty, // no living member: the recruit leaves the producer's exit
	Connected,
	CutOff
};
ERoute Route(bool bForceEmpty, int32 Hops);

// A paid recruit that has not joined its force yet. A waiting recruit is at the producer;
// otherwise it is in transit and arrives at ReadyAt. Its form is decided on arrival: the producer's
// recruit unit at that moment (a branch bought meanwhile applies).
struct FRecruit
{
	int32 Paid = 0;
	double ReadyAt = 0.;
	bool bWaiting = true;
};

enum class EOutcome : uint8
{
	Pending,
	Arrive, // spawn at a free formation slot of the force and join
	AtProducer // spawn at the producer's exit and join
};
// One evaluation. A cut-off route sends an in-transit recruit back to waiting; a restored route
// restarts the delay from Now.
EOutcome Advance(FRecruit& Recruit, ERoute Route, int32 Hops, double Now);

struct FCounts
{
	int32 InTransit = 0;
	int32 Waiting = 0;
};
FCounts Count(TConstArrayView<FRecruit> Recruits);
// Sum of Paid over all recruits: what the owner gets back when the producer dies.
int32 Refund(TConstArrayView<FRecruit> Recruits);
}
