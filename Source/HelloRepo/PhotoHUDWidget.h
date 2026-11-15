#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PhotoHUDWidget.generated.h"

class ACutActor;
class UTexture;

/**
 * C++ base class for WBP_PhotoHUD (Change View / Photo Mode HUD).
 * Synchronizes the embedded WBP_CutPreview widget with UFilmStyleSubsystem::ActiveCutActor
 * both on Construct and live whenever the active cut changes.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Photo HUD Widget"))
class HELLOREPO_API UPhotoHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Updates the embedded WBP_CutPreview widget to display CutActor's RenderTarget (or hides when empty). */
	UFUNCTION(BlueprintCallable, Category = "Photo HUD")
	void SyncCutPreview(ACutActor* CutActor);

	/** Resolves the child WBP_CutPreview widget inside this HUD's widget tree. */
	UFUNCTION(BlueprintPure, Category = "Photo HUD")
	UUserWidget* GetCutPreviewWidget() const;

#if WITH_EDITOR
	/** Editor utility: reparents WBP_PhotoHUD, copies Photo Preview UI from WBP_HUD, maps scroll cut inputs in IMC_PhotoMode, and saves. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Photo HUD|Editor")
	static bool EditorSyncPhotoHUDAssets();
#endif

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UUserWidget> WBP_CutPreview;

private:
	void HandleActiveCutActorChanged(ACutActor* PreviousCutActor, ACutActor* NewCutActor);
	void ApplyTextureToCutPreview(UUserWidget* PreviewWidget, UTexture* CutTexture);

	FDelegateHandle ActiveCutActorChangedHandle;
};
