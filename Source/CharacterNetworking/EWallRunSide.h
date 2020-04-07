#pragma once

#include "UObject/ObjectMacros.h"

UENUM(BlueprintType)
enum class EWallRunSide : uint8
{
	kLeft 	UMETA(DisplayName = "Left", ToolTip = "Running along the left side of a wall"),
	kRight	UMETA(DisplayName = "Right", ToolTip = "Running along the right side of a wall"),
};