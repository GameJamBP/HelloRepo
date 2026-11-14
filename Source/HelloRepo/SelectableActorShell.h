#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FilmStyleSubsystem.h"
#include "SelectableActorShell.generated.h"

class UPrimitiveComponent;
class USceneComponent;

/**
 * Actor shell (Actor壳子) with:
 * - Selection state (bIsSelected: Selected / Unselected), manually updated via UpdateSelectionState() / SetActorSelected()
 * - SelectActor recording state (bIsSelectActorRecording: Recording / Not Recording, synced from USelectActorSubsystem::bFilmMode)
 * - Reads FSelectActorStateData from USelectActorSubsystem on Tick:
 *   1) When Selected + SelectActor is in Checkerboard or RenderTarget state -> all collisions ignore Pawn so the player can pass through freely.
 *   2) When Selected + bFilmMode is true + SelectActor is in Checkerboard state -> all renderable components become render-invisible.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Selectable Actor Shell"))
class HELLOREPO_API ASelectableActorShell : public AActor
{
	GENERATED_BODY()

public:
	ASelectableActorShell();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Default root component so child meshes and collisions can be freely added in Blueprint or Level Editor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Select Shell")
	TObjectPtr<USceneComponent> DefaultSceneRoot;

	/** Whether this Actor shell is currently selected (选中状态: 被选中 / 没被选中). Call UpdateSelectionState() after changing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select Shell", meta = (DisplayName = "Is Selected"))
	bool bIsSelected = false;

	/** CustomDepth stencil value applied to all renderable components when selected (default 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select Shell", meta = (ClampMin = "1", ClampMax = "255"))
	int32 CustomDepthStencilValue = 1;

	/** SelectActor recording state (SelectActor状态: 录制中 / 没有在被录制中, synced from Subsystem.bFilmMode on Tick). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Select Shell|State", meta = (DisplayName = "Is SelectActor Recording (bFilmMode)"))
	bool bIsSelectActorRecording = false;

	/** Latest SelectActor state data read from USelectActorSubsystem on Tick. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Select Shell|State")
	FSelectActorStateData CachedSelectActorData;

	/** True when collisions on this Actor are currently set to let the player pass through. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Select Shell|State")
	bool bIsPlayerPassThroughActive = false;

	/** Runtime diagnostic for player collision contact and teleport eligibility. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Select Shell|State")
	FString PlayerPortalStatus;

	/** Extra collision margin to detect contact even when movement stops just short of a surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Select Shell|Portal", meta = (ClampMin = "0.1", Units = "cm"))
	float PlayerContactMargin = 2.f;

	/** True when renderable components on this Actor are currently hidden due to bFilmMode + Checkerboard. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Select Shell|State")
	bool bAreRenderablesHiddenByFilmMode = false;

	/**
	 * Manually trigger an update of this Actor's selection state:
	 * - If bIsSelected is true: enables RenderCustomDepthPass and sets CustomDepthStencilValue = 1 on all renderable components.
	 * - If bIsSelected is false: disables RenderCustomDepthPass on all renderable components and restores any overridden collision/visibility.
	 */
	UFUNCTION(BlueprintCallable, Category = "Select Shell", meta = (DisplayName = "Update Selection State"))
	void UpdateSelectionState();

	/** Convenience node: sets bIsSelected and immediately calls UpdateSelectionState(). */
	UFUNCTION(BlueprintCallable, Category = "Select Shell", meta = (DisplayName = "Set Actor Selected"))
	void SetActorSelected(bool bNewSelected);

	/** Returns whether this Actor shell is currently selected. */
	UFUNCTION(BlueprintPure, Category = "Select Shell")
	bool IsActorSelected() const { return bIsSelected; }

	/** Returns whether SelectActor is currently in recording mode (Subsystem.bFilmMode == true). */
	UFUNCTION(BlueprintPure, Category = "Select Shell")
	bool IsSelectActorRecording() const { return bIsSelectActorRecording; }

private:
	void CheckPlayerPortalContact();
	FVector PreviousPlayerLocation = FVector::ZeroVector;
	bool bHasPreviousPlayerLocation = false;
	bool bPlayerWasTouching = false;
	struct FCachedCollisionState
	{
		ECollisionResponse PawnResponse = ECR_Block;
		ECollisionResponse CameraResponse = ECR_Block;
	};

	struct FCachedVisibilityState
	{
		bool bVisible = true;
		bool bHiddenInGame = false;
	};

	static bool IsRenderablePrimitive(const UPrimitiveComponent* Comp);

	void RefreshEffectsFromSubsystemState();
	void SetPlayerPassThroughEnabled(bool bEnablePassThrough);
	void SetRenderablesHiddenByFilmMode(bool bHideRenderables);

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FCachedCollisionState> CachedCollisions;

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FCachedVisibilityState> CachedVisibilities;
};
