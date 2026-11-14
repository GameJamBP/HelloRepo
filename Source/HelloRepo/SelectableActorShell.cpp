#include "SelectableActorShell.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ShapeComponent.h"
#include "Engine/World.h"

ASelectableActorShell::ASelectableActorShell()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	SetRootComponent(DefaultSceneRoot);
}

void ASelectableActorShell::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateSelectionState();
}

void ASelectableActorShell::BeginPlay()
{
	Super::BeginPlay();
	UpdateSelectionState();
}

void ASelectableActorShell::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetPlayerPassThroughEnabled(false);
	SetRenderablesHiddenByFilmMode(false);
	Super::EndPlay(EndPlayReason);
}

void ASelectableActorShell::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (const UFilmStyleSubsystem* Subsystem = World->GetSubsystem<UFilmStyleSubsystem>())
	{
		CachedSelectActorData = Subsystem->SelectActorData;
		bIsSelectActorRecording = Subsystem->bFilmMode;
	}
	else
	{
		CachedSelectActorData = FSelectActorStateData{};
		bIsSelectActorRecording = false;
	}

	RefreshEffectsFromSubsystemState();
}

#if WITH_EDITOR
void ASelectableActorShell::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateSelectionState();
}
#endif

bool ASelectableActorShell::IsRenderablePrimitive(const UPrimitiveComponent* Comp)
{
	if (!IsValid(Comp) || Comp->IsEditorOnly())
	{
		return false;
	}

	// Pure collision shape components (Box, Sphere, Capsule) are not visual meshes.
	if (Comp->IsA<UShapeComponent>())
	{
		return false;
	}

	return true;
}

void ASelectableActorShell::SetActorSelected(bool bNewSelected)
{
	bIsSelected = bNewSelected;
	UpdateSelectionState();
}

void ASelectableActorShell::UpdateSelectionState()
{
	const int32 ClampedStencil = FMath::Clamp(CustomDepthStencilValue, 1, 255);

	TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
	GetComponents<UPrimitiveComponent>(PrimComponents, true);

	for (UPrimitiveComponent* Comp : PrimComponents)
	{
		if (!IsRenderablePrimitive(Comp))
		{
			continue;
		}

		if (bIsSelected)
		{
			Comp->SetRenderCustomDepth(true);
			Comp->SetCustomDepthStencilValue(ClampedStencil);
		}
		else
		{
			Comp->SetRenderCustomDepth(false);
		}
	}

	RefreshEffectsFromSubsystemState();
}

void ASelectableActorShell::RefreshEffectsFromSubsystemState()
{
	const bool bShouldPassThrough = bIsSelected && CachedSelectActorData.IsPassThroughState();
	if (bShouldPassThrough != bIsPlayerPassThroughActive)
	{
		SetPlayerPassThroughEnabled(bShouldPassThrough);
	}

	const bool bShouldHideRenderables = bIsSelected && bIsSelectActorRecording && CachedSelectActorData.IsCheckerboardState();
	if (bShouldHideRenderables != bAreRenderablesHiddenByFilmMode)
	{
		SetRenderablesHiddenByFilmMode(bShouldHideRenderables);
	}
}

void ASelectableActorShell::SetPlayerPassThroughEnabled(bool bEnablePassThrough)
{
	if (bEnablePassThrough)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
		GetComponents<UPrimitiveComponent>(PrimComponents, true);

		for (UPrimitiveComponent* Comp : PrimComponents)
		{
			if (!IsValid(Comp) || Comp->IsEditorOnly())
			{
				continue;
			}

			TWeakObjectPtr<UPrimitiveComponent> WeakComp(Comp);
			if (!CachedCollisions.Contains(WeakComp))
			{
				FCachedCollisionState OriginalState;
				OriginalState.PawnResponse = Comp->GetCollisionResponseToChannel(ECC_Pawn);
				OriginalState.CameraResponse = Comp->GetCollisionResponseToChannel(ECC_Camera);
				CachedCollisions.Add(WeakComp, OriginalState);
			}

			Comp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
			Comp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		}

		bIsPlayerPassThroughActive = true;
	}
	else
	{
		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FCachedCollisionState>& Pair : CachedCollisions)
		{
			if (UPrimitiveComponent* Comp = Pair.Key.Get())
			{
				Comp->SetCollisionResponseToChannel(ECC_Pawn, Pair.Value.PawnResponse);
				Comp->SetCollisionResponseToChannel(ECC_Camera, Pair.Value.CameraResponse);
			}
		}

		CachedCollisions.Empty();
		bIsPlayerPassThroughActive = false;
	}
}

void ASelectableActorShell::SetRenderablesHiddenByFilmMode(bool bHideRenderables)
{
	if (bHideRenderables)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
		GetComponents<UPrimitiveComponent>(PrimComponents, true);

		for (UPrimitiveComponent* Comp : PrimComponents)
		{
			if (!IsRenderablePrimitive(Comp))
			{
				continue;
			}

			TWeakObjectPtr<UPrimitiveComponent> WeakComp(Comp);
			if (!CachedVisibilities.Contains(WeakComp))
			{
				FCachedVisibilityState OriginalVis;
				OriginalVis.bVisible = Comp->IsVisible();
				OriginalVis.bHiddenInGame = Comp->bHiddenInGame;
				CachedVisibilities.Add(WeakComp, OriginalVis);
			}

			Comp->SetVisibility(false, false);
			Comp->SetHiddenInGame(true, false);
		}

		bAreRenderablesHiddenByFilmMode = true;
	}
	else
	{
		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>, FCachedVisibilityState>& Pair : CachedVisibilities)
		{
			if (UPrimitiveComponent* Comp = Pair.Key.Get())
			{
				Comp->SetVisibility(Pair.Value.bVisible, false);
				Comp->SetHiddenInGame(Pair.Value.bHiddenInGame, false);
			}
		}

		CachedVisibilities.Empty();
		bAreRenderablesHiddenByFilmMode = false;
	}
}
