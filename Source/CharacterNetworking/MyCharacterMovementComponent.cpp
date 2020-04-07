// Fill out your copyright notice in the Description page of Project Settings.


#include "MyCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "ECustomMovementMode.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

FNetworkPredictionData_Client* UMyCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		// Return our custom client prediction class instead
		UMyCharacterMovementComponent* MutableThis = const_cast<UMyCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_My(*this);
	}

	return ClientPredictionData;
}

void UMyCharacterMovementComponent::SetSprinting(bool sprinting)
{
	SprintKeyDown = sprinting;
}

bool UMyCharacterMovementComponent::BeginWallRun()
{
	// Only allow wall running to begin if the required keys are down
	if (WallRunKeysDown == true)
	{
		// Set the movement mode to wall running. UE4 will handle replicating this change to all connected clients.
		SetMovementMode(EMovementMode::MOVE_Custom, ECustomMovementMode::CMOVE_WallRunning);
		return true;
	}

	return false;
}

void UMyCharacterMovementComponent::EndWallRun()
{
	// Set the movement mode back to falling
	SetMovementMode(EMovementMode::MOVE_Falling);
}

bool UMyCharacterMovementComponent::AreRequiredWallRunKeysDown() const
{
	// Since this function is checking for input, it should only be called for locally controlled character
	if (GetPawnOwner()->IsLocallyControlled() == false)
		return false;

	// Make sure the spring key is down (the player may only wall run if he's hold sprint)
	TArray<FInputActionKeyMapping> sprintKeyMappings;
	UInputSettings::GetInputSettings()->GetActionMappingByName("Sprint", sprintKeyMappings);
	for (FInputActionKeyMapping& sprintKeyMapping : sprintKeyMappings)
	{
		if (GetPawnOwner()->GetController<APlayerController>()->IsInputKeyDown(sprintKeyMapping.Key))
		{
			return true;
		}
	}

	return false;
}

bool UMyCharacterMovementComponent::IsNextToWall(float vertical_tolerance)
{
	// Do a line trace from the player into the wall to make sure we're stil along the side of a wall
	FVector crossVector = WallRunSide == EWallRunSide::kLeft ? FVector(0.0f, 0.0f, -1.0f) : FVector(0.0f, 0.0f, 1.0f);
	FVector traceStart = GetPawnOwner()->GetActorLocation() + (WallRunDirection * 20.0f);
	FVector traceEnd = traceStart + (FVector::CrossProduct(WallRunDirection, crossVector) * 100);
	FHitResult hitResult;

	// Create a helper lambda for performing the line trace
	auto lineTrace = [&](const FVector& start, const FVector& end)
	{
		return (GetWorld()->LineTraceSingleByChannel(hitResult, start, end, ECollisionChannel::ECC_Visibility));
	};

	// If a vertical tolerance was provided we want to do two line traces - one above and one below the calculated line
	if (vertical_tolerance > FLT_EPSILON)
	{
		// If both line traces miss the wall then return false, we're not next to a wall
		if (lineTrace(FVector(traceStart.X, traceStart.Y, traceStart.Z + vertical_tolerance / 2.0f), FVector(traceEnd.X, traceEnd.Y, traceEnd.Z + vertical_tolerance / 2.0f)) == false &&
			lineTrace(FVector(traceStart.X, traceStart.Y, traceStart.Z - vertical_tolerance / 2.0f), FVector(traceEnd.X, traceEnd.Y, traceEnd.Z - vertical_tolerance / 2.0f)) == false)
		{
			return false;
		}
	}
	// If no vertical tolerance was provided we just want to do one line trace using the caclulated line
	else
	{
		// return false if the line trace misses the wall
		if (lineTrace(traceStart, traceEnd) == false)
			return false;
	}

	// Make sure we're still on the side of the wall we expect to be on
	EWallRunSide newWallRunSide;
	FindWallRunDirectionAndSide(hitResult.ImpactNormal, WallRunDirection, newWallRunSide);
	if (newWallRunSide != WallRunSide)
	{
		return false;
	}

	return true;
}

void UMyCharacterMovementComponent::FindWallRunDirectionAndSide(const FVector& surface_normal, FVector& direction, EWallRunSide& side) const
{
	FVector crossVector;

	if (FVector2D::DotProduct(FVector2D(surface_normal), FVector2D(GetPawnOwner()->GetActorRightVector())) > 0.0)
	{
		side = EWallRunSide::kRight;
		crossVector = FVector(0.0f, 0.0f, 1.0f);
	}
	else
	{
		side = EWallRunSide::kLeft;
		crossVector = FVector(0.0f, 0.0f, -1.0f);
	}

	// Find the direction parallel to the wall in the direction the player is moving
	direction = FVector::CrossProduct(surface_normal, crossVector);
}

bool UMyCharacterMovementComponent::CanSurfaceBeWallRan(const FVector& surface_normal) const
{
	// Return false if the surface normal is facing down
	if (surface_normal.Z < -0.05f)
		return false;

	FVector normalNoZ = FVector(surface_normal.X, surface_normal.Y, 0.0f);
	normalNoZ.Normalize();

	// Find the angle of the wall
	float wallAngle = FMath::Acos(FVector::DotProduct(normalNoZ, surface_normal));

	// Return true if the wall angle is less than the walkable floor angle
	return wallAngle < GetWalkableFloorAngle();
}

bool UMyCharacterMovementComponent::IsCustomMovementMode(uint8 custom_movement_mode) const
{
	return MovementMode == EMovementMode::MOVE_Custom && CustomMovementMode == custom_movement_mode;
}

void UMyCharacterMovementComponent::OnActorHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit)
{
	if (IsCustomMovementMode(ECustomMovementMode::CMOVE_WallRunning))
		return;

	// Make sure we're falling. Wall running can only begin if we're currently in the air
	if (IsFalling() == false)
		return;

	// Make sure the surface can be wall ran based on the angle of the surface that we hit
	if (CanSurfaceBeWallRan(Hit.ImpactNormal) == false)
		return;

	// Update the wall run direction and side
	FindWallRunDirectionAndSide(Hit.ImpactNormal, WallRunDirection, WallRunSide);

	// Make sure we're next to a wall
	if (IsNextToWall() == false)
		return;

	BeginWallRun();
}

void UMyCharacterMovementComponent::BeginPlay()
{
	Super::BeginPlay();

	// We don't want simulated proxies detecting their own collision
	if (GetPawnOwner()->GetLocalRole() > ROLE_SimulatedProxy)
	{
		// Bind to the OnActorHot component so we're notified when the owning actor hits something (like a wall)
		GetPawnOwner()->OnActorHit.AddDynamic(this, &UMyCharacterMovementComponent::OnActorHit);
	}
}

void UMyCharacterMovementComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (GetPawnOwner() != nullptr && GetPawnOwner()->GetLocalRole() > ROLE_SimulatedProxy)
	{
		// Unbind from all events
		GetPawnOwner()->OnActorHit.RemoveDynamic(this, &UMyCharacterMovementComponent::OnActorHit);
	}

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UMyCharacterMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Peform local only checks
	if (GetPawnOwner()->IsLocallyControlled())
	{
		if (SprintKeyDown == true)
		{
			// Only set WantsToSprint to true if the player is moving forward (so that he can't sprint backwards)
			FVector velocity2D = GetPawnOwner()->GetVelocity();
			FVector forward2D = GetPawnOwner()->GetActorForwardVector();
			velocity2D.Z = 0.0f;
			forward2D.Z = 0.0f;
			velocity2D.Normalize();
			forward2D.Normalize();

			WantsToSprint = FVector::DotProduct(velocity2D, forward2D) > 0.5f;
		}
		else
		{
			WantsToSprint = false;
		}

		// Update if the required wall run key(s) are being pressed
		WallRunKeysDown = AreRequiredWallRunKeysDown();
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UMyCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	/*  There are 4 custom move flags for us to use. Below is what each is currently being used for:
		FLAG_Custom_0		= 0x10, // Sprinting
		FLAG_Custom_1		= 0x20, // WallRunning
		FLAG_Custom_2		= 0x40, // Unused
		FLAG_Custom_3		= 0x80, // Unused
	*/

	// Read the values from the compressed flags
	WantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	WallRunKeysDown = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

void UMyCharacterMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	if (MovementMode == MOVE_Custom)
	{
		switch (CustomMovementMode)
		{
		// Did we just start wall running?
		case ECustomMovementMode::CMOVE_WallRunning:
		{
			// Stop current movement and constrain the character to only horizontal movement
			StopMovementImmediately();
			bConstrainToPlane = true;
			SetPlaneConstraintNormal(FVector(0.0f, 0.0f, 1.0f));
		}
		break;
		}
	}

	if (PreviousMovementMode == MOVE_Custom)
	{
		switch (PreviousCustomMode)
		{
		// Did we just finish wall running?
		case ECustomMovementMode::CMOVE_WallRunning:
		{
			// Unconstrain the character from horizontal movement
			bConstrainToPlane = false;
		}
		break;
		}
	}

	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
}

void UMyCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	// Phys* functions should only run for characters with ROLE_Authority or ROLE_AutonomousProxy. However, Unreal calls PhysCustom in
	// two seperate locations, one of which doesn't check the role, so we must check it here to prevent this code from running on simulated proxies.
	if (GetOwner()->GetLocalRole() == ROLE_SimulatedProxy)
		return;

	switch (CustomMovementMode)
	{
	case ECustomMovementMode::CMOVE_WallRunning:
	{
		PhysWallRunning(deltaTime, Iterations);
		break;
	}
	}

	// Not sure if this is needed
	Super::PhysCustom(deltaTime, Iterations);
}

void UMyCharacterMovementComponent::PhysWallRunning(float deltaTime, int32 Iterations)
{
	// IMPORTANT NOTE: This function (and all other Phys* functions) will be called on characters with ROLE_Authority and ROLE_AutonomousProxy
	// but not ROLE_SimulatedProxy. All movement should be performed in this function so that is runs locally and on the server. UE4 will handle
	// replicating the final position, velocity, etc.. to the other simulated proxies.

	// Make sure the required wall run keys are still down
	if (WallRunKeysDown == false)
	{
		EndWallRun();
		return;
	}

	// Make sure we're still next to a wall. Provide a vertial tolerance for the line trace since it's possible the the server has
	// moved our character slightly since we've began the wall run. In the event we're right at the top/bottom of a wall we need this
	// tolerance value so we don't immiedetly fall of the wall 
	if (IsNextToWall(LineTraceVerticalTolerance) == false)
	{
		EndWallRun();
		return;
	}

	// Set the owning player's new velocity based on the wall run direction
	FVector newVelocity = WallRunDirection;
	newVelocity.X *= WallRunSpeed;
	newVelocity.Y *= WallRunSpeed;
	newVelocity.Z *= 0.0f;
	Velocity = newVelocity;

	const FVector Adjusted = Velocity * deltaTime;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);
}

float UMyCharacterMovementComponent::GetMaxSpeed() const
{
	switch (MovementMode)
	{
	case MOVE_Walking:
	case MOVE_NavWalking:
	{
		if (IsCrouching())
		{
			return MaxWalkSpeedCrouched;
		}
		else
		{
			if (WantsToSprint)
			{
				return SprintSpeed;
			}

			return RunSpeed;
		}
	}
	case MOVE_Falling:
		return RunSpeed;
	case MOVE_Swimming:
		return MaxSwimSpeed;
	case MOVE_Flying:
		return MaxFlySpeed;
	case MOVE_Custom:
		return MaxCustomMovementSpeed;
	case MOVE_None:
	default:
		return 0.f;
	}
}

float UMyCharacterMovementComponent::GetMaxAcceleration() const
{
	if (IsMovingOnGround())
	{
		if (WantsToSprint)
		{
			return SprintAcceleration;
		}

		return RunAcceleration;
	}

	return Super::GetMaxAcceleration();
}

void UMyCharacterMovementComponent::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
	Super::ProcessLanded(Hit, remainingTime, Iterations);

	// If we landed while wall running, make sure we stop wall running
	if (IsCustomMovementMode(ECustomMovementMode::CMOVE_WallRunning))
	{
		EndWallRun();
	}
}

void FSavedMove_My::Clear()
{
	Super::Clear();

	// Clear all values
	SavedWantsToSprint = 0;
	SavedWallRunKeysDown = 0;
}

uint8 FSavedMove_My::GetCompressedFlags() const
{
	uint8 Result = Super::GetCompressedFlags();

	/* There are 4 custom move flags for us to use. Below is what each is currently being used for:
	FLAG_Custom_0		= 0x10, // Sprinting
	FLAG_Custom_1		= 0x20, // WallRunning
	FLAG_Custom_2		= 0x40, // Unused
	FLAG_Custom_3		= 0x80, // Unused
	*/

	// Write to the compressed flags 
	if (SavedWantsToSprint)
		Result |= FLAG_Custom_0;
	if (SavedWallRunKeysDown)
		Result |= FLAG_Custom_1;

	return Result;
}

bool FSavedMove_My::CanCombineWith(const FSavedMovePtr& NewMovePtr, ACharacter* Character, float MaxDelta) const
{
	const FSavedMove_My* NewMove = static_cast<const FSavedMove_My*>(NewMovePtr.Get());

	// As an optimization, check if the engine can combine saved moves.
	if (SavedWantsToSprint != NewMove->SavedWantsToSprint ||
		SavedWallRunKeysDown != NewMove->SavedWallRunKeysDown)
	{
		return false;
	}

	return Super::CanCombineWith(NewMovePtr, Character, MaxDelta);
}

void FSavedMove_My::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

	UMyCharacterMovementComponent* charMov = static_cast<UMyCharacterMovementComponent*>(Character->GetCharacterMovement());
	if (charMov)
	{
		// Copy values into the saved move
		SavedWantsToSprint = charMov->WantsToSprint;
		SavedWallRunKeysDown = charMov->WallRunKeysDown;
	}
}

void FSavedMove_My::PrepMoveFor(class ACharacter* Character)
{
	Super::PrepMoveFor(Character);

	UMyCharacterMovementComponent* charMov = Cast<UMyCharacterMovementComponent>(Character->GetCharacterMovement());
	if (charMov)
	{
		// Copt values out of the saved move
		charMov->WantsToSprint = SavedWantsToSprint;
		charMov->WallRunKeysDown = SavedWallRunKeysDown;
	}
}

FNetworkPredictionData_Client_My::FNetworkPredictionData_Client_My(const UCharacterMovementComponent& ClientMovement)
	: Super(ClientMovement)
{

}

FSavedMovePtr FNetworkPredictionData_Client_My::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_My());
}