#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PhotoHoverPromptWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UHorizontalBox;
class UImage;
class USizeBox;
class UTextBlock;
class UTexture2D;

/**
 * Fixed screen-space popup displayed while the crosshair hovers an APhotoActor.
 * Styled to match WBP_HUD / WBP_Key and defaults to displaying "Get Cut Clip".
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Photo Hover Prompt Widget"))
class HELLOREPO_API UPhotoHoverPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPhotoHoverPromptWidget(const FObjectInitializer& ObjectInitializer);

	/** Fixed screen-space card size in Slate units (independent of 3D distance to the PhotoActor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FVector2D FixedPopupSize = FVector2D(224.0f, 48.0f);

	/** Offset from the screen-center crosshair anchor (0.5, 0.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FVector2D ScreenOffset = FVector2D(0.0f, 40.0f);

	/** Semi-transparent dark card tint aligned with WBP_HUD styling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor BackgroundColor = FLinearColor(0.03f, 0.035f, 0.045f, 0.86f);

	/** Subtle border stroke around the rounded popup card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor OutlineColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.24f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0"))
	float CornerRadius = 10.0f;

	/** Left mouse button icon matching WBP_Key (/Game/Art/UI/mouse-input-icons/mouse-left). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	TSoftObjectPtr<UTexture2D> KeyIconTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FVector2D KeyIconSize = FVector2D(22.0f, 22.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "8.0", ClampMax = "48.0"))
	float PromptFontSize = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance")
	FLinearColor PromptTextColor = FLinearColor(0.97f, 0.97f, 0.97f, 1.0f);

	/** Duration in seconds for the pop-in fade and subtle scale-in animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RevealDuration = 0.12f;

	/** Displays the popup in fixed screen space with the supplied text (defaults to "Get Cut Clip"). */
	UFUNCTION(BlueprintCallable, Category = "Photo|UI")
	void ShowPrompt(const FText& InPromptText);

	/** Hides the popup cleanly without destroying the widget instance. */
	UFUNCTION(BlueprintCallable, Category = "Photo|UI")
	void HidePrompt();

	UFUNCTION(BlueprintPure, Category = "Photo|UI")
	FText GetPromptText() const;

	UFUNCTION(BlueprintPure, Category = "Photo|UI")
	FVector2D GetFixedPopupSize() const { return FixedPopupSize; }

	USizeBox* GetPopupSizeBox() const { return PopupSizeBox; }
	UTextBlock* GetPromptTextBlock() const { return PromptTextBlock; }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> PopupSizeBox;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UBorder> PopupBorder;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ContentBox;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UImage> KeyIconImage;

	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PromptTextBlock;

private:
	void BuildDefaultWidgetTree();
	void ApplyFixedLayoutAndStyling();
	void UpdateRevealTransform(float Alpha);

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> LoadedKeyIconTexture;

	FText CurrentPromptText;
	bool bIsShowing = false;
	float RevealElapsed = 0.0f;
};
