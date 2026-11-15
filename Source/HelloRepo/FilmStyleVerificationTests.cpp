#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "CutActor.h"
#include "FilmStyleControlActor.h"
#include "FilmStyleSetting.h"
#include "FilmStyleSubsystem.h"
#include "HelloRepoCharacter.h"
#include "HelloRepoPlayerController.h"

namespace FilmStyleVerifyInternal
{
	class FFilmStyleVerifyLatentCommand : public IAutomationLatentCommand
	{
	public:
		explicit FFilmStyleVerifyLatentCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
		}

		virtual bool Update() override
		{
			UWorld* PlayWorld = GEditor ? GEditor->PlayWorld.Get() : nullptr;
			if (!PlayWorld || !PlayWorld->HasBegunPlay())
			{
				return false;
			}

			APlayerController* BasePC = PlayWorld->GetFirstPlayerController();
			AHelloRepoPlayerController* PC = Cast<AHelloRepoPlayerController>(BasePC);
			AHelloRepoCharacter* Char = PC ? Cast<AHelloRepoCharacter>(PC->GetPawn()) : nullptr;
			UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(PlayWorld);
			if (!PC || !Char || !Subsystem)
			{
				return WaitTicks++ > 120;
			}

			AFilmStyleSetting* Setting = nullptr;
			for (TActorIterator<AFilmStyleSetting> It(PlayWorld); It; ++It)
			{
				Setting = *It;
				break;
			}
			AFilmStyleControlActor* ControlActor = nullptr;
			for (TActorIterator<AFilmStyleControlActor> It(PlayWorld); It; ++It)
			{
				ControlActor = *It;
				break;
			}
			if (!Setting)
			{
				Test->AddError(TEXT("Missing AFilmStyleSetting in Lvl_GreenScreenStudio"));
				return true;
			}

			ULocalPlayer* LP = PC->GetLocalPlayer();
			UEnhancedInputLocalPlayerSubsystem* InputSys = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
			if (!InputSys)
			{
				Test->AddError(TEXT("Missing UEnhancedInputLocalPlayerSubsystem"));
				return true;
			}

			const float Dt = PlayWorld->GetDeltaSeconds();
			StageElapsed += Dt;
			++StageTicks;

			switch (Stage)
			{
			case EStage::InitWait:
			{
				if (StageTicks == 1)
				{
					// Simulate pressing StartButton in WBP_StartMenu
					UWidgetLayoutLibrary::RemoveAllWidgets(PlayWorld);
					PC->SetViewTarget(Char);
					PC->SetInputMode(FInputModeGameOnly());
					PC->bShowMouseCursor = false;

					const AFilmStyleSetting* CDO = GetDefault<AFilmStyleSetting>();
					Test->TestTrue(TEXT("CDO Default RTOpacityInTiming == (0,1)"),
						CDO->RTOpacityInTiming.Equals(FVector2D(0.0, 1.0), 0.001));
					Test->TestTrue(TEXT("CDO Default RTOpacityOutTiming == (0,1)"),
						CDO->RTOpacityOutTiming.Equals(FVector2D(0.0, 1.0), 0.001));
					Test->TestTrue(TEXT("CDO Default RTOpacityRange == (1.0, 0.75)"),
						CDO->RTOpacityRange.Equals(FVector2D(1.0, 0.75), 0.001));
					Test->TestTrue(TEXT("Initial RenderTargetOpacity matches Setting RTOpacityRange.X"),
						FMath::IsNearlyEqual(Setting->RenderTargetOpacity, FMath::Clamp(static_cast<float>(Setting->RTOpacityRange.X), 0.0f, 1.0f), 0.01f));

					// Configure custom RT opacity timing & before/after range for deterministic test
					Setting->RTOpacityRange = FVector2D(0.9, 0.4);
					Setting->RTOpacityInTiming = FVector2D(0.0, 0.3);
					Setting->RTOpacityOutTiming = FVector2D(0.0, 0.3);
				}

				if (StageTicks >= 5 && PC->PlayerCameraManager && PC->PlayerCameraManager->GetCameraCacheTime() > 0.f)
				{
					Test->TestTrue(TEXT("Idle RenderTargetOpacity updates to customized Before value 0.9"),
						FMath::IsNearlyEqual(Setting->RenderTargetOpacity, 0.9f, 0.01f));
					Test->TestTrue(TEXT("SelectActorData.RenderTargetOpacity synced to 0.9"),
						FMath::IsNearlyEqual(Subsystem->GetSelectActorData().RenderTargetOpacity, 0.9f, 0.01f));
					Transition(EStage::SpawnCutOutsideFilmMode);
				}
				return false;
			}

			case EStage::SpawnCutOutsideFilmMode:
			{
				if (StageTicks == 1)
				{
					PC->SetControlRotation(FRotator(0.f, 0.f, 0.f));
					if (UInputAction* TakePhotoAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_TakePhoto.IA_TakePhoto")))
					{
						InputSys->InjectInputForAction(TakePhotoAction, FInputActionValue(true), {}, {});
					}
					else
					{
						Test->AddError(TEXT("Missing /Game/Input/Actions/IA_TakePhoto"));
						return true;
					}
					return false;
				}

				if (StageTicks == 2)
				{
					Cut1 = Subsystem->GetActiveCutActor();
					Test->TestNotNull(TEXT("IA_TakePhoto spawned Cut1"), Cut1.Get());
					if (!Cut1.IsValid()) return true;
				}

				if (StageTicks >= 5)
				{
					Test->TestFalse(TEXT("Subsystem.bFilmMode is false after taking photo"), Subsystem->GetFilmMode());
					Test->TestEqual(TEXT("Cut1 is ActiveCutActor"), Subsystem->GetActiveCutActor(), Cut1.Get());
					Test->TestTrue(TEXT("CutActors contains Cut1"), Subsystem->GetCutActors().Contains(Cut1.Get()));
					Test->TestFalse(TEXT("Cut1 Tick disabled after taking photo"), Cut1->IsActorTickEnabled());
					Test->TestFalse(TEXT("Cut1 bCaptureEveryFrame disabled after taking photo"), Cut1->CaptureComponent2D->bCaptureEveryFrame);
					Test->TestFalse(TEXT("Cut1 bMirrorWorld false outside FilmMode"), Cut1->bMirrorWorld);
					Test->TestNotNull(TEXT("Cut1 snapshot allocated RenderTarget"), Cut1->RenderTarget.Get());

					Cut1FrozenLoc = Cut1->GetActorLocation();
					Cut1FrozenRot = Cut1->GetActorRotation();
					Cut1FrozenFOV = Cut1->CaptureComponent2D->FOVAngle;

					Transition(EStage::MoveOutsideFilmMode);
				}
				return false;
			}

			case EStage::MoveOutsideFilmMode:
			{
				// Rotate & walk player while bFilmMode == false
				PC->SetControlRotation(FRotator(-10.f, 45.f, 0.f));
				Char->AddMovementInput(FVector::ForwardVector, 1.0f);
				if (Char->GetFollowCamera())
				{
					Char->GetFollowCamera()->SetFieldOfView(72.f);
				}

				if (StageTicks >= 15)
				{
					Test->TestTrue(TEXT("Cut1 Location stayed frozen outside FilmMode"),
						Cut1->GetActorLocation().Equals(Cut1FrozenLoc, 0.01f));
					Test->TestTrue(TEXT("Cut1 Rotation stayed frozen outside FilmMode"),
						Cut1->GetActorRotation().Equals(Cut1FrozenRot, 0.01f));
					Test->TestTrue(TEXT("Cut1 FOV stayed frozen outside FilmMode"),
						FMath::IsNearlyEqual(Cut1->CaptureComponent2D->FOVAngle, Cut1FrozenFOV, 0.01f));
					Test->TestFalse(TEXT("Cut1 Tick still disabled"), Cut1->IsActorTickEnabled());
					Test->TestFalse(TEXT("Cut1 bCaptureEveryFrame still disabled"), Cut1->CaptureComponent2D->bCaptureEveryFrame);

					// Spawn second cut outside FilmMode and cycle between them
					Cut2 = PlayWorld->SpawnActor<ACutActor>(ACutActor::StaticClass(), FTransform());
					Test->TestNotNull(TEXT("Spawned Cut2"), Cut2.Get());
					if (!Cut2.IsValid()) return true;
					Cut2->SetActiveCutActor();

					if (ControlActor)
					{
						ControlActor->PreviousCut();
						Test->TestFalse(TEXT("PreviousCut outside FilmMode keeps Cut1 Tick disabled"), Cut1->IsActorTickEnabled());
						Test->TestFalse(TEXT("PreviousCut outside FilmMode keeps Cut1 bCaptureEveryFrame disabled"), Cut1->CaptureComponent2D->bCaptureEveryFrame);
						ControlActor->NextCut();
					}

					Transition(EStage::EnterFilmMode);
				}
				return false;
			}

			case EStage::EnterFilmMode:
			{
				if (StageTicks == 1)
				{
					Test->TestFalse(TEXT("Cut2 inactive capture before entering FilmMode"), Cut2->CaptureComponent2D->bCaptureEveryFrame);
					Cut2PreFilmLoc = Cut2->GetActorLocation();
					Cut2PreFilmRot = Cut2->GetActorRotation();

					UInputAction* ChangeModeAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_ChangeMode.IA_ChangeMode"));
					UInputMappingContext* DefaultIMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_Default.IMC_Default"));
					UInputMappingContext* FilmIMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_FilmMode.IMC_FilmMode"));
					Test->TestNotNull(TEXT("Loaded IA_ChangeMode"), ChangeModeAction);
					Test->TestNotNull(TEXT("Loaded IMC_Default"), DefaultIMC);
					Test->TestNotNull(TEXT("Loaded IMC_FilmMode"), FilmIMC);

					if (DefaultIMC)
					{
						Test->TestTrue(TEXT("IMC_Default active before entering FilmMode"), InputSys->HasMappingContext(DefaultIMC));
					}
					if (ChangeModeAction)
					{
						InputSys->InjectInputForAction(ChangeModeAction, FInputActionValue(true), {}, {});
					}

					// Let EnhancedInput process in this tick; verify right after input tick in StageTicks >= 2
					return false;
				}

				if (StageTicks == 2)
				{
					Test->TestTrue(TEXT("IA_ChangeMode entered bFilmMode == true"), Subsystem->GetFilmMode());
					if (UInputMappingContext* FilmIMC = LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Input/IMC_FilmMode.IMC_FilmMode")))
					{
						Test->TestTrue(TEXT("BP OnFilmModeChanged(true) added IMC_FilmMode"), InputSys->HasMappingContext(FilmIMC));
					}
					Test->TestTrue(TEXT("Cut2 Tick enabled in FilmMode"), Cut2->IsActorTickEnabled());
					Test->TestTrue(TEXT("Cut2 bCaptureEveryFrame enabled in FilmMode"), Cut2->CaptureComponent2D->bCaptureEveryFrame);
					Test->TestTrue(TEXT("Cut2 bMirrorWorld is TRUE in FilmMode (BP not inverted)"), Cut2->bMirrorWorld);
					Test->TestFalse(TEXT("Inactive Cut1 Tick remains disabled in FilmMode"), Cut1->IsActorTickEnabled());
					Test->TestFalse(TEXT("Inactive Cut1 bCaptureEveryFrame remains disabled in FilmMode"), Cut1->CaptureComponent2D->bCaptureEveryFrame);
					Test->TestFalse(TEXT("Inactive Cut1 bMirrorWorld remains false in FilmMode"), Cut1->bMirrorWorld);

					Transition(EStage::MoveInsideFilmMode);
				}
				return false;
			}

			case EStage::MoveInsideFilmMode:
			{
				// Rotate & walk player AND change FOV inside FilmMode
				PC->SetControlRotation(FRotator(15.f, 95.f, 0.f));
				Char->AddMovementInput(Char->GetActorForwardVector(), 1.0f);
				if (Char->GetFollowCamera())
				{
					Char->GetFollowCamera()->SetFieldOfView(55.f);
				}

				if (StageElapsed >= 0.45f && StageTicks >= 15)
				{
					const float LocDist = FVector::Dist(Cut2->GetActorLocation(), Cut2PreFilmLoc);
					const bool bRotated = !Cut2->GetActorRotation().Equals(Cut2PreFilmRot, 1.0f);

					Test->TestTrue(FString::Printf(TEXT("Cut2 moved in FilmMode (dist=%.2f)"), LocDist), LocDist > 5.0f);
					Test->TestTrue(TEXT("Cut2 rotated in FilmMode"), bRotated);
					Test->TestTrue(FString::Printf(TEXT("Cut2 synced FOV in FilmMode (got %.2f, expected 55)"), Cut2->CaptureComponent2D->FOVAngle),
						FMath::IsNearlyEqual(Cut2->CaptureComponent2D->FOVAngle, 55.f, 0.5f));
					Test->TestTrue(TEXT("Cut2 stays bCaptureEveryFrame == true during FilmMode"), Cut2->CaptureComponent2D->bCaptureEveryFrame);
					Test->TestTrue(TEXT("Cut2 stays bMirrorWorld == true during FilmMode"), Cut2->bMirrorWorld);

					Test->TestTrue(TEXT("Cut1 still completely frozen while Cut2 moves in FilmMode"),
						Cut1->GetActorLocation().Equals(Cut1FrozenLoc, 0.01f) && Cut1->GetActorRotation().Equals(Cut1FrozenRot, 0.01f));

					Test->TestTrue(FString::Printf(TEXT("RenderTargetOpacity transitioned to In-FilmMode value 0.4 (got %.3f)"), Setting->RenderTargetOpacity),
						FMath::IsNearlyEqual(Setting->RenderTargetOpacity, 0.4f, 0.05f));
					Test->TestTrue(TEXT("SelectActorData.RenderTargetOpacity matches ~0.4"),
						FMath::IsNearlyEqual(Subsystem->GetSelectActorData().RenderTargetOpacity, 0.4f, 0.05f));

					Transition(EStage::ExitFilmMode);
				}
				return false;
			}

			case EStage::ExitFilmMode:
			{
				if (StageTicks == 1)
				{
					Cut2PostFilmLoc = Cut2->GetActorLocation();
					Cut2PostFilmRot = Cut2->GetActorRotation();

					UInputAction* ChangeModeAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_ChangeMode.IA_ChangeMode"));
					if (ChangeModeAction)
					{
						InputSys->InjectInputForAction(ChangeModeAction, FInputActionValue(true), {}, {});
					}
					return false;
				}

				if (StageTicks == 2)
				{
					Test->TestFalse(TEXT("IA_ChangeMode exited bFilmMode == false"), Subsystem->GetFilmMode());
					Test->TestFalse(TEXT("Cut2 Tick disabled on exit FilmMode"), Cut2->IsActorTickEnabled());
					Test->TestFalse(TEXT("Cut2 bCaptureEveryFrame disabled on exit FilmMode"), Cut2->CaptureComponent2D->bCaptureEveryFrame);
					Test->TestFalse(TEXT("Cut2 bMirrorWorld false on exit FilmMode"), Cut2->bMirrorWorld);
					// Reset baseline to where Cut2 was right at exit
					Cut2PostFilmLoc = Cut2->GetActorLocation();
					Cut2PostFilmRot = Cut2->GetActorRotation();
				}

				// Move & rotate player AFTER exiting FilmMode; Cut2 must stay frozen while RT opacity fades back to 0.9
				PC->SetControlRotation(FRotator(-20.f, -30.f, 0.f));
				Char->AddMovementInput(FVector::RightVector, 1.0f);

				if (StageElapsed >= 0.45f && StageTicks >= 15)
				{
					Test->TestTrue(TEXT("Cut2 stayed frozen after exiting FilmMode (Loc)"),
						Cut2->GetActorLocation().Equals(Cut2PostFilmLoc, 0.01f));
					Test->TestTrue(TEXT("Cut2 stayed frozen after exiting FilmMode (Rot)"),
						Cut2->GetActorRotation().Equals(Cut2PostFilmRot, 0.01f));
					Test->TestTrue(FString::Printf(TEXT("RenderTargetOpacity transitioned back to Before value 0.9 (got %.3f)"), Setting->RenderTargetOpacity),
						FMath::IsNearlyEqual(Setting->RenderTargetOpacity, 0.9f, 0.05f));
					return true;
				}
				return false;
			}
			}
			return true;
		}

	private:
		enum class EStage
		{
			InitWait,
			SpawnCutOutsideFilmMode,
			MoveOutsideFilmMode,
			EnterFilmMode,
			MoveInsideFilmMode,
			ExitFilmMode
		};

		void Transition(EStage NextStage)
		{
			Stage = NextStage;
			StageElapsed = 0.0f;
			StageTicks = 0;
		}

		FAutomationTestBase* Test = nullptr;
		EStage Stage = EStage::InitWait;
		float StageElapsed = 0.0f;
		int32 StageTicks = 0;
		int32 WaitTicks = 0;

		TWeakObjectPtr<ACutActor> Cut1;
		TWeakObjectPtr<ACutActor> Cut2;
		FVector Cut1FrozenLoc = FVector::ZeroVector;
		FRotator Cut1FrozenRot = FRotator::ZeroRotator;
		float Cut1FrozenFOV = 90.f;

		FVector Cut2PreFilmLoc = FVector::ZeroVector;
		FRotator Cut2PreFilmRot = FRotator::ZeroRotator;
		FVector Cut2PostFilmLoc = FVector::ZeroVector;
		FRotator Cut2PostFilmRot = FRotator::ZeroRotator;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilmStyleVerifyCutActorAndRTOpacityTest, "FilmStyle.VerifyCutActorAndRTOpacity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFilmStyleVerifyCutActorAndRTOpacityTest::RunTest(const FString& Parameters)
{
	// Ignore pre-existing WBP_StartMenu UIOnly focus log emitted on level BeginPlay
	AddExpectedErrorPlain(TEXT("Attempting to focus Non-Focusable widget"), EAutomationExpectedErrorFlags::Contains, -1);

	AddCommand(new FEditorLoadMap(TEXT("/Game/Art/Maps/Lvl_GreenScreenStudio")));
	AddCommand(new FWaitForShadersToFinishCompiling());
	AddCommand(new FStartPIECommand(false));
	AddCommand(new FilmStyleVerifyInternal::FFilmStyleVerifyLatentCommand(this));
	AddCommand(new FEndPlayMapCommand());
	AddCommand(new FWaitLatentCommand(0.5f));
	return true;
}
#endif
