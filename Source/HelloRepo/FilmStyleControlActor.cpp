#include "FilmStyleControlActor.h"
#include "FilmStyleSetting.h"
#include "FilmStyleSubsystem.h"
#include "PhotoActor.h"
#include "PhotoHoverPromptWidget.h"
#include "SelectableActorShell.h"
#include "CutActor.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

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

	if (auto* Context = UFilmStyleSubsystem::Get(this))
	{
		HoveredPhotoChangedHandle = Context->GetOnHoveredPhotoActorChangedNative().AddUObject(
			this, &AFilmStyleControlActor::HandleHoveredPhotoActorChanged);
		TemporaryActiveActorChangedHandle = Context->GetOnTemporaryActiveActorChangedNative().AddUObject(
			this, &AFilmStyleControlActor::HandleTemporaryActiveActorChanged);
		CutActorsChangedHandle = Context->GetOnCutActorsChangedNative().AddUObject(
			this, &AFilmStyleControlActor::HandleCutActorsChanged);
		ActiveCutActorChangedHandle = Context->GetOnActiveCutActorChangedNative().AddUObject(
			this, &AFilmStyleControlActor::HandleActiveCutActorChanged);
		SelectActorDataChangedHandle = Context->GetOnSelectActorDataChangedNative().AddUObject(
			this, &AFilmStyleControlActor::HandleSelectActorDataChanged);
		FilmModeChangedHandle = Context->GetOnFilmModeChangedNative().AddUObject(
			this, &AFilmStyleControlActor::HandleFilmModeChanged);
		RefreshHoverPrompt();
	}
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
	APhotoActor* HoveredPhoto = nullptr;
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
			const APawn* Pawn = PC->GetPawn();
			if (Pawn)
			{
				Params.AddIgnoredActor(Pawn);
			}
			const FVector InteractorLocation = Pawn ? Pawn->GetActorLocation() : Start;
			if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start + Direction * TraceDistance, TraceChannel, Params))
			{
				for (AActor* Actor = Hit.GetActor(); Actor; Actor = Actor->GetAttachParentActor())
				{
					if (ASelectableActorShell* Selectable = Cast<ASelectableActorShell>(Actor))
					{
						Target = Selectable;
						break;
					}
					if (APhotoActor* Photo = Cast<APhotoActor>(Actor))
					{
						if (Photo->CanInteractFromLocation(InteractorLocation))
						{
							HoveredPhoto = Photo;
						}
						break;
					}
				}
			}
		}
	}
	if (IsValid(Target))
	{
		Context->SetTemporaryActiveActor(Target);
	}
	else if (IsValid(HoveredPhoto))
	{
		Context->SetTemporaryHoveredPhotoActor(HoveredPhoto);
	}
	else
	{
		Context->SetTemporaryActiveActor(nullptr);
		Context->SetTemporaryHoveredPhotoActor(nullptr);
	}

	if (bSelectionRequested)
	{
		bSelectionRequested = false;
		if (IsValid(Target))
		{
			Target->SetActorSelected(!Target->bIsSelected);
		}
		else if (IsValid(HoveredPhoto))
		{
			HoveredPhoto->Interact(PC ? PC->GetPawn() : nullptr);
			RefreshHoverPrompt();
		}
	}

	// Re-attach if UI mode switching called RemoveAllWidgets() while a hover prompt is active.
	if (!Context->GetFilmMode() && PhotoHoverPromptWidget && PhotoHoverPromptWidget->IsPromptShowing() && !PhotoHoverPromptWidget->IsInViewport())
	{
		PhotoHoverPromptWidget->AddToPlayerScreen(50);
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
		return;
	}
	if (Direction == 0) return;

	const int32 CurrentCutIndex = Cuts.IndexOfByKey(Context->GetActiveCutActor());
	const int32 Step = Direction > 0 ? 1 : -1;
	// An unselected cut enters at the first/last photo; cycling has no empty slot.
	ActiveCutIndex = CurrentCutIndex == INDEX_NONE
		? (Step > 0 ? 0 : CutCount - 1)
		: (CurrentCutIndex + Step + CutCount) % CutCount;
	// The subsystem synchronizes Cut textures; film mask mode is controlled separately.
	Context->SetActiveCutActor(Cuts[ActiveCutIndex]);
}

void AFilmStyleControlActor::HandleHoveredPhotoActorChanged(APhotoActor* /*PreviousPhoto*/, APhotoActor* /*NewPhoto*/)
{
	RefreshHoverPrompt();
}

void AFilmStyleControlActor::HandleTemporaryActiveActorChanged(ASelectableActorShell* /*PreviousActor*/, ASelectableActorShell* /*NewActor*/)
{
	RefreshHoverPrompt();
}

void AFilmStyleControlActor::HandleCutActorsChanged()
{
	RefreshHoverPrompt();
}

void AFilmStyleControlActor::HandleActiveCutActorChanged(ACutActor* /*PreviousCutActor*/, ACutActor* /*NewCutActor*/)
{
	RefreshHoverPrompt();
}

void AFilmStyleControlActor::HandleSelectActorDataChanged(const FSelectActorStateData& /*NewData*/)
{
	RefreshHoverPrompt();
}

void AFilmStyleControlActor::HandleFilmModeChanged(bool /*bNewFilmMode*/)
{
	RefreshHoverPrompt();
}

void AFilmStyleControlActor::RefreshHoverPrompt()
{
	UFilmStyleSubsystem* Context = UFilmStyleSubsystem::Get(this);
	if (!Context || Context->GetFilmMode())
	{
		if (PhotoHoverPromptWidget)
		{
			PhotoHoverPromptWidget->HidePrompt();
		}
		return;
	}

	FText PromptText;
	bool bDimmed = false;
	bool bShouldShow = false;

	if (APhotoActor* HoveredPhoto = Context->GetTemporaryHoveredPhotoActor(); IsValid(HoveredPhoto))
	{
		HoveredPhoto->GetResolvedHoverPrompt(PromptText, bDimmed);
		bShouldShow = true;
	}
	else if (ASelectableActorShell* HoveredShell = Context->GetTemporaryActiveActor(); IsValid(HoveredShell))
	{
		HoveredShell->GetResolvedHoverPrompt(PromptText, bDimmed);
		bShouldShow = true;
	}

	if (!bShouldShow)
	{
		if (PhotoHoverPromptWidget)
		{
			PhotoHoverPromptWidget->HidePrompt();
		}
		return;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (!PhotoHoverPromptWidget)
	{
		UClass* WidgetClass = PhotoHoverPromptClass.Get();
		if (!WidgetClass)
		{
			static const TCHAR* DefaultPromptBlueprintPath = TEXT("/Game/UI/WBP_PhotoHoverPrompt");
			if (FPackageName::DoesPackageExist(DefaultPromptBlueprintPath))
			{
				WidgetClass = LoadClass<UPhotoHoverPromptWidget>(
					nullptr, TEXT("/Game/UI/WBP_PhotoHoverPrompt.WBP_PhotoHoverPrompt_C"));
			}
		}
		if (!WidgetClass)
		{
			WidgetClass = UPhotoHoverPromptWidget::StaticClass();
		}

		PhotoHoverPromptWidget = CreateWidget<UPhotoHoverPromptWidget>(PC, WidgetClass);
	}

	if (PhotoHoverPromptWidget)
	{
		if (!PhotoHoverPromptWidget->IsInViewport())
		{
			PhotoHoverPromptWidget->AddToPlayerScreen(50);
		}
		PhotoHoverPromptWidget->ShowPrompt(PromptText, bDimmed);
	}
}

void AFilmStyleControlActor::EndPlay(const EEndPlayReason::Type Reason)
{
	CancelSelection();
	if (auto* Context = UFilmStyleSubsystem::Get(this))
	{
		if (HoveredPhotoChangedHandle.IsValid())
		{
			Context->GetOnHoveredPhotoActorChangedNative().Remove(HoveredPhotoChangedHandle);
			HoveredPhotoChangedHandle.Reset();
		}
		if (TemporaryActiveActorChangedHandle.IsValid())
		{
			Context->GetOnTemporaryActiveActorChangedNative().Remove(TemporaryActiveActorChangedHandle);
			TemporaryActiveActorChangedHandle.Reset();
		}
		if (CutActorsChangedHandle.IsValid())
		{
			Context->GetOnCutActorsChangedNative().Remove(CutActorsChangedHandle);
			CutActorsChangedHandle.Reset();
		}
		if (ActiveCutActorChangedHandle.IsValid())
		{
			Context->GetOnActiveCutActorChangedNative().Remove(ActiveCutActorChangedHandle);
			ActiveCutActorChangedHandle.Reset();
		}
		if (SelectActorDataChangedHandle.IsValid())
		{
			Context->GetOnSelectActorDataChangedNative().Remove(SelectActorDataChangedHandle);
			SelectActorDataChangedHandle.Reset();
		}
		if (FilmModeChangedHandle.IsValid())
		{
			Context->GetOnFilmModeChangedNative().Remove(FilmModeChangedHandle);
			FilmModeChangedHandle.Reset();
		}
		Context->SetTemporaryActiveActor(nullptr);
		Context->SetTemporaryHoveredPhotoActor(nullptr);
	}
	if (PhotoHoverPromptWidget)
	{
		PhotoHoverPromptWidget->HidePrompt();
		PhotoHoverPromptWidget->RemoveFromParent();
		PhotoHoverPromptWidget = nullptr;
	}
	Super::EndPlay(Reason);
}
