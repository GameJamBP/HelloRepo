#include "PhotoHUDWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "CutActor.h"
#include "Engine/Texture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "FilmStyleSubsystem.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "FileHelpers.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"
#endif

void UPhotoHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsDesignTime())
	{
		return;
	}

	if (UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this))
	{
		if (ActiveCutActorChangedHandle.IsValid())
		{
			Subsystem->GetOnActiveCutActorChangedNative().Remove(ActiveCutActorChangedHandle);
			ActiveCutActorChangedHandle.Reset();
		}

		ActiveCutActorChangedHandle = Subsystem->GetOnActiveCutActorChangedNative().AddUObject(
			this, &UPhotoHUDWidget::HandleActiveCutActorChanged);

		SyncCutPreview(Subsystem->GetActiveCutActor());
	}
	else
	{
		SyncCutPreview(nullptr);
	}
}

void UPhotoHUDWidget::NativeDestruct()
{
	if (ActiveCutActorChangedHandle.IsValid())
	{
		if (UFilmStyleSubsystem* Subsystem = UFilmStyleSubsystem::Get(this))
		{
			Subsystem->GetOnActiveCutActorChangedNative().Remove(ActiveCutActorChangedHandle);
		}
		ActiveCutActorChangedHandle.Reset();
	}

	Super::NativeDestruct();
}

UUserWidget* UPhotoHUDWidget::GetCutPreviewWidget() const
{
	if (IsValid(WBP_CutPreview))
	{
		return WBP_CutPreview.Get();
	}
	return WidgetTree ? Cast<UUserWidget>(WidgetTree->FindWidget(TEXT("WBP_CutPreview"))) : nullptr;
}

void UPhotoHUDWidget::HandleActiveCutActorChanged(ACutActor* /*PreviousCutActor*/, ACutActor* NewCutActor)
{
	SyncCutPreview(NewCutActor);
}

void UPhotoHUDWidget::SyncCutPreview(ACutActor* CutActor)
{
	UUserWidget* PreviewWidget = GetCutPreviewWidget();
	if (!IsValid(PreviewWidget))
	{
		return;
	}

	UTexture* CutTexture = IsValid(CutActor) ? Cast<UTexture>(CutActor->RenderTarget.Get()) : nullptr;
	ApplyTextureToCutPreview(PreviewWidget, CutTexture);
}

void UPhotoHUDWidget::ApplyTextureToCutPreview(UUserWidget* PreviewWidget, UTexture* CutTexture)
{
	if (!IsValid(PreviewWidget))
	{
		return;
	}

	if (!IsValid(CutTexture))
	{
		PreviewWidget->SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	static const FName SetCutPreviewBrushFuncName(TEXT("SetCutPreviewBrush"));
	if (UFunction* SetBrushFunc = PreviewWidget->FindFunction(SetCutPreviewBrushFuncName))
	{
		FStructOnScope FuncParams(SetBrushFunc);
		for (TFieldIterator<FObjectProperty> It(SetBrushFunc); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				It->SetObjectPropertyValue_InContainer(FuncParams.GetStructMemory(), CutTexture);
				break;
			}
		}
		PreviewWidget->ProcessEvent(SetBrushFunc, FuncParams.GetStructMemory());
	}

	PreviewWidget->SetVisibility(ESlateVisibility::Visible);
}

#if WITH_EDITOR
namespace PhotoHUDEditorSync
{
	static void RemoveWidgetTreeNodeIfPresent(UWidgetBlueprint* WidgetBP, UWidgetTree* Tree, const FName WidgetName)
	{
		if (!Tree)
		{
			return;
		}
		if (WidgetBP)
		{
			WidgetBP->OnVariableRemoved(WidgetName);
		}
		if (UWidget* Existing = Tree->FindWidget(WidgetName))
		{
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Existing))
			{
				TArray<UWidget*> ChildrenToRemove;
				for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
				{
					if (UWidget* Child = Panel->GetChildAt(Index))
					{
						ChildrenToRemove.Add(Child);
					}
				}
				Panel->ClearChildren();
				for (UWidget* Child : ChildrenToRemove)
				{
					RemoveWidgetTreeNodeIfPresent(WidgetBP, Tree, Child->GetFName());
				}
			}
			Existing->RemoveFromParent();
			Tree->RemoveWidget(Existing);
		}
	}

	static void EnsureWidgetVariableGuid(UWidgetBlueprint* WidgetBP, const UWidget* Widget)
	{
		if (WidgetBP && Widget && !WidgetBP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))
		{
			WidgetBP->OnVariableAdded(Widget->GetFName());
		}
	}

	static bool EnsureActionMapping(UInputMappingContext* Context, const UInputAction* Action, const FKey& Key)
	{
		if (!Context || !Action)
		{
			return false;
		}
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action == Action && Mapping.Key == Key)
			{
				return false;
			}
		}
		Context->Modify();
		Context->MapKey(Action, Key);
		return true;
	}
}

bool UPhotoHUDWidget::EditorSyncPhotoHUDAssets()
{
	UWidgetBlueprint* HudBP = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/UI/WBP_HUD.WBP_HUD"));
	UWidgetBlueprint* PhotoHudBP = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/UI/WBP_PhotoHUD.WBP_PhotoHUD"));
	if (!HudBP || !PhotoHudBP || !HudBP->WidgetTree || !PhotoHudBP->WidgetTree)
	{
		UE_LOG(LogTemp, Error, TEXT("EditorSyncPhotoHUDAssets: Failed to load WBP_HUD or WBP_PhotoHUD."));
		return false;
	}

	TArray<UPackage*> PackagesToSave;

	if (PhotoHudBP->ParentClass != UPhotoHUDWidget::StaticClass())
	{
		PhotoHudBP->Modify();
		PhotoHudBP->ParentClass = UPhotoHUDWidget::StaticClass();
		FBlueprintEditorUtils::RefreshAllNodes(PhotoHudBP);
	}

	UCanvasPanel* TargetCanvas = Cast<UCanvasPanel>(PhotoHudBP->WidgetTree->RootWidget);
	if (!TargetCanvas)
	{
		TargetCanvas = Cast<UCanvasPanel>(PhotoHudBP->WidgetTree->FindWidget(TEXT("CanvasPanel_39")));
	}
	if (!TargetCanvas)
	{
		UE_LOG(LogTemp, Error, TEXT("EditorSyncPhotoHUDAssets: WBP_PhotoHUD root is not a CanvasPanel."));
		return false;
	}

	// Idempotently clear any previously synced Photo Preview widgets before cloning fresh from WBP_HUD.
	PhotoHUDEditorSync::RemoveWidgetTreeNodeIfPresent(PhotoHudBP, PhotoHudBP->WidgetTree, TEXT("VerticalBox_0"));
	PhotoHUDEditorSync::RemoveWidgetTreeNodeIfPresent(PhotoHudBP, PhotoHudBP->WidgetTree, TEXT("TextBlock_245"));
	PhotoHUDEditorSync::RemoveWidgetTreeNodeIfPresent(PhotoHudBP, PhotoHudBP->WidgetTree, TEXT("Border_0"));
	PhotoHUDEditorSync::RemoveWidgetTreeNodeIfPresent(PhotoHudBP, PhotoHudBP->WidgetTree, TEXT("WBP_CutPreview"));
	PhotoHUDEditorSync::RemoveWidgetTreeNodeIfPresent(PhotoHudBP, PhotoHudBP->WidgetTree, TEXT("WBP_Key_4"));

	UVerticalBox* SrcVBox = Cast<UVerticalBox>(HudBP->WidgetTree->FindWidget(TEXT("VerticalBox_0")));
	UTextBlock* SrcTitle = Cast<UTextBlock>(HudBP->WidgetTree->FindWidget(TEXT("TextBlock_245")));
	UBorder* SrcBorder = Cast<UBorder>(HudBP->WidgetTree->FindWidget(TEXT("Border_0")));
	UWidget* SrcCutPreview = HudBP->WidgetTree->FindWidget(TEXT("WBP_CutPreview"));
	UWidget* SrcKey4 = HudBP->WidgetTree->FindWidget(TEXT("WBP_Key_4"));
	if (!SrcVBox || !SrcTitle || !SrcBorder || !SrcCutPreview || !SrcKey4)
	{
		UE_LOG(LogTemp, Error, TEXT("EditorSyncPhotoHUDAssets: Missing Photo Preview widgets in WBP_HUD."));
		return false;
	}

	UVerticalBox* NewVBox = PhotoHudBP->WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("VerticalBox_0"));
	NewVBox->bIsVariable = SrcVBox->bIsVariable;
	PhotoHUDEditorSync::EnsureWidgetVariableGuid(PhotoHudBP, NewVBox);

	UTextBlock* NewTitle = DuplicateObject<UTextBlock>(SrcTitle, PhotoHudBP->WidgetTree, TEXT("TextBlock_245"));
	NewTitle->Slot = nullptr;
	NewTitle->bIsVariable = SrcTitle->bIsVariable;
	PhotoHUDEditorSync::EnsureWidgetVariableGuid(PhotoHudBP, NewTitle);

	UBorder* NewBorder = PhotoHudBP->WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("Border_0"));
	NewBorder->bIsVariable = SrcBorder->bIsVariable;
	NewBorder->SetBrush(SrcBorder->Background);
	NewBorder->SetBrushColor(SrcBorder->GetBrushColor());
	NewBorder->SetPadding(SrcBorder->GetPadding());
	NewBorder->SetContentColorAndOpacity(SrcBorder->GetContentColorAndOpacity());
	NewBorder->SetHorizontalAlignment(SrcBorder->GetHorizontalAlignment());
	NewBorder->SetVerticalAlignment(SrcBorder->GetVerticalAlignment());
	PhotoHUDEditorSync::EnsureWidgetVariableGuid(PhotoHudBP, NewBorder);

	UWidget* NewCutPreview = DuplicateObject<UWidget>(SrcCutPreview, PhotoHudBP->WidgetTree, TEXT("WBP_CutPreview"));
	NewCutPreview->Slot = nullptr;
	NewCutPreview->bIsVariable = true;
	PhotoHUDEditorSync::EnsureWidgetVariableGuid(PhotoHudBP, NewCutPreview);

	UWidget* NewKey4 = DuplicateObject<UWidget>(SrcKey4, PhotoHudBP->WidgetTree, TEXT("WBP_Key_4"));
	NewKey4->Slot = nullptr;
	NewKey4->bIsVariable = SrcKey4->bIsVariable;
	PhotoHUDEditorSync::EnsureWidgetVariableGuid(PhotoHudBP, NewKey4);

	NewBorder->SetContent(NewCutPreview);
	if (const UBorderSlot* SrcBorderSlot = Cast<UBorderSlot>(SrcCutPreview->Slot))
	{
		if (UBorderSlot* NewBorderSlot = Cast<UBorderSlot>(NewCutPreview->Slot))
		{
			NewBorderSlot->SetPadding(SrcBorderSlot->GetPadding());
			NewBorderSlot->SetHorizontalAlignment(SrcBorderSlot->GetHorizontalAlignment());
			NewBorderSlot->SetVerticalAlignment(SrcBorderSlot->GetVerticalAlignment());
		}
	}

	auto CopyVerticalBoxSlotLayout = [](const UVerticalBoxSlot* SrcSlot, UVerticalBoxSlot* DstSlot)
	{
		if (!SrcSlot || !DstSlot)
		{
			return;
		}
		DstSlot->SetPadding(SrcSlot->GetPadding());
		DstSlot->SetSize(SrcSlot->GetSize());
		DstSlot->SetHorizontalAlignment(SrcSlot->GetHorizontalAlignment());
		DstSlot->SetVerticalAlignment(SrcSlot->GetVerticalAlignment());
	};

	if (UVerticalBoxSlot* TitleSlot = NewVBox->AddChildToVerticalBox(NewTitle))
	{
		CopyVerticalBoxSlotLayout(Cast<UVerticalBoxSlot>(SrcTitle->Slot), TitleSlot);
	}
	if (UVerticalBoxSlot* BorderSlot = NewVBox->AddChildToVerticalBox(NewBorder))
	{
		CopyVerticalBoxSlotLayout(Cast<UVerticalBoxSlot>(SrcBorder->Slot), BorderSlot);
	}
	if (UVerticalBoxSlot* KeySlot = NewVBox->AddChildToVerticalBox(NewKey4))
	{
		CopyVerticalBoxSlotLayout(Cast<UVerticalBoxSlot>(SrcKey4->Slot), KeySlot);
	}

	if (UCanvasPanelSlot* VBoxCanvasSlot = TargetCanvas->AddChildToCanvas(NewVBox))
	{
		if (const UCanvasPanelSlot* SrcCanvasSlot = Cast<UCanvasPanelSlot>(SrcVBox->Slot))
		{
			VBoxCanvasSlot->SetLayout(SrcCanvasSlot->GetLayout());
			VBoxCanvasSlot->SetAutoSize(SrcCanvasSlot->GetAutoSize());
			VBoxCanvasSlot->SetZOrder(SrcCanvasSlot->GetZOrder());
		}
		else
		{
			VBoxCanvasSlot->SetAnchors(FAnchors(1.0f, 1.0f, 1.0f, 1.0f));
			VBoxCanvasSlot->SetAlignment(FVector2D(1.0f, 1.0f));
			VBoxCanvasSlot->SetPosition(FVector2D(0.0f, -35.0f));
			VBoxCanvasSlot->SetAutoSize(true);
		}
	}

	PhotoHudBP->Modify();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(PhotoHudBP);
	FKismetEditorUtilities::CompileBlueprint(PhotoHudBP);
	if (PhotoHudBP->Status == BS_Error)
	{
		UE_LOG(LogTemp, Error, TEXT("EditorSyncPhotoHUDAssets: Failed to compile WBP_PhotoHUD."));
		return false;
	}
	PhotoHudBP->MarkPackageDirty();
	PackagesToSave.Add(PhotoHudBP->GetOutermost());

	// Also allow cycling active cuts via mouse scroll in Change View (PhotoMode) to match the ChangePhoto key prompt.
	UInputMappingContext* PhotoIMC = LoadObject<UInputMappingContext>(
		nullptr, TEXT("/Game/Input/IMC_PhotoMode.IMC_PhotoMode"));
	UInputAction* PrevCutAction = LoadObject<UInputAction>(
		nullptr, TEXT("/Game/Input/Actions/IA_PreviousCut.IA_PreviousCut"));
	UInputAction* NextCutAction = LoadObject<UInputAction>(
		nullptr, TEXT("/Game/Input/Actions/IA_NextCut.IA_NextCut"));
	if (PhotoIMC && PrevCutAction && NextCutAction)
	{
		bool bIMCModified = false;
		bIMCModified |= PhotoHUDEditorSync::EnsureActionMapping(PhotoIMC, PrevCutAction, EKeys::MouseScrollUp);
		bIMCModified |= PhotoHUDEditorSync::EnsureActionMapping(PhotoIMC, NextCutAction, EKeys::MouseScrollDown);
		if (bIMCModified)
		{
			PhotoIMC->MarkPackageDirty();
			PackagesToSave.Add(PhotoIMC->GetOutermost());
		}
	}

	return UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);
}
#endif
