// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "HelloRepoPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class UHelloRepoInputComponent;
class UHelloRepoInputConfig;
class AFilmStyleControlActor;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings
 */
UCLASS(abstract)
class AHelloRepoPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void SetInputMode(const FInputModeDataBase& InData) override;
	virtual void FlushPressedKeys() override;

protected:
	/** Authored Action-to-Tag pairs. Key mappings stay in the IMCs below. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<const UHelloRepoInputConfig> InputConfig;

	/** Optional runtime assignment; otherwise resolved from the current world on use. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "Input|Film Style")
	TObjectPtr<AFilmStyleControlActor> FilmStyleControlActor;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;

private:
	void BindFilmStyleInput();
	void ClearFilmStyleInputBindings();
	void CancelFilmStyleSelection();
	AFilmStyleControlActor* ResolveFilmStyleControlActor();
	void Input_SelectStarted();
	void Input_SelectCompleted();
	void Input_SelectCanceled();
	void Input_ToggleFilmMask();
	void Input_NextCut();
	void Input_PreviousCut();

	TWeakObjectPtr<UHelloRepoInputComponent> BoundFilmStyleInputComponent;
	TWeakObjectPtr<AFilmStyleControlActor> SelectionControlActor;
	TArray<uint32> FilmStyleInputBindingHandles;
};
