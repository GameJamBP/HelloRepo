#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CutActor.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class APlayerCameraManager;

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

	UFUNCTION(BlueprintCallable, Category = "Cut")
	void SetActiveCutActor();

	/** Move the local player's view onto this capture, preserving the pawn/camera rig. */
	UFUNCTION(BlueprintCallable, Category = "Cut")
	bool TeleportPlayerToCapture();

	UFUNCTION(BlueprintCallable, Category = "Cut", meta = (WorldContext = "WorldContextObject"))
	static void ClearActive(const UObject* WorldContextObject);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void SetCaptureActive(bool bActive);
	bool UpdateRenderTarget();
	void InitializeSnapshot();
	void CopyCameraParameters(const APlayerCameraManager* Camera);
	FTransform PreviousPlayerView;
	bool bHasPreviousPlayerView = false;
	bool bSnapshotInitialized = false;
	FTimerHandle InitializationTimer;
};
