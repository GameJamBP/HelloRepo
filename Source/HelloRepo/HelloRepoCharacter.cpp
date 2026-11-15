// Copyright Epic Games, Inc. All Rights Reserved.

#include "HelloRepoCharacter.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "Materials/MaterialInterface.h"
#include "FilmStyleSubsystem.h"
#include "HelloRepo.h"

AHelloRepoCharacter::AHelloRepoCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	// Default viewfinder post-process asset created under Content/Art/Materials.
	ViewfinderPostProcessMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/Art/Materials/M_PP_PhotoViewfinder.M_PP_PhotoViewfinder")));
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 500.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f;
	CameraBoom->bUsePawnControlRotation = true;

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)
}

void AHelloRepoCharacter::BeginPlay()
{
	Super::BeginPlay();
	InitializeCameraViewRig();
}

void AHelloRepoCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasCameraViewPoints())
	{
		return;
	}

	if (!bIsPhotoMode)
	{
		const float TargetAlpha = IsFilmModeActive() ? 1.0f : 0.0f;
		if (!FMath::IsNearlyEqual(CameraViewAlpha, TargetAlpha))
		{
			CameraViewAlpha = FMath::FInterpConstantTo(CameraViewAlpha, TargetAlpha, DeltaSeconds, CameraViewBlendSpeed);
		}
	}

	ApplyCameraViewTransform();
}

void AHelloRepoCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (HasCameraViewPoints())
	{
		// Keep FollowCamera synced even if an external system evaluates the camera outside regular Tick (e.g. CutActor).
		ApplyCameraViewTransform();
	}

	Super::CalcCamera(DeltaTime, OutResult);
}

void AHelloRepoCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AHelloRepoCharacter::Move);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AHelloRepoCharacter::Look);

		// Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AHelloRepoCharacter::Look);
	}
	else
	{
		UE_LOG(LogHelloRepo, Error, TEXT("'%s' Failed to find an Enhanced Input component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void AHelloRepoCharacter::TogglePhotoMode()
{
	UE_LOG(LogHelloRepo, Log, TEXT("%s TogglePhotoMode (current bIsPhotoMode=%d)"), *GetNameSafe(this), bIsPhotoMode ? 1 : 0);
	SetPhotoMode(!bIsPhotoMode);
}

void AHelloRepoCharacter::SetPhotoMode(bool bEnabled)
{
	if (!HasCameraViewPoints())
	{
		ResolveCameraPointComponents();
		if (!HasCameraViewPoints())
		{
			UE_LOG(LogHelloRepo, Warning, TEXT("%s SetPhotoMode(%d) aborted: missing TPSPoint/FPSPoint/FollowCamera."), *GetNameSafe(this), bEnabled ? 1 : 0);
			return;
		}
	}

	const bool bPhotoModeChanged = bIsPhotoMode != bEnabled;
	bIsPhotoMode = bEnabled;
	SetViewfinderOverlayActive(bIsPhotoMode);

	// Photo Mode switches immediately without lerping; exiting respects whether FilmMode currently holds FPS.
	CameraViewAlpha = (bIsPhotoMode || IsFilmModeActive()) ? 1.0f : 0.0f;
	ApplyCameraViewTransform();
	NotifyCameraCut();
	UE_LOG(LogHelloRepo, Log, TEXT("%s SetPhotoMode(%d) applied -> Alpha=%.3f"), *GetNameSafe(this), bIsPhotoMode ? 1 : 0, CameraViewAlpha);
	if (bPhotoModeChanged)
	{
		OnPhotoModeChanged(bIsPhotoMode);
	}
}

void AHelloRepoCharacter::InitializeCameraViewRig()
{
	ResolveCameraPointComponents();

	if (!ViewfinderPostProcessMaterial.IsNull())
	{
		LoadedViewfinderMaterial = ViewfinderPostProcessMaterial.LoadSynchronous();
	}

	bIsPhotoMode = false;
	SetViewfinderOverlayActive(false);

	if (!HasCameraViewPoints())
	{
		return;
	}

	// Run Character Tick in PostPhysics after CameraBoom and Mesh so TPSPoint/FPSPoint world transforms are current
	// before PlayerController::UpdateCameraManager runs for the frame.
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	if (CameraBoom)
	{
		AddTickPrerequisiteComponent(CameraBoom);
	}
	if (USkeletalMeshComponent* CharMesh = GetMesh())
	{
		AddTickPrerequisiteComponent(CharMesh);
	}

	CameraViewAlpha = IsFilmModeActive() ? 1.0f : 0.0f;
	ApplyCameraViewTransform();
}

void AHelloRepoCharacter::ResolveCameraPointComponents()
{
	CachedTPSPoint.Reset();
	CachedFPSPoint.Reset();

	TInlineComponentArray<USceneComponent*> SceneComponents(this);
	for (USceneComponent* Comp : SceneComponents)
	{
		if (!Comp)
		{
			continue;
		}

		const FName CompName = Comp->GetFName();
		if (CompName == TPSPointComponentName)
		{
			CachedTPSPoint = Comp;
		}
		else if (CompName == FPSPointComponentName)
		{
			CachedFPSPoint = Comp;
		}
	}
}

bool AHelloRepoCharacter::HasCameraViewPoints() const
{
	return FollowCamera != nullptr && CachedTPSPoint.IsValid() && CachedFPSPoint.IsValid();
}

bool AHelloRepoCharacter::IsFilmModeActive() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const UFilmStyleSubsystem* Subsystem = World->GetSubsystem<UFilmStyleSubsystem>())
		{
			return Subsystem->GetFilmMode();
		}
	}
	return false;
}

void AHelloRepoCharacter::SetViewfinderOverlayActive(bool bActive)
{
	if (!FollowCamera)
	{
		return;
	}

	if (!LoadedViewfinderMaterial && !ViewfinderPostProcessMaterial.IsNull())
	{
		LoadedViewfinderMaterial = ViewfinderPostProcessMaterial.LoadSynchronous();
	}

	if (!LoadedViewfinderMaterial)
	{
		UE_LOG(LogHelloRepo, Warning, TEXT("%s SetViewfinderOverlayActive(%d): Viewfinder material failed to load (%s)."),
			*GetNameSafe(this), bActive ? 1 : 0, *ViewfinderPostProcessMaterial.ToString());
		return;
	}

	if (bActive)
	{
		FollowCamera->AddOrUpdateBlendable(LoadedViewfinderMaterial.Get(), 1.0f);
	}
	else
	{
		FollowCamera->RemoveBlendable(LoadedViewfinderMaterial.Get());
	}
}

void AHelloRepoCharacter::ApplyCameraViewTransform()
{
	const USceneComponent* TPS = CachedTPSPoint.Get();
	const USceneComponent* FPS = CachedFPSPoint.Get();
	if (!FollowCamera || !TPS || !FPS)
	{
		return;
	}

	const FVector TPSLocation = TPS->GetComponentLocation();
	const FQuat TPSRotation = TPS->GetComponentQuat();

	const FVector FPSLocation = FPS->GetComponentLocation();
	const FQuat FPSRotation = (bUseControlRotationInFPS && Controller)
		? Controller->GetControlRotation().Quaternion()
		: FPS->GetComponentQuat();

	const float ClampedAlpha = FMath::Clamp(CameraViewAlpha, 0.0f, 1.0f);
	const float EasedAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, ClampedAlpha, FMath::Max(1.0f, CameraViewEaseExponent));

	const FVector BlendedLocation = FMath::Lerp(TPSLocation, FPSLocation, EasedAlpha);
	const FQuat BlendedRotation = FQuat::Slerp(TPSRotation, FPSRotation, EasedAlpha);

	FollowCamera->SetWorldLocationAndRotation(BlendedLocation, BlendedRotation, false, nullptr, ETeleportType::TeleportPhysics);
}

void AHelloRepoCharacter::NotifyCameraCut()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}
}

void AHelloRepoCharacter::Move(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	// route the input
	DoMove(MovementVector.X, MovementVector.Y);
}

void AHelloRepoCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// route the input
	DoLook(LookAxisVector.X, LookAxisVector.Y);
}

void AHelloRepoCharacter::DoMove(float Right, float Forward)
{
	if (GetController() != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = GetController()->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);

		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, Forward);
		AddMovementInput(RightDirection, Right);
	}
}

void AHelloRepoCharacter::DoLook(float Yaw, float Pitch)
{
	if (GetController() != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AHelloRepoCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AHelloRepoCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}
