#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "CutActor.h"
#include "CutPoint.h"
#include "FilmStyleControlActor.h"
#include "FilmStyleSetting.h"
#include "FilmStyleSubsystem.h"
#include "HelloRepoCharacter.h"
#include "HelloRepoPlayerController.h"
#include "PhotoActor.h"
#include "PhotoHoverPromptWidget.h"
#include "Components/SizeBox.h"

namespace CutPointPhotoVerifyInternal
{
	class FCutPointPhotoVerifyLatentCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCutPointPhotoVerifyLatentCommand(FAutomationTestBase* InTest)
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

			AHelloRepoPlayerController* PC = Cast<AHelloRepoPlayerController>(PlayWorld->GetFirstPlayerController());
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

			if (!Setting || !ControlActor)
			{
				Test->AddError(TEXT("Missing AFilmStyleSetting or AFilmStyleControlActor in Lvl_GreenScreenStudio"));
				return true;
			}

			ULocalPlayer* LP = PC->GetLocalPlayer();
			UEnhancedInputLocalPlayerSubsystem* InputSys = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
			if (!InputSys)
			{
				Test->AddError(TEXT("Missing UEnhancedInputLocalPlayerSubsystem"));
				return true;
			}

			++StageTicks;

			switch (Stage)
			{
			case EStage::InitAndSpawn:
			{
				if (StageTicks == 1)
				{
					UWidgetLayoutLibrary::RemoveAllWidgets(PlayWorld);
					PC->SetViewTarget(Char);
					PC->SetInputMode(FInputModeGameOnly());
					PC->bShowMouseCursor = false;
				}

				if (StageTicks < 5 || !PC->PlayerCameraManager || PC->PlayerCameraManager->GetCameraCacheTime() <= 0.0f)
				{
					return false;
				}

				// Verify generated Blueprint & Material assets exist and load cleanly.
				UClass* BPCutPointClass = LoadClass<ACutPoint>(nullptr, TEXT("/Game/CommonActor/BP_CutPoint.BP_CutPoint_C"));
				UClass* BPPhotoActorClass = LoadClass<APhotoActor>(nullptr, TEXT("/Game/CommonActor/BP_PhotoActor.BP_PhotoActor_C"));
				UMaterialInterface* PhotoSurfaceMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/CommonActor/M_PhotoSurface.M_PhotoSurface"));
				SelectAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Input/Actions/IA_Select.IA_Select"));

				Test->TestNotNull(TEXT("BP_CutPoint_C loaded"), BPCutPointClass);
				Test->TestNotNull(TEXT("BP_PhotoActor_C loaded"), BPPhotoActorClass);
				Test->TestNotNull(TEXT("M_PhotoSurface loaded"), PhotoSurfaceMat);
				Test->TestNotNull(TEXT("IA_Select loaded"), SelectAction.Get());
				if (!BPCutPointClass || !BPPhotoActorClass || !PhotoSurfaceMat || !SelectAction.IsValid())
				{
					return true;
				}

				// Place a brightly lit/colored cube inside the studio view in front of CutPoint.
				const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
				const FRotator CamRot = PC->PlayerCameraManager->GetCameraRotation();
				const FVector CamForward = CamRot.Vector();
				const FVector CamRight = FRotationMatrix(CamRot).GetScaledAxis(EAxis::Y);

				CutPointLocation = CamLoc + CamForward * 80.0f + CamRight * 120.0f;
				CutPointRotation = (CamRot + FRotator(-5.0f, -15.0f, 0.0f)).GetNormalized();

				TargetBox = PlayWorld->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
					FTransform(FRotator::ZeroRotator, CutPointLocation + CutPointRotation.Vector() * 250.0f));
				if (TargetBox.IsValid() && TargetBox->GetStaticMeshComponent())
				{
					TargetBox->SetMobility(EComponentMobility::Movable);
					if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
					{
						TargetBox->GetStaticMeshComponent()->SetStaticMesh(Cube);
					}
					if (UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
					{
						UMaterialInstanceDynamic* BoxMID = UMaterialInstanceDynamic::Create(BaseMat, TargetBox.Get());
						BoxMID->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.95f, 0.25f, 0.1f, 1.0f));
						TargetBox->GetStaticMeshComponent()->SetMaterial(0, BoxMID);
					}
				}

				// Spawn BP_PhotoActor directly in front of player camera, facing -CamForward so +X photo normal faces the camera.
				const FVector PhotoLoc = CamLoc + CamForward * 260.0f;
				const FRotator PhotoRot = (-CamForward).Rotation();

				PhotoActor = PlayWorld->SpawnActorDeferred<APhotoActor>(
					BPPhotoActorClass, FTransform(PhotoRot, PhotoLoc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				CutPoint = PlayWorld->SpawnActorDeferred<ACutPoint>(
					BPCutPointClass, FTransform(CutPointRotation, CutPointLocation), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

				if (!PhotoActor.IsValid() || !CutPoint.IsValid())
				{
					Test->AddError(TEXT("Failed to deferred-spawn BP_CutPoint / BP_PhotoActor"));
					return true;
				}

				CutPoint->LinkedPhotoActor = PhotoActor.Get();
				CutPoint->PhotoMaxDimension = 320;
				CutPoint->CaptureComponent2D->FOVAngle = 60.0f;
				PhotoActor->LinkedCutPoint = CutPoint.Get();

				PhotoActor->FinishSpawning(FTransform(PhotoRot, PhotoLoc));
				CutPoint->FinishSpawning(FTransform(CutPointRotation, CutPointLocation));

				// Also test reverse single-sided link on raw C++ actors (only PhotoActor->LinkedCutPoint set).
				RawPhotoActor = PlayWorld->SpawnActorDeferred<APhotoActor>(
					APhotoActor::StaticClass(), FTransform(PhotoRot, PhotoLoc + CamRight * 180.0f), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				RawCutPoint = PlayWorld->SpawnActorDeferred<ACutPoint>(
					ACutPoint::StaticClass(), FTransform(CutPointRotation, CutPointLocation + FVector(0.0f, 200.0f, 0.0f)), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				RawPhotoActor->LinkedCutPoint = RawCutPoint.Get();
				RawCutPoint->FinishSpawning(FTransform(CutPointRotation, CutPointLocation + FVector(0.0f, 200.0f, 0.0f)));
				RawPhotoActor->FinishSpawning(FTransform(PhotoRot, PhotoLoc + CamRight * 180.0f));

				// CutPoint is NOT an ACutActor and must NOT register into CutActors on BeginPlay.
				Test->TestFalse(TEXT("CutPoint is not an ACutActor"), CutPoint->IsA<ACutActor>());
				Test->TestEqual(TEXT("CutPoint does not register into Subsystem CutActors during BeginPlay"),
					Subsystem->GetCutActors().Num(), 0);
				Test->TestNull(TEXT("ActiveCutActor remains null during CutPoint BeginPlay"),
					Subsystem->GetActiveCutActor());

				Transition(EStage::WaitCaptureFinalize);
				return false;
			}

			case EStage::WaitCaptureFinalize:
			{
				if (StageTicks < 8)
				{
					return false;
				}

				Test->TestTrue(TEXT("CutPoint captured initial photo"), CutPoint->bInitialPhotoCaptured);
				Test->TestTrue(TEXT("CutPoint SceneCapture permanently disabled after BeginPlay"), CutPoint->bCapturePermanentlyDisabled);
				Test->TestFalse(TEXT("CutPoint IsSceneCaptureActive() == false"), CutPoint->IsSceneCaptureActive());
				Test->TestFalse(TEXT("CaptureComponent2D bCaptureEveryFrame == false"), CutPoint->CaptureComponent2D->bCaptureEveryFrame);
				Test->TestFalse(TEXT("CaptureComponent2D bCaptureOnMovement == false"), CutPoint->CaptureComponent2D->bCaptureOnMovement);
				Test->TestFalse(TEXT("CaptureComponent2D IsActive() == false"), CutPoint->CaptureComponent2D->IsActive());
				Test->TestFalse(TEXT("CaptureComponent2D IsComponentTickEnabled() == false"), CutPoint->CaptureComponent2D->IsComponentTickEnabled());
				Test->TestFalse(TEXT("CaptureComponent2D IsVisible() == false"), CutPoint->CaptureComponent2D->IsVisible());
				Test->TestNull(TEXT("CaptureComponent2D TextureTarget detached"), CutPoint->CaptureComponent2D->TextureTarget.Get());

				// Reverse-linked C++ pair checks
				Test->TestTrue(TEXT("Reverse-linked RawCutPoint also captured & disabled"),
					RawCutPoint->bInitialPhotoCaptured && RawCutPoint->bCapturePermanentlyDisabled && !RawCutPoint->IsSceneCaptureActive());
				Test->TestEqual(TEXT("Reverse-linked RawPhotoActor received RawCutPoint RT"),
					RawPhotoActor->CurrentPhotoTexture.Get(), static_cast<UTexture*>(RawCutPoint->PhotoRenderTarget.Get()));

				// Validate RT resolution & viewport aspect ratio
				UTextureRenderTarget2D* PhotoRT = CutPoint->PhotoRenderTarget.Get();
				Test->TestNotNull(TEXT("CutPoint PhotoRenderTarget created"), PhotoRT);
				if (!PhotoRT) return true;

				FVector2D ViewportSize = FVector2D::ZeroVector;
				if (UGameViewportClient* Viewport = PlayWorld->GetGameViewport())
				{
					Viewport->GetViewportSize(ViewportSize);
				}
				const float ViewportAspect = ViewportSize.Y > 0.0f ? (ViewportSize.X / ViewportSize.Y) : (16.0f / 9.0f);
				const float RTAspect = static_cast<float>(PhotoRT->SizeX) / static_cast<float>(PhotoRT->SizeY);
				const int32 MaxEdge = FMath::Max(PhotoRT->SizeX, PhotoRT->SizeY);

				Test->TestEqual(TEXT("PhotoRT max dimension equals PhotoMaxDimension (320)"), MaxEdge, CutPoint->PhotoMaxDimension);
				Test->TestTrue(TEXT("PhotoRT aspect ratio matches viewport aspect ratio"),
					FMath::IsNearlyEqual(RTAspect, ViewportAspect, 0.02f));

				// Validate pixel content when GPU rendering is active
				if (FTextureRenderTargetResource* RTResource = PhotoRT->GameThread_GetRenderTargetResource())
				{
					InitialPixels.Reset();
					if (RTResource->ReadPixels(InitialPixels) && InitialPixels.Num() > 0)
					{
						uint8 MaxR = 0, MaxG = 0, MaxB = 0, MaxA = 0;
						bool bHasNonBlack = false;
						bool bHasColorVariance = false;
						const FColor FirstPx = InitialPixels[0];
						for (const FColor& Px : InitialPixels)
						{
							MaxR = FMath::Max(MaxR, Px.R);
							MaxG = FMath::Max(MaxG, Px.G);
							MaxB = FMath::Max(MaxB, Px.B);
							MaxA = FMath::Max(MaxA, Px.A);
							if (Px.R > 2 || Px.G > 2 || Px.B > 2)
							{
								bHasNonBlack = true;
							}
							if (Px != FirstPx)
							{
								bHasColorVariance = true;
							}
						}
						Test->TestTrue(
							FString::Printf(TEXT("CutPoint PhotoRenderTarget contains rendered non-black scene pixels (MaxRGBA=%u,%u,%u,%u, Sample0=%u,%u,%u,%u)"),
								MaxR, MaxG, MaxB, MaxA, FirstPx.R, FirstPx.G, FirstPx.B, FirstPx.A),
							bHasNonBlack && bHasColorVariance);
					}
				}

				// Validate PhotoActor material & aspect ratio layout
				Test->TestEqual(TEXT("PhotoActor CurrentPhotoTexture == CutPoint PhotoRenderTarget"),
					PhotoActor->CurrentPhotoTexture.Get(), static_cast<UTexture*>(PhotoRT));
				Test->TestNotNull(TEXT("PhotoActor created PhotoDynamicMaterial"), PhotoActor->PhotoDynamicMaterial.Get());
				if (PhotoActor->PhotoDynamicMaterial)
				{
					UTexture* BoundTex = nullptr;
					PhotoActor->PhotoDynamicMaterial->GetTextureParameterValue(PhotoActor->TextureParameterName, BoundTex);
					Test->TestEqual(TEXT("PhotoDynamicMaterial PhotoTexture parameter bound to PhotoRenderTarget"),
						BoundTex, static_cast<UTexture*>(PhotoRT));
				}

				const FVector PhotoScale = PhotoActor->PhotoMesh->GetRelativeScale3D();
				const float DisplayedAspect = PhotoScale.Y > 0.0f ? (PhotoScale.X / PhotoScale.Y) : 0.0f;
				Test->TestTrue(TEXT("PhotoMesh aspect scale matches CapturedAspectRatio"),
					FMath::IsNearlyEqual(DisplayedAspect, CutPoint->CapturedAspectRatio, 0.01f));

				// Verify PhotoMesh front normal faces +X and local U/V axes are upright & unmirrored
				const FRotator MeshRelRot = PhotoActor->PhotoMesh->GetRelativeRotation();
				const FQuat MeshQuat = MeshRelRot.Quaternion();
				Test->TestTrue(TEXT("PhotoMesh +Z normal points along actor +X"),
					MeshQuat.RotateVector(FVector::UpVector).Equals(FVector::ForwardVector, 0.01f));
				Test->TestTrue(TEXT("PhotoMesh +X (U=0->1) points along actor -Y (viewer left->right)"),
					MeshQuat.RotateVector(FVector::ForwardVector).Equals(-FVector::RightVector, 0.01f));
				Test->TestTrue(TEXT("PhotoMesh +Y (V=0->1) points along actor -Z (viewer top->bottom)"),
					MeshQuat.RotateVector(FVector::RightVector).Equals(-FVector::UpVector, 0.01f));

				// Perturb scene in front of CutPoint to verify SceneCapture never updates again.
				if (TargetBox.IsValid())
				{
					TargetBox->Destroy();
				}
				CutPoint->AddActorWorldOffset(FVector(50.0f, 0.0f, 0.0f));
				CutPoint->SetActorLocation(CutPointLocation);

				Transition(EStage::VerifyStaysInactiveAndHover);
				return false;
			}

			case EStage::VerifyStaysInactiveAndHover:
			{
				if (StageTicks == 1)
				{
					// Move PhotoActor closer (130 cm) and aim directly at center to measure close-range screen-space size.
					OrigPhotoLocation = PhotoActor->GetActorLocation();
					const FVector CamLocNow = PC->PlayerCameraManager->GetCameraLocation();
					const FVector AimForward = (OrigPhotoLocation - CamLocNow).GetSafeNormal();
					PhotoActor->SetActorLocation(CamLocNow + AimForward * 130.0f);
					PC->SetControlRotation(AimForward.Rotation());
					return false;
				}

				if (StageTicks < 10)
				{
					return false;
				}

				// SceneCapture must still be completely inactive and pixels completely unchanged.
				Test->TestFalse(TEXT("CutPoint SceneCapture stays inactive over time"), CutPoint->IsSceneCaptureActive());
				if (UTextureRenderTarget2D* PhotoRT = CutPoint->PhotoRenderTarget.Get())
				{
					if (FTextureRenderTargetResource* RTResource = PhotoRT->GameThread_GetRenderTargetResource())
					{
						TArray<FColor> LaterPixels;
						if (RTResource->ReadPixels(LaterPixels) && InitialPixels.Num() > 0)
						{
							Test->TestEqual(TEXT("PhotoRenderTarget pixel count unchanged"), LaterPixels.Num(), InitialPixels.Num());
							Test->TestTrue(TEXT("PhotoRenderTarget pixels stayed bit-for-bit frozen after BeginPlay"),
								LaterPixels == InitialPixels);
						}
					}
				}

				// Crosshair hover highlight checks
				Test->TestEqual(TEXT("Subsystem TemporaryHoveredPhotoActor == PhotoActor"),
					Subsystem->GetTemporaryHoveredPhotoActor(), PhotoActor.Get());
				Test->TestTrue(TEXT("PhotoActor bIsHovered == true when aimed at"), PhotoActor->bIsHovered);
				Test->TestTrue(TEXT("PhotoMesh RenderCustomDepth enabled on hover"),
					PhotoActor->PhotoMesh->bRenderCustomDepth);
				Test->TestEqual(TEXT("PhotoMesh CustomDepthStencilValue == HoverStencilValue (2)"),
					PhotoActor->PhotoMesh->CustomDepthStencilValue, 2);
				Test->TestTrue(TEXT("FrameMesh RenderCustomDepth enabled on hover"),
					PhotoActor->FrameMesh->bRenderCustomDepth);
				Test->TestEqual(TEXT("FrameMesh CustomDepthStencilValue == HoverStencilValue (2)"),
					PhotoActor->FrameMesh->CustomDepthStencilValue, 2);

				// Screen-space fixed-size hover popup checks at close range (130 cm)
				UPhotoHoverPromptWidget* HoverPopup = ControlActor->GetPhotoHoverPromptWidget();
				Test->TestNotNull(TEXT("PhotoHoverPromptWidget created when hovering PhotoActor"), HoverPopup);
				if (HoverPopup)
				{
					Test->TestEqual(TEXT("Hover popup uses WBP_PhotoHoverPrompt_C"),
						HoverPopup->GetClass()->GetPathName(),
						FString(TEXT("/Game/UI/WBP_PhotoHoverPrompt.WBP_PhotoHoverPrompt_C")));
					Test->TestTrue(TEXT("Hover popup is in viewport/player screen"), HoverPopup->IsInViewport());
					Test->TestEqual(TEXT("Hover popup visibility is HitTestInvisible so clicks pass through"),
						HoverPopup->GetVisibility(), ESlateVisibility::HitTestInvisible);
					Test->TestEqual(TEXT("Hover popup displays 'Get Cut Clip'"),
						HoverPopup->GetPromptText().ToString(), FString(TEXT("Get Cut Clip")));

					if (USizeBox* PopupSizeBox = HoverPopup->GetPopupSizeBox())
					{
						Test->TestTrue(TEXT("PopupSizeBox WidthOverride == 224"),
							FMath::IsNearlyEqual(PopupSizeBox->GetWidthOverride(), 224.0f, 0.1f));
						Test->TestTrue(TEXT("PopupSizeBox HeightOverride == 48"),
							FMath::IsNearlyEqual(PopupSizeBox->GetHeightOverride(), 48.0f, 0.1f));

						ClosePopupScreenSize = PopupSizeBox->GetCachedGeometry().GetLocalSize();
						Test->TestTrue(
							FString::Printf(TEXT("Popup close-range screen-space size is 224x48 (actual=%.1fx%.1f)"),
								ClosePopupScreenSize.X, ClosePopupScreenSize.Y),
							ClosePopupScreenSize.Equals(FVector2D(224.0f, 48.0f), 1.0f));
					}
					else
					{
						Test->AddError(TEXT("Hover popup missing PopupSizeBox"));
					}
				}

				Transition(EStage::VerifyFixedPopupAtFarDistance);
				return false;
			}

			case EStage::VerifyFixedPopupAtFarDistance:
			{
				if (StageTicks == 1)
				{
					// Move PhotoActor 3.2x farther (420 cm) along view ray to prove popup stays fixed in screen space.
					const FVector CamLocNow = PC->PlayerCameraManager->GetCameraLocation();
					const FVector AimForward = (OrigPhotoLocation - CamLocNow).GetSafeNormal();
					PhotoActor->SetActorLocation(CamLocNow + AimForward * 420.0f);
					PC->SetControlRotation(AimForward.Rotation());
					return false;
				}

				if (StageTicks < 5)
				{
					return false;
				}

				Test->TestTrue(TEXT("PhotoActor stays hovered at far range"), PhotoActor->bIsHovered);
				if (UPhotoHoverPromptWidget* HoverPopup = ControlActor->GetPhotoHoverPromptWidget())
				{
					Test->TestEqual(TEXT("Hover popup remains HitTestInvisible at far range"),
						HoverPopup->GetVisibility(), ESlateVisibility::HitTestInvisible);
					if (USizeBox* PopupSizeBox = HoverPopup->GetPopupSizeBox())
					{
						const FVector2D FarPopupScreenSize = PopupSizeBox->GetCachedGeometry().GetLocalSize();
						Test->TestTrue(
							FString::Printf(TEXT("Popup screen-space size unchanged at 3.2x distance (close=%.1fx%.1f, far=%.1fx%.1f)"),
								ClosePopupScreenSize.X, ClosePopupScreenSize.Y,
								FarPopupScreenSize.X, FarPopupScreenSize.Y),
							FarPopupScreenSize.Equals(ClosePopupScreenSize, 0.5f)
							&& FarPopupScreenSize.Equals(FVector2D(224.0f, 48.0f), 1.0f));
					}
				}

				PhotoActor->SetActorLocation(OrigPhotoLocation);
				const FVector AimDir = (OrigPhotoLocation - PC->PlayerCameraManager->GetCameraLocation()).GetSafeNormal();
				PC->SetControlRotation(AimDir.Rotation());

				Transition(EStage::VerifyFirstInteraction);
				return false;
			}

			case EStage::VerifyFirstInteraction:
			{
				if (StageTicks == 1)
				{
					// First left-click interaction via IA_Select (must pass straight through HitTestInvisible popup)
					InputSys->InjectInputForAction(SelectAction.Get(), FInputActionValue(true), {}, {});
					return false;
				}

				if (StageTicks < 5)
				{
					return false;
				}

				SpawnedCut1 = CutPoint->GetSpawnedCutActor();
				Test->TestNotNull(TEXT("First click on PhotoActor spawned CutActor at CutPoint"), SpawnedCut1.Get());
				if (!SpawnedCut1.IsValid()) return true;

				Test->TestTrue(TEXT("Subsystem CutActors contains SpawnedCut1"),
					Subsystem->GetCutActors().Contains(SpawnedCut1.Get()));
				Test->TestEqual(TEXT("Subsystem ActiveCutActor == SpawnedCut1"),
					Subsystem->GetActiveCutActor(), SpawnedCut1.Get());
				Test->TestTrue(TEXT("SpawnedCut1 location matches CutPoint"),
					SpawnedCut1->GetActorLocation().Equals(CutPointLocation, 1.0f));
				Test->TestTrue(TEXT("SpawnedCut1 rotation matches CutPoint"),
					SpawnedCut1->GetActorRotation().Equals(CutPointRotation, 1.0f));
				Test->TestTrue(TEXT("SpawnedCut1 preserved CutPoint FOV (60 deg)"),
					FMath::IsNearlyEqual(SpawnedCut1->CaptureComponent2D->FOVAngle, 60.0f, 0.01f));
				Test->TestNotNull(TEXT("SpawnedCut1 initialized its RenderTarget"),
					SpawnedCut1->RenderTarget.Get());
				Test->TestEqual(TEXT("FilmStyleSetting ReplaceTexture synced to SpawnedCut1 RenderTarget"),
					Setting->ReplaceTexture.Get(), static_cast<UTexture*>(SpawnedCut1->RenderTarget.Get()));

				// Create another CutActor and activate it so SpawnedCut1 stays in list but becomes inactive.
				ACutActor* OtherCut = PlayWorld->SpawnActor<ACutActor>(ACutActor::StaticClass(), FTransform());
				Test->TestNotNull(TEXT("Spawned OtherCut"), OtherCut);
				if (!OtherCut) return true;
				OtherCut->SetActiveCutActor();
				Test->TestEqual(TEXT("ActiveCutActor switched to OtherCut"), Subsystem->GetActiveCutActor(), OtherCut);

				// Re-aim just in case and click PhotoActor again while SpawnedCut1 is still in the 3-slot list.
				const FVector AimDir = (PhotoActor->GetActorLocation() - PC->PlayerCameraManager->GetCameraLocation()).GetSafeNormal();
				PC->SetControlRotation(AimDir.Rotation());
				InputSys->InjectInputForAction(SelectAction.Get(), FInputActionValue(true), {}, {});

				Transition(EStage::VerifyReuseAndRespawn);
				return false;
			}

			case EStage::VerifyReuseAndRespawn:
			{
				if (StageTicks == 1)
				{
					return false;
				}

				if (StageTicks == 2)
				{
					Test->TestEqual(TEXT("Clicking PhotoActor again reused SpawnedCut1 as ActiveCutActor"),
						Subsystem->GetActiveCutActor(), SpawnedCut1.Get());
					Test->TestEqual(TEXT("No duplicate CutActor added when reusing from CutActors list"),
						Subsystem->GetCutActors().Num(), 2);

					// Now spawn 3 fresh CutActors to evict SpawnedCut1 from MaxCutActors = 3.
					for (int32 i = 0; i < 3; ++i)
					{
						if (ACutActor* Filler = PlayWorld->SpawnActor<ACutActor>(ACutActor::StaticClass(), FTransform()))
						{
							Filler->SetActiveCutActor();
						}
					}

					Test->TestFalse(TEXT("SpawnedCut1 evicted after spawning 3 newer CutActors"),
						Subsystem->GetCutActors().Contains(SpawnedCut1.Get()));
					Test->TestEqual(TEXT("CutActors capped at MaxCutActors = 3"),
						Subsystem->GetCutActors().Num(), 3);

					// Click PhotoActor again after eviction -> must spawn a new CutActor at CutPoint and activate it.
					InputSys->InjectInputForAction(SelectAction.Get(), FInputActionValue(true), {}, {});
					return false;
				}

				if (StageTicks < 5)
				{
					return false;
				}

				ACutActor* SpawnedCut2 = CutPoint->GetSpawnedCutActor();
				Test->TestNotNull(TEXT("Post-eviction click spawned a new CutActor"), SpawnedCut2);
				Test->TestNotEqual(TEXT("Post-eviction CutActor is a fresh instance != evicted SpawnedCut1"),
					SpawnedCut2, SpawnedCut1.Get());
				Test->TestTrue(TEXT("Subsystem CutActors contains new SpawnedCut2"),
					Subsystem->GetCutActors().Contains(SpawnedCut2));
				Test->TestEqual(TEXT("CutActors list stays capped at 3"),
					Subsystem->GetCutActors().Num(), 3);
				Test->TestEqual(TEXT("Subsystem ActiveCutActor == new SpawnedCut2"),
					Subsystem->GetActiveCutActor(), SpawnedCut2);
				if (SpawnedCut2)
				{
					Test->TestTrue(TEXT("Respawned CutActor location matches CutPoint"),
						SpawnedCut2->GetActorLocation().Equals(CutPointLocation, 1.0f));
					Test->TestTrue(TEXT("Respawned CutActor preserved CutPoint FOV (60 deg)"),
						FMath::IsNearlyEqual(SpawnedCut2->CaptureComponent2D->FOVAngle, 60.0f, 0.01f));
				}

				Transition(EStage::VerifyPopupHidesOnUnhover);
				return false;
			}

			case EStage::VerifyPopupHidesOnUnhover:
			{
				if (StageTicks == 1)
				{
					// Aim away from PhotoActor so hover ends.
					PC->SetControlRotation(FRotator(80.0f, 0.0f, 0.0f));
					return false;
				}

				if (StageTicks < 4)
				{
					return false;
				}

				Test->TestNull(TEXT("TemporaryHoveredPhotoActor cleared on unhover"),
					Subsystem->GetTemporaryHoveredPhotoActor());
				Test->TestFalse(TEXT("PhotoActor bIsHovered == false on unhover"), PhotoActor->bIsHovered);
				if (UPhotoHoverPromptWidget* HoverPopup = ControlActor->GetPhotoHoverPromptWidget())
				{
					Test->TestEqual(TEXT("Hover popup Collapsed when unhovered"),
						HoverPopup->GetVisibility(), ESlateVisibility::Collapsed);
				}
				return true;
			}
			}

			return true;
		}

	private:
		enum class EStage : uint8
		{
			InitAndSpawn,
			WaitCaptureFinalize,
			VerifyStaysInactiveAndHover,
			VerifyFixedPopupAtFarDistance,
			VerifyFirstInteraction,
			VerifyReuseAndRespawn,
			VerifyPopupHidesOnUnhover
		};

		void Transition(EStage NextStage)
		{
			Stage = NextStage;
			StageTicks = 0;
		}

		FAutomationTestBase* Test = nullptr;
		EStage Stage = EStage::InitAndSpawn;
		int32 StageTicks = 0;
		int32 WaitTicks = 0;

		FVector CutPointLocation = FVector::ZeroVector;
		FRotator CutPointRotation = FRotator::ZeroRotator;
		FVector OrigPhotoLocation = FVector::ZeroVector;
		FVector2D ClosePopupScreenSize = FVector2D::ZeroVector;

		TWeakObjectPtr<UInputAction> SelectAction;
		TWeakObjectPtr<AStaticMeshActor> TargetBox;
		TWeakObjectPtr<ACutPoint> CutPoint;
		TWeakObjectPtr<APhotoActor> PhotoActor;
		TWeakObjectPtr<ACutPoint> RawCutPoint;
		TWeakObjectPtr<APhotoActor> RawPhotoActor;
		TWeakObjectPtr<ACutActor> SpawnedCut1;
		TArray<FColor> InitialPixels;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilmStyleVerifyCutPointAndPhotoActorTest, "FilmStyle.VerifyCutPointAndPhotoActor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFilmStyleVerifyCutPointAndPhotoActorTest::RunTest(const FString& Parameters)
{
	// Ignore pre-existing WBP_StartMenu UIOnly focus log emitted on level BeginPlay
	AddExpectedErrorPlain(TEXT("Attempting to focus Non-Focusable widget"), EAutomationExpectedErrorFlags::Contains, -1);

	AddCommand(new FEditorLoadMap(TEXT("/Game/Art/Maps/Lvl_GreenScreenStudio")));
	AddCommand(new FWaitForShadersToFinishCompiling());
	AddCommand(new FStartPIECommand(false));
	AddCommand(new CutPointPhotoVerifyInternal::FCutPointPhotoVerifyLatentCommand(this));
	AddCommand(new FEndPlayMapCommand());
	AddCommand(new FWaitLatentCommand(0.5f));
	return true;
}
#endif
