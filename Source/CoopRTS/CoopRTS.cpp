#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
namespace CoopRTSNetworkVerification
{
void Start();
void Stop();
}
namespace CoopRTSMatchSimulation
{
void Start();
void Stop();
}
#endif

class FCoopRTSModule final : public FDefaultGameModuleImpl
{
public:
	void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
		CoopRTSNetworkVerification::Start();
		CoopRTSMatchSimulation::Start();
#endif
	}
	void ShutdownModule() override
	{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
		CoopRTSMatchSimulation::Stop();
		CoopRTSNetworkVerification::Stop();
#endif
		FDefaultGameModuleImpl::ShutdownModule();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FCoopRTSModule, CoopRTS, "CoopRTS");
