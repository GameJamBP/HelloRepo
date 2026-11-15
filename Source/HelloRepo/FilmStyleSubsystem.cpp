#include "FilmStyleSubsystem.h"
#include "CutActor.h"
#include "FilmStyleSetting.h"
#include "PhotoActor.h"
#include "SelectableActorShell.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UFilmStyleSubsystem* UFilmStyleSubsystem::Get(const UObject* WorldContextObject)
{
	if (!IsValid(WorldContextObject)) return nullptr;
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
		: WorldContextObject->GetWorld();
	return World ? World->GetSubsystem<UFilmStyleSubsystem>() : nullptr;
}

TArray<ACutActor*> UFilmStyleSubsystem::GetCutActorsForBlueprint() const
{
	TArray<ACutActor*> Actors;
	Actors.Reserve(CutActors.Num());
	for (ACutActor* Actor : CutActors)
	{
		Actors.Add(Actor);
	}
	return Actors;
}

void UFilmStyleSubsystem::SetCutActors(const TArray<ACutActor*>& InCutActors)
{
	TArray<TWeakObjectPtr<ACutActor>> Actors;
	Actors.Reserve(InCutActors.Num());
	for (ACutActor* Actor : InCutActors) Actors.Add(Actor);
	RunCutStateChange([this, Actors = MoveTemp(Actors)] { ApplyCutActors(Actors); });
}

void UFilmStyleSubsystem::ApplyCutActors(const TArray<TWeakObjectPtr<ACutActor>>& Actors)
{
	const TArray<TObjectPtr<ACutActor>> PreviousCutActors = CutActors;

	TArray<TObjectPtr<ACutActor>> NewActors;
	NewActors.Reserve(Actors.Num());
	for (const TWeakObjectPtr<ACutActor>& WeakActor : Actors)
	{
		ACutActor* Actor = WeakActor.Get();
		if (IsValid(Actor) && Actor->GetWorld() == GetWorld())
		{
			NewActors.AddUnique(Actor);
		}
	}
	for (ACutActor* Actor : CutActors)
	{
		if (IsValid(Actor) && !NewActors.Contains(Actor))
		{
			Actor->SetCaptureActive(false);
		}
	}
	CutActors = MoveTemp(NewActors);
	TrimCutActorsToLimit();
	if (CutActors != PreviousCutActors)
	{
		CutActorsChangedNative.Broadcast();
	}
	if (ActiveCutActor && !CutActors.Contains(ActiveCutActor))
	{
		ApplyActiveCutActor(nullptr);
	}
}

void UFilmStyleSubsystem::RegisterCutActor(ACutActor* Actor)
{
	const TWeakObjectPtr<ACutActor> WeakActor(Actor);
	RunCutStateChange([this, WeakActor]
	{
		ApplyRegisterCutActor(WeakActor.Get());
		if (ActiveCutActor && !CutActors.Contains(ActiveCutActor))
		{
			ApplyActiveCutActor(nullptr);
		}
	});
}

void UFilmStyleSubsystem::ApplyRegisterCutActor(ACutActor* Actor)
{
	if (!IsValid(Actor) || Actor->GetWorld() != GetWorld()) return;
	const TArray<TObjectPtr<ACutActor>> PreviousCutActors = CutActors;
	CutActors.RemoveAll([](const TObjectPtr<ACutActor>& Cut) { return !IsValid(Cut); });
	CutActors.AddUnique(Actor);
	TrimCutActorsToLimit();
	if (CutActors != PreviousCutActors)
	{
		CutActorsChangedNative.Broadcast();
	}
}

void UFilmStyleSubsystem::TrimCutActorsToLimit()
{
	const int32 ExcessCount = CutActors.Num() - MaxCutActors;
	if (ExcessCount <= 0) return;
	for (int32 Index = 0; Index < ExcessCount; ++Index)
	{
		if (ACutActor* Cut = CutActors[Index].Get(); IsValid(Cut))
		{
			Cut->SetCaptureActive(false);
		}
	}
	CutActors.RemoveAt(0, ExcessCount, EAllowShrinking::No);
}

void UFilmStyleSubsystem::UnregisterCutActor(ACutActor* Actor)
{
	const TWeakObjectPtr<ACutActor> ActorToRemove(Actor);
	RunCutStateChange([this, ActorToRemove]
	{
		const TArray<TObjectPtr<ACutActor>> PreviousCutActors = CutActors;
		// Compare weak identities even when EndPlay has already invalidated the actor.
		CutActors.RemoveAll([ActorToRemove](const TObjectPtr<ACutActor>& Cut)
		{
			return !IsValid(Cut) || TWeakObjectPtr<ACutActor>(Cut.Get()).HasSameIndexAndSerialNumber(ActorToRemove);
		});
		if (CutActors != PreviousCutActors)
		{
			CutActorsChangedNative.Broadcast();
		}
		if (TWeakObjectPtr<ACutActor>(ActiveCutActor.Get()).HasSameIndexAndSerialNumber(ActorToRemove))
		{
			ApplyActiveCutActor(nullptr);
		}
	});
}

void UFilmStyleSubsystem::ClearCutActors()
{
	SetCutActors({});
}

void UFilmStyleSubsystem::SetActiveCutActor(ACutActor* Actor)
{
	const TWeakObjectPtr<ACutActor> WeakActor(Actor);
	const bool bClear = Actor == nullptr;
	RunCutStateChange([this, WeakActor, bClear]
	{
		// An expired queued actor is ignored, rather than interpreted as an explicit clear.
		if (bClear) ApplyActiveCutActor(nullptr);
		else if (ACutActor* Cut = WeakActor.Get()) ApplyActiveCutActor(Cut);
	});
}

void UFilmStyleSubsystem::RunCutStateChange(TFunction<void()> Change)
{
	PendingCutStateChanges.Add(MoveTemp(Change));
	if (bIsChangingCutState) return;

	TGuardValue<bool> ChangingGuard(bIsChangingCutState, true);
	int32 ChangeIndex = 0;
	while (ChangeIndex < PendingCutStateChanges.Num())
	{
		// Move out before executing: listeners may append and reallocate the queue.
		TFunction<void()> PendingChange = MoveTemp(PendingCutStateChanges[ChangeIndex++]);
		PendingChange();
	}
	PendingCutStateChanges.Reset();
}

void UFilmStyleSubsystem::ApplyActiveCutActor(ACutActor* Actor)
{
	if (ActiveCutActor == Actor) return;
	// Pending actors can have changed lifecycle state before their queued turn.
	if (Actor && (!IsValid(Actor) || Actor->GetWorld() != GetWorld() || Actor->IsActorBeingDestroyed()
		|| (!Actor->HasActorBegunPlay() && !Actor->IsActorBeginningPlay()))) return;

	ACutActor* PreviousCutActor = ActiveCutActor.Get();
	if (IsValid(PreviousCutActor)) PreviousCutActor->SetCaptureActive(false);
	if (Actor) ApplyRegisterCutActor(Actor);
	for (ACutActor* Cut : CutActors)
	{
		if (IsValid(Cut)) Cut->SetCaptureActive(false);
	}
	if (Actor)
	{
		Actor->UpdateRenderTarget();
		Actor->SetCaptureActive(bFilmMode);
	}
	ActiveCutActor = Actor;
	RefreshActiveCutConsumers();

	ActiveCutActorChangedNative.Broadcast(PreviousCutActor, Actor);
	OnActiveCutActorChanged.Broadcast(PreviousCutActor, Actor);
}

void UFilmStyleSubsystem::SyncActiveCutCaptureState()
{
	ACutActor* Active = ActiveCutActor.Get();
	for (ACutActor* Cut : CutActors)
	{
		if (IsValid(Cut))
		{
			Cut->SetCaptureActive(bFilmMode && Cut == Active);
		}
	}
	if (IsValid(Active))
	{
		Active->SetCaptureActive(bFilmMode);
		if (bFilmMode)
		{
			Active->UpdateRenderTarget();
		}
	}
}

void UFilmStyleSubsystem::RefreshActiveCutConsumers()
{
	for (TActorIterator<AFilmStyleSetting> It(GetWorld()); It; ++It)
	{
		It->RefreshActiveCutTexture();
	}
}

void UFilmStyleSubsystem::SetFilmMode(bool bInFilmMode)
{
	const bool bChanged = (bFilmMode != bInFilmMode);
	bFilmMode = bInFilmMode;
	RunCutStateChange([this]
	{
		SyncActiveCutCaptureState();
	});
	if (bChanged)
	{
		FilmModeChangedNative.Broadcast(bFilmMode);
	}
}

void UFilmStyleSubsystem::SetSelectActorData(const FSelectActorStateData& InData)
{
	if (SelectActorData == InData)
	{
		return;
	}
	SelectActorData = InData;
	SelectActorDataChangedNative.Broadcast(SelectActorData);
}

void UFilmStyleSubsystem::SetTemporaryActiveActor(ASelectableActorShell* Actor)
{
	if (Actor && (!IsValid(Actor) || Actor->GetWorld() != GetWorld())) return;
	if (Actor)
	{
		SetTemporaryHoveredPhotoActor(nullptr);
	}
	if (!TemporaryActiveActor.IsStale() && TemporaryActiveActor.Get() == Actor)
	{
		return;
	}
	ASelectableActorShell* Old = TemporaryActiveActor.Get();
	if (Old)
	{
		Old->SetTemporarilyActive(false);
	}
	TemporaryActiveActor = Actor;
	if (IsValid(Actor))
	{
		Actor->SetTemporarilyActive(true);
	}
	TemporaryActiveActorChangedNative.Broadcast(Old, Actor);
}

ASelectableActorShell* UFilmStyleSubsystem::GetTemporaryActiveActor() const
{
	return TemporaryActiveActor.Get();
}

void UFilmStyleSubsystem::SetTemporaryHoveredPhotoActor(APhotoActor* Actor)
{
	if (Actor && (!IsValid(Actor) || Actor->GetWorld() != GetWorld())) return;
	if (Actor)
	{
		SetTemporaryActiveActor(nullptr);
	}
	if (!TemporaryHoveredPhotoActor.IsStale() && TemporaryHoveredPhotoActor.Get() == Actor)
	{
		return;
	}
	APhotoActor* Old = TemporaryHoveredPhotoActor.Get();
	if (Old)
	{
		Old->SetHovered(false);
	}
	TemporaryHoveredPhotoActor = Actor;
	if (IsValid(Actor))
	{
		Actor->SetHovered(true);
	}
	HoveredPhotoActorChangedNative.Broadcast(Old, Actor);
	OnHoveredPhotoActorChanged.Broadcast(Old, Actor);
}

APhotoActor* UFilmStyleSubsystem::GetTemporaryHoveredPhotoActor() const
{
	return TemporaryHoveredPhotoActor.Get();
}
