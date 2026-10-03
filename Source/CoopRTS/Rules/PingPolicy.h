#pragma once

#include "CoreMinimal.h"

namespace PingPolicy
{
constexpr double CooldownSeconds = 2.;
constexpr float LifetimeSeconds = 6.f;

class FThrottle
{
public:
	bool Accept(double Now)
	{
		if (bSent && Now - LastAccepted < CooldownSeconds)
			return false;
		LastAccepted = Now;
		bSent = true;
		return true;
	}
private:
	double LastAccepted = 0.;
	bool bSent = false;
};
}
