#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CutPoint.generated.h"

class ACutActor;
class APhotoActor;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * Pre-authored camera marker in the level that captures one low-resolution screenshot
 * matching the viewport aspect ratio during BeginPlay, pushes it onto its paired PhotoActor,
 * and permanently shuts down its SceneCaptureComponent2D afterward.
 *
 * This actor is NOT an ACutActor and never registers into UFilmStyleSubsystem::CutActors
 * until a player interacts with the paired PhotoActor.
 */
UCLASS(Blueprintable)
class HELLOREPO_API ACutPoint : public AActor
{
	GENERATED_BODY()

public:
	ACutPoint();

	/** Scene capture positioned at this CutPoint. Disabled permanently after the initial BeginPlay capture. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CutPoint|Capture")
	TObjectPtr<USceneCaptureComponent2D> CaptureComponent2D;

	/** Paired interactive PhotoActor that displays this CutPoint's initial snapshot. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "CutPoint|Link")
	TObjectPtr<APhotoActor> LinkedPhotoActor;

	/** CutActor class spawned when the paired PhotoActor is interacted with (defaults to ACutActor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CutPoint|Capture")
	TSubclassOf<ACutActor> CutActorClass;

	/** Maximum pixel dimension (long edge) for the initial low-res snapshot; short edge follows viewport aspect ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CutPoint|Capture", meta = (ClampMin = "16", UIMin = "64", UIMax = "2048"))
	int32 PhotoMaxDimension = 320;

	/** Copy FOV, projection mode and ortho width from the player camera instead of using this CutPoint's authored settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CutPoint|Capture")
	bool bInheritPlayerCameraFOV = false;

	/** Low-resolution snapshot generated once during BeginPlay. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CutPoint|Capture")
	TObjectPtr<UTextureRenderTarget2D> PhotoRenderTarget;

	/** Aspect ratio (Width / Height) of PhotoRenderTarget computed from the game viewport. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CutPoint|Capture")
	float CapturedAspectRatio = 16.0f / 9.0f;

	/** True once the single BeginPlay capture has been queued. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CutPoint|Capture")
	bool bInitialPhotoCaptured = false;

	/** True once FinalizeAndDisableCapture() has permanently turned off CaptureComponent2D. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "CutPoint|Capture")
	bool bCapturePermanentlyDisabled = false;

	/** Registers a PhotoActor consumer and immediately pushes the snapshot if already captured. */
	void RegisterPhotoActor(APhotoActor* PhotoActor);

	/**
	 * Called when a paired PhotoActor is interacted with.
	 * Re-activates the existing spawned CutActor if it is still inside UFilmStyleSubsystem::CutActors;
	 * otherwise spawns a new CutActor at this CutPoint's transform, registers it, and sets it active.
	 */
	UFUNCTION(BlueprintCallable, Category = "CutPoint|Capture")
	ACutActor* ActivateOrSpawnCutActor();

	/** Returns the last CutActor spawned by this CutPoint if it is still alive. */
	UFUNCTION(BlueprintPure, Category = "CutPoint|Capture")
	ACutActor* GetSpawnedCutActor() const { return SpawnedCutActor.Get(); }

	/** Returns true if CaptureComponent2D still has any active, ticking, visible or target state. */
	UFUNCTION(BlueprintPure, Category = "CutPoint|Capture")
	bool IsSceneCaptureActive() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void CaptureInitialPhoto();
	void OnViewportDrawnAfterCapture();
	void FinalizeAndDisableCapture();
	void NotifyPhotoConsumers();

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<APhotoActor>> RegisteredPhotoActors;

	UPROPERTY(Transient)
	TWeakObjectPtr<ACutActor> SpawnedCutActor;

	FDelegateHandle ViewportDrawnHandle;
	FTimerHandle InitializationTimer;
	FTimerHandle FinalizeFallbackTimer;
};
