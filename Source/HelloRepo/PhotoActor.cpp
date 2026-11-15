#include "PhotoActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CutActor.h"
#include "CutPoint.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "FilmStyleSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

APhotoActor::APhotoActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FrameMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrameMesh"));
	FrameMesh->SetupAttachment(SceneRoot);
	FrameMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	PhotoMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhotoMesh"));
	PhotoMesh->SetupAttachment(SceneRoot);
	PhotoMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	// Map /Engine/BasicShapes/Plane (XY plane, +Z normal, U:+X, V:+Y) onto actor +X normal
	// with upright V (Top=+Z, Bottom=-Z) and unmirrored U for a viewer looking along -X (Left=+Y, Right=-Y).
	const FRotator PhotoPlaneRotation = FMatrix(
		FVector(0.0f, -1.0f, 0.0f),
		FVector(0.0f, 0.0f, -1.0f),
		FVector(1.0f, 0.0f, 0.0f),
		FVector::ZeroVector).Rotator();
	PhotoMesh->SetRelativeRotation(PhotoPlaneRotation);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		FrameMesh->SetStaticMesh(CubeMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMeshFinder(TEXT("/Engine/BasicShapes/Plane"));
	if (PlaneMeshFinder.Succeeded())
	{
		PhotoMesh->SetStaticMesh(PlaneMeshFinder.Object);
	}

	PhotoMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/CommonActor/M_PhotoSurface.M_PhotoSurface")));

	UpdatePhotoLayout(CurrentAspectRatio);
}

void APhotoActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (bAutoSizeToAspectRatio)
	{
		UpdatePhotoLayout(CurrentAspectRatio);
	}

	if (PhotoMesh && !PhotoDynamicMaterial)
	{
		if (UMaterialInterface* BaseMaterial = PhotoMaterial.IsNull() ? nullptr : PhotoMaterial.LoadSynchronous())
		{
			PhotoMesh->SetMaterial(0, BaseMaterial);
		}
	}
}

void APhotoActor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoSizeToAspectRatio)
	{
		UpdatePhotoLayout(CurrentAspectRatio);
	}

	EnsurePhotoDynamicMaterial();
	ApplyHoverVisuals(bIsHovered);

	if (IsValid(LinkedCutPoint))
	{
		LinkedCutPoint->RegisterPhotoActor(this);
	}
}

void APhotoActor::UpdatePhotoLayout(const float AspectRatio)
{
	if (!PhotoMesh || !FrameMesh)
	{
		return;
	}

	const float SafeAspect = FMath::Max(AspectRatio, 0.1f);
	const float DisplayHeight = FMath::Max(PhotoDisplayHeight, 1.0f);
	const float DisplayWidth = DisplayHeight * SafeAspect;
	const float Thick = FMath::Max(FrameThickness, 0.5f);
	const float Border = FMath::Max(FrameBorderSize, 0.0f);

	// /Engine/BasicShapes/Plane and Cube both span 100cm per unit scale.
	FrameMesh->SetRelativeLocation(FVector::ZeroVector);
	FrameMesh->SetRelativeScale3D(FVector(
		Thick / 100.0f,
		(DisplayWidth + 2.0f * Border) / 100.0f,
		(DisplayHeight + 2.0f * Border) / 100.0f));

	PhotoMesh->SetRelativeLocation(FVector(Thick * 0.5f + 0.25f, 0.0f, 0.0f));
	PhotoMesh->SetRelativeScale3D(FVector(
		DisplayWidth / 100.0f,
		DisplayHeight / 100.0f,
		1.0f));
}

void APhotoActor::EnsurePhotoDynamicMaterial()
{
	if (!PhotoMesh)
	{
		return;
	}

	if (IsValid(PhotoDynamicMaterial))
	{
		if (PhotoMesh->GetMaterial(0) != PhotoDynamicMaterial)
		{
			PhotoMesh->SetMaterial(0, PhotoDynamicMaterial);
		}
		return;
	}

	UMaterialInterface* BaseMaterial = PhotoMaterial.IsNull() ? nullptr : PhotoMaterial.LoadSynchronous();
	if (!BaseMaterial)
	{
		BaseMaterial = PhotoMesh->GetMaterial(0);
	}

	if (!BaseMaterial)
	{
		return;
	}

	if (UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(BaseMaterial))
	{
		PhotoDynamicMaterial = ExistingMID;
	}
	else
	{
		PhotoDynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	}

	if (IsValid(PhotoDynamicMaterial))
	{
		PhotoMesh->SetMaterial(0, PhotoDynamicMaterial);
	}
}

void APhotoActor::ApplyCapturedPhoto(UTexture* Texture, const float AspectRatio)
{
	CurrentPhotoTexture = Texture;
	if (AspectRatio > 0.0f)
	{
		CurrentAspectRatio = AspectRatio;
	}

	if (bAutoSizeToAspectRatio)
	{
		UpdatePhotoLayout(CurrentAspectRatio);
	}

	EnsurePhotoDynamicMaterial();
	if (IsValid(PhotoDynamicMaterial))
	{
		PhotoDynamicMaterial->SetTextureParameterValue(TextureParameterName, Texture);
	}

	OnPhotoTextureUpdated(Texture, CurrentAspectRatio);
}

void APhotoActor::ApplyHoverVisuals(const bool bEnable)
{
	auto ApplyToComponent = [this, bEnable](UStaticMeshComponent* Comp)
	{
		if (!Comp)
		{
			return;
		}
		Comp->SetRenderCustomDepth(bEnable);
		Comp->SetCustomDepthStencilValue(bEnable ? HoverStencilValue : 0);
	};

	ApplyToComponent(FrameMesh);
	ApplyToComponent(PhotoMesh);
}

void APhotoActor::SetHovered(const bool bNewHovered)
{
	const bool bChanged = (bIsHovered != bNewHovered);
	bIsHovered = bNewHovered;
	ApplyHoverVisuals(bIsHovered);

	if (bChanged)
	{
		OnHoverStateChanged(bIsHovered);
	}
}

bool APhotoActor::CanInteractFromLocation(const FVector& InteractorLocation) const
{
	if (MaxInteractionDistance <= 0.0f)
	{
		return true;
	}

	return FVector::DistSquared(GetActorLocation(), InteractorLocation) <= FMath::Square(MaxInteractionDistance);
}

ACutActor* APhotoActor::Interact(AActor* Interactor)
{
	if (!IsValid(LinkedCutPoint))
	{
		return nullptr;
	}

	if (IsValid(Interactor) && !CanInteractFromLocation(Interactor->GetActorLocation()))
	{
		return nullptr;
	}

	ACutActor* CutBefore = LinkedCutPoint->GetSpawnedCutActor();
	const UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this);
	const bool bWasInList = IsValid(CutBefore) && Subsystem && Subsystem->GetCutActors().Contains(CutBefore);

	ACutActor* ActivatedCut = LinkedCutPoint->ActivateOrSpawnCutActor();
	const bool bSpawnedNew = IsValid(ActivatedCut) && (!bWasInList || ActivatedCut != CutBefore);

	OnPhotoInteracted(ActivatedCut, bSpawnedNew);
	return ActivatedCut;
}
