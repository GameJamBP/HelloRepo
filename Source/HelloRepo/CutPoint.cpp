#include "CutPoint.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "CutActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FilmStyleSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "PhotoActor.h"
#include "TimerManager.h"

ACutPoint::ACutPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	CaptureComponent2D = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CaptureComponent2D"));
	SetRootComponent(CaptureComponent2D);
	CaptureComponent2D->bCaptureEveryFrame = false;
	CaptureComponent2D->bCaptureOnMovement = false;
	CaptureComponent2D->bAlwaysPersistRenderingState = false;
	CaptureComponent2D->bAutoActivate = false;
	CaptureComponent2D->SetComponentTickEnabled(false);
	CaptureComponent2D->CaptureSource = SCS_FinalColorLDR;
	CaptureComponent2D->bMainViewFamily = true;
	CaptureComponent2D->bInheritMainViewCameraPostProcessSettings = true;
}

void ACutPoint::BeginPlay()
{
	CaptureComponent2D->bCaptureEveryFrame = false;
	CaptureComponent2D->bCaptureOnMovement = false;
	CaptureComponent2D->bAlwaysPersistRenderingState = false;
	CaptureComponent2D->SetComponentTickEnabled(false);
	CaptureComponent2D->Deactivate();

	if (IsValid(LinkedPhotoActor))
	{
		if (!IsValid(LinkedPhotoActor->LinkedCutPoint))
		{
			LinkedPhotoActor->LinkedCutPoint = this;
		}
		RegisterPhotoActor(LinkedPhotoActor);
	}

	Super::BeginPlay();

	CaptureInitialPhoto();
}

void ACutPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(InitializationTimer);
	GetWorldTimerManager().ClearTimer(FinalizeFallbackTimer);
	if (ViewportDrawnHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				Viewport->OnDrawn().Remove(ViewportDrawnHandle);
			}
		}
		ViewportDrawnHandle.Reset();
	}
	FinalizeAndDisableCapture();
	Super::EndPlay(EndPlayReason);
}

void ACutPoint::RegisterPhotoActor(APhotoActor* PhotoActor)
{
	if (!IsValid(PhotoActor) || PhotoActor->GetWorld() != GetWorld())
	{
		return;
	}

	RegisteredPhotoActors.RemoveAll([](const TWeakObjectPtr<APhotoActor>& Existing)
	{
		return !Existing.IsValid();
	});

	for (const TWeakObjectPtr<APhotoActor>& Existing : RegisteredPhotoActors)
	{
		if (Existing.Get() == PhotoActor)
		{
			if (bInitialPhotoCaptured && IsValid(PhotoRenderTarget))
			{
				PhotoActor->ApplyCapturedPhoto(PhotoRenderTarget, CapturedAspectRatio);
			}
			return;
		}
	}

	RegisteredPhotoActors.Add(PhotoActor);
	if (!IsValid(LinkedPhotoActor))
	{
		LinkedPhotoActor = PhotoActor;
	}

	if (bInitialPhotoCaptured && IsValid(PhotoRenderTarget))
	{
		PhotoActor->ApplyCapturedPhoto(PhotoRenderTarget, CapturedAspectRatio);
	}
}

void ACutPoint::CaptureInitialPhoto()
{
	if (bInitialPhotoCaptured || bCapturePermanentlyDisabled || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	FVector2D ViewportSize = FVector2D::ZeroVector;
	if (Viewport)
	{
		Viewport->GetViewportSize(ViewportSize);
	}

	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (ViewportSize.X <= 0.0f || ViewportSize.Y <= 0.0f || !Camera || Camera->GetCameraCacheTime() <= 0.0f)
	{
		InitializationTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ACutPoint::CaptureInitialPhoto);
		return;
	}

	const int32 MaxDim = FMath::Max(PhotoMaxDimension, 16);
	const float ViewportAspect = ViewportSize.X / ViewportSize.Y;
	int32 TargetWidth = MaxDim;
	int32 TargetHeight = MaxDim;
	if (ViewportAspect >= 1.0f)
	{
		TargetWidth = MaxDim;
		TargetHeight = FMath::Max(16, FMath::RoundToInt(static_cast<float>(MaxDim) / ViewportAspect));
	}
	else
	{
		TargetHeight = MaxDim;
		TargetWidth = FMath::Max(16, FMath::RoundToInt(static_cast<float>(MaxDim) * ViewportAspect));
	}

	CapturedAspectRatio = static_cast<float>(TargetWidth) / static_cast<float>(TargetHeight);

	if (!PhotoRenderTarget)
	{
		PhotoRenderTarget = NewObject<UTextureRenderTarget2D>(this);
		PhotoRenderTarget->TargetGamma = 2.2f;
		PhotoRenderTarget->bAutoGenerateMips = false;
		PhotoRenderTarget->InitCustomFormat(TargetWidth, TargetHeight, PF_B8G8R8A8, false);
		PhotoRenderTarget->UpdateResourceImmediate(true);
	}
	else if (PhotoRenderTarget->SizeX != TargetWidth || PhotoRenderTarget->SizeY != TargetHeight)
	{
		PhotoRenderTarget->ResizeTarget(TargetWidth, TargetHeight);
	}

	const FMinimalViewInfo& View = Camera->GetCameraCacheView();
	if (bInheritPlayerCameraFOV)
	{
		CaptureComponent2D->FOVAngle = View.FOV;
		CaptureComponent2D->ProjectionType = View.ProjectionMode;
		CaptureComponent2D->OrthoWidth = View.OrthoWidth;
	}
	CaptureComponent2D->bOverride_CustomNearClippingPlane = View.PerspectiveNearClipPlane > 0.0f;
	CaptureComponent2D->CustomNearClippingPlane = View.PerspectiveNearClipPlane;
	CaptureComponent2D->PostProcessSettings = View.PostProcessSettings;
	CaptureComponent2D->PostProcessBlendWeight = View.PostProcessBlendWeight;

	CaptureComponent2D->bCaptureEveryFrame = false;
	CaptureComponent2D->bCaptureOnMovement = false;
	CaptureComponent2D->bAlwaysPersistRenderingState = false;
	CaptureComponent2D->TextureTarget = PhotoRenderTarget;
	CaptureComponent2D->SetVisibility(true, false);
	CaptureComponent2D->CaptureSceneDeferred();

	bInitialPhotoCaptured = true;

	NotifyPhotoConsumers();

	// Wait until the game viewport actually finishes drawing (and executing MainViewFamily deferred captures)
	// before clearing TextureTarget and permanently disabling SceneCapture.
	ViewportDrawnHandle = Viewport->OnDrawn().AddUObject(this, &ACutPoint::OnViewportDrawnAfterCapture);

	// Safety fallback if no viewport draw executes (e.g. NullRHI).
	FinalizeFallbackTimer = GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (!bCapturePermanentlyDisabled && IsValid(this))
		{
			FinalizeFallbackTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ACutPoint::FinalizeAndDisableCapture);
		}
	}));
}

void ACutPoint::OnViewportDrawnAfterCapture()
{
	if (ViewportDrawnHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				Viewport->OnDrawn().Remove(ViewportDrawnHandle);
			}
		}
		ViewportDrawnHandle.Reset();
	}

	FinalizeAndDisableCapture();
}

void ACutPoint::FinalizeAndDisableCapture()
{
	GetWorldTimerManager().ClearTimer(FinalizeFallbackTimer);

	if (!CaptureComponent2D)
	{
		bCapturePermanentlyDisabled = true;
		return;
	}

	if (ViewportDrawnHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				Viewport->OnDrawn().Remove(ViewportDrawnHandle);
			}
		}
		ViewportDrawnHandle.Reset();
	}

	bCapturePermanentlyDisabled = true;
	CaptureComponent2D->bCaptureEveryFrame = false;
	CaptureComponent2D->bCaptureOnMovement = false;
	CaptureComponent2D->bAlwaysPersistRenderingState = false;
	CaptureComponent2D->TextureTarget = nullptr;
	CaptureComponent2D->SetVisibility(false, false);
	CaptureComponent2D->SetComponentTickEnabled(false);
	CaptureComponent2D->Deactivate();
}

void ACutPoint::NotifyPhotoConsumers()
{
	if (!IsValid(PhotoRenderTarget))
	{
		return;
	}

	for (const TWeakObjectPtr<APhotoActor>& WeakPhoto : RegisteredPhotoActors)
	{
		if (APhotoActor* Photo = WeakPhoto.Get(); IsValid(Photo))
		{
			Photo->ApplyCapturedPhoto(PhotoRenderTarget, CapturedAspectRatio);
		}
	}
}

bool ACutPoint::IsSceneCaptureActive() const
{
	if (!CaptureComponent2D)
	{
		return false;
	}

	return CaptureComponent2D->IsActive()
		|| CaptureComponent2D->IsComponentTickEnabled()
		|| CaptureComponent2D->bCaptureEveryFrame
		|| CaptureComponent2D->bCaptureOnMovement
		|| CaptureComponent2D->IsVisible()
		|| CaptureComponent2D->TextureTarget != nullptr;
}

ACutActor* ACutPoint::ActivateOrSpawnCutActor()
{
	UWorld* World = GetWorld();
	UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this);
	if (!World || !Subsystem)
	{
		return nullptr;
	}

	if (ACutActor* ExistingCut = SpawnedCutActor.Get(); IsValid(ExistingCut) && !ExistingCut->IsActorBeingDestroyed())
	{
		if (Subsystem->GetCutActors().Contains(ExistingCut))
		{
			ExistingCut->SetActiveCutActor();
			return ExistingCut;
		}

		// Destroy evicted/cleared stale instance so repeated interactions do not leak hidden actors.
		ExistingCut->Destroy();
		SpawnedCutActor.Reset();
	}

	TSubclassOf<ACutActor> SpawnClass = CutActorClass ? CutActorClass : TSubclassOf<ACutActor>(ACutActor::StaticClass());
	const FTransform SpawnTransform = CaptureComponent2D
		? CaptureComponent2D->GetComponentTransform()
		: GetActorTransform();

	ACutActor* NewCut = World->SpawnActorDeferred<ACutActor>(
		SpawnClass,
		SpawnTransform,
		this,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!IsValid(NewCut))
	{
		return nullptr;
	}

	NewCut->ConfigureFromCutPoint(this);
	NewCut->FinishSpawning(SpawnTransform);

	SpawnedCutActor = NewCut;
	NewCut->SetActiveCutActor();
	return NewCut;
}
