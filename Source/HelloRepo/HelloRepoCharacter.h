// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "HelloRepoCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class USceneComponent;
class UInputAction;
class UMaterialInterface;
struct FInputActionValue;
struct FMinimalViewInfo;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A simple player-controllable third person character
 *  Implements a controllable orbiting camera
 */
UCLASS(abstract)
class AHelloRepoCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Camera boom positioning the camera behind the character */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USpringArmComponent* CameraBoom;

	/** Follow camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FollowCamera;
	
protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category="Input")
	UInputAction* MouseLookAction;

	/** Optional Enhanced Input Action for toggling Photo Mode. If unset, defaults to binding the F key directly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputAction> PhotoModeAction;

	/** Name of the Blueprint/Actor SceneComponent used as the third-person camera anchor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|ViewTransition")
	FName TPSPointComponentName = TEXT("TPSPoint");

	/** Name of the Blueprint/Actor SceneComponent used as the first-person camera anchor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Camera|ViewTransition")
	FName FPSPointComponentName = TEXT("FPSPoint");

	/** Alpha-per-second transition speed when FilmMode smoothly lerps between third-person and first-person. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|ViewTransition", meta = (ClampMin = "0.1"))
	float CameraViewBlendSpeed = 2.5f;

	/** Ease-in-out exponent applied to the camera blend curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|ViewTransition", meta = (ClampMin = "1.0"))
	float CameraViewEaseExponent = 2.0f;

	/** When true, first-person orientation uses Controller ControlRotation (allowing full pitch/yaw) at FPSPoint location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|ViewTransition")
	bool bUseControlRotationInFPS = true;

	/** Post-process material overlaid on FollowCamera while Photo Mode is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera|PhotoMode")
	TSoftObjectPtr<UMaterialInterface> ViewfinderPostProcessMaterial;

	/** Whether Photo Mode (F key toggle) is currently active. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Camera|PhotoMode")
	bool bIsPhotoMode = false;

	/** Current normalized camera blend alpha (0.0 = TPSPoint, 1.0 = FPSPoint). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Camera|ViewTransition")
	float CameraViewAlpha = 0.0f;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> CachedTPSPoint;

	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> CachedFPSPoint;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LoadedViewfinderMaterial;

public:

	/** Constructor */
	AHelloRepoCharacter();	

	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Initialize input action bindings */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

public:

	/** Toggle Photo Mode on/off: immediately switches camera view and viewfinder post-process overlay. */
	UFUNCTION(BlueprintCallable, Category="Camera|PhotoMode")
	void TogglePhotoMode();

	/** Enter or exit Photo Mode immediately. */
	UFUNCTION(BlueprintCallable, Category="Camera|PhotoMode")
	void SetPhotoMode(bool bEnabled);

	/** Returns true while Photo Mode is active. */
	UFUNCTION(BlueprintPure, Category="Camera|PhotoMode")
	bool IsPhotoMode() const { return bIsPhotoMode; }

	/** Returns current normalized camera blend alpha (0 = TPS, 1 = FPS). */
	UFUNCTION(BlueprintPure, Category="Camera|ViewTransition")
	float GetCameraViewAlpha() const { return CameraViewAlpha; }

private:

	void InitializeCameraViewRig();
	void ResolveCameraPointComponents();
	bool HasCameraViewPoints() const;
	bool IsFilmModeActive() const;
	void SetViewfinderOverlayActive(bool bActive);
	void ApplyCameraViewTransform();
	void NotifyCameraCut();

protected:

	/** Called for movement input */
	void Move(const FInputActionValue& Value);

	/** Called for looking input */
	void Look(const FInputActionValue& Value);

public:

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles look inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoLook(float Yaw, float Pitch);

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump pressed inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

public:

	/** Returns CameraBoom subobject **/
	FORCEINLINE class USpringArmComponent* GetCameraBoom() const { return CameraBoom; }

	/** Returns FollowCamera subobject **/
	FORCEINLINE class UCameraComponent* GetFollowCamera() const { return FollowCamera; }
};

