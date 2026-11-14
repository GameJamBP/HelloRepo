#include "SelectableActorShell.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "CutActor.h"
#include "Kismet/GameplayStatics.h"
#include "CollisionShape.h"
#include "GameFramework/Pawn.h"

ASelectableActorShell::ASelectableActorShell()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

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
	CheckPlayerPortalContact();
}

void ASelectableActorShell::CheckPlayerPortalContact()
{
	if (!bIsSelected)
	{
		PlayerPortalStatus = TEXT("Teleport disabled: Actor is not selected");
		// Discard contact and movement history while this shell is not selected.
		bHasPreviousPlayerLocation = false;
		bPlayerWasTouching = false;
		return;
	}

	UFilmStyleSubsystem* Context = GetWorld()->GetSubsystem<UFilmStyleSubsystem>();
	if (!Context || !Context->bFilmMode)
	{
		PlayerPortalStatus = TEXT("Teleport disabled: FilmMode is off");
		// Do not sweep movement accumulated while teleporting was disabled.
		bHasPreviousPlayerLocation = false;
		bPlayerWasTouching = false;
		return;
	}
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	UPrimitiveComponent* PlayerCollision = Player ? Cast<UPrimitiveComponent>(Player->GetRootComponent()) : nullptr;
	ACutActor* Cut = Context ? Context->ActiveCutActor.Get() : nullptr;
	// Selection, FilmMode and an active destination are required; visual mask state is independent.
	if (!PlayerCollision || !IsValid(Cut))
	{
		PlayerPortalStatus = !PlayerCollision ? TEXT("No player collision component") : TEXT("No ActiveCutActor");
		bHasPreviousPlayerLocation = false;
		bPlayerWasTouching = false;
		return;
	}
	const FVector Location = PlayerCollision->GetComponentLocation();
	const FVector Start = bHasPreviousPlayerLocation ? PreviousPlayerLocation : Location;
	// Character roots provide their scaled capsule; sphere/box pawn roots also work.
	const FCollisionShape Probe = PlayerCollision->GetCollisionShape(FMath::Max(0.f, PlayerContactMargin));
	const FQuat ProbeRotation = PlayerCollision->GetComponentQuat();
	bool bTouching = false;
	// Include both ChildActorComponents and actors attached in the level hierarchy.
	// GetComponents(..., true) alone only descends through ChildActorComponents.
	TArray<AActor*> PendingActors;
	TSet<AActor*> VisitedActors;
	PendingActors.Add(this);
	TArray<UStaticMeshComponent*> Components;
	while (!PendingActors.IsEmpty())
	{
		AActor* Actor = PendingActors.Pop(EAllowShrinking::No);
		if (!IsValid(Actor) || VisitedActors.Contains(Actor)) continue;
		VisitedActors.Add(Actor);
		TInlineComponentArray<UStaticMeshComponent*> ActorMeshes;
		Actor->GetComponents<UStaticMeshComponent>(ActorMeshes, true);
		for (UStaticMeshComponent* Mesh : ActorMeshes) Components.AddUnique(Mesh);
		TArray<AActor*> AttachedActors;
		Actor->GetAttachedActors(AttachedActors);
		PendingActors.Append(AttachedActors);
		TArray<AActor*> ChildActors;
		Actor->GetAllChildActors(ChildActors, true);
		PendingActors.Append(ChildActors);
	}
	int32 QueryMeshCount = 0;
	for (UStaticMeshComponent* Component : Components)
	{
		if (!IsValid(Component) || Component->IsEditorOnly() || !Component->IsQueryCollisionEnabled()) continue;
		++QueryMeshCount;
		FHitResult Hit;
		// Direct component geometry queries ignore Pawn/Camera channel responses
		// and do not require GenerateOverlapEvents, so pass-through meshes trigger.
		if (Component->OverlapComponent(Location, ProbeRotation, Probe)
			|| Component->SweepComponent(Hit, Start, Location, ProbeRotation, Probe)
			|| Component->SweepComponent(Hit, Start, Location, ProbeRotation, Probe, true))
		{
			bTouching = true;
			break;
		}
	}
	PreviousPlayerLocation = Location;
	bHasPreviousPlayerLocation = true;
	if (bTouching && !bPlayerWasTouching)
	{
		if (Cut->TeleportPlayerToCapture())
		{
			// Never sweep the teleport displacement on the following frame.
			PreviousPlayerLocation = PlayerCollision->GetComponentLocation();
			PlayerPortalStatus = TEXT("Teleported; waiting for player to leave mesh");
			bPlayerWasTouching = true;
		}
		else
		{
			PlayerPortalStatus = TEXT("Contact detected, teleport rejected (see Output Log)");
			// Retry while touching if the destination was not initialized yet.
			bPlayerWasTouching = false;
		}
		return;
	}
	if (!bTouching)
	{
		PlayerPortalStatus = QueryMeshCount > 0 ? TEXT("Waiting for player contact") : TEXT("No static mesh with query collision");
	}
	bPlayerWasTouching = bTouching;
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

		if (bIsSelected || bTemporarilyActive)
		{
			Comp->SetRenderCustomDepth(true);
			Comp->SetCustomDepthStencilValue(bIsSelected ? ClampedStencil : FMath::Clamp(HoverStencilValue, 1, 255));
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
