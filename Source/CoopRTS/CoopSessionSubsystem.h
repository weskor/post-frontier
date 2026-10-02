#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopSessionSubsystem.generated.h"

class UNetDriver;

// The game instance retains the lobby across seamless match restarts.
UCLASS()
class COOPRTS_API UCoopSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	bool IsSteamAvailable() const;
	bool CanHost() const;
	bool CanInvite() const;
	bool IsBusy() const { return Operation != EOperation::Idle; }
	bool IsHosting() const { return bHosting; }
	FString GetStatus() const;
	bool IsV2Selected() const { return bV2Selected; }
	FName GetSelectedMap() const { return bV2Selected ? FName(TEXT("/Game/Maps/AvailabilityZoneV2")) : FName(TEXT("/Game/Maps/AvailabilityZone")); }
	void SelectMap(bool bV2)
	{
		if (!IsBusy())
			bV2Selected = bV2;
	}
	void Host();
	void Invite();
	void Leave(bool bQuit = false);
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	void VerifySession(const TArray<FString>& Args);
#endif

private:
	enum class EOperation : uint8
	{
		Idle,
		Creating,
		Joining,
		Destroying
	};
	enum class EAfterDestroy : uint8
	{
		None,
		Menu,
		Quit,
		Join
	};
	IOnlineSessionPtr Sessions;
	EOperation Operation = EOperation::Idle;
	EAfterDestroy AfterDestroy = EAfterDestroy::None;
	bool bHosting = false;
	bool bV2Selected = true;
	FString Message;
	FOnlineSessionSearchResult PendingInvite;
	int32 InviteUser = 0;
	FDelegateHandle CreateHandle, JoinHandle, DestroyHandle, InviteHandle;
	FDelegateHandle NetworkFailureHandle, TravelFailureHandle;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	bool bVerifyPendingRejection = false;
#endif

	void SetMessage(const FString& Text);
	void DestroyThen(EAfterDestroy Next);
	void FinishDeparture();
	void JoinPendingInvite();
	void FailToMenu(const FString& Text);
	void OnCreated(FName Name, bool bSuccess);
	void OnJoined(FName Name, EOnJoinSessionCompleteResult::Type Result);
	void OnDestroyed(FName Name, bool bSuccess);
	void OnInviteAccepted(bool bSuccess, int32 LocalUser, FUniqueNetIdPtr User, const FOnlineSessionSearchResult& Result);
	void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
};
