#pragma once

#include "UObject/ObjectMacros.h"

/** Custom movement modes for Characters. */
UENUM(BlueprintType)
enum ECustomMovementMode
{
	CMOVE_WallRunning   UMETA(DisplayName = "WallRunning"),
	CMOVE_MAX			UMETA(Hidden),
};
