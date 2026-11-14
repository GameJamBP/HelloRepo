#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FilmStylePP.h"
#include "FilmStyleSubsystem.generated.h"

class ACutActor;
class ASelectableActorShell;

/**
	* Snapshot of SelectActor state stored inside USelectActorSubsystem.
	* Written by ASubsystemSelectActor and read by ASelectableActorShell on Tick.
	*/
USTRUCT(BlueprintType)
struct HELLOREPO_API FSelectActorStateData
{
	GENERATED_BODY()

	/** Whether an active ASubsystemSelectActor is currently writing to this struct. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	bool bHasValidSelectActor = false;

	/** Master enable state of SelectActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	bool bEnable = false;

	/** CustomDepth stencil value matched by SelectActor (default 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	int32 StencilValue = 1;

	/** Whether Outline is enabled on SelectActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	bool bEnableOutline = false;

	/** Whether FilmMask is enabled on SelectActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	bool bEnableFilmMask = false;

	/** Current FilmMask mode on SelectActor (None, RenderTarget, SolidColor, Checkerboard, ColorGrading). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	ESelectFilmMaskMode FilmMaskMode = ESelectFilmMaskMode::None;

	/** Whether FilmMode is currently enabled on SelectActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	bool bEnableFilmMode = false;

	/** Current FilmBlackBarAmount (0..1) on SelectActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	float FilmBlackBarAmount = 0.0f;

	/** Current FilmGradingAmount (0..1) on SelectActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	float FilmGradingAmount = 0.0f;

	/** Returns true when SelectActor is active and in Checkerboard (棋盘格) state. */
	bool IsCheckerboardState() const
	{
		return bHasValidSelectActor && bEnable && bEnableFilmMask && (FilmMaskMode == ESelectFilmMaskMode::Checkerboard);
	}

	/** Returns true when SelectActor is active and in RenderTarget state. */
	bool IsRenderTargetState() const
	{
		return bHasValidSelectActor && bEnable && bEnableFilmMask && (FilmMaskMode == ESelectFilmMaskMode::RenderTarget);
	}

	/** Returns true when SelectActor is in Checkerboard or RenderTarget state (player can pass through). */
	bool IsPassThroughState() const
	{
		return IsCheckerboardState() || IsRenderTargetState();
	}
};

/**
	* World Subsystem that pure-stores shared state between ASubsystemSelectActor and ASelectableActorShell.
	* Does not hold direct actor logic; ASubsystemSelectActor and ASelectableActorShell read/write data here.
	*/
UCLASS(DisplayName = "Select Actor Subsystem")
class HELLOREPO_API UFilmStyleSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Context only; registration and activation are owned by ACutActor. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cut")
	TArray<TObjectPtr<ACutActor>> CutActors;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Cut")
	TObjectPtr<ACutActor> ActiveCutActor;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Selection")
	TWeakObjectPtr<ASelectableActorShell> TemporaryActiveActor;

	void SetTemporaryActiveActor(ASelectableActorShell* Actor);

	/** Struct holding the current SelectActor state written by ASubsystemSelectActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select Subsystem")
	FSelectActorStateData SelectActorData;

	/** Whether FilmMode (录制模式) is enabled. When true, ASubsystemSelectActor enables FilmMode and interpolates AB/CD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select Subsystem")
	bool bFilmMode = false;

	/** Set bFilmMode (录制状态) in the subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Select Subsystem")
	void SetFilmMode(bool bInFilmMode);

	/** Get bFilmMode (录制状态) from the subsystem. */
	UFUNCTION(BlueprintPure, Category = "Select Subsystem")
	bool GetFilmMode() const { return bFilmMode; }

	/** Write SelectActor state data into the subsystem. */
	UFUNCTION(BlueprintCallable, Category = "Select Subsystem")
	void SetSelectActorData(const FSelectActorStateData& InData);

	/** Read SelectActor state data from the subsystem. */
	UFUNCTION(BlueprintPure, Category = "Select Subsystem")
	const FSelectActorStateData& GetSelectActorData() const { return SelectActorData; }
};
