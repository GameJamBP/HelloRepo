#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FilmStylePP.h"
#include "FilmStyleSubsystem.generated.h"

class ACutActor;
class APhotoActor;
class ASelectableActorShell;

/** Snapshot written by AFilmStyleSetting and read by selectable actors. */
USTRUCT(BlueprintType)
struct HELLOREPO_API FSelectActorStateData
{
	GENERATED_BODY()

	/** Whether an active AFilmStyleSetting is currently writing to this struct. */
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

	/** Current RenderTargetOpacity (0..1) applied when FilmMaskMode == RenderTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select State")
	float RenderTargetOpacity = 1.0f;

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

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFilmStyleActiveCutActorChanged,
	ACutActor*, PreviousCutActor, ACutActor*, NewCutActor);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnFilmStyleActiveCutActorChangedNative, ACutActor*, ACutActor*);

/** Owns the world's shared film-style state and active-cut transitions. */
UCLASS(BlueprintType, DisplayName = "Film Style Subsystem")
class HELLOREPO_API UFilmStyleSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Resolve the subsystem in the context object's world; returns null without a valid world. */
	UFUNCTION(BlueprintPure, Category = "Film Style", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Film Style Subsystem"))
	static UFilmStyleSubsystem* Get(const UObject* WorldContextObject);

	/** Read-only native view of the latest three unique registrations. */
	const TArray<TObjectPtr<ACutActor>>& GetCutActors() const { return CutActors; }

	/** Blueprint-compatible copy; UObject pointer wrappers cannot be exposed in UFUNCTIONs. */
	UFUNCTION(BlueprintGetter, Category = "Film Style|Cut", meta = (DisplayName = "Get Cut Actors"))
	TArray<ACutActor*> GetCutActorsForBlueprint() const;

	/** Replace the cuts, retaining the last three valid unique actors from this world. */
	UFUNCTION(BlueprintSetter, Category = "Film Style|Cut")
	void SetCutActors(const TArray<ACutActor*>& InCutActors);

	/** Register a cut, evicting the oldest when full. Re-registering does not change its order. */
	UFUNCTION(BlueprintCallable, Category = "Film Style|Cut")
	void RegisterCutActor(ACutActor* Actor);

	/** Removing the active cut also clears it and broadcasts the change. */
	UFUNCTION(BlueprintCallable, Category = "Film Style|Cut")
	void UnregisterCutActor(ACutActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "Film Style|Cut")
	void ClearCutActors();

	UFUNCTION(BlueprintGetter, Category = "Film Style|Cut")
	ACutActor* GetActiveCutActor() const { return ActiveCutActor.Get(); }

	/** Switch capture/texture state before notifying listeners. Null clears the active cut. */
	UFUNCTION(BlueprintSetter, Category = "Film Style|Cut")
	void SetActiveCutActor(ACutActor* Actor);

	/** Native listeners may use AddUObject/AddLambda and retain the returned delegate handle. */
	FOnFilmStyleActiveCutActorChangedNative& GetOnActiveCutActorChangedNative() { return ActiveCutActorChangedNative; }

	UFUNCTION(BlueprintPure, Category = "Film Style|Selection")
	ASelectableActorShell* GetTemporaryActiveActor() const;

	UFUNCTION(BlueprintCallable, Category = "Film Style|Selection")
	void SetTemporaryActiveActor(ASelectableActorShell* Actor);

	UFUNCTION(BlueprintPure, Category = "Film Style|Selection")
	APhotoActor* GetTemporaryHoveredPhotoActor() const;

	UFUNCTION(BlueprintCallable, Category = "Film Style|Selection")
	void SetTemporaryHoveredPhotoActor(APhotoActor* Actor);

	/** Drives the film-mode transition and gameplay state. */
	UFUNCTION(BlueprintSetter, Category = "Film Style")
	void SetFilmMode(bool bInFilmMode);

	UFUNCTION(BlueprintGetter, Category = "Film Style")
	bool GetFilmMode() const { return bFilmMode; }

	UFUNCTION(BlueprintSetter, Category = "Film Style")
	void SetSelectActorData(const FSelectActorStateData& InData);

	UFUNCTION(BlueprintGetter, Category = "Film Style")
	const FSelectActorStateData& GetSelectActorData() const { return SelectActorData; }

private:
	static constexpr int32 MaxCutActors = 3;

	void RunCutStateChange(TFunction<void()> Change);
	void ApplyCutActors(const TArray<TWeakObjectPtr<ACutActor>>& Actors);
	void ApplyRegisterCutActor(ACutActor* Actor);
	void TrimCutActorsToLimit();
	void ApplyActiveCutActor(ACutActor* Actor);
	void SyncActiveCutCaptureState();
	void RefreshActiveCutConsumers();

	UPROPERTY(Transient, BlueprintGetter = GetCutActorsForBlueprint, BlueprintSetter = SetCutActors, Category = "Film Style|Cut", meta = (AllowPrivateAccess = "true"))
	TArray<TObjectPtr<ACutActor>> CutActors;

	UPROPERTY(Transient, BlueprintGetter = GetActiveCutActor, BlueprintSetter = SetActiveCutActor, Category = "Film Style|Cut", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ACutActor> ActiveCutActor;

	// Keep weak ownership; Blueprint access uses the explicit functions above.
	UPROPERTY(Transient)
	TWeakObjectPtr<ASelectableActorShell> TemporaryActiveActor;

	UPROPERTY(Transient)
	TWeakObjectPtr<APhotoActor> TemporaryHoveredPhotoActor;

	UPROPERTY(Transient, BlueprintGetter = GetSelectActorData, BlueprintSetter = SetSelectActorData, Category = "Film Style", meta = (AllowPrivateAccess = "true"))
	FSelectActorStateData SelectActorData;

	UPROPERTY(Transient, BlueprintGetter = GetFilmMode, BlueprintSetter = SetFilmMode, Category = "Film Style", meta = (AllowPrivateAccess = "true"))
	bool bFilmMode = false;

	/** Fires once per actual change, after capture and texture state have been synchronized. */
	UPROPERTY(BlueprintAssignable, Category = "Film Style|Cut", meta = (AllowPrivateAccess = "true"))
	FOnFilmStyleActiveCutActorChanged OnActiveCutActorChanged;

	FOnFilmStyleActiveCutActorChangedNative ActiveCutActorChangedNative;
	// Serialize listener requests so both delegate broadcasts see a completed transition.
	bool bIsChangingCutState = false;
	TArray<TFunction<void()>> PendingCutStateChanges;
};
