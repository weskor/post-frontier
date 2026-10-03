#include "ForceOrderInput.h"
#include "ForceOrderPolicy.h"

namespace ForceOrderInput
{
const TCHAR* FResult::Label() const
{
	switch (Rejection)
	{
	case ERejection::Unavailable:
		return TEXT("Not allowed: match or commander unavailable.");
	case ERejection::NoSelection:
		return TEXT("Not allowed: select your forces or a production building.");
	case ERejection::InvalidTarget:
		return TEXT("Not allowed: choose a region on ground or minimap.");
	case ERejection::NotOwned:
		return TEXT("Not allowed: every selected force must belong to you.");
	case ERejection::QueueFull:
		return TEXT("Not allowed: three orders maximum, including the active order.");
	case ERejection::Unreachable:
		return TEXT("Not allowed: region is unreachable.");
	case ERejection::Pending:
		return TEXT("Waiting for order confirmation.");
	case ERejection::None:
		break;
	}
	switch (Resolution)
	{
	case EResolution::MoveHold:
		return TEXT("Move & Hold");
	case EResolution::Attack:
		return TEXT("Attack");
	case EResolution::Rally:
		return TEXT("Set rally point");
	case EResolution::Reject:
		return TEXT("Not allowed");
	}
	return TEXT("Not allowed");
}

FResult Resolve(const FContext& Context)
{
	if (!Context.bAvailable)
		return { EResolution::Reject, ERejection::Unavailable };
	if (Context.Forces.IsEmpty() && (!Context.bProducerSelected || Context.bAttack))
		return { EResolution::Reject, ERejection::NoSelection };
	if (Context.TargetRegion < 0 || Context.TargetRegion >= Context.Graph.Num())
		return { EResolution::Reject, ERejection::InvalidTarget };
	if (Context.Forces.IsEmpty())
	{
		if (ForceOrders::NextWaypoint(Context.Graph.GetData(), Context.Graph.Num(), Context.ProducerRegion, Context.TargetRegion) == INDEX_NONE)
			return { EResolution::Reject, ERejection::Unreachable };
		return { EResolution::Rally, ERejection::None };
	}
	for (const FForce& Force : Context.Forces)
	{
		if (!Force.bOwned)
			return { EResolution::Reject, ERejection::NotOwned };
		if (!ForceOrders::CanQueue(Force.OrderCount, Context.bQueue))
			return { EResolution::Reject, ERejection::QueueFull };
		if (ForceOrders::NextWaypoint(Context.Graph.GetData(), Context.Graph.Num(), Force.SourceRegion, Context.TargetRegion) == INDEX_NONE)
			return { EResolution::Reject, ERejection::Unreachable };
	}
	return { Context.bAttack || Context.bHostileStructure ? EResolution::Attack : EResolution::MoveHold, ERejection::None };
}
}
