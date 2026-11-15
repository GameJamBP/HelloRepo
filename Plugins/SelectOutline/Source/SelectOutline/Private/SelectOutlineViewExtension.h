#pragma once

#include "CoreMinimal.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "HAL/CriticalSection.h"
#include "RHI.h"

struct FPostProcessMaterialInputs;
class FTextureResource;

/** Plain render-side copy of ASelectActor's parameters. */
struct FSelectOutlineSettings
{
	bool bEnable = false;
	int32 StencilValue = 1;

	// ---- Outline ----
	bool bEnableOutline = true;
	float Thickness = 3.f;
	FLinearColor OutlineColor = FLinearColor(1.f, 0.6f, 0.f, 1.f);

	bool bStripes = true;
	float StripeWidth = 8.f;
	float StripeGap = 8.f;
	FLinearColor GapColor = FLinearColor(0.f, 0.f, 0.f, 0.f);
	float FlowSpeed = 20.f;
	bool bCompensateCameraRotation = true;

	// ---- Dither Repair ----
	bool bDitherRepair = true;
	int32 FilterRadius = 2;
	float FillConfidence = 0.35f;
	float KeepConfidence = 0.15f;

	// ---- FilmMask (电影遮罩 — 模板区域内 5 种显示模式) ----
	bool bEnableFilmMask = false;
	// 0 = None, 1 = RenderTarget, 2 = SolidColor, 3 = Checkerboard, 4 = ColorGrading
	int32 FilmMaskMode = 0;
	float FilmMaskOpacity = 1.f;
	float RenderTargetOpacity = 1.f;
	float DepthOcclusionBias = 0.0001f;

	// Mode 1: RenderTarget / Texture
	FTextureResource* ReplaceTextureResource = nullptr;
	bool bIgnoreReplaceAlpha = true;
	FVector2f ReplaceUVTiling = FVector2f(1.f, 1.f);
	FVector2f ReplaceUVOffset = FVector2f(0.f, 0.f);

	// Mode 2: Solid Color
	FLinearColor SolidColor = FLinearColor(0.1f, 0.6f, 1.f, 1.f);

	// Mode 3: Checkerboard
	FLinearColor CheckerColorA = FLinearColor(0.15f, 0.15f, 0.18f, 1.f);
	FLinearColor CheckerColorB = FLinearColor(0.85f, 0.85f, 0.90f, 1.f);
	float CheckerSize = 16.f;

	// Mode 4: Classic Cinematic Color Grading (Teal & Orange)
	float GradingIntensity = 0.85f;
	float GradingContrast = 1.25f;
	float GradingSaturation = 1.15f;
	FLinearColor GradingShadowTint = FLinearColor(0.12f, 0.45f, 0.62f, 1.f);
	FLinearColor GradingHighlightTint = FLinearColor(1.0f, 0.72f, 0.42f, 1.f);

	// ---- FilmMode (全局电影模式：上下宽银幕黑边 + 全局泛黄复古胶片调色) ----
	bool bEnableFilmMode = false;
	float FilmAspectRatio = 2.39f;
	float FilmBlackBarAmount = 1.f;
	float FilmGradingAmount = 1.f;
	FLinearColor FilmWarmTint = FLinearColor(1.06f, 0.93f, 0.72f, 1.f);
	FLinearColor FilmShadowLift = FLinearColor(0.05f, 0.035f, 0.015f, 1.f);
	float FilmContrast = 1.15f;
	float FilmSaturation = 0.82f;
	float FilmVignette = 0.25f;
};

/**
 * Hooks the Renderer after tonemapping and draws the outline + FilmMask + FilmMode:
 *   1. Mask      (output res) : custom stencil == StencilValue, cleaned by a confidence filter (.r) + unoccluded depth test (.g).
 *   2. DistanceX (output res) : horizontal distance to the nearest mask pixel (skipped when outline is disabled).
 *   3. Composite (output res) : FilmMask (5 modes) + outline + stripes + FilmMode (global warm vintage grading & aspect-ratio black bars).
 */
class FSelectOutlineViewExtension final : public FWorldSceneViewExtension
{
public:
	FSelectOutlineViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld);

	/** Game thread. */
	void SetSettings(const FSelectOutlineSettings& InSettings);

	//~ ISceneViewExtension
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

private:
	FScreenPassTexture PostProcess_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);

	/** Accumulated stripe shift (pixels along the stripe axis) that cancels camera rotation. Render thread. */
	double UpdateRotationCompensation(const FSceneView& View, const FIntPoint& OutputSize, double Period);

	FSelectOutlineSettings GetSettings() const;

	mutable FCriticalSection SettingsLock;
	FSelectOutlineSettings Settings;

	struct FViewRotationState
	{
		FRotator LastRotation = FRotator::ZeroRotator;
		double Offset = 0.0;
		bool bValid = false;
	};
	/** Keyed by FSceneView::GetViewKey(). Render thread only. */
	TMap<uint32, FViewRotationState> RotationStates;
};
