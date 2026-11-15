#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FilmStyleControlActor.generated.h"

class AFilmStyleSetting;
class ACutActor;
class APhotoActor;
class ASelectableActorShell;
struct FSelectActorStateData;
class UPhotoHoverPromptWidget;

UCLASS(Blueprintable)
class HELLOREPO_API AFilmStyleControlActor : public AActor
{
	GENERATED_BODY()

public:
	AFilmStyleControlActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Control")
	TObjectPtr<AFilmStyleSetting> FilmStyleSetting;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control", meta = (ClampMin = "1"))
	float TraceDistance = 100000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Control")
	int32 ActiveCutIndex = -1;

	/** Optional override widget class for the fixed screen-space photo hover prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control|UI")
	TSubclassOf<UPhotoHoverPromptWidget> PhotoHoverPromptClass;

	/** Returns the active photo hover prompt widget instance if created. */
	UFUNCTION(BlueprintPure, Category = "Control|UI")
	UPhotoHoverPromptWidget* GetPhotoHoverPromptWidget() const { return PhotoHoverPromptWidget; }

	/** Queue selection until this frame's camera and center trace have updated. */
	UFUNCTION(BlueprintCallable, Category = "Control|Selection")
	void BeginSelection();

	UFUNCTION(BlueprintCallable, Category = "Control|Selection")
	void EndSelection();

	/** Discard queued selection and restore the released outline appearance. */
	UFUNCTION(BlueprintCallable, Category = "Control|Selection")
	void CancelSelection();

	UFUNCTION(BlueprintCallable, Category = "Control|Film Mask")
	void ToggleFilmMask();

	UFUNCTION(BlueprintCallable, Category = "Control|Cut")
	void NextCut();

	UFUNCTION(BlueprintCallable, Category = "Control|Cut")
	void PreviousCut();

private:
	/** Cycle registered Cuts independently of film mask settings. */
	void ScrollCut(int32 Direction);

	void HandleHoveredPhotoActorChanged(APhotoActor* PreviousPhoto, APhotoActor* NewPhoto);
	void HandleTemporaryActiveActorChanged(ASelectableActorShell* PreviousActor, ASelectableActorShell* NewActor);
	void HandleCutActorsChanged();
	void HandleActiveCutActorChanged(ACutActor* PreviousCutActor, ACutActor* NewCutActor);
	void HandleSelectActorDataChanged(const FSelectActorStateData& NewData);
	void HandleFilmModeChanged(bool bNewFilmMode);
	void RefreshHoverPrompt();

	UPROPERTY(Transient)
	TObjectPtr<UPhotoHoverPromptWidget> PhotoHoverPromptWidget;

	FDelegateHandle HoveredPhotoChangedHandle;
	FDelegateHandle TemporaryActiveActorChangedHandle;
	FDelegateHandle CutActorsChangedHandle;
	FDelegateHandle ActiveCutActorChangedHandle;
	FDelegateHandle SelectActorDataChangedHandle;
	FDelegateHandle FilmModeChangedHandle;

	// Input runs before the camera update; apply clicks after this frame's trace.
	bool bSelectionRequested = false;
};
