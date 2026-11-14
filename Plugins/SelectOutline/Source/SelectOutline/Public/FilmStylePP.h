#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FilmStylePP.generated.h"

class FSelectOutlineViewExtension;
class UTexture;

/**
	* 5 display modes for FilmMask (电影遮罩) on the unoccluded custom-depth stencil region.
	*/
UENUM(BlueprintType)
enum class ESelectFilmMaskMode : uint8
{
	/** Do not draw anything inside the stencil region (keep original scene color). */
	None = 0          UMETA(DisplayName = "None (不显示)"),

	/** Replace the stencil region with a RenderTarget or Texture2D. */
	RenderTarget = 1  UMETA(DisplayName = "RenderTarget / Texture"),

	/** Fill the stencil region with a solid color. */
	SolidColor = 2    UMETA(DisplayName = "Solid Color (纯色)"),

	/** Fill the stencil region with a two-color screen-space checkerboard. */
	Checkerboard = 3  UMETA(DisplayName = "Checkerboard (棋盘格)"),

	/** Apply classic cinematic Teal & Orange color grading to the stencil region. */
	ColorGrading = 4  UMETA(DisplayName = "Color Grading (电影调色)")
};

/**
	* Place in a level to enable custom-depth stencil Outline, FilmMask (电影遮罩), and FilmMode (全局电影宽银幕黑边与复古胶片调色) for this world.
	* Automatically activates when bEnable is true.
	*/
UCLASS(Blueprintable, meta = (DisplayName = "FilmStylePP"))
class SELECTOUTLINE_API AFilmStylePP : public AActor
{
	GENERATED_BODY()

public:
	AFilmStylePP();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Push current property values to the render-thread view extension. */
	UFUNCTION(BlueprintCallable, Category = "FilmStylePP")
	void RefreshOutline();

	/** Enable or disable the Outline pass independently. */
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void SetOutlineEnabled(bool bInEnabled);

	/** Enable or disable the FilmMask (电影遮罩) independently. */
	UFUNCTION(BlueprintCallable, Category = "FilmMask")
	void SetFilmMaskEnabled(bool bInEnabled);

	/** Switch the FilmMask (电影遮罩) display mode on this actor. */
	UFUNCTION(BlueprintCallable, Category = "FilmMask")
	void SetFilmMaskMode(ESelectFilmMaskMode NewMode);

	/** Assign a Texture2D or TextureRenderTarget2D to FilmMask and optionally enable RenderTarget mode immediately. */
	UFUNCTION(BlueprintCallable, Category = "FilmMask")
	void SetReplaceTexture(UTexture* InTexture, bool bEnableReplacement = true);

	/** Enable or disable FilmMode (全局电影黑边与胶片调色) independently. */
	UFUNCTION(BlueprintCallable, Category = "FilmMode")
	void SetFilmModeEnabled(bool bInEnabled);

	/** Set FilmMode's black bar progress (0 = no bars, 1 = full aspect-ratio bars) and global grading progress (0 = ungraded, 1 = full vintage film grade). */
	UFUNCTION(BlueprintCallable, Category = "FilmMode")
	void SetFilmModeAmounts(float InBlackBarAmount, float InGradingAmount);

	// =================================================================================================================
	// 1. General & Dither Repair
	// =================================================================================================================

	/** Master toggle for this FilmStylePP. When placed in the world and enabled, active features run automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmStylePP", meta = (DisplayName = "Enable"))
	bool bEnable = true;

	/** CustomDepth Stencil value to match (1-255). Enable 'Render CustomDepth Pass' on target meshes and set their stencil to this value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmStylePP", meta = (ClampMin = "1", ClampMax = "255"))
	int32 StencilValue = 1;

	/** Enable the neighborhood confidence filter so dithered/masked materials do not spawn false internal outlines or holes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmStylePP|Dither Repair")
	bool bDitherRepair = true;

	/** Kernel radius in pixels (r => (2r+1)x(2r+1) window). 2 (= 5x5) handles standard 4x4 dither patterns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmStylePP|Dither Repair", meta = (ClampMin = "1", ClampMax = "4", EditCondition = "bDitherRepair"))
	int32 FilterRadius = 2;

	/** If a non-selected pixel's neighborhood stencil ratio >= FillConfidence and it is surrounded on both axes, fill the dither hole. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmStylePP|Dither Repair", meta = (ClampMin = "0.05", ClampMax = "1.0", EditCondition = "bDitherRepair"))
	float FillConfidence = 0.35f;

	/** If a selected pixel's neighborhood stencil ratio < KeepConfidence, discard it as stray dither noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmStylePP|Dither Repair", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bDitherRepair"))
	float KeepConfidence = 0.15f;

	// =================================================================================================================
	// 2. Outline (描边 — 独立分类与独立开关)
	// =================================================================================================================

	/** Independent toggle for the selection outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (DisplayName = "Enable Outline"))
	bool bEnableOutline = true;

	/** Outline width in output pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (ClampMin = "0.5", ClampMax = "32.0", EditCondition = "bEnableOutline"))
	float Thickness = 3.0f;

	/** Primary outline color (RGBA). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline", meta = (EditCondition = "bEnableOutline"))
	FLinearColor OutlineColor = FLinearColor(1.0f, 0.6f, 0.0f, 1.0f);

	/** Enable diagonal marching stripes along the outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Stripes", meta = (EditCondition = "bEnableOutline"))
	bool bStripes = true;

	/** Width of each solid stripe segment (in screen pixels). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Stripes", meta = (ClampMin = "1.0", ClampMax = "128.0", EditCondition = "bEnableOutline && bStripes"))
	float StripeWidth = 8.0f;

	/** Width of the gap between stripes (in screen pixels). Set to 0 for a solid line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Stripes", meta = (ClampMin = "0.0", ClampMax = "128.0", EditCondition = "bEnableOutline && bStripes"))
	float StripeGap = 8.0f;

	/** Color inside the gap between stripes (Alpha = 0 leaves the scene background visible, producing a dashed line). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Stripes", meta = (EditCondition = "bEnableOutline && bStripes"))
	FLinearColor GapColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	/** Stripe flow speed in pixels per second (positive and negative values flow in opposite directions; 0 = static). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Stripes", meta = (ClampMin = "-200.0", ClampMax = "200.0", EditCondition = "bEnableOutline && bStripes"))
	float FlowSpeed = 20.0f;

	/** Offset the stripe phase by camera yaw/pitch delta so rotating the camera does not make the stripes slide wildly across the object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline|Stripes", meta = (EditCondition = "bEnableOutline && bStripes"))
	bool bCompensateCameraRotation = true;

	// =================================================================================================================
	// 3. FilmMask (电影遮罩 — 独立分类、独立开关与 5 种显示模式)
	// =================================================================================================================

	/** Independent toggle for FilmMask (电影遮罩) inside the unoccluded stencil region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask", meta = (DisplayName = "Enable FilmMask"))
	bool bEnableFilmMask = false;

	/** Display mode inside the unoccluded stencil region (None, RenderTarget, SolidColor, Checkerboard, ColorGrading). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask", meta = (DisplayName = "FilmMask Mode", EditCondition = "bEnableFilmMask"))
	ESelectFilmMaskMode FilmMaskMode = ESelectFilmMaskMode::ColorGrading;

	/** Master opacity of the FilmMask effect (0 = original scene color, 1 = full effect). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableFilmMask && FilmMaskMode != ESelectFilmMaskMode::None"))
	float FilmMaskOpacity = 1.0f;

	/** Reverse-Z device-depth tolerance when comparing SceneDepth vs CustomDepth (occluded pixels keep the foreground scene color). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask", meta = (ClampMin = "0.0", ClampMax = "0.05", EditCondition = "bEnableFilmMask && FilmMaskMode != ESelectFilmMaskMode::None"))
	float DepthOcclusionBias = 0.0001f;

	// Mode 1: RenderTarget / Texture

	/** Texture or RenderTarget (UTexture2D / UTextureRenderTarget2D) drawn over unoccluded stencil pixels when FilmMaskMode == RenderTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|RenderTarget", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::RenderTarget"))
	TObjectPtr<UTexture> ReplaceTexture = nullptr;

	/** Ignore the replacement texture's Alpha channel (recommended for SceneCapture2D RenderTargets, which write Alpha = 0 by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|RenderTarget", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::RenderTarget"))
	bool bIgnoreReplaceAlpha = true;

	/** UV tiling of the replacement texture across the viewport. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|RenderTarget", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::RenderTarget"))
	FVector2D ReplaceUVTiling = FVector2D(1.0, 1.0);

	/** UV offset of the replacement texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|RenderTarget", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::RenderTarget"))
	FVector2D ReplaceUVOffset = FVector2D(0.0, 0.0);

	// Mode 2: Solid Color

	/** Color used when FilmMaskMode == SolidColor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Solid Color", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::SolidColor"))
	FLinearColor SolidColor = FLinearColor(0.1f, 0.6f, 1.0f, 1.0f);

	// Mode 3: Checkerboard

	/** First color of the checkerboard when FilmMaskMode == Checkerboard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Checkerboard", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::Checkerboard"))
	FLinearColor CheckerColorA = FLinearColor(0.15f, 0.15f, 0.18f, 1.0f);

	/** Second color of the checkerboard when FilmMaskMode == Checkerboard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Checkerboard", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::Checkerboard"))
	FLinearColor CheckerColorB = FLinearColor(0.85f, 0.85f, 0.90f, 1.0f);

	/** Square size of each checkerboard cell in screen pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Checkerboard", meta = (ClampMin = "1.0", ClampMax = "256.0", EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::Checkerboard"))
	float CheckerSize = 16.0f;

	// Mode 4: Classic Cinematic Color Grading

	/** Strength of the cinematic color grade when FilmMaskMode == ColorGrading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Color Grading", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::ColorGrading"))
	float GradingIntensity = 0.85f;

	/** Filmic S-curve contrast multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Color Grading", meta = (ClampMin = "0.5", ClampMax = "2.5", EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::ColorGrading"))
	float GradingContrast = 1.25f;

	/** Color saturation multiplier before split-toning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Color Grading", meta = (ClampMin = "0.0", ClampMax = "2.5", EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::ColorGrading"))
	float GradingSaturation = 1.15f;

	/** Shadow split-tone tint (classic cinema Teal by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Color Grading", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::ColorGrading"))
	FLinearColor GradingShadowTint = FLinearColor(0.12f, 0.45f, 0.62f, 1.0f);

	/** Highlight split-tone tint (classic cinema warm Orange by default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMask|Color Grading", meta = (EditCondition = "bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::ColorGrading"))
	FLinearColor GradingHighlightTint = FLinearColor(1.0f, 0.72f, 0.42f, 1.0f);

	// =================================================================================================================
	// 4. FilmMode (全局电影模式：宽银幕上下黑边 + 全局泛黄复古胶片调色)
	// =================================================================================================================

	/** Enable FilmMode (widescreen letterbox black bars + global vintage yellow film color grading across the whole screen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode", meta = (DisplayName = "Enable FilmMode"))
	bool bEnableFilmMode = false;

	/** Target widescreen aspect ratio (Width / Height, e.g. 2.39 for CinemaScope, 2.0 for Univisium, 1.85 for Academy Flat). Controls the height of top & bottom black bars. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode", meta = (ClampMin = "0.5", ClampMax = "4.0", EditCondition = "bEnableFilmMode"))
	float FilmAspectRatio = 2.39f;

	/** Smooth ease-in slider for the top & bottom black bars (0 = no black bars, 1 = full aspect-ratio black bars). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bEnableFilmMode"))
	float FilmBlackBarAmount = 1.0f;

	/** Slider for the global vintage yellow film color grading (0 = original ungraded image, 1 = full vintage film grade). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "bEnableFilmMode"))
	float FilmGradingAmount = 1.0f;

	/** Warm golden-yellow vintage film tint preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Film Grading Preset", meta = (EditCondition = "bEnableFilmMode"))
	FLinearColor FilmWarmTint = FLinearColor(1.06f, 0.93f, 0.72f, 1.0f);

	/** Faded warm sepia-brown shadow lift preset (controls vintage lifted blacks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Film Grading Preset", meta = (EditCondition = "bEnableFilmMode"))
	FLinearColor FilmShadowLift = FLinearColor(0.05f, 0.035f, 0.015f, 1.0f);

	/** Soft filmic S-curve contrast for the global film grade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Film Grading Preset", meta = (ClampMin = "0.5", ClampMax = "2.5", EditCondition = "bEnableFilmMode"))
	float FilmContrast = 1.15f;

	/** Saturation for the global film grade (0.82 gives a nostalgic slightly desaturated yellow film stock look). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Film Grading Preset", meta = (ClampMin = "0.0", ClampMax = "2.0", EditCondition = "bEnableFilmMode"))
	float FilmSaturation = 0.82f;

	/** Subtle lens vignette strength around the frame edges (0 = off, 1 = strong). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FilmMode|Film Grading Preset", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bEnableFilmMode"))
	float FilmVignette = 0.25f;

	/** Independent toggle for the selection outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline", meta = (DisplayName = "Enable Outline"))
	bool HoverbEnableOutline = true;

	/** Outline width in output pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline", meta = (ClampMin = "0.5", ClampMax = "32.0", EditCondition = "HoverbEnableOutline"))
	float HoverThickness = 3.0f;

	/** Primary outline color (RGBA). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline", meta = (EditCondition = "HoverbEnableOutline"))
	FLinearColor HoverOutlineColor = FLinearColor(1.0f, 0.6f, 0.0f, 1.0f);

	/** Enable diagonal marching stripes along the outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline|Stripes", meta = (EditCondition = "HoverbEnableOutline"))
	bool HoverbStripes = true;

	/** Width of each solid stripe segment (in screen pixels). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline|Stripes", meta = (ClampMin = "1.0", ClampMax = "128.0", EditCondition = "HoverbEnableOutline && HoverbStripes"))
	float HoverStripeWidth = 8.0f;

	/** Width of the gap between stripes (in screen pixels). Set to 0 for a solid line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline|Stripes", meta = (ClampMin = "0.0", ClampMax = "128.0", EditCondition = "HoverbEnableOutline && HoverbStripes"))
	float HoverStripeGap = 8.0f;

	/** Color inside the gap between stripes (Alpha = 0 leaves the scene background visible, producing a dashed line). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline|Stripes", meta = (EditCondition = "HoverbEnableOutline && HoverbStripes"))
	FLinearColor HoverGapColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	/** Stripe flow speed in pixels per second (positive and negative values flow in opposite directions; 0 = static). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline|Stripes", meta = (ClampMin = "-200.0", ClampMax = "200.0", EditCondition = "HoverbEnableOutline && HoverbStripes"))
	float HoverFlowSpeed = 20.0f;

	/** Offset the stripe phase by camera yaw/pitch delta so rotating the camera does not make the stripes slide wildly across the object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline|Stripes", meta = (EditCondition = "HoverbEnableOutline && HoverbStripes"))
	bool HoverbCompensateCameraRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hover Outline", meta = (ClampMin = "1", ClampMax = "255"))
	int32 HoverStencilValue = 2;

private:
	TSharedPtr<FSelectOutlineViewExtension, ESPMode::ThreadSafe> HoverViewExtension;
	void EnsureExtension();
	void ReleaseExtension();

	TSharedPtr<FSelectOutlineViewExtension, ESPMode::ThreadSafe> ViewExtension;
};

/**
	* Global Blueprint nodes to control Outline, FilmMask (电影遮罩), and FilmMode (全局电影模式) from any graph without an actor reference.
	*/
UCLASS()
class SELECTOUTLINE_API USelectOutlineBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ---- Global Outline Nodes ----

	/** Globally enable or disable the selection Outline pass. */
	UFUNCTION(BlueprintCallable, Category = "Select Outline|Outline", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Select Outline Enabled"))
	static void SetSelectOutlineEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** Return whether the selection Outline pass is currently enabled. */
	UFUNCTION(BlueprintPure, Category = "Select Outline|Outline", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Select Outline Enabled"))
	static bool IsSelectOutlineEnabled(const UObject* WorldContextObject);

	// ---- Global FilmMask (电影遮罩) Nodes ----

	/** Globally enable or disable FilmMask (电影遮罩). */
	UFUNCTION(BlueprintCallable, Category = "Select Outline|FilmMask", meta = (WorldContext = "WorldContextObject", DisplayName = "Set FilmMask Enabled"))
	static void SetFilmMaskEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** Return whether FilmMask (电影遮罩) is currently enabled. */
	UFUNCTION(BlueprintPure, Category = "Select Outline|FilmMask", meta = (WorldContext = "WorldContextObject", DisplayName = "Is FilmMask Enabled"))
	static bool IsFilmMaskEnabled(const UObject* WorldContextObject);

	/** Globally switch the FilmMask (电影遮罩) display mode (None, RenderTarget, SolidColor, Checkerboard, ColorGrading), and optionally enable FilmMask. */
	UFUNCTION(BlueprintCallable, Category = "Select Outline|FilmMask", meta = (WorldContext = "WorldContextObject", DisplayName = "Set FilmMask Mode"))
	static void SetFilmMaskMode(const UObject* WorldContextObject, ESelectFilmMaskMode NewMode, bool bEnableFilmMask = true);

	/** Return the current FilmMask (电影遮罩) display mode. */
	UFUNCTION(BlueprintPure, Category = "Select Outline|FilmMask", meta = (WorldContext = "WorldContextObject", DisplayName = "Get FilmMask Mode"))
	static ESelectFilmMaskMode GetFilmMaskMode(const UObject* WorldContextObject);

	/** Globally set FilmMask's solid color (and optionally switch to SolidColor mode & enable FilmMask). */
	UFUNCTION(BlueprintCallable, Category = "Select Outline|FilmMask", meta = (WorldContext = "WorldContextObject", DisplayName = "Set FilmMask Solid Color"))
	static void SetFilmMaskSolidColor(const UObject* WorldContextObject, FLinearColor NewColor, bool bActivateMode = true);

	/** Globally set FilmMask's checkerboard colors & cell size (and optionally switch to Checkerboard mode & enable FilmMask). */
	UFUNCTION(BlueprintCallable, Category = "Select Outline|FilmMask", meta = (WorldContext = "WorldContextObject", DisplayName = "Set FilmMask Checkerboard"))
	static void SetFilmMaskCheckerboard(const UObject* WorldContextObject, FLinearColor ColorA, FLinearColor ColorB, float CheckerSize = 16.0f, bool bActivateMode = true);

	/** Globally set FilmMask's replacement Texture / RenderTarget (and optionally switch to RenderTarget mode & enable FilmMask). */
	UFUNCTION(BlueprintCallable, Category = "Select Outline|FilmMask", meta = (WorldContext = "WorldContextObject", DisplayName = "Set FilmMask Render Target"))
	static void SetFilmMaskRenderTarget(const UObject* WorldContextObject, UTexture* InTexture, bool bActivateMode = true);

	// ---- Global FilmMode (全局电影黑边与胶片调色) Nodes ----

	/** Globally enable or disable FilmMode (widescreen letterbox black bars + global vintage yellow film grading). */
	UFUNCTION(BlueprintCallable, Category = "Select Outline|FilmMode", meta = (WorldContext = "WorldContextObject", DisplayName = "Set FilmMode Enabled"))
	static void SetFilmModeEnabled(const UObject* WorldContextObject, bool bEnabled);

	/** Return whether FilmMode is currently enabled. */
	UFUNCTION(BlueprintPure, Category = "Select Outline|FilmMode", meta = (WorldContext = "WorldContextObject", DisplayName = "Is FilmMode Enabled"))
	static bool IsFilmModeEnabled(const UObject* WorldContextObject);

	/** Globally set FilmMode's black bar slider (0..1), global film grading slider (0..1), and target aspect ratio. */
	UFUNCTION(BlueprintCallable, Category = "Select Outline|FilmMode", meta = (WorldContext = "WorldContextObject", DisplayName = "Set FilmMode Parameters"))
	static void SetFilmModeParameters(const UObject* WorldContextObject, float BlackBarAmount = 1.0f, float GradingAmount = 1.0f, float AspectRatio = 2.39f, bool bEnableFilmMode = true);
};
