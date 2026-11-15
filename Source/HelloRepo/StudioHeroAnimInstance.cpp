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
	DirectionAngle = GroundSpeed > 0.5f
		? FMath::RadiansToDegrees(FMath::Atan2(RightSpeed, ForwardSpeed))
		: DirectionAngle;

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
#include "AnimGraphNode_TransitionResult.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "Kismet/KismetMathLibrary.h"
#include "EdGraph/EdGraphSchema.h"
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

		// Configure BlendParameters[0] = Direction (-180..180, 8-grid wrapped) and [1] = Speed (0..600)
		if (FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters")))
		{
			FBlendParameter* Params = Prop->ContainerPtrToValuePtr<FBlendParameter>(BS);
			Params[0].DisplayName = TEXT("Direction");
			Params[0].Min = -180.0f;
			Params[0].Max = 180.0f;
			Params[0].GridNum = 8;
			Params[0].bSnapToGrid = true;
			Params[0].bWrapInput = true;

			Params[1].DisplayName = TEXT("Speed");
			Params[1].Min = 0.0f;
			Params[1].Max = 600.0f;
			Params[1].GridNum = 4;
			Params[1].bSnapToGrid = false;
			Params[1].bWrapInput = false;
		}

		BS->TargetWeightInterpolationSpeedPerSec = 5.0f;
		BS->bTargetWeightInterpolationEaseInOut = true;
		BS->bAllowMarkerBasedSync = true;

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

		UAnimSequence* Idle   = LoadSeq(TEXT("Idle"));
		UAnimSequence* WalkF  = LoadSeq(TEXT("Walk_Fwd"));
		UAnimSequence* WalkFR = LoadSeq(TEXT("Walk_Fwd_Right"));
		UAnimSequence* WalkR  = LoadSeq(TEXT("Walk_Right"));
		UAnimSequence* WalkBR = LoadSeq(TEXT("Walk_Bwd_Right"));
		UAnimSequence* WalkB  = LoadSeq(TEXT("Walk_Bwd"));
		UAnimSequence* WalkBL = LoadSeq(TEXT("Walk_Bwd_Left"));
		UAnimSequence* WalkL  = LoadSeq(TEXT("Walk_Left"));
		UAnimSequence* WalkFL = LoadSeq(TEXT("Walk_Fwd_Left"));

		UAnimSequence* RunF   = LoadSeq(TEXT("Run_Fwd"));
		UAnimSequence* RunFR  = LoadSeq(TEXT("Run_Fwd_Right"));
		UAnimSequence* RunR   = LoadSeq(TEXT("Run_Right"));
		UAnimSequence* RunBR  = LoadSeq(TEXT("Run_Bwd_Right"));
		UAnimSequence* RunB   = LoadSeq(TEXT("Run_Bwd"));
		UAnimSequence* RunBL  = LoadSeq(TEXT("Run_Bwd_Left"));
		UAnimSequence* RunL   = LoadSeq(TEXT("Run_Left"));
		UAnimSequence* RunFL  = LoadSeq(TEXT("Run_Fwd_Left"));

		if (Idle)
		{
			for (const float Angle : { -180.0f, -135.0f, -90.0f, -45.0f, 0.0f, 45.0f, 90.0f, 135.0f, 180.0f })
			{
				BS->AddSample(Idle, FVector(Angle, 0.0f, 0.0f));
			}
		}

		const float WalkSpeed = 240.0f;
		if (WalkF)  BS->AddSample(WalkF,  FVector(   0.0f, WalkSpeed, 0.0f));
		if (WalkFR) BS->AddSample(WalkFR, FVector(  45.0f, WalkSpeed, 0.0f));
		if (WalkR)  BS->AddSample(WalkR,  FVector(  90.0f, WalkSpeed, 0.0f));
		if (WalkBR) BS->AddSample(WalkBR, FVector( 135.0f, WalkSpeed, 0.0f));
		if (WalkB)
		{
			BS->AddSample(WalkB, FVector( 180.0f, WalkSpeed, 0.0f));
			BS->AddSample(WalkB, FVector(-180.0f, WalkSpeed, 0.0f));
		}
		if (WalkBL) BS->AddSample(WalkBL, FVector(-135.0f, WalkSpeed, 0.0f));
		if (WalkL)  BS->AddSample(WalkL,  FVector( -90.0f, WalkSpeed, 0.0f));
		if (WalkFL) BS->AddSample(WalkFL, FVector( -45.0f, WalkSpeed, 0.0f));

		const float RunSpeed = 550.0f;
		if (RunF)  BS->AddSample(RunF,  FVector(   0.0f, RunSpeed, 0.0f));
		if (RunFR) BS->AddSample(RunFR, FVector(  45.0f, RunSpeed, 0.0f));
		if (RunR)  BS->AddSample(RunR,  FVector(  90.0f, RunSpeed, 0.0f));
		if (RunBR) BS->AddSample(RunBR, FVector( 135.0f, RunSpeed, 0.0f));
		if (RunB)
		{
			BS->AddSample(RunB, FVector( 180.0f, RunSpeed, 0.0f));
			BS->AddSample(RunB, FVector(-180.0f, RunSpeed, 0.0f));
		}
		if (RunBL) BS->AddSample(RunBL, FVector(-135.0f, RunSpeed, 0.0f));
		if (RunL)  BS->AddSample(RunL,  FVector( -90.0f, RunSpeed, 0.0f));
		if (RunFL) BS->AddSample(RunFL, FVector( -45.0f, RunSpeed, 0.0f));

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
						GetNode->CreateNewGuid();
						Graph->AddNode(GetNode, false, false);
						GetNode->PostPlacedNewNode();
						GetNode->AllocateDefaultPins();
						if (UEdGraphPin* ValPin = GetNode->GetValuePin())
						{
							ValPin->MakeLinkTo(TargetPin);
						}
					};

					WireVar(PinX, TEXT("DirectionAngle"), 0.0f);
					WireVar(PinY, TEXT("GroundSpeed"), 60.0f);

					// Remove any now-unlinked variable getter nodes left behind from prior wires.
					for (int32 NodeIdx = Graph->Nodes.Num() - 1; NodeIdx >= 0; --NodeIdx)
					{
						if (UK2Node_VariableGet* OldGet = Cast<UK2Node_VariableGet>(Graph->Nodes[NodeIdx]))
						{
							UEdGraphPin* ValPin = OldGet->GetValuePin();
							if (!ValPin || ValPin->LinkedTo.Num() == 0)
							{
								Graph->RemoveNode(OldGet);
							}
						}
					}
				}
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ABP);
		FKismetEditorUtilities::CompileBlueprint(ABP);
		SaveAssetPackage(ABP);
	}

	void FixCameramanJumpTransition(UAnimBlueprint* ABP)
	{
		if (!ABP) return;

		TArray<UEdGraph*> Graphs;
		ABP->GetAllGraphs(Graphs);
		bool bFixed = false;

		for (UEdGraph* Graph : Graphs)
		{
			if (!Graph || !Graph->GetOuter() || Graph->GetOuter()->GetName() != TEXT("AnimStateTransitionNode_0"))
			{
				continue;
			}

			UAnimGraphNode_TransitionResult* ResultNode = nullptr;
			UK2Node_VariableGet* IsFallingNode = nullptr;
			UK2Node_CommutativeAssociativeBinaryOperator* AndNode = nullptr;

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!ResultNode)
				{
					ResultNode = Cast<UAnimGraphNode_TransitionResult>(Node);
				}
				if (UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Node))
				{
					if (VarGet->GetVarName() == TEXT("IsFalling"))
					{
						IsFallingNode = VarGet;
					}
				}
				if (!AndNode)
				{
					if (UK2Node_CommutativeAssociativeBinaryOperator* Op = Cast<UK2Node_CommutativeAssociativeBinaryOperator>(Node))
					{
						if (Op->GetFunctionName() == TEXT("BooleanAND"))
						{
							AndNode = Op;
						}
					}
				}
			}

			if (!ResultNode || !IsFallingNode || !AndNode)
			{
				continue;
			}

			// Remove every node except ResultNode, IsFallingNode, and AndNode so re-runs stay clean.
			TArray<UEdGraphNode*> ToRemove;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node != ResultNode && Node != IsFallingNode && Node != AndNode)
				{
					ToRemove.Add(Node);
				}
			}
			for (UEdGraphNode* Node : ToRemove)
			{
				Graph->RemoveNode(Node);
			}

			const UEdGraphSchema* Schema = Graph->GetSchema();
			UEdGraphPin* AndPinA = AndNode->FindPinChecked(TEXT("A"));
			UEdGraphPin* AndPinB = AndNode->FindPinChecked(TEXT("B"));
			UEdGraphPin* AndRet = AndNode->GetReturnValuePin();
			UEdGraphPin* ResultIn = ResultNode->FindPinChecked(TEXT("bCanEnterTransition"));

			AndPinA->BreakAllPinLinks();
			AndPinB->BreakAllPinLinks();
			AndRet->BreakAllPinLinks();
			ResultIn->BreakAllPinLinks();

			const bool bConnIsFalling = Schema->TryCreateConnection(IsFallingNode->GetValuePin(), AndPinA);
			const bool bConnResult = Schema->TryCreateConnection(AndRet, ResultIn);

			UK2Node_VariableGet* GetVS = NewObject<UK2Node_VariableGet>(Graph);
			GetVS->VariableReference.SetSelfMember(TEXT("VerticalSpeed"));
			GetVS->NodePosX = IsFallingNode->NodePosX;
			GetVS->NodePosY = IsFallingNode->NodePosY + 110;
			GetVS->CreateNewGuid();
			Graph->AddNode(GetVS, false, false);
			GetVS->PostPlacedNewNode();
			GetVS->AllocateDefaultPins();

			UK2Node_CallFunction* GreaterNode = NewObject<UK2Node_CallFunction>(Graph);
			GreaterNode->FunctionReference.SetExternalMember(
				GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Greater_DoubleDouble),
				UKismetMathLibrary::StaticClass());
			GreaterNode->NodePosX = IsFallingNode->NodePosX + 210;
			GreaterNode->NodePosY = IsFallingNode->NodePosY + 110;
			GreaterNode->CreateNewGuid();
			Graph->AddNode(GreaterNode, false, false);
			GreaterNode->PostPlacedNewNode();
			GreaterNode->AllocateDefaultPins();

			UEdGraphPin* GreaterPinA = GreaterNode->FindPinChecked(TEXT("A"));
			UEdGraphPin* GreaterPinB = GreaterNode->FindPinChecked(TEXT("B"));
			Schema->TrySetDefaultValue(*GreaterPinB, TEXT("100.0"));
			const bool bSetPinB = (GreaterPinB->GetDefaultAsString() == TEXT("100.0") || GreaterPinB->DefaultValue == TEXT("100.0"));

			const bool bConnVS = Schema->TryCreateConnection(GetVS->GetValuePin(), GreaterPinA);
			const bool bConnGreater = Schema->TryCreateConnection(GreaterNode->GetReturnValuePin(), AndPinB);

			UE_LOG(LogTemp, Display, TEXT("FixCameramanJumpTransition(%s): IsFalling->A=%d VS->GreaterA=%d Greater->B=%d And->Result=%d SetPinB=%d PinB='%s'"),
				*Graph->GetPathName(), (int32)bConnIsFalling, (int32)bConnVS, (int32)bConnGreater, (int32)bConnResult, (int32)bSetPinB, *GreaterPinB->GetDefaultAsString());
			bFixed = bConnIsFalling && bConnVS && bConnGreater && bConnResult && bSetPinB;
		}

		if (bFixed)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(ABP);
			FKismetEditorUtilities::CompileBlueprint(ABP);
			SaveAssetPackage(ABP);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("FixCameramanJumpTransition: Did not find/fix Main States AnimStateTransitionNode_0 cleanly."));
		}
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

	static void ExecuteBuildCameramanBlendSpace()
	{
		USkeleton* Skel = LoadObject<USkeleton>(nullptr, TEXT("/Game/Art/Character/Mesh/SKM_Hero_JackieChan_Skeleton.SKM_Hero_JackieChan_Skeleton"));
		if (!Skel)
		{
			UE_LOG(LogTemp, Error, TEXT("Studio.BuildCameramanBlendSpace: Could not load SKM_Hero_JackieChan_Skeleton"));
			return;
		}
		UBlendSpace* CamBS = Build4WayBlendSpaceAsset(TEXT("/Game/Art/Character/Blueprints/BS_Cameraman_4Way"), TEXT("BS_Cameraman_4Way"), TEXT("AS_Cam"), Skel);
		Assign4WayBlendSpaceToABP(TEXT("/Game/Art/Character/Blueprints/ABP_Cameraman.ABP_Cameraman"), CamBS);
		if (UAnimBlueprint* CamABP = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Art/Character/Blueprints/ABP_Cameraman.ABP_Cameraman")))
		{
			FixCameramanJumpTransition(CamABP);
		}
		UE_LOG(LogTemp, Display, TEXT("Studio.BuildCameramanBlendSpace completed successfully (samples=%d)."), CamBS ? CamBS->GetNumberOfBlendSamples() : -1);
	}

	static FAutoConsoleCommand GBuild4WayLocomotionCmd(
		TEXT("Studio.Build4WayLocomotion"),
		TEXT("Creates 2D directional BlendSpaces and wires them into ABP_Cameraman and ABP_JackieChan."),
		FConsoleCommandDelegate::CreateStatic(&ExecuteBuild4WayLocomotion)
	);

	static FAutoConsoleCommand GBuildCameramanBlendSpaceCmd(
		TEXT("Studio.BuildCameramanBlendSpace"),
		TEXT("Rebuilds BS_Cameraman_4Way with 8-way directional samples and wires it into ABP_Cameraman."),
		FConsoleCommandDelegate::CreateStatic(&ExecuteBuildCameramanBlendSpace)
	);
}

#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStudioBuild4WayLocomotionTest, "Studio.Build4WayLocomotion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStudioBuild4WayLocomotionTest::RunTest(const FString& Parameters)
{
	ExecuteBuild4WayLocomotion();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStudioBuildCameramanBlendSpaceTest, "Studio.BuildCameramanBlendSpace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStudioBuildCameramanBlendSpaceTest::RunTest(const FString& Parameters)
{
	ExecuteBuildCameramanBlendSpace();
	return true;
}
#endif
