#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CutActor.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class APlayerCameraManager;
class UFilmStyleSubsystem;
class ACutPoint;

/** Owns a frozen camera snapshot until explicitly activated. */
UCLASS(Blueprintable)
class HELLOREPO_API ACutActor : public AActor
{
	GENERATED_BODY()
public:
	ACutActor();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cut")
	TObjectPtr<USceneCaptureComponent2D> CaptureComponent2D;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cut")
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** Apply player camera deltas in the cut camera's local frame, preserving its offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cut")
	bool bMirrorWorld = false;

	/** Delay after a successful teleport before clearing cuts, selection and mask state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cut", meta = (ClampMin = "0.0", Units = "s"))
	float CleanupDelay = 2.0f;

	UFUNCTION(BlueprintCallable, Category = "Cut")
	void SetActiveCutActor();

	/** Configures this cut actor to preserve the transform and projection of a CutPoint. */
	void ConfigureFromCutPoint(const ACutPoint* CutPoint);

	/** Move the local player's view onto this capture. Requires Subsystem.GetFilmMode(). */
	UFUNCTION(BlueprintCallable, Category = "Cut")
	bool TeleportPlayerToCapture();

	UFUNCTION(BlueprintCallable, Category = "Cut", meta = (WorldContext = "WorldContextObject"))
	static void ClearActive(const UObject* WorldContextObject);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class UFilmStyleSubsystem;

	void CleanupAfterTeleport();
	void SetCaptureActive(bool bActive);
	bool UpdateRenderTarget();
	void InitializeSnapshot();
	void CopyCameraParameters(const APlayerCameraManager* Camera);
	FTransform PreviousPlayerView;
	bool bHasPreviousPlayerView = false;
	bool bSnapshotInitialized = false;
	bool bUsePresetTransform = false;
	bool bPreservePresetProjection = false;
	FTimerHandle InitializationTimer;
	FTimerHandle CleanupTimer;
};
