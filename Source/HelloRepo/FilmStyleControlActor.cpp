#include "FilmStyleControlActor.h"
#include "FilmStyleSetting.h"
#include "FilmStyleSubsystem.h"
#include "SelectableActorShell.h"
#include "CutActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "InputCoreTypes.h"

AFilmStyleControlActor::AFilmStyleControlActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void AFilmStyleControlActor::BeginPlay()
{
	Super::BeginPlay();

	if (!IsValid(FilmStyleSetting))
	{
		for (TActorIterator<AFilmStyleSetting> It(GetWorld()); It; ++It)
		{
			FilmStyleSetting = *It;
			break;
		}
	}
	ACutActor::ClearActive(this);
}

void AFilmStyleControlActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	auto* Context = GetWorld()->GetSubsystem<UFilmStyleSubsystem>();
	auto* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!Context)
	{
		return;
	}

	ASelectableActorShell* Target = nullptr;
	if (PC && PC->IsLocalController())
	{
		int32 Width;
		int32 Height;
		PC->GetViewportSize(Width, Height);
		FVector Start;
		FVector Direction;
		if (Width > 0 && Height > 0 && PC->DeprojectScreenPositionToWorld(Width * 0.5f, Height * 0.5f, Start, Direction))
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(FilmStyleSelection), true, this);
			if (PC->GetPawn())
			{
				Params.AddIgnoredActor(PC->GetPawn());
			}
			if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Direction * TraceDistance, TraceChannel, Params))
			{
				for (AActor* Actor = Hit.GetActor(); Actor; Actor = Actor->GetAttachParentActor())
				{
					Target = Cast<ASelectableActorShell>(Actor);
					if (Target)
					{
						break;
					}
				}
			}
		}
	}
	Context->SetTemporaryActiveActor(Target);

	const bool bPressed = PC && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	if (bPressed != bWasPressed)
	{
		if (bPressed && Target)
		{
			Target->SetActorSelected(!Target->bIsSelected);
		}
		if (IsValid(FilmStyleSetting))
		{
			FilmStyleSetting->SetSelectionPressed(bPressed);
		}
		bWasPressed = bPressed;
	}

	if (!PC || !IsValid(FilmStyleSetting))
	{
		return;
	}

	if (PC->WasInputKeyJustPressed(EKeys::F))
	{
		ESelectFilmMaskMode Mode = ESelectFilmMaskMode::Checkerboard;
		if (FilmStyleSetting->FilmMaskMode == ESelectFilmMaskMode::Checkerboard)
		{
			// Preserve the current Cut when toggling back from checkerboard.
			Mode = IsValid(Context->ActiveCutActor)
				? ESelectFilmMaskMode::RenderTarget
				: ESelectFilmMaskMode::SolidColor;
		}
		FilmStyleSetting->SetFilmMaskMode(Mode);
		FilmStyleSetting->SetFilmMaskEnabled(true);
		FilmStyleSetting->RefreshActiveCutTexture();
		FilmStyleSetting->SyncStateToSubsystem();
	}

	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		ScrollCut(1);
	}

	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		ScrollCut(-1);
	}
}

void AFilmStyleControlActor::ScrollCut(int32 Direction)
{
	if (FilmStyleSetting->FilmMaskMode != ESelectFilmMaskMode::RenderTarget && FilmStyleSetting->FilmMaskMode != ESelectFilmMaskMode::SolidColor)
	{
		return;
	}

	auto* Context = GetWorld()->GetSubsystem<UFilmStyleSubsystem>();
	TArray<ACutActor*> Cuts;
	for (auto Cut : Context->CutActors)
	{
		if (IsValid(Cut))
		{
			Cuts.Add(Cut.Get());
		}
	}

	ActiveCutIndex = Cuts.IndexOfByKey(Context->ActiveCutActor.Get());
	ActiveCutIndex = (ActiveCutIndex + 1 + Direction + Cuts.Num() + 1) % (Cuts.Num() + 1) - 1;
	if (ActiveCutIndex == -1)
	{
		ACutActor::ClearActive(this);
	}
	else
	{
		Cuts[ActiveCutIndex]->SetActiveCutActor();
	}

	FilmStyleSetting->SetFilmMaskMode(ActiveCutIndex == -1 ? ESelectFilmMaskMode::SolidColor : ESelectFilmMaskMode::RenderTarget);
	FilmStyleSetting->RefreshActiveCutTexture();
}

void AFilmStyleControlActor::EndPlay(const EEndPlayReason::Type Reason)
{
	if (auto* Context = GetWorld()->GetSubsystem<UFilmStyleSubsystem>())
	{
		Context->SetTemporaryActiveActor(nullptr);
	}
	if (IsValid(FilmStyleSetting))
	{
		FilmStyleSetting->SetSelectionPressed(false);
	}

	Super::EndPlay(Reason);
}
