#include "CommandMenuGameMode.h"
#include "CommandHUD.h"
#include "CommandPlayerController.h"

ACommandMenuGameMode::ACommandMenuGameMode()
{
	PlayerControllerClass = ACommandPlayerController::StaticClass();
	HUDClass = ACommandHUD::StaticClass();
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
}
