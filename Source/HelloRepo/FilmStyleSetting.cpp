#include "FilmStyleSetting.h"
#include "FilmStyleSubsystem.h"
#include "CutActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

AFilmStyleSetting::AFilmStyleSetting()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AFilmStyleSetting::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SyncStateToSubsystem();
}

void AFilmStyleSetting::BeginPlay()
{
	Super::BeginPlay();

	bWasSubsystemFilmMode = false;
	bIsFadingOutFilmMode = false;
	FilmModeElapsedTime = 0.0f;
	RefreshActiveCutTexture();

	if (UWorld* World = GetWorld())
	{
		if (UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this))
		{
			UpdateFilmModeTransition(0.0f, Subsystem->GetFilmMode());
		}
	}

	SyncStateToSubsystem();
}

void AFilmStyleSetting::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this))
		{
			Subsystem->SetSelectActorData(FSelectActorStateData{});
		}
	}

	Super::EndPlay(EndPlayReason);
}

void AFilmStyleSetting::Destroyed()
{
	if (UWorld* World = GetWorld())
	{
		if (UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this))
		{
			Subsystem->SetSelectActorData(FSelectActorStateData{});
		}
	}

	Super::Destroyed();
}

void AFilmStyleSetting::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UWorld* World = GetWorld();
	if (!World || HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	if (UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this))
	{
		RefreshActiveCutTexture();

		if (World->IsGameWorld() || Subsystem->GetFilmMode() || bWasSubsystemFilmMode || bIsFadingOutFilmMode)
		{
			UpdateFilmModeTransition(DeltaSeconds, Subsystem->GetFilmMode());
		}

		SyncStateToSubsystem();
	}
}

#if WITH_EDITOR
void AFilmStyleSetting::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncStateToSubsystem();
}
#endif

float AFilmStyleSetting::ComputeTransitionAlpha(float ElapsedTime, const FVector2D& TimingVec)
{
	const float Delay = FMath::Max(0.0f, static_cast<float>(TimingVec.X));
	const float Duration = FMath::Max(0.0f, static_cast<float>(TimingVec.Y));

	if (ElapsedTime < Delay)
	{
		return 0.0f;
	}

	if (Duration <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	return FMath::Clamp((ElapsedTime - Delay) / Duration, 0.0f, 1.0f);
}

void AFilmStyleSetting::UpdateFilmModeTransition(float DeltaSeconds, bool bSubsystemFilmMode)
{
	if (bSubsystemFilmMode)
	{
		if (!bWasSubsystemFilmMode)
		{
			bWasSubsystemFilmMode = true;
			bIsFadingOutFilmMode = false;
			FilmModeElapsedTime = 0.0f;
		}
		else
		{
			FilmModeElapsedTime += FMath::Max(0.0f, DeltaSeconds);
		}

		bEnableFilmMode = true;
		FilmBlackBarAmount = ComputeTransitionAlpha(FilmModeElapsedTime, AB);
		FilmGradingAmount = ComputeTransitionAlpha(FilmModeElapsedTime, CD);
		RefreshOutline();
	}
	else
	{
		if (bWasSubsystemFilmMode)
		{
			// Just transitioned from bFilmMode = true -> false: start EF / GH fade-out
			bWasSubsystemFilmMode = false;
			bIsFadingOutFilmMode = true;
			FilmModeElapsedTime = 0.0f;
			FadeOutStartBlackBarAmount = 1.0f;
			FadeOutStartGradingAmount = 1.0f;
		}
		else if (bIsFadingOutFilmMode)
		{
			FilmModeElapsedTime += FMath::Max(0.0f, DeltaSeconds);
		}

		if (bIsFadingOutFilmMode)
		{
			const float BlackBarOutAlpha = ComputeTransitionAlpha(FilmModeElapsedTime, EF);
			const float GradingOutAlpha = ComputeTransitionAlpha(FilmModeElapsedTime, GH);

			FilmBlackBarAmount = FadeOutStartBlackBarAmount * (1.0f - BlackBarOutAlpha);
			FilmGradingAmount = FadeOutStartGradingAmount * (1.0f - GradingOutAlpha);

			if (BlackBarOutAlpha >= 1.0f && GradingOutAlpha >= 1.0f)
			{
				bIsFadingOutFilmMode = false;
				bEnableFilmMode = false;
				FilmBlackBarAmount = 0.0f;
				FilmGradingAmount = 0.0f;
			}
			else
			{
				bEnableFilmMode = true;
			}

			RefreshOutline();
		}
	}
}

void AFilmStyleSetting::SyncStateToSubsystem()
{
	UWorld* World = GetWorld();
	if (!World || HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this);
	if (!Subsystem)
	{
		return;
	}

	FSelectActorStateData Data;
	Data.bHasValidSelectActor = true;
	Data.bEnable = bEnable;
	Data.StencilValue = StencilValue;
	Data.bEnableOutline = bEnableOutline;
	Data.bEnableFilmMask = bEnableFilmMask;
	Data.FilmMaskMode = FilmMaskMode;
	Data.bEnableFilmMode = bEnableFilmMode;
	Data.FilmBlackBarAmount = FilmBlackBarAmount;
	Data.FilmGradingAmount = FilmGradingAmount;

	Subsystem->SetSelectActorData(Data);
}

void AFilmStyleSetting::RefreshActiveCutTexture()
{
	UWorld* World = GetWorld();
	if (!World || HasAnyFlags(RF_ClassDefaultObject)) return;
	if (UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this))
	{
		ACutActor* ActiveCutActor = IsValid(Subsystem->GetActiveCutActor()) ? Subsystem->GetActiveCutActor() : nullptr;
		UTextureRenderTarget2D* ActiveRenderTarget = ActiveCutActor ? ActiveCutActor->RenderTarget.Get() : nullptr;
		const TWeakObjectPtr<ACutActor> CurrentActor(ActiveCutActor);
		const TWeakObjectPtr<UTextureRenderTarget2D> CurrentRenderTarget(ActiveRenderTarget);
		bool bMaskChanged = false;
		// Also detect delayed RT creation after activation and replacement of the RT object.
		if (LastActiveCutActor != CurrentActor || LastActiveCutRenderTarget != CurrentRenderTarget)
		{
			LastActiveCutActor = CurrentActor;
			LastActiveCutRenderTarget = CurrentRenderTarget;
			ReplaceTexture = ActiveRenderTarget;
			bMaskChanged = true;
		}

		// Evaluate every tick: the active actor's RT may become ready later,
		// or Blueprint may change the mask mode without changing the actor.
		if (FilmMaskMode == ESelectFilmMaskMode::SolidColor && IsValid(ActiveRenderTarget))
		{
			ReplaceTexture = ActiveRenderTarget;
			FilmMaskMode = ESelectFilmMaskMode::RenderTarget;
			bMaskChanged = true;
		}
		else if (FilmMaskMode == ESelectFilmMaskMode::RenderTarget && !IsValid(ReplaceTexture.Get()))
		{
			FilmMaskMode = ESelectFilmMaskMode::SolidColor;
			bMaskChanged = true;
		}

		if (bMaskChanged)
		{
			RefreshOutline();
		}

		SyncStateToSubsystem();
	}
}

void AFilmStyleSetting::SetSelectionPressed(bool bPressed)
{
	if (bSelectionPressed == bPressed)
	{
		return;
	}
	bSelectionPressed = bPressed;
	if (bPressed)
	{
		ReleasedOutlineColor = OutlineColor;
		ReleasedHoverColor = HoverOutlineColor;
		OutlineColor = ReleasedOutlineColor * FMath::Max(0.f, PressedBrightness);
		HoverOutlineColor = ReleasedHoverColor * FMath::Max(0.f, PressedBrightness);
		OutlineColor.A = ReleasedOutlineColor.A;
		HoverOutlineColor.A = ReleasedHoverColor.A;
	}
	else
	{
		OutlineColor = ReleasedOutlineColor;
		HoverOutlineColor = ReleasedHoverColor;
	}
	RefreshOutline();
}
