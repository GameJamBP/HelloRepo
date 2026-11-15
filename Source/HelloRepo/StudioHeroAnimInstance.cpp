#include "StudioHeroAnimInstance.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UStudioHeroAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	OwnerCharacter = Cast<ACharacter>(TryGetPawnOwner());
	if (OwnerCharacter)
	{
		MovementComponent = OwnerCharacter->GetCharacterMovement();
	}
}

void UStudioHeroAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!OwnerCharacter)
	{
		OwnerCharacter = Cast<ACharacter>(TryGetPawnOwner());
	}
	if (!OwnerCharacter)
	{
		return;
	}
	if (!MovementComponent)
	{
		MovementComponent = OwnerCharacter->GetCharacterMovement();
	}

	const FVector Velocity = OwnerCharacter->GetVelocity();
	const FVector HorizontalVel(Velocity.X, Velocity.Y, 0.0f);

	GroundSpeed = HorizontalVel.Size();
	VerticalSpeed = Velocity.Z;
	ForwardSpeed = FVector::DotProduct(HorizontalVel, OwnerCharacter->GetActorForwardVector());
	RightSpeed = FVector::DotProduct(HorizontalVel, OwnerCharacter->GetActorRightVector());
	DirectionAngle = GroundSpeed > 1.0f
		? FMath::RadiansToDegrees(FMath::Atan2(RightSpeed, ForwardSpeed))
		: 0.0f;

	bIsInAir = MovementComponent ? MovementComponent->IsFalling() : false;
	bIsMoving = GroundSpeed > 3.0f;
	bIsRunning = GroundSpeed >= RunSpeedThreshold;
}

#if WITH_EDITOR
#include "Animation/BlendSpace.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "K2Node_VariableGet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"

namespace
{
	void SaveAssetPackage(UObject* Asset)
	{
		if (!Asset) return;
		UPackage* Pkg = Asset->GetOutermost();
		Pkg->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Pkg, Asset, *Filename, SaveArgs);
	}

	UBlendSpace* Build4WayBlendSpaceAsset(const FString& PackagePath, const FString& AssetName, const FString& AnimPrefix, USkeleton* Skeleton)
	{
		const FString FullPath = PackagePath + TEXT(".") + AssetName;
		UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *FullPath);
		if (!BS)
		{
			UPackage* Pkg = CreatePackage(*PackagePath);
			Pkg->FullyLoad();
			BS = NewObject<UBlendSpace>(Pkg, *AssetName, RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(BS);
		}
		BS->SetSkeleton(Skeleton);

		// Configure BlendParameters[0] = Direction (-180..180, wrapped) and [1] = Speed (0..600)
		if (FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters")))
		{
			FBlendParameter* Params = Prop->ContainerPtrToValuePtr<FBlendParameter>(BS);
			Params[0].DisplayName = TEXT("Direction");
			Params[0].Min = -180.0f;
			Params[0].Max = 180.0f;
			Params[0].GridNum = 4;
			Params[0].bWrapInput = true;

			Params[1].DisplayName = TEXT("Speed");
			Params[1].Min = 0.0f;
			Params[1].Max = 600.0f;
			Params[1].GridNum = 4;
			Params[1].bWrapInput = false;
		}

		// Clear existing samples safely
		while (BS->GetNumberOfBlendSamples() > 0)
		{
			BS->DeleteSample(0);
		}

		auto LoadSeq = [&](const TCHAR* Suffix) -> UAnimSequence*
		{
			const FString Path = FString::Printf(TEXT("/Game/Art/Character/Anim/%s_%s.%s_%s"), *AnimPrefix, Suffix, *AnimPrefix, Suffix);
			return LoadObject<UAnimSequence>(nullptr, *Path);
		};

		UAnimSequence* Idle    = LoadSeq(TEXT("Idle"));
		UAnimSequence* WalkF   = LoadSeq(TEXT("Walk_Fwd"));
		UAnimSequence* WalkB   = LoadSeq(TEXT("Walk_Bwd"));
		UAnimSequence* WalkL   = LoadSeq(TEXT("Walk_Left"));
		UAnimSequence* WalkR   = LoadSeq(TEXT("Walk_Right"));
		UAnimSequence* RunF    = LoadSeq(TEXT("Run_Fwd"));
		UAnimSequence* RunB    = LoadSeq(TEXT("Run_Bwd"));
		UAnimSequence* RunL    = LoadSeq(TEXT("Run_Left"));
		UAnimSequence* RunR    = LoadSeq(TEXT("Run_Right"));

		if (Idle)
		{
			BS->AddSample(Idle, FVector(   0.0f,   0.0f, 0.0f));
			BS->AddSample(Idle, FVector(-180.0f,   0.0f, 0.0f));
			BS->AddSample(Idle, FVector( 180.0f,   0.0f, 0.0f));
		}
		if (WalkF) BS->AddSample(WalkF, FVector(   0.0f, 240.0f, 0.0f));
		if (WalkR) BS->AddSample(WalkR, FVector(  90.0f, 240.0f, 0.0f));
		if (WalkL) BS->AddSample(WalkL, FVector( -90.0f, 240.0f, 0.0f));
		if (WalkB)
		{
			BS->AddSample(WalkB, FVector( 180.0f, 240.0f, 0.0f));
			BS->AddSample(WalkB, FVector(-180.0f, 240.0f, 0.0f));
		}
		if (RunF)  BS->AddSample(RunF,  FVector(   0.0f, 550.0f, 0.0f));
		if (RunR)  BS->AddSample(RunR,  FVector(  90.0f, 550.0f, 0.0f));
		if (RunL)  BS->AddSample(RunL,  FVector( -90.0f, 550.0f, 0.0f));
		if (RunB)
		{
			BS->AddSample(RunB,  FVector( 180.0f, 550.0f, 0.0f));
			BS->AddSample(RunB,  FVector(-180.0f, 550.0f, 0.0f));
		}

		BS->ValidateSampleData();
		BS->ResampleData();
		SaveAssetPackage(BS);
		return BS;
	}

	void Assign4WayBlendSpaceToABP(const FString& AbpPath, UBlendSpace* BS)
	{
		UAnimBlueprint* ABP = LoadObject<UAnimBlueprint>(nullptr, *AbpPath);
		if (!ABP || !BS) return;

		TArray<UEdGraph*> Graphs;
		ABP->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			TArray<UEdGraphNode*> NodesCopy = Graph->Nodes;
			for (UEdGraphNode* Node : NodesCopy)
			{
				if (UAnimGraphNode_BlendSpacePlayer* BSNode = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
				{
					BSNode->Node.SetBlendSpace(BS);
					BSNode->ReconstructNode();

					UEdGraphPin* PinX = BSNode->FindPin(TEXT("X"));
					UEdGraphPin* PinY = BSNode->FindPin(TEXT("Y"));

					auto WireVar = [&](UEdGraphPin* TargetPin, const FName VarName, float OffsetY)
					{
						if (!TargetPin) return;
						TargetPin->BreakAllPinLinks();
						UK2Node_VariableGet* GetNode = NewObject<UK2Node_VariableGet>(Graph);
						GetNode->VariableReference.SetSelfMember(VarName);
						GetNode->NodePosX = BSNode->NodePosX - 240;
						GetNode->NodePosY = BSNode->NodePosY + static_cast<int32>(OffsetY);
						Graph->AddNode(GetNode, false, false);
						GetNode->AllocateDefaultPins();
						if (UEdGraphPin* ValPin = GetNode->GetValuePin())
						{
							ValPin->MakeLinkTo(TargetPin);
						}
					};

					WireVar(PinX, TEXT("DirectionAngle"), 0.0f);
					WireVar(PinY, TEXT("GroundSpeed"), 60.0f);
				}
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ABP);
		FKismetEditorUtilities::CompileBlueprint(ABP);
		SaveAssetPackage(ABP);
	}

	static void ExecuteBuild4WayLocomotion()
	{
		USkeleton* Skel = LoadObject<USkeleton>(nullptr, TEXT("/Game/Art/Character/Mesh/SKM_Hero_JackieChan_Skeleton.SKM_Hero_JackieChan_Skeleton"));
		if (!Skel)
		{
			UE_LOG(LogTemp, Error, TEXT("Studio.Build4WayLocomotion: Could not load SKM_Hero_JackieChan_Skeleton"));
			return;
		}
		UBlendSpace* CamBS = Build4WayBlendSpaceAsset(TEXT("/Game/Art/Character/Blueprints/BS_Cameraman_4Way"), TEXT("BS_Cameraman_4Way"), TEXT("AS_Cam"), Skel);
		UBlendSpace* HeroBS = Build4WayBlendSpaceAsset(TEXT("/Game/Art/Character/Blueprints/BS_JackieChan_4Way"), TEXT("BS_JackieChan_4Way"), TEXT("AS_Hero"), Skel);
		Assign4WayBlendSpaceToABP(TEXT("/Game/Art/Character/Blueprints/ABP_Cameraman.ABP_Cameraman"), CamBS);
		Assign4WayBlendSpaceToABP(TEXT("/Game/Art/Character/Blueprints/ABP_JackieChan.ABP_JackieChan"), HeroBS);
		UE_LOG(LogTemp, Display, TEXT("Studio.Build4WayLocomotion completed successfully."));
	}

	static FAutoConsoleCommand GBuild4WayLocomotionCmd(
		TEXT("Studio.Build4WayLocomotion"),
		TEXT("Creates 2D 4-way BlendSpaces and wires them into ABP_Cameraman and ABP_JackieChan."),
		FConsoleCommandDelegate::CreateStatic(&ExecuteBuild4WayLocomotion)
	);
}

#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStudioBuild4WayLocomotionTest, "Studio.Build4WayLocomotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStudioBuild4WayLocomotionTest::RunTest(const FString& Parameters)
{
	ExecuteBuild4WayLocomotion();
	return true;
}
#endif
