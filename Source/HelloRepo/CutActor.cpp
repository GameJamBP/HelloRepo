#include "CutActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "FilmStyleSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ACutActor::ACutActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	CaptureComponent2D = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("CaptureComponent2D"));
	SetRootComponent(CaptureComponent2D);
	CaptureComponent2D->bCaptureEveryFrame = false;
	CaptureComponent2D->bCaptureOnMovement = false;
	CaptureComponent2D->CaptureSource = SCS_FinalColorLDR;
	CaptureComponent2D->bMainViewFamily = true;
	CaptureComponent2D->bInheritMainViewCameraPostProcessSettings = true;
}

void ACutActor::BeginPlay()
{
	GetWorld()->GetSubsystem<UFilmStyleSubsystem>()->CutActors.AddUnique(this);
	SetCaptureActive(false);
	// Super dispatches Blueprint BeginPlay; prepare state before it can activate us.
	Super::BeginPlay();
	InitializeSnapshot();
}

bool ACutActor::UpdateRenderTarget()
{
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	FVector2D Size = FVector2D::ZeroVector;
	if (Viewport) Viewport->GetViewportSize(Size);
	const int32 Width = FMath::FloorToInt(Size.X);
	const int32 Height = FMath::FloorToInt(Size.Y);
	if (Width <= 0 || Height <= 0) return false;
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this);
		RenderTarget->TargetGamma = 2.2f;
		RenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
		CaptureComponent2D->TextureTarget = RenderTarget;
	}
	else if (RenderTarget->SizeX != Width || RenderTarget->SizeY != Height)
	{
		RenderTarget->ResizeTarget(Width, Height);
	}
	return true;
}

void ACutActor::CopyCameraParameters(const APlayerCameraManager* Camera)
{
	const FMinimalViewInfo& View = Camera->GetCameraCacheView();
	CaptureComponent2D->FOVAngle = View.FOV;
	CaptureComponent2D->ProjectionType = View.ProjectionMode;
	CaptureComponent2D->OrthoWidth = View.OrthoWidth;
	CaptureComponent2D->bOverride_CustomNearClippingPlane = View.PerspectiveNearClipPlane > 0;
	CaptureComponent2D->CustomNearClippingPlane = View.PerspectiveNearClipPlane;
	CaptureComponent2D->PostProcessSettings = View.PostProcessSettings;
	CaptureComponent2D->PostProcessBlendWeight = View.PostProcessBlendWeight;
}

void ACutActor::InitializeSnapshot()
{
	if (bSnapshotInitialized || GetNetMode() == NM_DedicatedServer) return;
	APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera || Camera->GetCameraCacheTime() <= 0.f || !UpdateRenderTarget())
	{
		InitializationTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ACutActor::InitializeSnapshot);
		return;
	}
	const FMinimalViewInfo& View = Camera->GetCameraCacheView();
	SetActorLocationAndRotation(View.Location, View.Rotation);
	PreviousPlayerView = FTransform(View.Rotation, View.Location);
	bHasPreviousPlayerView = true;
	CopyCameraParameters(Camera);
	bSnapshotInitialized = true;
	// Queue with the main view so its final post-process settings are available.
	CaptureComponent2D->CaptureSceneDeferred();
}

void ACutActor::SetCaptureActive(bool bActive)
{
	SetActorTickEnabled(bActive);
	CaptureComponent2D->bCaptureEveryFrame = bActive;
	CaptureComponent2D->bCaptureOnMovement = false;
	bHasPreviousPlayerView = false;
	if (bActive)
	{
		if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
		{
			const FMinimalViewInfo& View = Camera->GetCameraCacheView();
			PreviousPlayerView = FTransform(View.Rotation, View.Location);
			bHasPreviousPlayerView = true;
		}
	}
}

void ACutActor::SetActiveCutActor()
{
	if (!HasActorBegunPlay() && !IsActorBeginningPlay()) return;
	UFilmStyleSubsystem* Context = GetWorld()->GetSubsystem<UFilmStyleSubsystem>();
	Context->CutActors.RemoveAll([](const TObjectPtr<ACutActor>& Actor) { return !IsValid(Actor); });
	Context->CutActors.AddUnique(this);
	for (ACutActor* Actor : Context->CutActors) Actor->SetCaptureActive(Actor == this);
	Context->ActiveCutActor = this;
}

void ACutActor::ClearActive(const UObject* WorldContextObject)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World) return;
	UFilmStyleSubsystem* Context = World->GetSubsystem<UFilmStyleSubsystem>();
	if (IsValid(Context->ActiveCutActor)) Context->ActiveCutActor->SetCaptureActive(false);
	Context->ActiveCutActor = nullptr;
}

void ACutActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bSnapshotInitialized) return;
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Camera) { bHasPreviousPlayerView = false; return; }
	const FMinimalViewInfo& View = Camera->GetCameraCacheView();
	const FTransform PlayerView(View.Rotation, View.Location);
	if (bMirrorWorld && bHasPreviousPlayerView)
	{
		const FQuat Mapping = GetActorQuat() * PreviousPlayerView.GetRotation().Inverse();
		SetActorLocationAndRotation(GetActorLocation() + Mapping.RotateVector(PlayerView.GetLocation() - PreviousPlayerView.GetLocation()),
			Mapping * PlayerView.GetRotation());
	}
	PreviousPlayerView = PlayerView;
	bHasPreviousPlayerView = true;
	CopyCameraParameters(Camera);
	UpdateRenderTarget();
}

void ACutActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(InitializationTimer);
	SetCaptureActive(false);
	if (UFilmStyleSubsystem* Context = GetWorld()->GetSubsystem<UFilmStyleSubsystem>())
	{
		Context->CutActors.Remove(this);
		if (Context->ActiveCutActor == this) Context->ActiveCutActor = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
