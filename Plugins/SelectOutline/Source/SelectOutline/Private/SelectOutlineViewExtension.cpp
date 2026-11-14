#include "SelectOutlineViewExtension.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RHIStaticStates.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneView.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "TextureResource.h"

namespace SelectOutline
{
	constexpr int32 MaxFilterRadius = 4;
	constexpr float MaxThickness = 32.f;
}

// ---------------------------------------------------------------------------------------------------------------------
// Shaders. NOTE: every pixel shader here is driven by FScreenPassVS, so its input signature in the .usf
// must start with TEXCOORD0 followed by SV_POSITION, otherwise D3D12 fails PSO creation (linkage error).
// ---------------------------------------------------------------------------------------------------------------------

class FSelectOutlineShader : public FGlobalShader
{
public:
	FSelectOutlineShader() = default;
	FSelectOutlineShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FGlobalShader(Initializer) {}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FSelectOutlineMaskPS : public FSelectOutlineShader
{
public:
	DECLARE_GLOBAL_SHADER(FSelectOutlineMaskPS);
	SHADER_USE_PARAMETER_STRUCT(FSelectOutlineMaskPS, FSelectOutlineShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, StencilTexture)
		SHADER_PARAMETER(uint32, StencilValue)
		SHADER_PARAMETER(int32, FilterRadius)
		SHADER_PARAMETER(float, FillConfidence)
		SHADER_PARAMETER(float, KeepConfidence)
		SHADER_PARAMETER(float, DepthOcclusionBias)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectOutlineMaskPS, "/Plugin/SelectOutline/Private/SelectOutline.usf", "MaskPS", SF_Pixel);

class FSelectOutlineDistanceXPS : public FSelectOutlineShader
{
public:
	DECLARE_GLOBAL_SHADER(FSelectOutlineDistanceXPS);
	SHADER_USE_PARAMETER_STRUCT(FSelectOutlineDistanceXPS, FSelectOutlineShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaskTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearSampler)
		SHADER_PARAMETER(FVector2f, OutputSize)
		SHADER_PARAMETER(int32, SearchRadius)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectOutlineDistanceXPS, "/Plugin/SelectOutline/Private/SelectOutline.usf", "DistanceXPS", SF_Pixel);

class FSelectOutlineCompositePS : public FSelectOutlineShader
{
public:
	DECLARE_GLOBAL_SHADER(FSelectOutlineCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FSelectOutlineCompositePS, FSelectOutlineShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MaskTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DistanceXTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReplaceTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, BilinearSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, ReplaceSampler)
		SHADER_PARAMETER(FVector4f, ColorRect)            // xy = min, zw = size (pixels)
		SHADER_PARAMETER(FVector2f, ColorInvExtent)
		SHADER_PARAMETER(FVector4f, OutputRect)           // xy = min, zw = size (pixels)
		SHADER_PARAMETER(FVector4f, OutlineColor)
		SHADER_PARAMETER(FVector4f, GapColor)
		SHADER_PARAMETER(FVector4f, StripeParams)         // x = stripe width, y = gap width, z = phase (px), w = enabled
		SHADER_PARAMETER(FVector4f, ReplaceUVParams)      // xy = tiling, zw = offset
		SHADER_PARAMETER(FVector4f, SolidColor)           // rgba for SolidColor mode
		SHADER_PARAMETER(FVector4f, CheckerColorA)        // rgba for Checkerboard mode A
		SHADER_PARAMETER(FVector4f, CheckerColorB)        // rgba for Checkerboard mode B
		SHADER_PARAMETER(FVector4f, GradingShadowTint)    // rgba for FilmMask ColorGrading shadow tint
		SHADER_PARAMETER(FVector4f, GradingHighlightTint) // rgba for FilmMask ColorGrading highlight tint
		SHADER_PARAMETER(FVector4f, GradingParams)        // x = intensity, y = contrast, z = saturation, w = checkerSize
		SHADER_PARAMETER(FVector2f, FillParams)           // x = opacity, y = ignoreReplaceAlpha (0/1)
		SHADER_PARAMETER(int32, FillMode)                 // 0=None, 1=RenderTarget, 2=SolidColor, 3=Checkerboard, 4=ColorGrading
		SHADER_PARAMETER(float, Thickness)
		SHADER_PARAMETER(int32, SearchRadius)
		SHADER_PARAMETER(FVector4f, FilmModeParams)       // x = aspectRatio, y = blackBarAmount(0..1), z = gradingAmount(0..1), w = enabled(0/1)
		SHADER_PARAMETER(FVector4f, FilmModeTint)         // rgb = warm yellow tint, a = contrast
		SHADER_PARAMETER(FVector4f, FilmModeShadowLift)   // rgb = sepia shadow lift, a = vignette
		SHADER_PARAMETER(float, FilmModeSaturation)       // global film saturation
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FSelectOutlineCompositePS, "/Plugin/SelectOutline/Private/SelectOutline.usf", "CompositePS", SF_Pixel);

// ---------------------------------------------------------------------------------------------------------------------

FSelectOutlineViewExtension::FSelectOutlineViewExtension(const FAutoRegister& AutoRegister, UWorld* InWorld)
	: FWorldSceneViewExtension(AutoRegister, InWorld)
{
}

void FSelectOutlineViewExtension::SetSettings(const FSelectOutlineSettings& InSettings)
{
	FScopeLock Lock(&SettingsLock);
	Settings = InSettings;
}

FSelectOutlineSettings FSelectOutlineViewExtension::GetSettings() const
{
	FScopeLock Lock(&SettingsLock);
	return Settings;
}

bool FSelectOutlineViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return GetSettings().bEnable && FWorldSceneViewExtension::IsActiveThisFrame_Internal(Context);
}

void FSelectOutlineViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View, FPostProcessingPassDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	// After tonemapping: outline, FilmMask, and FilmMode operate in final display space.
	if (Pass == EPostProcessingPass::Tonemap && GetSettings().bEnable)
	{
		InOutPassCallbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FSelectOutlineViewExtension::PostProcess_RenderThread));
	}
}

double FSelectOutlineViewExtension::UpdateRotationCompensation(const FSceneView& View, const FIntPoint& OutputSize, double Period)
{
	if (RotationStates.Num() > 32)
	{
		RotationStates.Reset();
	}

	FViewRotationState& State = RotationStates.FindOrAdd(View.GetViewKey());
	const FRotator Rotation = View.ViewRotation;

	if (State.bValid && View.IsPerspectiveProjection())
	{
		// Focal length in output pixels: a rotation of d radians moves the image by ~ d * F pixels.
		const FMatrix& Projection = View.ViewMatrices.GetViewToClipNoAA();
		const double Fx = Projection.M[0][0] * OutputSize.X * 0.5;
		const double Fy = Projection.M[1][1] * OutputSize.Y * 0.5;

		const double DeltaYaw = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(State.LastRotation.Yaw, Rotation.Yaw));
		const double DeltaPitch = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(State.LastRotation.Pitch, Rotation.Pitch));

		// Yaw right -> the world slides left on screen; pitch up -> the world slides down (screen Y grows downwards).
		const double ShiftX = -DeltaYaw * Fx;
		const double ShiftY = DeltaPitch * Fy;

		// Stripe coordinate is (x + y) / sqrt(2): follow the image shift so stripes stay attached to the object.
		const double DeltaPhase = (ShiftX + ShiftY) * UE_INV_SQRT_2;
		State.Offset = FMath::Fmod(State.Offset + DeltaPhase, Period);
	}

	State.LastRotation = Rotation;
	State.bValid = true;
	return State.Offset;
}

FScreenPassTexture FSelectOutlineViewExtension::PostProcess_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	const FSelectOutlineSettings S = GetSettings();
	const FScreenPassTextureSlice SceneColorSlice = Inputs.GetInput(EPostProcessMaterialInput::SceneColor);
	FRDGTextureRef CustomDepth = Inputs.CustomDepthTexture;

	const bool bHasValidCustomDepth = CustomDepth != nullptr
		&& CustomDepth->HasBeenProduced()
		&& CustomDepth->Desc.Format == PF_DepthStencil
		&& View.ViewUniformBuffer.IsValid();

	const float Thickness = (S.bEnableOutline && bHasValidCustomDepth) ? FMath::Clamp(S.Thickness, 0.f, SelectOutline::MaxThickness) : 0.f;
	const int32 RequestedFilmMaskMode = (S.bEnableFilmMask && bHasValidCustomDepth) ? FMath::Clamp(S.FilmMaskMode, 0, 4) : 0;
	const bool bRunFilmMode = S.bEnableFilmMode && (S.FilmBlackBarAmount > 0.f || S.FilmGradingAmount > 0.f);

	const bool bCanRun = S.bEnable
		&& (Thickness > 0.f || RequestedFilmMaskMode != 0 || bRunFilmMode)
		&& SceneColorSlice.IsValid()
		&& IsFeatureLevelSupported(View.GetShaderPlatform(), ERHIFeatureLevel::SM5);
	if (!bCanRun)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	RDG_EVENT_SCOPE(GraphBuilder, "SelectOutline");

	const FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, SceneColorSlice);

	// The composite samples SceneColor, so it must never render into it.
	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid() || Output.Texture == SceneColor.Texture)
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, ERenderTargetLoadAction::ENoAction, TEXT("SelectOutline.Output"));
	}
	const FIntPoint OutputSize = Output.ViewRect.Size();
	if (OutputSize.X <= 0 || OutputSize.Y <= 0)
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FScreenPassVS> VertexShader(ShaderMap);
	FRHISamplerState* PointSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FRHISamplerState* BilinearSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FRHISamplerState* ReplaceSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

	const int32 SearchRadius = FMath::Max(FMath::CeilToInt(Thickness), 0) + 1;

	// 1) Mask (only needed when Outline or FilmMask is active and CustomDepth is available):
	//    .r = repaired stencil silhouette (used for outline distance transform)
	//    .g = depth-tested visible stencil mask (1 only where stencil matches AND CustomDepth is not behind SceneDepth)
	FRDGTextureRef MaskTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
	if (Thickness > 0.f || RequestedFilmMaskMode != 0)
	{
		MaskTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(OutputSize, PF_R8G8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource),
			TEXT("SelectOutline.Mask"));

		const FScreenPassTextureViewport MaskViewport(OutputSize, FIntRect(FIntPoint::ZeroValue, OutputSize));

		auto* Parameters = GraphBuilder.AllocParameters<FSelectOutlineMaskPS::FParameters>();
		Parameters->View = View.ViewUniformBuffer;
		Parameters->SceneTextures = Inputs.SceneTextures;
		Parameters->CustomDepthTexture = CustomDepth;
		Parameters->StencilTexture = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateWithPixelFormat(CustomDepth, PF_X24_G8));
		Parameters->StencilValue = (uint32)FMath::Clamp(S.StencilValue, 1, 255);
		Parameters->FilterRadius = S.bDitherRepair ? FMath::Clamp(S.FilterRadius, 1, SelectOutline::MaxFilterRadius) : 0;
		Parameters->FillConfidence = FMath::Clamp(S.FillConfidence, 0.f, 1.f);
		Parameters->KeepConfidence = FMath::Clamp(S.KeepConfidence, 0.f, 1.f);
		Parameters->DepthOcclusionBias = FMath::Max(0.f, S.DepthOcclusionBias);
		Parameters->RenderTargets[0] = FRenderTargetBinding(MaskTexture, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FSelectOutlineMaskPS> PixelShader(ShaderMap);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Mask %dx%d", OutputSize.X, OutputSize.Y), View, MaskViewport, MaskViewport, VertexShader, PixelShader, Parameters);
	}

	// 2) Horizontal distance to the mask, at output resolution (only needed when Outline is enabled).
	FRDGTextureRef DistanceXTexture = GSystemTextures.GetBlackDummy(GraphBuilder);
	if (Thickness > 0.f)
	{
		DistanceXTexture = GraphBuilder.CreateTexture(
			FRDGTextureDesc::Create2D(OutputSize, PF_R16F, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource),
			TEXT("SelectOutline.DistanceX"));

		const FScreenPassTextureViewport DistanceViewport(OutputSize, FIntRect(FIntPoint::ZeroValue, OutputSize));

		auto* Parameters = GraphBuilder.AllocParameters<FSelectOutlineDistanceXPS::FParameters>();
		Parameters->MaskTexture = MaskTexture;
		Parameters->BilinearSampler = BilinearSampler;
		Parameters->OutputSize = FVector2f(OutputSize.X, OutputSize.Y);
		Parameters->SearchRadius = SearchRadius;
		Parameters->RenderTargets[0] = FRenderTargetBinding(DistanceXTexture, ERenderTargetLoadAction::ENoAction);

		TShaderMapRef<FSelectOutlineDistanceXPS> PixelShader(ShaderMap);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("DistanceX"), View, DistanceViewport, DistanceViewport, VertexShader, PixelShader, Parameters);
	}

	// Stripe phase: flow over time + camera rotation compensation, reduced on the CPU in double precision.
	const double StripeWidth = FMath::Max(S.StripeWidth, 1.f);
	const double StripeGap = FMath::Max(S.StripeGap, 0.f);
	const double Period = StripeWidth + StripeGap;
	double Phase = 0.0;
	if (Thickness > 0.f && S.bStripes)
	{
		const double Time = View.Family ? View.Family->Time.GetRealTimeSeconds() : 0.0;
		Phase = FMath::Fmod(Time * S.FlowSpeed, Period);
		if (S.bCompensateCameraRotation)
		{
			Phase += UpdateRotationCompensation(View, OutputSize, Period);
		}
		Phase = FMath::Fmod(Phase, Period);
		if (Phase < 0.0)
		{
			Phase += Period;
		}
	}

	// Register the user-supplied Texture2D / RenderTarget2D into RDG on the render thread when FilmMaskMode == 1 (RenderTarget).
	int32 EffectiveFilmMaskMode = RequestedFilmMaskMode;
	FRDGTextureRef ReplaceRDGTexture = GSystemTextures.GetWhiteDummy(GraphBuilder);
	if (EffectiveFilmMaskMode == 1)
	{
		FTextureRHIRef ReplaceRHI;
		if (S.ReplaceTextureResource != nullptr)
		{
			ReplaceRHI = S.ReplaceTextureResource->GetTexture2DRHI();
			if (!ReplaceRHI.IsValid())
			{
				ReplaceRHI = S.ReplaceTextureResource->TextureRHI;
			}
		}

		if (ReplaceRHI.IsValid())
		{
			ReplaceRDGTexture = RegisterExternalTexture(GraphBuilder, ReplaceRHI, TEXT("SelectOutline.ReplaceTexture"));
		}
		else
		{
			EffectiveFilmMaskMode = 0;
		}
	}

	// 3) Composite (FilmMask + Outline + FilmMode).
	{
		const FIntRect ColorRect = SceneColor.ViewRect;
		const FIntPoint ColorExtent = SceneColor.Texture->Desc.Extent;

		auto* Parameters = GraphBuilder.AllocParameters<FSelectOutlineCompositePS::FParameters>();
		Parameters->SceneColorTexture = SceneColor.Texture;
		Parameters->MaskTexture = MaskTexture;
		Parameters->DistanceXTexture = DistanceXTexture;
		Parameters->ReplaceTexture = ReplaceRDGTexture;
		Parameters->PointSampler = PointSampler;
		Parameters->BilinearSampler = BilinearSampler;
		Parameters->ReplaceSampler = ReplaceSampler;
		Parameters->ColorRect = FVector4f(ColorRect.Min.X, ColorRect.Min.Y, ColorRect.Width(), ColorRect.Height());
		Parameters->ColorInvExtent = FVector2f(1.f / ColorExtent.X, 1.f / ColorExtent.Y);
		Parameters->OutputRect = FVector4f(Output.ViewRect.Min.X, Output.ViewRect.Min.Y, OutputSize.X, OutputSize.Y);
		Parameters->OutlineColor = FVector4f(S.OutlineColor);
		Parameters->GapColor = FVector4f(S.GapColor);
		Parameters->StripeParams = FVector4f(StripeWidth, StripeGap, Phase, S.bStripes && StripeGap > 0.0 ? 1.f : 0.f);
		Parameters->ReplaceUVParams = FVector4f(S.ReplaceUVTiling.X, S.ReplaceUVTiling.Y, S.ReplaceUVOffset.X, S.ReplaceUVOffset.Y);
		Parameters->SolidColor = FVector4f(S.SolidColor);
		Parameters->CheckerColorA = FVector4f(S.CheckerColorA);
		Parameters->CheckerColorB = FVector4f(S.CheckerColorB);
		Parameters->GradingShadowTint = FVector4f(S.GradingShadowTint);
		Parameters->GradingHighlightTint = FVector4f(S.GradingHighlightTint);
		Parameters->GradingParams = FVector4f(S.GradingIntensity, S.GradingContrast, S.GradingSaturation, FMath::Max(1.f, S.CheckerSize));
		Parameters->FillParams = FVector2f(FMath::Clamp(S.FilmMaskOpacity, 0.f, 1.f), S.bIgnoreReplaceAlpha ? 1.f : 0.f);
		Parameters->FillMode = EffectiveFilmMaskMode;
		Parameters->Thickness = Thickness;
		Parameters->SearchRadius = SearchRadius;
		Parameters->FilmModeParams = FVector4f(
			FMath::Clamp(S.FilmAspectRatio, 0.1f, 10.f),
			FMath::Clamp(S.FilmBlackBarAmount, 0.f, 1.f),
			FMath::Clamp(S.FilmGradingAmount, 0.f, 1.f),
			bRunFilmMode ? 1.f : 0.f);
		Parameters->FilmModeTint = FVector4f(S.FilmWarmTint.R, S.FilmWarmTint.G, S.FilmWarmTint.B, FMath::Max(0.1f, S.FilmContrast));
		Parameters->FilmModeShadowLift = FVector4f(S.FilmShadowLift.R, S.FilmShadowLift.G, S.FilmShadowLift.B, FMath::Clamp(S.FilmVignette, 0.f, 1.f));
		Parameters->FilmModeSaturation = FMath::Max(0.f, S.FilmSaturation);
		Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();

		TShaderMapRef<FSelectOutlineCompositePS> PixelShader(ShaderMap);
		AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Composite"), View, FScreenPassTextureViewport(Output), FScreenPassTextureViewport(SceneColor), VertexShader, PixelShader, Parameters);
	}

	return MoveTemp(Output);
}
