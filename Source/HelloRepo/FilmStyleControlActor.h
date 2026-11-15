#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FilmStyleControlActor.generated.h"

class AFilmStyleSetting;

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

	// Input runs before the camera update; apply clicks after this frame's trace.
	bool bSelectionRequested = false;
};
