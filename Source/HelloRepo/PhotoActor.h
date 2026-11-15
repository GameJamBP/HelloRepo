#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhotoActor.generated.h"

class ACutActor;
class ACutPoint;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;
class UTexture;

/**
 * Interactive framed photograph linked to an ACutPoint.
 * Displays the low-resolution screen-aspect snapshot captured by its CutPoint during BeginPlay.
 * Highlights with the Hover outline when aimed at and spawns/activates a CutActor at the CutPoint when clicked.
 */
UCLASS(Blueprintable)
class HELLOREPO_API APhotoActor : public AActor
{
	GENERATED_BODY()

public:
	APhotoActor();

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Photo|Components")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Backing board / frame mesh behind the photo plane. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Photo|Components")
	TObjectPtr<UStaticMeshComponent> FrameMesh;

	/** Display plane facing +X onto which the CutPoint snapshot texture is mapped. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Photo|Components")
	TObjectPtr<UStaticMeshComponent> PhotoMesh;

	/** CutPoint that supplies the initial snapshot and target Cut transform for this photo. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Photo|Link")
	TObjectPtr<ACutPoint> LinkedCutPoint;

	/** Optional maximum distance from the interactor (cm). <= 0 allows any crosshair trace hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxInteractionDistance = 0.0f;

	/** CustomDepth stencil value matching AFilmStylePP::HoverStencilValue so crosshair hover renders the marching outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Interaction", meta = (ClampMin = "0", ClampMax = "255"))
	int32 HoverStencilValue = 2;

	/** Prompt text shown in the fixed screen-space hover popup when the crosshair targets this photo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Interaction")
	FText HoverPromptText;

	/** Prompt text shown (dimmed) when this photo's CutActor is already present in UFilmStyleSubsystem::CutActors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Interaction")
	FText AcquiredPromptText;

	/** Automatically scales PhotoMesh and FrameMesh to match the captured viewport aspect ratio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Appearance")
	bool bAutoSizeToAspectRatio = true;

	/** Vertical height of the displayed photo surface in local space (cm). Width is PhotoDisplayHeight * AspectRatio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Appearance", meta = (ClampMin = "1.0", Units = "cm"))
	float PhotoDisplayHeight = 80.0f;

	/** Border width of FrameMesh around PhotoMesh (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Appearance", meta = (ClampMin = "0.0", Units = "cm"))
	float FrameBorderSize = 4.0f;

	/** Depth/thickness of FrameMesh along local X (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Appearance", meta = (ClampMin = "0.5", Units = "cm"))
	float FrameThickness = 4.0f;

	/** Surface material with a Texture2D parameter matching TextureParameterName. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Appearance")
	TSoftObjectPtr<UMaterialInterface> PhotoMaterial;

	/** Material parameter name assigned to the CutPoint snapshot texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Photo|Appearance")
	FName TextureParameterName = TEXT("PhotoTexture");

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Photo|Appearance")
	TObjectPtr<UMaterialInstanceDynamic> PhotoDynamicMaterial;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Photo|Appearance")
	TObjectPtr<UTexture> CurrentPhotoTexture;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Photo|Appearance")
	float CurrentAspectRatio = 16.0f / 9.0f;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Photo|Interaction")
	bool bIsHovered = false;

	/** Applies a captured photo texture and updates mesh proportions to match AspectRatio. */
	UFUNCTION(BlueprintCallable, Category = "Photo|Appearance")
	void ApplyCapturedPhoto(UTexture* Texture, float AspectRatio);

	/** Updates Hover outline state (CustomDepth stencil = HoverStencilValue). */
	UFUNCTION(BlueprintCallable, Category = "Photo|Interaction")
	void SetHovered(bool bNewHovered);

	/** Returns the localized prompt text displayed when hovering this photo (defaults to "Get Cut Clip"). */
	UFUNCTION(BlueprintPure, Category = "Photo|Interaction")
	const FText& GetHoverPromptText() const { return HoverPromptText; }

	/** Returns the localized prompt text displayed when this photo's CutActor is already in the Cut list (defaults to "Already Acquired"). */
	UFUNCTION(BlueprintPure, Category = "Photo|Interaction")
	const FText& GetAcquiredPromptText() const { return AcquiredPromptText; }

	/** Returns true when LinkedCutPoint has a spawned CutActor currently registered in UFilmStyleSubsystem::CutActors. */
	UFUNCTION(BlueprintPure, Category = "Photo|Interaction")
	bool IsAlreadyAcquiredInCutList() const;

	/** Resolves the current hover prompt text and whether the popup should render dimmed/grayed. */
	UFUNCTION(BlueprintPure, Category = "Photo|Interaction")
	void GetResolvedHoverPrompt(FText& OutPromptText, bool& bOutDimmed) const;

	/** Checks whether an interactor at InteractorLocation is within MaxInteractionDistance. */
	UFUNCTION(BlueprintPure, Category = "Photo|Interaction")
	bool CanInteractFromLocation(const FVector& InteractorLocation) const;

	/** Triggers interaction: activates or spawns the CutActor at LinkedCutPoint and sets it active. */
	UFUNCTION(BlueprintCallable, Category = "Photo|Interaction")
	ACutActor* Interact(AActor* Interactor = nullptr);

protected:
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintImplementableEvent, Category = "Photo|Appearance")
	void OnPhotoTextureUpdated(UTexture* Texture, float AspectRatio);

	UFUNCTION(BlueprintImplementableEvent, Category = "Photo|Interaction")
	void OnHoverStateChanged(bool bNewHovered);

	UFUNCTION(BlueprintImplementableEvent, Category = "Photo|Interaction")
	void OnPhotoInteracted(ACutActor* ActivatedCutActor, bool bSpawnedNew);

private:
	void UpdatePhotoLayout(float AspectRatio);
	void EnsurePhotoDynamicMaterial();
	void ApplyHoverVisuals(bool bEnable);
};
