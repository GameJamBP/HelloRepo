#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "StudioHeroAnimInstance.generated.h"

class ACharacter;
class UCharacterMovementComponent;

/**
 * Custom AnimInstance for Jackie Chan (hero) supporting 4-way directional locomotion,
 * walk/run speed blending, and jump state transitions.
 */
UCLASS(Blueprintable, BlueprintType)
class HELLOREPO_API UStudioHeroAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	TObjectPtr<ACharacter> OwnerCharacter = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	TObjectPtr<UCharacterMovementComponent> MovementComponent = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float GroundSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float ForwardSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float RightSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float DirectionAngle = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	float VerticalSpeed = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsInAir = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsMoving = false;

	UPROPERTY(BlueprintReadOnly, Category = "Locomotion")
	bool bIsRunning = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Locomotion")
	float RunSpeedThreshold = 350.0f;
};
