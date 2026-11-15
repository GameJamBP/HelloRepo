#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "StudioHeroAnimInstance.h"

namespace CameramanVerifyInternal
{
	static const TCHAR* OutJsonPath = TEXT("C:/Users/86184/.gemini/antigravity/brain/43a2ce02-2330-4553-99d4-f5ec0b2f419b/scratch/step5_runtime_verify.json");
	static const TCHAR* CaptureDir = TEXT("C:/Users/86184/.gemini/antigravity/brain/43a2ce02-2330-4553-99d4-f5ec0b2f419b/scratch/ue_captures");

	struct FPhaseStats
	{
		FString Name;
		bool bIsRun = false;
		float ExpectedAngleDeg = 0.0f;
		float DurationSec = 0.0f;
		int32 TotalTicks = 0;
		int32 SteadyTicks = 0;
		float MinSpeed = BIG_NUMBER;
		float MaxSpeed = 0.0f;
		double SumAngleErrorAbs = 0.0;
		float MaxAngleErrorAbs = 0.0f;
		double SumPoseSpeedFirstHalf = 0.0;
		int32 CountFirstHalf = 0;
		double SumPoseSpeedSecondHalf = 0.0;
		int32 CountSecondHalf = 0;
		int32 CurStaticRun = 0;
		int32 MaxStaticRun = 0;
		float MinFootBoneRelFloor = BIG_NUMBER;
		float MinBoundsRelFloor = BIG_NUMBER;
		float MaxBoundsRelFloor = -BIG_NUMBER;
		float MinHandCamDist = BIG_NUMBER;
		float MaxHandCamDist = 0.0f;
		float MinLeftHandCamDist = BIG_NUMBER;
		float MaxLeftHandCamDist = 0.0f;
		FVector StartLoc = FVector::ZeroVector;
		FVector EndLoc = FVector::ZeroVector;
		TSet<FString> MainStatesSeen;
		TSet<FString> LocoStatesSeen;
		bool bCapturedShot = false;
		FString ShotFilename;
	};

	struct FJumpVerifyStats
	{
		FString Name;
		bool bForwardRunJump = false;
		float PeakVerticalSpeed = -BIG_NUMBER;
		float MinVerticalSpeed = BIG_NUMBER;
		float PeakActorZRelOrigin = 0.0f;
		float LandingMinBoundsRelFloor = BIG_NUMBER;
		TArray<FString> StateSequence;
		bool bEnteredJump = false;
		bool bEnteredFall = false;
		bool bEnteredLand = false;
		bool bReturnedToLoco = false;
		bool bCapTakeoff = false;
		bool bCapFall = false;
		bool bCapLand = false;
	};

	static FString JsonEscape(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		return Out;
	}

	class FCameramanPIERuntimeLatentCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCameramanPIERuntimeLatentCommand(FAutomationTestBase* InTest)
			: Test(InTest)
		{
			BuildPhaseList();
		}

		virtual ~FCameramanPIERuntimeLatentCommand() override = default;

		virtual bool Update() override
		{
			UWorld* PlayWorld = (GEditor ? GEditor->PlayWorld.Get() : nullptr);
			if (!PlayWorld)
			{
				WaitPieTicks++;
				if (WaitPieTicks > 600)
				{
					Test->AddError(TEXT("Timed out waiting for PlayWorld to start."));
					return true;
				}
				return false;
			}

			APlayerController* PC = UGameplayStatics::GetPlayerController(PlayWorld, 0);
			ACharacter* Char = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
			if (!Char || !Char->GetMesh() || !Char->GetCharacterMovement())
			{
				WaitPieTicks++;
				if (WaitPieTicks > 600)
				{
					Test->AddError(TEXT("Timed out waiting for BP_Player_Cameraman pawn."));
					return true;
				}
				return false;
			}

			const float Dt = PlayWorld->GetDeltaSeconds();
			if (Dt <= 0.0f)
			{
				return false;
			}

			if (!bInitialized)
			{
				return StepInitialize(PlayWorld, PC, Char, Dt);
			}

			// Clamp very slow frames only in elapsed calculation? No, keep true sim dt; skip abnormal spike > 0.1s for pose derivative.
			const float SafeDt = FMath::Min(Dt, 0.05f);
			USkeletalMeshComponent* Mesh = Char->GetMesh();
			UStudioHeroAnimInstance* AnimInst = Cast<UStudioHeroAnimInstance>(Mesh->GetAnimInstance());

			switch (Stage)
			{
			case EStage::IdleTest:
				return StepIdle(PlayWorld, PC, Char, Mesh, AnimInst, Dt, SafeDt);
			case EStage::SettleBeforeMove:
				return StepSettleMove(PlayWorld, PC, Char, Mesh, AnimInst, Dt);
			case EStage::MoveDirTest:
				return StepMoveDir(PlayWorld, PC, Char, Mesh, AnimInst, Dt, SafeDt);
			case EStage::SettleBeforeJump:
				return StepSettleJump(PlayWorld, PC, Char, Dt);
			case EStage::RunUpJump:
				return StepRunUpJump(PlayWorld, PC, Char, Dt);
			case EStage::ExecuteJump:
				return StepExecuteJump(PlayWorld, PC, Char, Mesh, AnimInst, Dt);
			case EStage::Finished:
			default:
				FinalizeAndValidate();
				return true;
			}
		}

	private:
		enum class EStage : uint8
		{
			IdleTest,
			SettleBeforeMove,
			MoveDirTest,
			SettleBeforeJump,
			RunUpJump,
			ExecuteJump,
			Finished
		};

		struct FMoveCase
		{
			FString Name;
			bool bIsRun;
			float DirYawDeg;
			float Duration;
			FVector CamOffset;
		};

		FAutomationTestBase* Test = nullptr;
		int32 WaitPieTicks = 0;
		bool bInitialized = false;
		float InitSettleElapsed = 0.0f;
		EStage Stage = EStage::IdleTest;

		FVector SafeOriginLoc = FVector::ZeroVector;
		FVector LitStageOriginLoc = FVector::ZeroVector;
		float LitStageFloorZ = 0.0f;
		float BaselineFloorZ = 0.0f;
		float MeshRelZ = 0.0f;
		float CapsuleHalfHeight = 0.0f;
		float InitialMaxWalkSpeed = 0.0f;
		float SprintPressedMaxWalkSpeed = 0.0f;
		float SprintReleasedMaxWalkSpeed = 0.0f;
		bool bHasShoulderCameraProp = false;
		FString ShoulderCameraInfo;
		bool bControlRotYaw = false;
		bool bOrientRotToMove = false;
		int32 MainStatesMachineIdx = INDEX_NONE;
		int32 LocomotionMachineIdx = INDEX_NONE;

		TWeakObjectPtr<USceneComponent> ShoulderCamComp;
		TWeakObjectPtr<ASceneCapture2D> CaptureActor;
		TStrongObjectPtr<UTextureRenderTarget2D> RenderTarget;

		TArray<FMoveCase> MoveCases;
		int32 CurrentMoveIdx = 0;
		bool bPhotoShootPass = true;
		bool bShiftIsPressed = false;
		bool bIdleSettled = false;
		float IdleSettleElapsed = 0.0f;
		float PhaseElapsed = 0.0f;
		TArray<FVector> PrevLocalBones;
		FPhaseStats IdleStats;
		TArray<FPhaseStats> MoveResults;
		FPhaseStats CurrentMoveStats;

		TArray<FJumpVerifyStats> JumpResults;
		int32 CurrentJumpIdx = 0; // 0 = standing jump, 1 = forward running jump
		FJumpVerifyStats CurrentJump;
		int32 JumpHoldTicks = 0;
		bool bJumpTriggered = false;

		void BuildPhaseList()
		{
			// All 8 Walk directions (>2 full 1.0s loops each); CamOffset uses ~350cm 3/4 framing (FOV=60) clear of dollies/crew
			MoveCases.Add({TEXT("Walk_Fwd"),       false,    0.0f, 2.20f, FVector( 305.0f,  185.0f,   8.0f)});
			MoveCases.Add({TEXT("Walk_Fwd_Right"), false,   45.0f, 2.20f, FVector( 305.0f,  185.0f,   8.0f)});
			MoveCases.Add({TEXT("Walk_Right"),     false,   90.0f, 2.20f, FVector( 310.0f, -175.0f,   8.0f)});
			MoveCases.Add({TEXT("Walk_Bwd_Right"), false,  135.0f, 2.20f, FVector( 315.0f,  165.0f,   8.0f)});
			MoveCases.Add({TEXT("Walk_Bwd"),       false,  180.0f, 2.20f, FVector( 315.0f, -165.0f,   8.0f)});
			MoveCases.Add({TEXT("Walk_Bwd_Left"),  false, -135.0f, 2.20f, FVector( 315.0f, -165.0f,   8.0f)});
			MoveCases.Add({TEXT("Walk_Left"),      false,  -90.0f, 2.20f, FVector( 310.0f,  175.0f,   8.0f)});
			MoveCases.Add({TEXT("Walk_Fwd_Left"),  false,  -45.0f, 2.20f, FVector( 305.0f, -185.0f,   8.0f)});

			// All 8 Run/Sprint directions (>2 full 0.667s loops each)
			MoveCases.Add({TEXT("Run_Fwd"),        true,     0.0f, 1.60f, FVector( 315.0f,  185.0f,   8.0f)});
			MoveCases.Add({TEXT("Run_Fwd_Right"),  true,    45.0f, 1.60f, FVector( 315.0f,  185.0f,   8.0f)});
			MoveCases.Add({TEXT("Run_Right"),      true,    90.0f, 1.60f, FVector( 315.0f, -175.0f,   8.0f)});
			MoveCases.Add({TEXT("Run_Bwd_Right"),  true,   135.0f, 1.60f, FVector( 315.0f,  165.0f,   8.0f)});
			MoveCases.Add({TEXT("Run_Bwd"),        true,   180.0f, 1.60f, FVector( 315.0f, -165.0f,   8.0f)});
			MoveCases.Add({TEXT("Run_Bwd_Left"),   true,  -135.0f, 1.60f, FVector( 315.0f, -165.0f,   8.0f)});
			MoveCases.Add({TEXT("Run_Left"),       true,   -90.0f, 1.60f, FVector( 315.0f,  175.0f,   8.0f)});
			MoveCases.Add({TEXT("Run_Fwd_Left"),   true,   -45.0f, 1.60f, FVector( 315.0f, -185.0f,   8.0f)});
		}

		bool StepInitialize(UWorld* PlayWorld, APlayerController* PC, ACharacter* Char, float Dt)
		{
			InitSettleElapsed += Dt;
			if (!Char->GetCharacterMovement()->CurrentFloor.bBlockingHit || InitSettleElapsed < 0.35f)
			{
				return false;
			}

			IFileManager::Get().MakeDirectory(CaptureDir, true);

			USkeletalMeshComponent* Mesh = Char->GetMesh();
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->TickAnimation(0.0f, false);
			Mesh->RefreshBoneTransforms();

			MeshRelZ = Mesh->GetRelativeLocation().Z;
			CapsuleHalfHeight = Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			InitialMaxWalkSpeed = Char->GetCharacterMovement()->MaxWalkSpeed;
			bControlRotYaw = Char->bUseControllerRotationYaw;
			bOrientRotToMove = Char->GetCharacterMovement()->bOrientRotationToMovement;

			for (USceneComponent* Child : Mesh->GetAttachChildren())
			{
				if (Child && (Child->GetAttachSocketName() == TEXT("Socket_ShoulderCamera") || Child->GetName().Contains(TEXT("ShoulderCamera"))))
				{
					ShoulderCamComp = Child;
					bHasShoulderCameraProp = true;
					ShoulderCameraInfo = FString::Printf(TEXT("%s attached to %s"), *Child->GetName(), *Child->GetAttachSocketName().ToString());
					break;
				}
			}

			if (UAnimInstance* AnimInst = Mesh->GetAnimInstance())
			{
				MainStatesMachineIdx = AnimInst->GetStateMachineIndex(TEXT("Main States"));
				LocomotionMachineIdx = AnimInst->GetStateMachineIndex(TEXT("Locomotion"));
			}

			// Find geometric center of open studio floor around PlayerStart so no run hits studio walls.
			const FVector StartLoc = Char->GetActorLocation();
			auto TraceClear = [&](const FVector& Dir) -> float
			{
				FHitResult Hit;
				FCollisionQueryParams Q(TEXT("VerifyClearTrace"), false, Char);
				const FVector P0 = StartLoc + FVector(0, 0, 20.0f);
				const FVector P1 = P0 + Dir * 2500.0f;
				if (PlayWorld->LineTraceSingleByChannel(Hit, P0, P1, ECC_WorldStatic, Q))
				{
					return Hit.Distance;
				}
				return 2500.0f;
			};
			const float PosX = TraceClear(FVector::ForwardVector);
			const float NegX = TraceClear(-FVector::ForwardVector);
			const float PosY = TraceClear(FVector::RightVector);
			const float NegY = TraceClear(-FVector::RightVector);
			SafeOriginLoc = StartLoc + FVector((PosX - NegX) * 0.5f, (PosY - NegY) * 0.5f, 5.0f);
			// Clamp shift within 900cm and re-snap onto floor
			SafeOriginLoc.X = FMath::Clamp(SafeOriginLoc.X, StartLoc.X - 900.0f, StartLoc.X + 900.0f);
			SafeOriginLoc.Y = FMath::Clamp(SafeOriginLoc.Y, StartLoc.Y - 900.0f, StartLoc.Y + 900.0f);
			FHitResult FloorHit;
			FCollisionQueryParams FQ(TEXT("FloorCenterTrace"), false, Char);
			if (PlayWorld->LineTraceSingleByChannel(FloorHit, SafeOriginLoc + FVector(0, 0, 200.0f), SafeOriginLoc - FVector(0, 0, 400.0f), ECC_WorldStatic, FQ))
			{
				SafeOriginLoc.Z = FloorHit.ImpactPoint.Z + CapsuleHalfHeight + 0.5f;
				BaselineFloorZ = FloorHit.ImpactPoint.Z;
			}
			else
			{
				SafeOriginLoc = StartLoc;
				BaselineFloorZ = Char->GetCharacterMovement()->CurrentFloor.HitResult.ImpactPoint.Z;
			}

			// Compute well-lit green-screen stage center (0, 80) for lit PNG captures (facing -Y toward studio cameras, green screen behind).
			// Use multi-trace from Z=150 down to -150 and skip any starting-penetrating overhead collider hits.
			LitStageOriginLoc = FVector(0.0f, 80.0f, BaselineFloorZ + CapsuleHalfHeight + 0.5f);
			LitStageFloorZ = BaselineFloorZ;
			bool bFoundStageFloor = false;
			FString StageFloorHitDesc = TEXT("FallbackBaseline");
			auto FindValidStageFloor = [&](ECollisionChannel Channel) -> bool
			{
				TArray<FHitResult> StageHits;
				if (!PlayWorld->LineTraceMultiByChannel(StageHits, FVector(0.0f, 80.0f, 150.0f), FVector(0.0f, 80.0f, -150.0f), Channel, FQ))
				{
					return false;
				}
				float BestZ = -BIG_NUMBER;
				bool bMatched = false;
				for (const FHitResult& H : StageHits)
				{
					if (!H.bStartPenetrating && H.ImpactNormal.Z > 0.8f && H.ImpactPoint.Z < 30.0f)
					{
						if (!bMatched || H.ImpactPoint.Z > BestZ)
						{
							BestZ = H.ImpactPoint.Z;
							bMatched = true;
							StageFloorHitDesc = FString::Printf(TEXT("%s.%s (Z=%.2f)"),
								H.GetActor() ? *H.GetActor()->GetName() : TEXT("None"),
								H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("None"),
								BestZ);
						}
					}
				}
				if (bMatched)
				{
					LitStageFloorZ = BestZ;
					LitStageOriginLoc.Z = LitStageFloorZ + CapsuleHalfHeight + 0.5f;
				}
				return bMatched;
			};
			bFoundStageFloor = FindValidStageFloor(ECC_Pawn) || FindValidStageFloor(ECC_Visibility);
			UE_LOG(LogTemp, Display, TEXT("VerifyCameramanRuntime StageFloorTrace found=%d hit=%s LitFloorZ=%.2f"),
				bFoundStageFloor ? 1 : 0, *StageFloorHitDesc, LitStageFloorZ);

			// Start on the brightly lit green-screen stage for Idle + Stage Photo Shoot + Standing Jump captures.
			Char->SetActorLocationAndRotation(LitStageOriginLoc, FRotator(0.0f, -90.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(FRotator(0.0f, -90.0f, 0.0f));
			Char->GetCharacterMovement()->StopMovementImmediately();

			// Spawn offscreen SceneCapture2D + RT
			UTextureRenderTarget2D* NewRT = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), TEXT("CameramanVerifyRT"));
			NewRT->RenderTargetFormat = RTF_RGBA8_SRGB;
			NewRT->InitAutoFormat(1280, 720);
			NewRT->UpdateResourceImmediate(true);
			RenderTarget.Reset(NewRT);

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector WarmPos = LitStageOriginLoc + FVector(185.0f, -305.0f, 8.0f);
			ASceneCapture2D* Cap = PlayWorld->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), WarmPos, FRotator::ZeroRotator, SpawnParams);
			if (Cap && Cap->GetCaptureComponent2D())
			{
				USceneCaptureComponent2D* CapComp = Cap->GetCaptureComponent2D();
				CapComp->TextureTarget = RenderTarget.Get();
				CapComp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
				CapComp->bCaptureEveryFrame = false;
				CapComp->bCaptureOnMovement = false;
				CapComp->bAlwaysPersistRenderingState = true;
				CapComp->FOVAngle = 60.0f;
				CapComp->PostProcessSettings.bOverride_MotionBlurAmount = true;
				CapComp->PostProcessSettings.MotionBlurAmount = 0.0f;
				CapComp->PostProcessSettings.bOverride_AutoExposureSpeedUp = true;
				CapComp->PostProcessSettings.AutoExposureSpeedUp = 20.0f;
				CapComp->PostProcessSettings.bOverride_AutoExposureSpeedDown = true;
				CapComp->PostProcessSettings.AutoExposureSpeedDown = 20.0f;
				CaptureActor = Cap;

				// Warm up auto-exposure on lit stage
				const FRotator WarmRot = (LitStageOriginLoc - WarmPos).Rotation();
				Cap->SetActorRotation(WarmRot);
				for (int32 w = 0; w < 6; ++w)
				{
					CapComp->CaptureScene();
				}
				const FColor CenterPx = UKismetRenderingLibrary::ReadRenderTargetPixel(PlayWorld, RenderTarget.Get(), 640, 360);
				UE_LOG(LogTemp, Display, TEXT("VerifyCameramanRuntime Warmup Stage CenterPixel=(R=%d G=%d B=%d A=%d)"), CenterPx.R, CenterPx.G, CenterPx.B, CenterPx.A);
			}

			bPhotoShootPass = true;
			bIdleSettled = false;
			IdleSettleElapsed = 0.0f;
			IdleStats = FPhaseStats();
			IdleStats.Name = TEXT("Idle");
			PhaseElapsed = 0.0f;
			PrevLocalBones.Reset();
			Stage = EStage::IdleTest;
			bInitialized = true;
			UE_LOG(LogTemp, Display, TEXT("VerifyCameramanRuntime Init: SafeOrigin=(%.1f,%.1f,%.1f) LitStage=(%.1f,%.1f,%.1f) LitFloorZ=%.2f MeshRelZ=%.2f"),
				SafeOriginLoc.X, SafeOriginLoc.Y, SafeOriginLoc.Z, LitStageOriginLoc.X, LitStageOriginLoc.Y, LitStageOriginLoc.Z, LitStageFloorZ, MeshRelZ);
			return false;
		}

		TArray<FVector> SampleLocalBones(ACharacter* Char, USkeletalMeshComponent* Mesh) const
		{
			static const FName Bones[] = {
				TEXT("pelvis"), TEXT("foot_l"), TEXT("foot_r"), TEXT("ball_l"), TEXT("ball_r"),
				TEXT("hand_l"), TEXT("hand_r"), TEXT("head")
			};
			const FTransform InvMesh = Mesh->GetComponentTransform().Inverse();
			TArray<FVector> Out;
			Out.Reserve(UE_ARRAY_COUNT(Bones));
			for (FName B : Bones)
			{
				const FVector W = Mesh->GetBoneLocation(B, EBoneSpaces::WorldSpace);
				Out.Add(InvMesh.TransformPosition(W));
			}
			return Out;
		}

		void SampleTelemetryTick(ACharacter* Char, USkeletalMeshComponent* Mesh, UStudioHeroAnimInstance* AnimInst, FPhaseStats& Stats, float Elapsed, float SafeDt, float SteadyStartSec)
		{
			Stats.TotalTicks++;
			const float GroundSpeed = AnimInst ? AnimInst->GroundSpeed : Char->GetVelocity().Size2D();
			const float DirAngle = AnimInst ? AnimInst->DirectionAngle : 0.0f;
			if (AnimInst)
			{
				if (MainStatesMachineIdx != INDEX_NONE)
				{
					Stats.MainStatesSeen.Add(AnimInst->GetCurrentStateName(MainStatesMachineIdx).ToString());
				}
				if (LocomotionMachineIdx != INDEX_NONE)
				{
					Stats.LocoStatesSeen.Add(AnimInst->GetCurrentStateName(LocomotionMachineIdx).ToString());
				}
			}

			TArray<FVector> CurBones = SampleLocalBones(Char, Mesh);
			float PoseSpeed = 0.0f;
			if (PrevLocalBones.Num() == CurBones.Num() && SafeDt > 0.0005f)
			{
				float SumDelta = 0.0f;
				for (int32 i = 0; i < CurBones.Num(); ++i)
				{
					SumDelta += FVector::Distance(CurBones[i], PrevLocalBones[i]);
				}
				PoseSpeed = SumDelta / SafeDt;
			}
			PrevLocalBones = MoveTemp(CurBones);

			const float FloorZ = Char->GetCharacterMovement()->CurrentFloor.bBlockingHit
				? Char->GetCharacterMovement()->CurrentFloor.HitResult.ImpactPoint.Z
				: BaselineFloorZ;

			const float FootL_Z = Mesh->GetBoneLocation(TEXT("foot_l"), EBoneSpaces::WorldSpace).Z - FloorZ;
			const float FootR_Z = Mesh->GetBoneLocation(TEXT("foot_r"), EBoneSpaces::WorldSpace).Z - FloorZ;
			const float BallL_Z = Mesh->GetBoneLocation(TEXT("ball_l"), EBoneSpaces::WorldSpace).Z - FloorZ;
			const float BallR_Z = Mesh->GetBoneLocation(TEXT("ball_r"), EBoneSpaces::WorldSpace).Z - FloorZ;
			const float MinFoot = FMath::Min(FMath::Min(FootL_Z, FootR_Z), FMath::Min(BallL_Z, BallR_Z));
			const float MeshMinZ = Mesh->CalcBounds(Mesh->GetComponentTransform()).GetBox().Min.Z - FloorZ;

			if (ShoulderCamComp.IsValid())
			{
				const FVector CamWorld = ShoulderCamComp->GetComponentLocation();
				const float DR = FVector::Distance(Mesh->GetBoneLocation(TEXT("hand_r"), EBoneSpaces::WorldSpace), CamWorld);
				const float DL = FVector::Distance(Mesh->GetBoneLocation(TEXT("hand_l"), EBoneSpaces::WorldSpace), CamWorld);
				Stats.MinHandCamDist = FMath::Min(Stats.MinHandCamDist, DR);
				Stats.MaxHandCamDist = FMath::Max(Stats.MaxHandCamDist, DR);
				Stats.MinLeftHandCamDist = FMath::Min(Stats.MinLeftHandCamDist, DL);
				Stats.MaxLeftHandCamDist = FMath::Max(Stats.MaxLeftHandCamDist, DL);
			}

			if (Elapsed >= SteadyStartSec)
			{
				Stats.SteadyTicks++;
				Stats.MinSpeed = FMath::Min(Stats.MinSpeed, GroundSpeed);
				Stats.MaxSpeed = FMath::Max(Stats.MaxSpeed, GroundSpeed);
				const float AngErr = FMath::Abs(FMath::FindDeltaAngleDegrees(Stats.ExpectedAngleDeg, DirAngle));
				Stats.SumAngleErrorAbs += AngErr;
				Stats.MaxAngleErrorAbs = FMath::Max(Stats.MaxAngleErrorAbs, AngErr);

				const float HalfPoint = SteadyStartSec + 0.5f * FMath::Max(0.4f, Stats.DurationSec - SteadyStartSec);
				if (Elapsed < HalfPoint)
				{
					Stats.SumPoseSpeedFirstHalf += PoseSpeed;
					Stats.CountFirstHalf++;
				}
				else
				{
					Stats.SumPoseSpeedSecondHalf += PoseSpeed;
					Stats.CountSecondHalf++;
				}

				if (Stats.SteadyTicks > 2)
				{
					if (PoseSpeed < 8.0f)
					{
						Stats.CurStaticRun++;
						Stats.MaxStaticRun = FMath::Max(Stats.MaxStaticRun, Stats.CurStaticRun);
					}
					else
					{
						Stats.CurStaticRun = 0;
					}
				}

				Stats.MinFootBoneRelFloor = FMath::Min(Stats.MinFootBoneRelFloor, MinFoot);
				Stats.MinBoundsRelFloor = FMath::Min(Stats.MinBoundsRelFloor, MeshMinZ);
				Stats.MaxBoundsRelFloor = FMath::Max(Stats.MaxBoundsRelFloor, MeshMinZ);
			}
		}

		void CaptureShot(UWorld* PlayWorld, ACharacter* Char, const FString& NameNoExt, const FVector& LocalOffsetFromChar, float LookAtOffsetZ, float AnchorZ = -BIG_NUMBER, float FovDeg = 60.0f)
		{
			ASceneCapture2D* Cap = CaptureActor.Get();
			if (!Cap || !Cap->GetCaptureComponent2D() || !RenderTarget.Get())
			{
				return;
			}
			const FVector CharLoc = Char->GetActorLocation();
			const float BaseZ = (AnchorZ > -BIG_NUMBER * 0.5f) ? AnchorZ : CharLoc.Z;
			const FTransform RefXform(FRotator(0.0f, Char->GetActorRotation().Yaw, 0.0f), FVector(CharLoc.X, CharLoc.Y, BaseZ));
			const FVector CamPos = RefXform.TransformPositionNoScale(LocalOffsetFromChar);
			const FVector LookTarget = FVector(CharLoc.X, CharLoc.Y, BaseZ + LookAtOffsetZ);
			const FRotator CamRot = (LookTarget - CamPos).Rotation();
			Cap->SetActorLocationAndRotation(CamPos, CamRot);

			USceneCaptureComponent2D* Comp = Cap->GetCaptureComponent2D();
			Comp->FOVAngle = FovDeg;
			Comp->CaptureScene();
			Comp->CaptureScene(); // Prime persistent view/exposure history on new pose
			UKismetRenderingLibrary::ExportRenderTarget(PlayWorld, RenderTarget.Get(), CaptureDir, NameNoExt + TEXT(".png"));
		}

		bool StepIdle(UWorld* PlayWorld, APlayerController* PC, ACharacter* Char, USkeletalMeshComponent* Mesh, UStudioHeroAnimInstance* AnimInst, float Dt, float SafeDt)
		{
			if (!bIdleSettled)
			{
				IdleSettleElapsed += Dt;
				const bool bOnGround = Char->GetCharacterMovement()->IsMovingOnGround();
				const FString MainStateName = (AnimInst && MainStatesMachineIdx != INDEX_NONE)
					? AnimInst->GetCurrentStateName(MainStatesMachineIdx).ToString()
					: TEXT("Locomotion");
				if ((IdleSettleElapsed >= 0.25f && bOnGround && MainStateName == TEXT("Locomotion")) || IdleSettleElapsed >= 1.2f)
				{
					bIdleSettled = true;
					LitStageOriginLoc = Char->GetActorLocation();
					PhaseElapsed = 0.0f;
					PrevLocalBones.Reset();
				}
				return false;
			}

			if (PhaseElapsed == 0.0f)
			{
				IdleStats.DurationSec = 1.4f;
				IdleStats.StartLoc = Char->GetActorLocation();
			}
			PhaseElapsed += Dt;
			SampleTelemetryTick(Char, Mesh, AnimInst, IdleStats, PhaseElapsed, SafeDt, 0.2f);

			if (!IdleStats.bCapturedShot && PhaseElapsed >= 0.7f)
			{
				IdleStats.bCapturedShot = true;
				IdleStats.ShotFilename = TEXT("ue_idle_34.png");
				CaptureShot(PlayWorld, Char, TEXT("ue_idle_34"), FVector(305.0f, 185.0f, 8.0f), -4.0f, -BIG_NUMBER, 60.0f);
				CaptureShot(PlayWorld, Char, TEXT("ue_idle_feet_closeup"), FVector(125.0f, 85.0f, -55.0f), -80.0f, -BIG_NUMBER, 45.0f);
				CaptureShot(PlayWorld, Char, TEXT("ue_idle_shoulder_cam"), FVector(108.0f, 86.0f, 52.0f), 40.0f, -BIG_NUMBER, 45.0f);
			}

			if (PhaseElapsed >= 1.4f)
			{
				IdleStats.EndLoc = Char->GetActorLocation();
				CurrentMoveIdx = 0;
				bPhotoShootPass = true;
				BeginNextMoveSettle(PC, Char);
			}
			return false;
		}

		void BeginNextMoveSettle(APlayerController* PC, ACharacter* Char)
		{
			if (CurrentMoveIdx >= MoveCases.Num())
			{
				if (bShiftIsPressed)
				{
					PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::LeftShift, IE_Released, 0.0f, false, FPlatformTime::Cycles64()));
					bShiftIsPressed = false;
				}
				if (bPhotoShootPass)
				{
					// Completed lit-stage photo shoot! Now run full 2-cycle telemetry on unobstructed floor.
					bPhotoShootPass = false;
					CurrentMoveIdx = 0;
				}
				else
				{
					CurrentJumpIdx = 0;
					BeginJumpCase(PC, Char);
					return;
				}
			}

			const FMoveCase& MC = MoveCases[CurrentMoveIdx];
			if (MC.bIsRun && !bShiftIsPressed)
			{
				PC->InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), EKeys::LeftShift, IE_Pressed, 1.0f, false, FPlatformTime::Cycles64()));
				bShiftIsPressed = true;
				SprintPressedMaxWalkSpeed = Char->GetCharacterMovement()->MaxWalkSpeed;
			}

			const FVector StartOrigin = bPhotoShootPass ? LitStageOriginLoc : SafeOriginLoc;
			const float StartYaw = bPhotoShootPass ? -90.0f : 0.0f;
			Char->SetActorLocationAndRotation(StartOrigin, FRotator(0.0f, StartYaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(FRotator(0.0f, StartYaw, 0.0f));
			Char->GetCharacterMovement()->StopMovementImmediately();
			PhaseElapsed = 0.0f;
			PrevLocalBones.Reset();
			Stage = EStage::SettleBeforeMove;
		}

		bool StepSettleMove(UWorld* PlayWorld, APlayerController* PC, ACharacter* Char, USkeletalMeshComponent* Mesh, UStudioHeroAnimInstance* AnimInst, float Dt)
		{
			PhaseElapsed += Dt;
			const float GroundSpeed = AnimInst ? AnimInst->GroundSpeed : Char->GetVelocity().Size2D();
			if (PhaseElapsed >= 0.30f && GroundSpeed < 5.0f)
			{
				const FMoveCase& MC = MoveCases[CurrentMoveIdx];
				if (MC.bIsRun)
				{
					SprintPressedMaxWalkSpeed = FMath::Max(SprintPressedMaxWalkSpeed, Char->GetCharacterMovement()->MaxWalkSpeed);
				}
				CurrentMoveStats = FPhaseStats();
				CurrentMoveStats.Name = MC.Name;
				CurrentMoveStats.bIsRun = MC.bIsRun;
				CurrentMoveStats.ExpectedAngleDeg = MC.DirYawDeg;
				CurrentMoveStats.DurationSec = bPhotoShootPass ? (MC.bIsRun ? 0.50f : 0.75f) : MC.Duration;
				CurrentMoveStats.StartLoc = Char->GetActorLocation();
				PhaseElapsed = 0.0f;
				PrevLocalBones.Reset();
				Stage = EStage::MoveDirTest;
			}
			return false;
		}

		bool StepMoveDir(UWorld* PlayWorld, APlayerController* PC, ACharacter* Char, USkeletalMeshComponent* Mesh, UStudioHeroAnimInstance* AnimInst, float Dt, float SafeDt)
		{
			const FMoveCase& MC = MoveCases[CurrentMoveIdx];
			const FRotator CtrlRot = PC->GetControlRotation();
			const FVector WorldDir = (FRotator(0.0f, CtrlRot.Yaw + MC.DirYawDeg, 0.0f)).Vector();
			Char->AddMovementInput(WorldDir, 1.0f);

			PhaseElapsed += Dt;
			if (!bPhotoShootPass)
			{
				SampleTelemetryTick(Char, Mesh, AnimInst, CurrentMoveStats, PhaseElapsed, SafeDt, 0.38f);
			}
			else
			{
				const float ShotTime = MC.bIsRun ? 0.40f : 0.65f;
				if (!CurrentMoveStats.bCapturedShot && PhaseElapsed >= ShotTime)
				{
					CurrentMoveStats.bCapturedShot = true;
					const FString ShotName = FString::Printf(TEXT("ue_%s"), *MC.Name.ToLower());
					CaptureShot(PlayWorld, Char, ShotName, MC.CamOffset, -4.0f, -BIG_NUMBER, 60.0f);
				}
			}

			const float PhaseLen = CurrentMoveStats.DurationSec;
			if (PhaseElapsed >= PhaseLen)
			{
				if (!bPhotoShootPass)
				{
					CurrentMoveStats.EndLoc = Char->GetActorLocation();
					CurrentMoveStats.ShotFilename = FString::Printf(TEXT("ue_%s.png"), *MC.Name.ToLower());
					MoveResults.Add(CurrentMoveStats);
				}
				CurrentMoveIdx++;
				BeginNextMoveSettle(PC, Char);
			}
			return false;
		}

		void BeginJumpCase(APlayerController* PC, ACharacter* Char)
		{
			if (CurrentJumpIdx >= 2)
			{
				Stage = EStage::Finished;
				return;
			}
			CurrentJump = FJumpVerifyStats();
			CurrentJump.bForwardRunJump = (CurrentJumpIdx == 1);
			CurrentJump.Name = CurrentJump.bForwardRunJump ? TEXT("Jump_Running_Fwd") : TEXT("Jump_Standing");
			const FVector JumpOrigin = CurrentJump.bForwardRunJump ? SafeOriginLoc : LitStageOriginLoc;
			const float JumpYaw = CurrentJump.bForwardRunJump ? 0.0f : -90.0f;
			Char->SetActorLocationAndRotation(JumpOrigin, FRotator(0.0f, JumpYaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(FRotator(0.0f, JumpYaw, 0.0f));
			Char->GetCharacterMovement()->StopMovementImmediately();
			PhaseElapsed = 0.0f;
			JumpHoldTicks = 0;
			bJumpTriggered = false;
			Stage = EStage::SettleBeforeJump;
		}

		bool StepSettleJump(UWorld* PlayWorld, APlayerController* PC, ACharacter* Char, float Dt)
		{
			PhaseElapsed += Dt;
			if (PhaseElapsed >= 0.30f && Char->GetCharacterMovement()->IsMovingOnGround())
			{
				if (!CurrentJump.bForwardRunJump)
				{
					// Wait >= 0.3s after LeftShift release before checking MaxWalkSpeed restoration
					SprintReleasedMaxWalkSpeed = Char->GetCharacterMovement()->MaxWalkSpeed;
				}
				PhaseElapsed = 0.0f;
				if (CurrentJump.bForwardRunJump)
				{
					Stage = EStage::RunUpJump;
				}
				else
				{
					Stage = EStage::ExecuteJump;
				}
			}
			return false;
		}

		bool StepRunUpJump(UWorld* PlayWorld, APlayerController* PC, ACharacter* Char, float Dt)
		{
			Char->AddMovementInput(PC->GetControlRotation().Vector(), 1.0f);
			PhaseElapsed += Dt;
			if (PhaseElapsed >= 0.55f)
			{
				PhaseElapsed = 0.0f;
				Stage = EStage::ExecuteJump;
			}
			return false;
		}

		bool StepExecuteJump(UWorld* PlayWorld, APlayerController* PC, ACharacter* Char, USkeletalMeshComponent* Mesh, UStudioHeroAnimInstance* AnimInst, float Dt)
		{
			if (CurrentJump.bForwardRunJump)
			{
				Char->AddMovementInput(PC->GetControlRotation().Vector(), 1.0f);
			}

			if (!bJumpTriggered)
			{
				Char->Jump();
				JumpHoldTicks++;
				if (JumpHoldTicks >= 2)
				{
					Char->StopJumping();
					bJumpTriggered = true;
				}
			}

			PhaseElapsed += Dt;
			const FVector RefOrigin = CurrentJump.bForwardRunJump ? SafeOriginLoc : LitStageOriginLoc;
			const float VZ = AnimInst ? AnimInst->VerticalSpeed : Char->GetVelocity().Z;
			CurrentJump.PeakVerticalSpeed = FMath::Max(CurrentJump.PeakVerticalSpeed, VZ);
			CurrentJump.MinVerticalSpeed = FMath::Min(CurrentJump.MinVerticalSpeed, VZ);
			CurrentJump.PeakActorZRelOrigin = FMath::Max(CurrentJump.PeakActorZRelOrigin, Char->GetActorLocation().Z - RefOrigin.Z);

			FString MainStateName = TEXT("Unknown");
			if (AnimInst && MainStatesMachineIdx != INDEX_NONE)
			{
				MainStateName = AnimInst->GetCurrentStateName(MainStatesMachineIdx).ToString();
				if (CurrentJump.StateSequence.Num() == 0 || CurrentJump.StateSequence.Last() != MainStateName)
				{
					CurrentJump.StateSequence.Add(MainStateName);
				}
			}

			if (MainStateName == TEXT("Jump"))
			{
				CurrentJump.bEnteredJump = true;
				if (!CurrentJump.bForwardRunJump && !CurrentJump.bCapTakeoff && PhaseElapsed >= 0.14f)
				{
					CurrentJump.bCapTakeoff = true;
					CaptureShot(PlayWorld, Char, TEXT("ue_jump_takeoff"), FVector(315.0f, 185.0f, 40.0f), 25.0f, LitStageOriginLoc.Z, 62.0f);
				}
			}
			else if (MainStateName == TEXT("Fall Loop"))
			{
				CurrentJump.bEnteredFall = true;
				if (!CurrentJump.bForwardRunJump && !CurrentJump.bCapFall)
				{
					CurrentJump.bCapFall = true;
					CaptureShot(PlayWorld, Char, TEXT("ue_jump_fall"), FVector(380.0f, 220.0f, 75.0f), 65.0f, LitStageOriginLoc.Z, 68.0f);
				}
			}
			else if (MainStateName == TEXT("Land"))
			{
				CurrentJump.bEnteredLand = true;
				const float FloorZ = Char->GetCharacterMovement()->CurrentFloor.bBlockingHit
					? Char->GetCharacterMovement()->CurrentFloor.HitResult.ImpactPoint.Z
					: BaselineFloorZ;
				const float BoundsZ = Mesh->CalcBounds(Mesh->GetComponentTransform()).GetBox().Min.Z - FloorZ;
				CurrentJump.LandingMinBoundsRelFloor = FMath::Min(CurrentJump.LandingMinBoundsRelFloor, BoundsZ);
				if (!CurrentJump.bForwardRunJump && !CurrentJump.bCapLand)
				{
					CurrentJump.bCapLand = true;
					CaptureShot(PlayWorld, Char, TEXT("ue_jump_land"), FVector(310.0f, 185.0f, 8.0f), -4.0f, LitStageOriginLoc.Z, 60.0f);
				}
			}
			else if (MainStateName == TEXT("Locomotion") && (CurrentJump.bEnteredJump || CurrentJump.bEnteredFall || CurrentJump.bEnteredLand) && PhaseElapsed > 0.45f)
			{
				CurrentJump.bReturnedToLoco = true;
			}

			if ((CurrentJump.bReturnedToLoco && PhaseElapsed >= 0.85f) || PhaseElapsed >= 3.0f)
			{
				JumpResults.Add(CurrentJump);
				CurrentJumpIdx++;
				BeginJumpCase(PC, Char);
			}
			return false;
		}

		void FinalizeAndValidate()
		{
			// Cleanup capture actor/RT
			if (CaptureActor.IsValid())
			{
				CaptureActor->Destroy();
			}
			RenderTarget.Reset();

			auto SerializePhase = [](const FPhaseStats& S) -> FString
			{
				const float FirstMean = S.CountFirstHalf > 0 ? float(S.SumPoseSpeedFirstHalf / S.CountFirstHalf) : 0.0f;
				const float SecondMean = S.CountSecondHalf > 0 ? float(S.SumPoseSpeedSecondHalf / S.CountSecondHalf) : 0.0f;
				const float MeanAngErr = S.SteadyTicks > 0 ? float(S.SumAngleErrorAbs / S.SteadyTicks) : 999.0f;
				const float Displacement = FVector::Distance(S.StartLoc, S.EndLoc);
				FString MainStr, LocoStr;
				for (const FString& M : S.MainStatesSeen) { MainStr += (MainStr.IsEmpty() ? TEXT("") : TEXT(", ")) + FString::Printf(TEXT("\"%s\""), *JsonEscape(M)); }
				for (const FString& L : S.LocoStatesSeen) { LocoStr += (LocoStr.IsEmpty() ? TEXT("") : TEXT(", ")) + FString::Printf(TEXT("\"%s\""), *JsonEscape(L)); }
				return FString::Printf(
					TEXT("    {\n")
					TEXT("      \"name\": \"%s\",\n")
					TEXT("      \"is_run\": %s,\n")
					TEXT("      \"expected_angle_deg\": %.1f,\n")
					TEXT("      \"total_ticks\": %d,\n")
					TEXT("      \"steady_ticks\": %d,\n")
					TEXT("      \"min_speed\": %.2f,\n")
					TEXT("      \"max_speed\": %.2f,\n")
					TEXT("      \"displacement_cm\": %.1f,\n")
					TEXT("      \"mean_angle_err_deg\": %.2f,\n")
					TEXT("      \"max_angle_err_deg\": %.2f,\n")
					TEXT("      \"first_half_pose_speed\": %.2f,\n")
					TEXT("      \"second_half_pose_speed\": %.2f,\n")
					TEXT("      \"max_consecutive_static_frames\": %d,\n")
					TEXT("      \"min_foot_bone_rel_floor\": %.2f,\n")
					TEXT("      \"min_bounds_rel_floor\": %.2f,\n")
					TEXT("      \"max_bounds_rel_floor\": %.2f,\n")
					TEXT("      \"hand_r_cam_dist\": [%.2f, %.2f],\n")
					TEXT("      \"hand_l_cam_dist\": [%.2f, %.2f],\n")
					TEXT("      \"main_states\": [%s],\n")
					TEXT("      \"loco_states\": [%s],\n")
					TEXT("      \"shot\": \"%s\"\n")
					TEXT("    }"),
					*JsonEscape(S.Name), S.bIsRun ? TEXT("true") : TEXT("false"), S.ExpectedAngleDeg,
					S.TotalTicks, S.SteadyTicks,
					S.MinSpeed < BIG_NUMBER * 0.5f ? S.MinSpeed : 0.0f, S.MaxSpeed, Displacement,
					MeanAngErr, S.MaxAngleErrorAbs,
					FirstMean, SecondMean, S.MaxStaticRun,
					S.MinFootBoneRelFloor < BIG_NUMBER * 0.5f ? S.MinFootBoneRelFloor : 0.0f,
					S.MinBoundsRelFloor < BIG_NUMBER * 0.5f ? S.MinBoundsRelFloor : 0.0f,
					S.MaxBoundsRelFloor > -BIG_NUMBER * 0.5f ? S.MaxBoundsRelFloor : 0.0f,
					S.MinHandCamDist < BIG_NUMBER * 0.5f ? S.MinHandCamDist : 0.0f, S.MaxHandCamDist,
					S.MinLeftHandCamDist < BIG_NUMBER * 0.5f ? S.MinLeftHandCamDist : 0.0f, S.MaxLeftHandCamDist,
					*MainStr, *LocoStr, *JsonEscape(S.ShotFilename));
			};

			FString MovesJson;
			for (int32 i = 0; i < MoveResults.Num(); ++i)
			{
				if (i > 0) MovesJson += TEXT(",\n");
				MovesJson += SerializePhase(MoveResults[i]);
			}

			FString JumpsJson;
			for (int32 i = 0; i < JumpResults.Num(); ++i)
			{
				const FJumpVerifyStats& J = JumpResults[i];
				FString Seq;
				for (int32 k = 0; k < J.StateSequence.Num(); ++k)
				{
					if (k > 0) Seq += TEXT(", ");
					Seq += FString::Printf(TEXT("\"%s\""), *JsonEscape(J.StateSequence[k]));
				}
				if (i > 0) JumpsJson += TEXT(",\n");
				JumpsJson += FString::Printf(
					TEXT("    {\n")
					TEXT("      \"name\": \"%s\",\n")
					TEXT("      \"peak_vertical_speed\": %.2f,\n")
					TEXT("      \"min_vertical_speed\": %.2f,\n")
					TEXT("      \"peak_z_rel_origin\": %.2f,\n")
					TEXT("      \"landing_min_bounds_rel_floor\": %.2f,\n")
					TEXT("      \"entered_jump\": %s,\n")
					TEXT("      \"entered_fall\": %s,\n")
					TEXT("      \"entered_land\": %s,\n")
					TEXT("      \"returned_to_loco\": %s,\n")
					TEXT("      \"state_sequence\": [%s]\n")
					TEXT("    }"),
					*JsonEscape(J.Name), J.PeakVerticalSpeed, J.MinVerticalSpeed, J.PeakActorZRelOrigin,
					J.LandingMinBoundsRelFloor < BIG_NUMBER * 0.5f ? J.LandingMinBoundsRelFloor : 0.0f,
					J.bEnteredJump ? TEXT("true") : TEXT("false"),
					J.bEnteredFall ? TEXT("true") : TEXT("false"),
					J.bEnteredLand ? TEXT("true") : TEXT("false"),
					J.bReturnedToLoco ? TEXT("true") : TEXT("false"),
					*Seq);
			}

			const FString JsonOut = FString::Printf(
				TEXT("{\n")
				TEXT("  \"setup\": {\n")
				TEXT("    \"mesh_rel_z\": %.2f,\n")
				TEXT("    \"capsule_half_height\": %.2f,\n")
				TEXT("    \"initial_max_walk_speed\": %.2f,\n")
				TEXT("    \"sprint_pressed_max_walk_speed\": %.2f,\n")
				TEXT("    \"sprint_released_max_walk_speed\": %.2f,\n")
				TEXT("    \"use_controller_rotation_yaw\": %s,\n")
				TEXT("    \"orient_rotation_to_movement\": %s,\n")
				TEXT("    \"has_shoulder_camera\": %s,\n")
				TEXT("    \"shoulder_camera_info\": \"%s\",\n")
				TEXT("    \"safe_origin\": [%.1f, %.1f, %.1f],\n")
				TEXT("    \"baseline_floor_z\": %.2f\n")
				TEXT("  },\n")
				TEXT("  \"idle\":\n%s,\n")
				TEXT("  \"locomotion\": [\n%s\n  ],\n")
				TEXT("  \"jumps\": [\n%s\n  ]\n")
				TEXT("}\n"),
				MeshRelZ, CapsuleHalfHeight, InitialMaxWalkSpeed, SprintPressedMaxWalkSpeed, SprintReleasedMaxWalkSpeed,
				bControlRotYaw ? TEXT("true") : TEXT("false"),
				bOrientRotToMove ? TEXT("true") : TEXT("false"),
				bHasShoulderCameraProp ? TEXT("true") : TEXT("false"), *JsonEscape(ShoulderCameraInfo),
				SafeOriginLoc.X, SafeOriginLoc.Y, SafeOriginLoc.Z, BaselineFloorZ,
				*SerializePhase(IdleStats), *MovesJson, *JumpsJson);

			FFileHelper::SaveStringToFile(JsonOut, OutJsonPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogTemp, Display, TEXT("VerifyCameramanRuntime wrote %s"), OutJsonPath);

			// Assertions
			Test->TestTrue(TEXT("MeshRelZ is -95.5"), FMath::IsNearlyEqual(MeshRelZ, -95.5f, 0.5f));
			Test->TestTrue(TEXT("Has ShoulderCamera prop attached"), bHasShoulderCameraProp);
			Test->TestTrue(TEXT("Initial MaxWalkSpeed is ~240"), FMath::IsNearlyEqual(InitialMaxWalkSpeed, 240.0f, 1.0f));
			Test->TestTrue(TEXT("LeftShift Sprint MaxWalkSpeed is ~550"), FMath::IsNearlyEqual(SprintPressedMaxWalkSpeed, 550.0f, 1.0f));
			Test->TestTrue(TEXT("LeftShift Release restores MaxWalkSpeed ~240"), FMath::IsNearlyEqual(SprintReleasedMaxWalkSpeed, 240.0f, 1.0f));
			Test->TestTrue(TEXT("LitStageFloorZ < 30cm (valid studio floor)"), LitStageFloorZ < 30.0f);
			Test->TestTrue(TEXT("Idle remains stationary on floor (< 5cm)"), FVector::Distance(IdleStats.StartLoc, IdleStats.EndLoc) < 5.0f);
			Test->TestTrue(TEXT("Idle shoes near floor (-4.0..+4.0cm)"), IdleStats.MinBoundsRelFloor >= -4.0f && IdleStats.MinBoundsRelFloor <= 4.0f);

			for (const FPhaseStats& S : MoveResults)
			{
				const float FirstMean = S.CountFirstHalf > 0 ? float(S.SumPoseSpeedFirstHalf / S.CountFirstHalf) : 0.0f;
				const float SecondMean = S.CountSecondHalf > 0 ? float(S.SumPoseSpeedSecondHalf / S.CountSecondHalf) : 0.0f;
				const float MeanAngErr = S.SteadyTicks > 0 ? float(S.SumAngleErrorAbs / S.SteadyTicks) : 999.0f;
				const float ExpSpeed = S.bIsRun ? 480.0f : 200.0f;
				Test->TestTrue(FString::Printf(TEXT("%s reached speed >= %.0f"), *S.Name, ExpSpeed), S.MaxSpeed >= ExpSpeed);
				Test->TestTrue(FString::Printf(TEXT("%s direction angle error <= 20 deg (got %.2f)"), *S.Name, MeanAngErr), MeanAngErr <= 20.0f);
				Test->TestTrue(FString::Printf(TEXT("%s has non-empty second half (1st=%.1f 2nd=%.1f)"), *S.Name, FirstMean, SecondMean), SecondMean >= 40.0f);
				Test->TestTrue(FString::Printf(TEXT("%s has no frozen tail (maxStatic=%d)"), *S.Name, S.MaxStaticRun), S.MaxStaticRun < 6);
				Test->TestTrue(FString::Printf(TEXT("%s shoe grounding bounds in [-4.5..+4.5] (got %.2f)"), *S.Name, S.MinBoundsRelFloor),
					S.MinBoundsRelFloor >= -4.5f && S.MinBoundsRelFloor <= 4.5f);
			}

			for (const FJumpVerifyStats& J : JumpResults)
			{
				Test->TestTrue(FString::Printf(TEXT("%s entered Jump"), *J.Name), J.bEnteredJump);
				Test->TestTrue(FString::Printf(TEXT("%s entered Fall Loop"), *J.Name), J.bEnteredFall);
				if (!J.bForwardRunJump)
				{
					Test->TestTrue(FString::Printf(TEXT("%s entered Land"), *J.Name), J.bEnteredLand);
				}
				Test->TestTrue(FString::Printf(TEXT("%s returned cleanly to Locomotion"), *J.Name), J.bReturnedToLoco);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStudioVerifyCameramanRuntimeTest, "Studio.VerifyCameramanRuntime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStudioVerifyCameramanRuntimeTest::RunTest(const FString& Parameters)
{
	AddCommand(new FEditorLoadMap(TEXT("/Game/Art/Maps/Lvl_GreenScreenStudio")));
	AddCommand(new FWaitForShadersToFinishCompiling());
	AddCommand(new FStartPIECommand(false));
	AddCommand(new CameramanVerifyInternal::FCameramanPIERuntimeLatentCommand(this));
	AddCommand(new FEndPlayMapCommand());
	AddCommand(new FWaitLatentCommand(0.5f));
	return true;
}
#endif
