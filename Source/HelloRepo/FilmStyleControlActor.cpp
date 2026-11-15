#include "FilmStyleControlActor.h"
#include "FilmStyleSetting.h"
#include "FilmStyleSubsystem.h"
#include "SelectableActorShell.h"
#include "CutActor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/Class.h"

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

	auto* Context = UFilmStyleSubsystem::Get(this);
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

	if (bSelectionRequested)
	{
		bSelectionRequested = false;
		if (IsValid(Target))
		{
			Target->SetActorSelected(!Target->bIsSelected);
		}
	}
}

void AFilmStyleControlActor::BeginSelection()
{
	bSelectionRequested = true;
	if (IsValid(FilmStyleSetting))
	{
		FilmStyleSetting->SetSelectionPressed(true);
	}
}

void AFilmStyleControlActor::EndSelection()
{
	if (IsValid(FilmStyleSetting))
	{
		FilmStyleSetting->SetSelectionPressed(false);
	}
}

void AFilmStyleControlActor::CancelSelection()
{
	bSelectionRequested = false;
	EndSelection();
}

void AFilmStyleControlActor::ToggleFilmMask()
{
	auto* Context = UFilmStyleSubsystem::Get(this);
	if (!Context || !IsValid(FilmStyleSetting))
	{
		return;
	}

	ESelectFilmMaskMode Mode = ESelectFilmMaskMode::Checkerboard;
	if (FilmStyleSetting->FilmMaskMode == ESelectFilmMaskMode::Checkerboard)
	{
		// Preserve the current Cut when toggling back from checkerboard.
		Mode = IsValid(Context->GetActiveCutActor())
			? ESelectFilmMaskMode::RenderTarget
			: ESelectFilmMaskMode::SolidColor;
	}
	FilmStyleSetting->SetFilmMaskMode(Mode);
	FilmStyleSetting->SetFilmMaskEnabled(true);
	FilmStyleSetting->RefreshActiveCutTexture();
	FilmStyleSetting->SyncStateToSubsystem();
}

void AFilmStyleControlActor::NextCut()
{
	ScrollCut(1);
}

void AFilmStyleControlActor::PreviousCut()
{
	ScrollCut(-1);
}

void AFilmStyleControlActor::ScrollCut(int32 Direction)
{
	if (!IsValid(FilmStyleSetting))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 5.f, FColor::Red,
				FString::Printf(TEXT("[%s] ScrollCut stopped: FilmStyleSetting is invalid."), *GetName()));
		}
		return;
	}

	if (FilmStyleSetting->FilmMaskMode != ESelectFilmMaskMode::RenderTarget && FilmStyleSetting->FilmMaskMode != ESelectFilmMaskMode::SolidColor)
	{
		if (GEngine)
		{
			const int32 ModeValue = static_cast<int32>(FilmStyleSetting->FilmMaskMode);
			const FString ModeName = StaticEnum<ESelectFilmMaskMode>()->GetNameStringByValue(ModeValue);
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 5.f, FColor::Yellow,
				FString::Printf(TEXT("[%s] ScrollCut stopped: FilmMaskMode=%s (%d). Expected RenderTarget or SolidColor."),
					*GetName(), *ModeName, ModeValue));
		}
		return;
	}

	auto* Context = UFilmStyleSubsystem::Get(this);
	if (!Context)
	{
		return;
	}
	TArray<ACutActor*> Cuts;
	for (auto Cut : Context->GetCutActors())
	{
		if (IsValid(Cut))
		{
			Cuts.Add(Cut.Get());
		}
	}

	const int32 CutCount = Cuts.Num();
	if (CutCount == 0)
	{
		ActiveCutIndex = INDEX_NONE;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 5.f, FColor::Yellow,
				FString::Printf(TEXT("[%s] ScrollCut stopped: no valid cuts (CutCount=0)."), *GetName()));
		}
		return;
	}
	if (Direction == 0) return;

	const int32 CurrentCutIndex = Cuts.IndexOfByKey(Context->GetActiveCutActor());
	const int32 Step = Direction > 0 ? 1 : -1;
	// An unselected cut enters at the first/last photo; cycling has no empty slot.
	ActiveCutIndex = CurrentCutIndex == INDEX_NONE
		? (Step > 0 ? 0 : CutCount - 1)
		: (CurrentCutIndex + Step + CutCount) % CutCount;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 5.f, FColor::Cyan,
			FString::Printf(TEXT("[%s] ScrollCut request: CutCount=%d, CurrentIndex=%d, NextIndex=%d, Step=%d."),
				*GetName(), CutCount, CurrentCutIndex, ActiveCutIndex, Step));
	}
	Cuts[ActiveCutIndex]->SetActiveCutActor();

	if (IsValid(FilmStyleSetting))
	{
		FilmStyleSetting->SetFilmMaskMode(ESelectFilmMaskMode::RenderTarget);
		FilmStyleSetting->RefreshActiveCutTexture();
	}
}

void AFilmStyleControlActor::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelSelection();
	if (auto* Context = UFilmStyleSubsystem::Get(this))
	{
		Context->SetTemporaryActiveActor(nullptr);
	}
	Super::EndPlay(Reason);
}
