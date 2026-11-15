#include "PhotoHoverPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"

namespace PhotoHoverPromptDefaults
{
	static FText GetDefaultPromptText()
	{
		return NSLOCTEXT("PhotoActor", "DefaultHoverPrompt", "Get Cut Clip");
	}
}

UPhotoHoverPromptWidget::UPhotoHoverPromptWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
	KeyIconTexture = TSoftObjectPtr<UTexture2D>(
		FSoftObjectPath(TEXT("/Game/Art/UI/mouse-input-icons/mouse-left.mouse-left")));
	CurrentPromptText = PhotoHoverPromptDefaults::GetDefaultPromptText();
}

void UPhotoHoverPromptWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (!IsDesignTime())
	{
		HidePrompt();
	}
	else
	{
		ApplyFixedLayoutAndStyling();
		SetVisibility(ESlateVisibility::HitTestInvisible);
		UpdateRevealTransform(1.0f);
	}
}

void UPhotoHoverPromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsShowing || RevealDuration <= KINDA_SMALL_NUMBER || RevealElapsed >= RevealDuration)
	{
		return;
	}

	RevealElapsed = FMath::Min(RevealElapsed + InDeltaTime, RevealDuration);
	const float Alpha = FMath::Clamp(RevealElapsed / RevealDuration, 0.0f, 1.0f);
	const float SmoothAlpha = FMath::InterpEaseOut(0.0f, 1.0f, Alpha, 2.5f);
	UpdateRevealTransform(SmoothAlpha);
}

TSharedRef<SWidget> UPhotoHoverPromptWidget::RebuildWidget()
{
	const bool bHasEmptyDefaultCanvas = WidgetTree
		&& WidgetTree->RootWidget
		&& WidgetTree->RootWidget->IsA<UCanvasPanel>()
		&& CastChecked<UCanvasPanel>(WidgetTree->RootWidget)->GetChildrenCount() == 0;

	if (WidgetTree && (!WidgetTree->RootWidget || bHasEmptyDefaultCanvas))
	{
		BuildDefaultWidgetTree();
	}
	else
	{
		ApplyFixedLayoutAndStyling();
	}

	return Super::RebuildWidget();
}

void UPhotoHoverPromptWidget::ShowPrompt(const FText& InPromptText, const bool bInDimmed)
{
	const FText ResolvedText = InPromptText.IsEmpty()
		? PhotoHoverPromptDefaults::GetDefaultPromptText()
		: InPromptText;

	CurrentPromptText = ResolvedText;
	bDimmed = bInDimmed;
	if (PromptTextBlock)
	{
		PromptTextBlock->SetText(ResolvedText);
	}

	ApplyFixedLayoutAndStyling();

	const bool bWasShowing = bIsShowing && GetVisibility() != ESlateVisibility::Collapsed;
	bIsShowing = true;
	// HitTestInvisible ensures mouse clicks (IA_Select) always pass straight through to gameplay.
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (!bWasShowing)
	{
		RevealElapsed = 0.0f;
		UpdateRevealTransform(RevealDuration > KINDA_SMALL_NUMBER ? 0.0f : 1.0f);
	}
}

void UPhotoHoverPromptWidget::HidePrompt()
{
	bIsShowing = false;
	bDimmed = false;
	RevealElapsed = 0.0f;
	SetVisibility(ESlateVisibility::Collapsed);
}

FText UPhotoHoverPromptWidget::GetPromptText() const
{
	if (PromptTextBlock)
	{
		return PromptTextBlock->GetText();
	}
	return CurrentPromptText.IsEmpty()
		? PhotoHoverPromptDefaults::GetDefaultPromptText()
		: CurrentPromptText;
}

void UPhotoHoverPromptWidget::BuildDefaultWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	UCanvasPanel* CanvasRoot = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!CanvasRoot)
	{
		CanvasRoot = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
		WidgetTree->RootWidget = CanvasRoot;
	}
	RootCanvas = CanvasRoot;

	PopupSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PopupSizeBox"));
	PopupBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PopupBorder"));
	ContentBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ContentBox"));
	KeyIconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("KeyIconImage"));
	PromptTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PromptTextBlock"));

	RootCanvas->AddChildToCanvas(PopupSizeBox);
	PopupSizeBox->SetContent(PopupBorder);
	PopupBorder->SetContent(ContentBox);
	ContentBox->AddChildToHorizontalBox(KeyIconImage);
	ContentBox->AddChildToHorizontalBox(PromptTextBlock);

	ApplyFixedLayoutAndStyling();
}

void UPhotoHoverPromptWidget::ApplyFixedLayoutAndStyling()
{
	if (RootCanvas)
	{
		RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	const FText TextToShow = CurrentPromptText.IsEmpty()
		? PhotoHoverPromptDefaults::GetDefaultPromptText()
		: CurrentPromptText;

	if (KeyIconImage && !LoadedKeyIconTexture && !KeyIconTexture.IsNull())
	{
		LoadedKeyIconTexture = KeyIconTexture.LoadSynchronous();
	}

	const FSlateFontInfo PromptFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), PromptFontSize);
	float MeasuredTextWidth = 0.0f;
	if (FSlateApplication::IsInitialized())
	{
		if (const FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer())
		{
			MeasuredTextWidth = static_cast<float>(Renderer->GetFontMeasureService()->Measure(TextToShow, PromptFont).X);
		}
	}
	if (MeasuredTextWidth <= 0.0f)
	{
		MeasuredTextWidth = static_cast<float>(TextToShow.ToString().Len()) * (PromptFontSize * 0.62f);
	}

	const bool bShowKeyIcon = (KeyIconImage != nullptr) && (LoadedKeyIconTexture != nullptr);
	const float HorizontalChrome = 28.0f + (bShowKeyIcon ? (KeyIconSize.X + 8.0f) : 0.0f) + 16.0f;
	const float TargetWidth = FMath::Max(FixedPopupSize.X, FMath::CeilToFloat(MeasuredTextWidth + HorizontalChrome));
	ResolvedPopupSize = FVector2D(TargetWidth, FixedPopupSize.Y);

	if (PopupSizeBox)
	{
		PopupSizeBox->SetWidthOverride(ResolvedPopupSize.X);
		PopupSizeBox->SetHeightOverride(ResolvedPopupSize.Y);
		PopupSizeBox->SetRenderTransformPivot(FVector2D(0.5f, 0.0f));

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PopupSizeBox->Slot))
		{
			CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CanvasSlot->SetAlignment(FVector2D(0.5f, 0.0f));
			CanvasSlot->SetAutoSize(false);
			CanvasSlot->SetSize(ResolvedPopupSize);
			CanvasSlot->SetPosition(ScreenOffset);
			CanvasSlot->SetZOrder(10);
		}
	}

	if (PopupBorder)
	{
		FSlateBrush CardBrush;
		CardBrush.DrawAs = ESlateBrushDrawType::RoundedBox;
		CardBrush.TintColor = FSlateColor(GetActiveBackgroundColor());
		CardBrush.OutlineSettings = FSlateBrushOutlineSettings(CornerRadius, FSlateColor(GetActiveOutlineColor()), 1.5f);
		PopupBorder->SetBrush(CardBrush);
		PopupBorder->SetPadding(FMargin(14.0f, 8.0f));
		PopupBorder->SetHorizontalAlignment(HAlign_Center);
		PopupBorder->SetVerticalAlignment(VAlign_Center);
	}

	if (KeyIconImage)
	{
		if (LoadedKeyIconTexture)
		{
			KeyIconImage->SetBrushFromTexture(LoadedKeyIconTexture, false);
			KeyIconImage->SetDesiredSizeOverride(KeyIconSize);
			KeyIconImage->SetColorAndOpacity(GetActiveKeyIconColor());
			KeyIconImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			KeyIconImage->SetVisibility(ESlateVisibility::Collapsed);
		}

		if (UHorizontalBoxSlot* IconSlot = Cast<UHorizontalBoxSlot>(KeyIconImage->Slot))
		{
			IconSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
			IconSlot->SetHorizontalAlignment(HAlign_Center);
			IconSlot->SetVerticalAlignment(VAlign_Center);
			IconSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
	}

	if (PromptTextBlock)
	{
		PromptTextBlock->SetText(TextToShow);
		PromptTextBlock->SetFont(PromptFont);
		PromptTextBlock->SetColorAndOpacity(FSlateColor(GetActiveTextColor()));
		PromptTextBlock->SetShadowOffset(FVector2D(1.0f, 1.0f));
		PromptTextBlock->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));

		if (UHorizontalBoxSlot* TextSlot = Cast<UHorizontalBoxSlot>(PromptTextBlock->Slot))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
	}
}

void UPhotoHoverPromptWidget::UpdateRevealTransform(float Alpha)
{
	if (!PopupSizeBox)
	{
		return;
	}

	const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	const float Scale = FMath::Lerp(0.94f, 1.0f, ClampedAlpha);
	PopupSizeBox->SetRenderOpacity(ClampedAlpha);
	PopupSizeBox->SetRenderScale(FVector2D(Scale, Scale));
}
