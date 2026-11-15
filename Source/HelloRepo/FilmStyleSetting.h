#pragma once

#include "CoreMinimal.h"
#include "FilmStylePP.h"
#include "FilmStyleSetting.generated.h"

class ACutActor;
class UTextureRenderTarget2D;

/**
	* Project subclass of AFilmStylePP that writes its state into UFilmStyleSubsystem
	* and drives the FilmMode transition (AB/CD for fade-in, EF/GH for fade-out)
	* when UFilmStyleSubsystem::GetFilmMode() changes.
	*/
UCLASS(Blueprintable, meta = (DisplayName = "FilmStyleSetting"))
class HELLOREPO_API AFilmStyleSetting : public AFilmStylePP
{
	GENERATED_BODY()

public:
	AFilmStyleSetting();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Timing (in seconds) for FilmBlackBarAmount fade-in (0 -> 1) when UFilmStyleSubsystem::GetFilmMode() becomes true:
	 * X (A) = Delay in seconds before starting
	 * Y (B) = Duration in seconds to interpolate FilmBlackBarAmount from 0 to 1
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Subsystem Transition", meta = (DisplayName = "AB (A=BlackBar In Delay, B=In Duration Sec)"))
	FVector2D AB = FVector2D(0.0, 1.0);

	/**
	 * Timing (in seconds) for FilmGradingAmount fade-in (0 -> 1) when UFilmStyleSubsystem::GetFilmMode() becomes true (starts counting at the same moment as A):
	 * X (C) = Delay in seconds before starting
	 * Y (D) = Duration in seconds to interpolate FilmGradingAmount from 0 to 1
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Subsystem Transition", meta = (DisplayName = "CD (C=Grading In Delay, D=In Duration Sec)"))
	FVector2D CD = FVector2D(0.0, 1.0);

	/**
	 * Timing (in seconds) for FilmBlackBarAmount fade-out (1 -> 0) when UFilmStyleSubsystem::GetFilmMode() becomes false:
	 * X (E) = Delay in seconds before starting fade-out
	 * Y (F) = Duration in seconds to interpolate FilmBlackBarAmount from 1 to 0
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Subsystem Transition", meta = (DisplayName = "EF (E=BlackBar Out Delay, F=Out Duration Sec)"))
	FVector2D EF = FVector2D(0.0, 1.0);

	/**
	 * Timing (in seconds) for FilmGradingAmount fade-out (1 -> 0) when UFilmStyleSubsystem::GetFilmMode() becomes false (starts counting at the same moment as E):
	 * X (G) = Delay in seconds before starting fade-out
	 * Y (H) = Duration in seconds to interpolate FilmGradingAmount from 1 to 0
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Subsystem Transition", meta = (DisplayName = "GH (G=Grading Out Delay, H=Out Duration Sec)"))
	FVector2D GH = FVector2D(0.0, 1.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Subsystem Transition", meta = (DisplayName = "RT Opacity In Timing (X=In Delay, Y=In Duration Sec)"))
	FVector2D RTOpacityInTiming = FVector2D(0.0, 1.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Subsystem Transition", meta = (DisplayName = "RT Opacity Out Timing (X=Out Delay, Y=Out Duration Sec)"))
	FVector2D RTOpacityOutTiming = FVector2D(0.0, 1.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Subsystem Transition", meta = (DisplayName = "RT Opacity Values (X=Before FilmMode, Y=In FilmMode)"))
	FVector2D RTOpacityRange = FVector2D(1.0, 0.75);

	/** Push this FilmStyleSetting's current state through UFilmStyleSubsystem::SetSelectActorData(). */
	UFUNCTION(BlueprintCallable, Category = "FilmStyleSetting|Subsystem")
	void SyncStateToSubsystem();

	/** Called on cut activation/RT readiness as well as Tick. */
	UFUNCTION(BlueprintCallable, Category = "FilmStyleSetting|Subsystem")
	void RefreshActiveCutTexture();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Input")
	float PressedBrightness = 0.5f;

	UFUNCTION(BlueprintCallable, Category = "Outline|Input")
	void SetSelectionPressed(bool bPressed);

private:
	bool bSelectionPressed = false;
	FLinearColor ReleasedOutlineColor;
	FLinearColor ReleasedHoverColor;

	TWeakObjectPtr<ACutActor> LastActiveCutActor;
	TWeakObjectPtr<UTextureRenderTarget2D> LastActiveCutRenderTarget;

	float FadeInStartRTOpacity = 1.0f;
	float FadeOutStartRTOpacity = 1.0f;

	void UpdateFilmModeTransition(float DeltaSeconds, bool bSubsystemFilmMode);
	static float ComputeTransitionAlpha(float ElapsedTime, const FVector2D& TimingVec);

	bool bWasSubsystemFilmMode = false;
	bool bIsFadingOutFilmMode = false;
	float FilmModeElapsedTime = 0.0f;
	float FadeOutStartBlackBarAmount = 1.0f;
	float FadeOutStartGradingAmount = 1.0f;
};
