// Copyright Epic Games, Inc. All Rights Reserved.


#include "HelloRepoPlayerController.h"
#include "HelloRepoInputComponent.h"
#include "HelloRepoInputTags.h"
#include "FilmStyleControlActor.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "HelloRepo.h"
#include "Widgets/Input/SVirtualJoystick.h"

void AHelloRepoPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogHelloRepo, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AHelloRepoPlayerController::SetupInputComponent()
{
	ClearFilmStyleInputBindings();
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
		BindFilmStyleInput();
	}
}

void AHelloRepoPlayerController::BindFilmStyleInput()
{
	if (!InputConfig)
	{
		UE_LOG(LogHelloRepo, Warning, TEXT("%s has no InputConfig; film-style input is not bound."), *GetName());
		return;
	}

	UHelloRepoInputComponent* Input = Cast<UHelloRepoInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogHelloRepo, Error, TEXT("%s requires HelloRepoInputComponent as the default input component class."), *GetName());
		return;
	}

	BoundFilmStyleInputComponent = Input;
	const auto BindNativeAction = [this, Input](const FGameplayTag& Tag, ETriggerEvent Event,
		void (AHelloRepoPlayerController::*Handler)(), bool bLogNotFound = true)
	{
		if (FEnhancedInputActionEventBinding* Binding = Input->BindNativeAction(InputConfig, Tag, Event, this, Handler, bLogNotFound))
		{
			FilmStyleInputBindingHandles.Add(Binding->GetHandle());
		}
	};

	BindNativeAction(HelloRepoInputTags::FilmStyle_Select, ETriggerEvent::Started, &ThisClass::Input_SelectStarted);
	BindNativeAction(HelloRepoInputTags::FilmStyle_Select, ETriggerEvent::Completed, &ThisClass::Input_SelectCompleted, false);
	BindNativeAction(HelloRepoInputTags::FilmStyle_Select, ETriggerEvent::Canceled, &ThisClass::Input_SelectCanceled, false);
	BindNativeAction(HelloRepoInputTags::FilmStyle_ToggleFilmMask, ETriggerEvent::Started, &ThisClass::Input_ToggleFilmMask);
	// Wheel actions can remain triggered over consecutive frames of scrolling.
	BindNativeAction(HelloRepoInputTags::FilmStyle_NextCut, ETriggerEvent::Triggered, &ThisClass::Input_NextCut);
	BindNativeAction(HelloRepoInputTags::FilmStyle_PreviousCut, ETriggerEvent::Triggered, &ThisClass::Input_PreviousCut);
}

void AHelloRepoPlayerController::ClearFilmStyleInputBindings()
{
	CancelFilmStyleSelection();
	if (UHelloRepoInputComponent* Input = BoundFilmStyleInputComponent.Get())
	{
		for (uint32 Handle : FilmStyleInputBindingHandles)
		{
			Input->RemoveBindingByHandle(Handle);
		}
	}
	FilmStyleInputBindingHandles.Reset();
	BoundFilmStyleInputComponent.Reset();
}

AFilmStyleControlActor* AHelloRepoPlayerController::ResolveFilmStyleControlActor()
{
	if (IsValid(FilmStyleControlActor) && FilmStyleControlActor->GetWorld() == GetWorld())
	{
		return FilmStyleControlActor;
	}

	FilmStyleControlActor = nullptr;
	for (TActorIterator<AFilmStyleControlActor> It(GetWorld()); It; ++It)
	{
		FilmStyleControlActor = *It;
		break;
	}
	return FilmStyleControlActor;
}

void AHelloRepoPlayerController::Input_SelectStarted()
{
	CancelFilmStyleSelection();
	if (AFilmStyleControlActor* Control = ResolveFilmStyleControlActor())
	{
		SelectionControlActor = Control;
		Control->BeginSelection();
	}
}

void AHelloRepoPlayerController::Input_SelectCompleted()
{
	if (AFilmStyleControlActor* Control = SelectionControlActor.Get())
	{
		Control->EndSelection();
	}
	// Retain the target so a UI-mode change can also cancel an unprocessed click.
}

void AHelloRepoPlayerController::Input_SelectCanceled()
{
	CancelFilmStyleSelection();
}

void AHelloRepoPlayerController::CancelFilmStyleSelection()
{
	if (AFilmStyleControlActor* Control = SelectionControlActor.Get())
	{
		Control->CancelSelection();
	}
	SelectionControlActor.Reset();
}

void AHelloRepoPlayerController::Input_ToggleFilmMask()
{
	if (AFilmStyleControlActor* Control = ResolveFilmStyleControlActor())
	{
		Control->ToggleFilmMask();
	}
}

void AHelloRepoPlayerController::Input_NextCut()
{
	if (AFilmStyleControlActor* Control = ResolveFilmStyleControlActor())
	{
		Control->NextCut();
	}
}

void AHelloRepoPlayerController::Input_PreviousCut()
{
	if (AFilmStyleControlActor* Control = ResolveFilmStyleControlActor())
	{
		Control->PreviousCut();
	}
}

void AHelloRepoPlayerController::SetInputMode(const FInputModeDataBase& InData)
{
	CancelFilmStyleSelection();
	Super::SetInputMode(InData);
}

void AHelloRepoPlayerController::FlushPressedKeys()
{
	Super::FlushPressedKeys();
	CancelFilmStyleSelection();
}

void AHelloRepoPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearFilmStyleInputBindings();
	Super::EndPlay(EndPlayReason);
}

bool AHelloRepoPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
