#include "SelectActor.h"
#include "SelectOutlineViewExtension.h"
#include "Engine/Engine.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TextureResource.h"
#include "UObject/UObjectIterator.h"

namespace
{
	TOptional<bool> GGlobalOutlineEnabledOverride;
	TOptional<bool> GGlobalFilmMaskEnabledOverride;
	TOptional<ESelectFilmMaskMode> GGlobalFilmMaskModeOverride;
	TOptional<bool> GGlobalFilmModeEnabledOverride;

	template <typename FuncType>
	void ForEachSelectActor(const UObject* WorldContextObject, FuncType&& Func)
	{
		UWorld* TargetWorld = nullptr;
		if (WorldContextObject && GEngine)
		{
			TargetWorld = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		}

		if (TargetWorld)
		{
			for (TActorIterator<ASelectActor> It(TargetWorld); It; ++It)
			{
				if (ASelectActor* Actor = *It)
				{
					Func(Actor);
				}
			}
			return;
		}

		for (TObjectIterator<ASelectActor> It; It; ++It)
		{
			ASelectActor* Actor = *It;
			if (IsValid(Actor) && !Actor->HasAnyFlags(RF_ClassDefaultObject) && Actor->GetWorld())
			{
				Func(Actor);
			}
		}
	}
}

ASelectActor::ASelectActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetHidden(true);
	SetCanBeDamaged(false);
}

void ASelectActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshOutline();
}

void ASelectActor::BeginPlay()
{
	Super::BeginPlay();
	if (GGlobalOutlineEnabledOverride.IsSet())
	{
		bEnableOutline = GGlobalOutlineEnabledOverride.GetValue();
	}
	if (GGlobalFilmMaskEnabledOverride.IsSet())
	{
		bEnableFilmMask = GGlobalFilmMaskEnabledOverride.GetValue();
	}
	if (GGlobalFilmMaskModeOverride.IsSet())
	{
		FilmMaskMode = GGlobalFilmMaskModeOverride.GetValue();
	}
	if (GGlobalFilmModeEnabledOverride.IsSet())
	{
		bEnableFilmMode = GGlobalFilmModeEnabledOverride.GetValue();
	}
	RefreshOutline();
}

void ASelectActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseExtension();
	Super::EndPlay(EndPlayReason);
}

void ASelectActor::Destroyed()
{
	ReleaseExtension();
	Super::Destroyed();
}

void ASelectActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Keep the RHI texture resource synced when a UTexture / UTextureRenderTarget2D finishes streaming or reallocates its resource.
	if (bEnable && bEnableFilmMask && FilmMaskMode == ESelectFilmMaskMode::RenderTarget && ReplaceTexture)
	{
		RefreshOutline();
	}
}

#if WITH_EDITOR
void ASelectActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	GGlobalOutlineEnabledOverride.Reset();
	GGlobalFilmMaskEnabledOverride.Reset();
	GGlobalFilmMaskModeOverride.Reset();
	GGlobalFilmModeEnabledOverride.Reset();
	RefreshOutline();
}
#endif

void ASelectActor::SetOutlineEnabled(bool bInEnabled)
{
	bEnableOutline = bInEnabled;
	RefreshOutline();
}

void ASelectActor::SetFilmMaskEnabled(bool bInEnabled)
{
	bEnableFilmMask = bInEnabled;
	RefreshOutline();
}

void ASelectActor::SetFilmMaskMode(ESelectFilmMaskMode NewMode)
{
	FilmMaskMode = NewMode;
	RefreshOutline();
}

void ASelectActor::SetReplaceTexture(UTexture* InTexture, bool bEnableReplacement)
{
	ReplaceTexture = InTexture;
	if (bEnableReplacement && InTexture != nullptr)
	{
		bEnableFilmMask = true;
		FilmMaskMode = ESelectFilmMaskMode::RenderTarget;
	}
	else if (!bEnableReplacement && FilmMaskMode == ESelectFilmMaskMode::RenderTarget)
	{
		bEnableFilmMask = false;
	}
	RefreshOutline();
}

void ASelectActor::SetFilmModeEnabled(bool bInEnabled)
{
	bEnableFilmMode = bInEnabled;
	RefreshOutline();
}

void ASelectActor::SetFilmModeAmounts(float InBlackBarAmount, float InGradingAmount)
{
	FilmBlackBarAmount = FMath::Clamp(InBlackBarAmount, 0.f, 1.f);
	FilmGradingAmount = FMath::Clamp(InGradingAmount, 0.f, 1.f);
	RefreshOutline();
}

void ASelectActor::EnsureExtension()
{
	if (!ViewExtension.IsValid())
	{
		UWorld* World = GetWorld();
		if (World && !HasAnyFlags(RF_ClassDefaultObject))
		{
			ViewExtension = FSceneViewExtensions::NewExtension<FSelectOutlineViewExtension>(World);
		}
	}
}

void ASelectActor::ReleaseExtension()
{
	if (ViewExtension.IsValid())
	{
		ViewExtension->SetSettings(FSelectOutlineSettings{});
		ViewExtension.Reset();
	}
}

void ASelectActor::RefreshOutline()
{
	if (HasAnyFlags(RF_ClassDefaultObject) || !GetWorld())
	{
		return;
	}

	const bool bHasOutline = bEnableOutline && (Thickness > 0.f);
	const bool bHasFilmMask = bEnableFilmMask && (FilmMaskMode != ESelectFilmMaskMode::None) && (FilmMaskOpacity > 0.f);
	const bool bHasFilmMode = bEnableFilmMode && (FilmBlackBarAmount > 0.f || FilmGradingAmount > 0.f);
	if (!bEnable || (!bHasOutline && !bHasFilmMask && !bHasFilmMode))
	{
		ReleaseExtension();
		return;
	}

	EnsureExtension();
	if (!ViewExtension.IsValid())
	{
		return;
	}

	FTextureResource* ReplaceResource = (bHasFilmMask && FilmMaskMode == ESelectFilmMaskMode::RenderTarget && ReplaceTexture)
		? ReplaceTexture->GetResource()
		: nullptr;

	FSelectOutlineSettings S;
	S.bEnable = bEnable;
	S.StencilValue = StencilValue;

	S.bEnableOutline = bEnableOutline;
	S.Thickness = bEnableOutline ? Thickness : 0.f;
	S.OutlineColor = OutlineColor;

	S.bStripes = bStripes;
	S.StripeWidth = StripeWidth;
	S.StripeGap = StripeGap;
	S.GapColor = GapColor;
	S.FlowSpeed = FlowSpeed;
	S.bCompensateCameraRotation = bCompensateCameraRotation;

	S.bDitherRepair = bDitherRepair;
	S.FilterRadius = FilterRadius;
	S.FillConfidence = FillConfidence;
	S.KeepConfidence = KeepConfidence;

	S.bEnableFilmMask = bEnableFilmMask;
	S.FilmMaskMode = bEnableFilmMask ? static_cast<int32>(FilmMaskMode) : 0;
	S.FilmMaskOpacity = FMath::Clamp(FilmMaskOpacity, 0.f, 1.f);
	S.DepthOcclusionBias = FMath::Max(0.f, DepthOcclusionBias);

	S.ReplaceTextureResource = ReplaceResource;
	S.bIgnoreReplaceAlpha = bIgnoreReplaceAlpha;
	S.ReplaceUVTiling = FVector2f(ReplaceUVTiling);
	S.ReplaceUVOffset = FVector2f(ReplaceUVOffset);

	S.SolidColor = SolidColor;

	S.CheckerColorA = CheckerColorA;
	S.CheckerColorB = CheckerColorB;
	S.CheckerSize = FMath::Max(1.f, CheckerSize);

	S.GradingIntensity = FMath::Clamp(GradingIntensity, 0.f, 1.f);
	S.GradingContrast = FMath::Max(0.1f, GradingContrast);
	S.GradingSaturation = FMath::Max(0.f, GradingSaturation);
	S.GradingShadowTint = GradingShadowTint;
	S.GradingHighlightTint = GradingHighlightTint;

	S.bEnableFilmMode = bEnableFilmMode;
	S.FilmAspectRatio = FMath::Clamp(FilmAspectRatio, 0.1f, 10.f);
	S.FilmBlackBarAmount = bEnableFilmMode ? FMath::Clamp(FilmBlackBarAmount, 0.f, 1.f) : 0.f;
	S.FilmGradingAmount = bEnableFilmMode ? FMath::Clamp(FilmGradingAmount, 0.f, 1.f) : 0.f;
	S.FilmWarmTint = FilmWarmTint;
	S.FilmShadowLift = FilmShadowLift;
	S.FilmContrast = FMath::Max(0.1f, FilmContrast);
	S.FilmSaturation = FMath::Max(0.f, FilmSaturation);
	S.FilmVignette = FMath::Clamp(FilmVignette, 0.f, 1.f);

	ViewExtension->SetSettings(S);
}

// ---------------------------------------------------------------------------------------------------------------------
// USelectOutlineBlueprintLibrary
// ---------------------------------------------------------------------------------------------------------------------

void USelectOutlineBlueprintLibrary::SetSelectOutlineEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	GGlobalOutlineEnabledOverride = bEnabled;
	ForEachSelectActor(WorldContextObject, [bEnabled](ASelectActor* Actor)
	{
		Actor->SetOutlineEnabled(bEnabled);
	});
}

bool USelectOutlineBlueprintLibrary::IsSelectOutlineEnabled(const UObject* WorldContextObject)
{
	bool bResult = GGlobalOutlineEnabledOverride.Get(true);
	ForEachSelectActor(WorldContextObject, [&bResult](ASelectActor* Actor)
	{
		bResult = Actor->bEnableOutline;
	});
	return bResult;
}

void USelectOutlineBlueprintLibrary::SetFilmMaskEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	GGlobalFilmMaskEnabledOverride = bEnabled;
	ForEachSelectActor(WorldContextObject, [bEnabled](ASelectActor* Actor)
	{
		Actor->SetFilmMaskEnabled(bEnabled);
	});
}

bool USelectOutlineBlueprintLibrary::IsFilmMaskEnabled(const UObject* WorldContextObject)
{
	bool bResult = GGlobalFilmMaskEnabledOverride.Get(false);
	ForEachSelectActor(WorldContextObject, [&bResult](ASelectActor* Actor)
	{
		bResult = Actor->bEnableFilmMask;
	});
	return bResult;
}

void USelectOutlineBlueprintLibrary::SetFilmMaskMode(const UObject* WorldContextObject, ESelectFilmMaskMode NewMode, bool bEnableFilmMask)
{
	GGlobalFilmMaskModeOverride = NewMode;
	if (bEnableFilmMask)
	{
		GGlobalFilmMaskEnabledOverride = (NewMode != ESelectFilmMaskMode::None);
	}
	ForEachSelectActor(WorldContextObject, [NewMode, bEnableFilmMask](ASelectActor* Actor)
	{
		Actor->FilmMaskMode = NewMode;
		if (bEnableFilmMask)
		{
			Actor->bEnableFilmMask = (NewMode != ESelectFilmMaskMode::None);
		}
		Actor->RefreshOutline();
	});
}

ESelectFilmMaskMode USelectOutlineBlueprintLibrary::GetFilmMaskMode(const UObject* WorldContextObject)
{
	ESelectFilmMaskMode Result = GGlobalFilmMaskModeOverride.Get(ESelectFilmMaskMode::None);
	ForEachSelectActor(WorldContextObject, [&Result](ASelectActor* Actor)
	{
		Result = Actor->FilmMaskMode;
	});
	return Result;
}

void USelectOutlineBlueprintLibrary::SetFilmMaskSolidColor(const UObject* WorldContextObject, FLinearColor NewColor, bool bActivateMode)
{
	if (bActivateMode)
	{
		GGlobalFilmMaskEnabledOverride = true;
		GGlobalFilmMaskModeOverride = ESelectFilmMaskMode::SolidColor;
	}
	ForEachSelectActor(WorldContextObject, [NewColor, bActivateMode](ASelectActor* Actor)
	{
		Actor->SolidColor = NewColor;
		if (bActivateMode)
		{
			Actor->bEnableFilmMask = true;
			Actor->FilmMaskMode = ESelectFilmMaskMode::SolidColor;
		}
		Actor->RefreshOutline();
	});
}

void USelectOutlineBlueprintLibrary::SetFilmMaskCheckerboard(const UObject* WorldContextObject, FLinearColor ColorA, FLinearColor ColorB, float CheckerSize, bool bActivateMode)
{
	if (bActivateMode)
	{
		GGlobalFilmMaskEnabledOverride = true;
		GGlobalFilmMaskModeOverride = ESelectFilmMaskMode::Checkerboard;
	}
	ForEachSelectActor(WorldContextObject, [ColorA, ColorB, CheckerSize, bActivateMode](ASelectActor* Actor)
	{
		Actor->CheckerColorA = ColorA;
		Actor->CheckerColorB = ColorB;
		Actor->CheckerSize = FMath::Max(1.f, CheckerSize);
		if (bActivateMode)
		{
			Actor->bEnableFilmMask = true;
			Actor->FilmMaskMode = ESelectFilmMaskMode::Checkerboard;
		}
		Actor->RefreshOutline();
	});
}

void USelectOutlineBlueprintLibrary::SetFilmMaskRenderTarget(const UObject* WorldContextObject, UTexture* InTexture, bool bActivateMode)
{
	if (bActivateMode && InTexture != nullptr)
	{
		GGlobalFilmMaskEnabledOverride = true;
		GGlobalFilmMaskModeOverride = ESelectFilmMaskMode::RenderTarget;
	}
	ForEachSelectActor(WorldContextObject, [InTexture, bActivateMode](ASelectActor* Actor)
	{
		Actor->SetReplaceTexture(InTexture, bActivateMode);
	});
}

void USelectOutlineBlueprintLibrary::SetFilmModeEnabled(const UObject* WorldContextObject, bool bEnabled)
{
	GGlobalFilmModeEnabledOverride = bEnabled;
	ForEachSelectActor(WorldContextObject, [bEnabled](ASelectActor* Actor)
	{
		Actor->SetFilmModeEnabled(bEnabled);
	});
}

bool USelectOutlineBlueprintLibrary::IsFilmModeEnabled(const UObject* WorldContextObject)
{
	bool bResult = GGlobalFilmModeEnabledOverride.Get(false);
	ForEachSelectActor(WorldContextObject, [&bResult](ASelectActor* Actor)
	{
		bResult = Actor->bEnableFilmMode;
	});
	return bResult;
}

void USelectOutlineBlueprintLibrary::SetFilmModeParameters(const UObject* WorldContextObject, float BlackBarAmount, float GradingAmount, float AspectRatio, bool bEnableFilmMode)
{
	GGlobalFilmModeEnabledOverride = bEnableFilmMode;
	ForEachSelectActor(WorldContextObject, [BlackBarAmount, GradingAmount, AspectRatio, bEnableFilmMode](ASelectActor* Actor)
	{
		Actor->bEnableFilmMode = bEnableFilmMode;
		Actor->FilmBlackBarAmount = FMath::Clamp(BlackBarAmount, 0.f, 1.f);
		Actor->FilmGradingAmount = FMath::Clamp(GradingAmount, 0.f, 1.f);
		Actor->FilmAspectRatio = FMath::Clamp(AspectRatio, 0.5f, 4.f);
		Actor->RefreshOutline();
	});
}
