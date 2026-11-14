#include "SBangoGraphNode.h"

#include "BangoEdGraphNode.h"
#include "BangoEditorStyle.h"
#include "BangoGraphEdit.h"
#include "BangoGraph.h"
#include "BangoGraphLayout.h"
#include "BangoText.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "HAL/PlatformTime.h"
#include "GraphEditorSettings.h"
#include "SBangoGraphPin.h"
#include "SBangoRerouteNode.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SMenuAnchor.h"
#include "BangoCommand.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SBangoGraphNode"

namespace BangoGraphNodeStyle
{
	/**
	 * Value boxes stay on one line between these widths; longer values end in "..." (hover for the whole value).
	 * Wrapping made nodes change shape while typing.
	 */
	static constexpr float MinValueWidth = 40.0f;
	static constexpr float MaxValueWidth = 280.0f;

	/** Size of the type icon at the start of a row. */
	static constexpr float IconSize = 12.0f;

	/** Space between a pin column and the fields (the pin columns already reach half a circle into the body). */
	static constexpr float FieldsSideGap = 4.0f;
	/** Space above and below each pin: with the 10-pixel circle, pins are one grid square (16 pixels) apart. */
	static constexpr float PinRowGap = 3.0f;

	/**
	 * Extra space above the first pin, to put every pin's centre on a grid line (a whole number of grid squares
	 * below the node's top edge). Nodes snap to the grid, and so does a reroute's dot, so then a wire between a
	 * pin and a reroute at the same height runs straight. Tweak by eye: if the pins sit N pixels above the grid
	 * line, add N; if below, take N away (or add 16 - N). Changing it changes every node's height a little.
	 */
	static constexpr float PinGridOffset = 13.0f;

	/** A switch's case row: as tall as a plain pin's row (the circle and PinRowGap above and below), so pins stay on the grid. */
	static constexpr float CaseRowHeight = 10.0f + PinRowGap * 2.0f;
	/** Space above a node's fields (inside the fields area's own top gap of 2). */
	static constexpr float FieldsTopGap = 9.0f;
	/** The grid's gap above each value box. */
	static constexpr float ValueRowGap = 1.0f;

	static const FLinearColor LabelColor(0.75f, 0.75f, 0.75f);
	static const FLinearColor EmptyColor(0.45f, 0.45f, 0.45f);
	static const FLinearColor ErrorColor(0.9f, 0.15f, 0.15f);
	static const FLinearColor WarningColor(0.95f, 0.75f, 0.1f);
}

void SBangoGraphNode::Construct(const FArguments& InArgs, UBangoEdGraphNode* InNode)
{
	GraphNode = InNode;
	BangoNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	UpdateGraphNode();
}

TSharedRef<SWidget> SBangoGraphNode::CreateNodeContentArea()
{
	using namespace BangoGraphNodeStyle;
	// The pin columns hang half a circle outside the body, so each circle's centre is on the body's edge: their
	// slots have negative padding of that much (less any space the editor settings leave between the body and the
	// node's edge). The pin columns are never narrower than what's left inside the body, so the fields start
	// at the same place on nodes with no input pin (Script Start).
	const FMargin BodyInset = GetDefault<UGraphEditorSettings>()->GetNonPinNodeBodyPadding();
	const float HalfPin = SBangoGraphPin::PinSize * 0.5f;
	// A switch's case boxes are made with their pins (AddPin), which comes after this.
	CaseEditBoxes.Reset();
	CaseEditBoxes.SetNum(BangoNode && BangoNode->IsSwitch() ? BangoNode->CaseValues.Num() : 0);
	EditingCase = INDEX_NONE;
	// A disabled node: SGraphNode fades the title and the body and adds the Disabled banner; this fades the fields
	// and the pins to match.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetNoBrush())
		.Padding(FMargin(0.0f, 3.0f))
		.ColorAndOpacity_Lambda([this]()
		{
			return BangoNode && BangoNode->IsDisabledNode() ? FLinearColor(1.0f, 1.0f, 1.0f, 0.45f) : FLinearColor::White;
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.MinDesiredWidth(BodyInset.Left + HalfPin)
				[
					SAssignNew(LeftNodeBox, SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox).HeightOverride(PinGridOffset)
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(FieldsSideGap, 2.0f, FieldsSideGap, 6.0f)
			[
				MakeFieldsArea()
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.MinDesiredWidth(BodyInset.Right + HalfPin)
				[
					SAssignNew(RightNodeBox, SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox).HeightOverride(PinGridOffset)
					]
				]
			]
		];
}

/**
 * A sequence's or switch's Add pin button, drawn like a pin: a dark grey ring (an unwired pin's circle) on the node's
 * edge, with a "+" just inside the node to its left. Clicking either adds an output.
 */
class SBangoAddPinButton : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoAddPinButton) {}
		SLATE_EVENT(FSimpleDelegate, OnClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnClicked = InArgs._OnClicked;
		SetCursor(EMouseCursor::Hand);
		// The "+" and the circle are both part of the button. It's a whole pin row tall (the circle and the gap above
		// and below it), so it stays on the grid and is easy to hit.
		ChildSlot
		[
			SNew(SBox)
			.HeightOverride(SBangoGraphPin::PinSize + BangoGraphNodeStyle::PinRowGap * 2.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::AsCultureInvariant(TEXT("+")))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.ColorAndOpacity_Lambda([this]() { return GetColor(); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(SBangoGraphPin::PinSize)
					.HeightOverride(SBangoGraphPin::PinSize)
					[
						SNew(SImage)
						.Image(FBangoEditorStyle::GetPinBrush(/*bConnected*/ false)) // a ring, like an unwired pin
						.ColorAndOpacity_Lambda([this]() { return GetColor(); })
					]
				]
			]
		];
	}

	FSlateColor GetColor() const
	{
		return IsHovered() ? FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)) : FSlateColor(FLinearColor(0.05f, 0.05f, 0.05f));
	}

	virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		return FReply::Handled().CaptureMouse(AsShared()); // so a click doesn't start dragging the node
	}

	virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !HasMouseCapture())
		{
			return FReply::Unhandled();
		}
		if (Geometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
		{
			OnClicked.ExecuteIfBound();
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

private:
	FSimpleDelegate OnClicked;
};

void SBangoGraphNode::CreateOutputSideAddButton(TSharedPtr<SVerticalBox> OutputBox)
{
	// Nothing here: SGraphNode would put it under all the outputs, but it goes above Then (see AddPin).
}

void SBangoGraphNode::AddAddPinButton(const TSharedPtr<SVerticalBox>& OutputBox)
{
	using namespace BangoGraphNodeStyle;
	if (!BangoNode || !(BangoNode->IsSequence() || BangoNode->IsSwitch()) || !OutputBox.IsValid())
	{
		return;
	}
	AddOutputGap();
	// Placed like an output pin, in a row of its own: its circle's centre on the node's right edge.
	const FMargin BodyInset = GetDefault<UGraphEditorSettings>()->GetNonPinNodeBodyPadding();
	const float HalfPin = SBangoGraphPin::PinSize * 0.5f;
	const FText Tip = BangoNode->IsSwitch()
		? LOCTEXT("AddSwitchPinTooltip", "Add pin: adds a case after the others, before Default. Type its value (or values, separated by commas) in its box. (To remove one, right-click its pin.)")
		: LOCTEXT("AddSequencePinTooltip", "Add pin: adds a scope after the others: another block, run after them and before Then. (To remove one, right-click it.)");
	OutputBox->AddSlot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(0.0f, 0.0f, BodyInset.Right - HalfPin, 0.0f) // the button is a whole pin row tall
		[
			SNew(SBangoAddPinButton)
			.ToolTipText(Tip)
			.OnClicked_Lambda([this]() { OnAddPin(); })
		];
}

FReply SBangoGraphNode::OnAddPin()
{
	if (BangoNode && BangoNode->IsSequence())
	{
		BangoNode->AddSequenceOutput();
	}
	else if (BangoNode && BangoNode->IsSwitch())
	{
		// The new case's box opens for typing once the node has rebuilt.
		BangoNode->AddSwitchCase();
		PendingCaseEdit = BangoNode->CaseValues.Num() - 1;
		RefreshSoon();
	}
	return FReply::Handled();
}

void SBangoGraphNode::AddOutputGap()
{
	if (BangoNode && (BangoNode->IsSequence() || BangoNode->IsSwitch()) && RightNodeBox.IsValid() && OutputPins.Num() > 0)
	{
		RightNodeBox->AddSlot()
			.AutoHeight()
			[
				SNew(SBox).HeightOverride((float)BangoGraphGridSize)
			];
	}
}

void SBangoGraphNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	using namespace BangoGraphNodeStyle;
	PinToAdd->SetOwner(SharedThis(this));
	const FMargin BodyInset = GetDefault<UGraphEditorSettings>()->GetNonPinNodeBodyPadding();
	const float HalfPin = SBangoGraphPin::PinSize * 0.5f;
	if (PinToAdd->GetDirection() == EGPD_Output && PinToAdd->GetPinObj() && PinToAdd->GetPinObj()->PinName == UBangoEdGraphNode::NextPinName)
	{
		AddAddPinButton(RightNodeBox); // a sequence's or switch's Add pin goes after its other outputs, above Then
	}
	if (PinToAdd->GetDirection() == EGPD_Output)
	{
		AddOutputGap();
	}
	if (PinToAdd->GetDirection() == EGPD_Input)
	{
		LeftNodeBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.Padding(BodyInset.Left - HalfPin, PinRowGap, 0.0f, PinRowGap)
			[
				PinToAdd
			];
		InputPins.Add(PinToAdd);
	}
	else if (const int32 Case = BangoNode ? BangoNode->GetCaseOfPin(PinToAdd->GetPinObj()) : INDEX_NONE; Case != INDEX_NONE && CaseEditBoxes.IsValidIndex(Case))
	{
		// A switch's case: its values in a box just inside the node, then the pin. The row is exactly as tall as a
		// plain pin's (PinRowGap above and below the circle), so the pins stay on the grid.
		RightNodeBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, 0.0f, BodyInset.Right - HalfPin, 0.0f)
			[
				SNew(SBox)
				.HeightOverride(CaseRowHeight)
				[
					SNew(SHorizontalBox)
					// "Case" before the first case's box.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, Case == 0 ? 6.0f : 0.0f, 0.0f)
					[
						Case == 0
							? StaticCastSharedRef<SWidget>(SNew(STextBlock)
								.Text(LOCTEXT("CaseLabel", "Case"))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.ColorAndOpacity(FSlateColor(LabelColor)))
							: SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Fill) // the box fills its row (16 high)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						MakeCaseValueBox(Case)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						PinToAdd
					]
				]
			];
		OutputPins.Add(PinToAdd);
	}
	else
	{
		RightNodeBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, PinRowGap, BodyInset.Right - HalfPin, PinRowGap)
			[
				PinToAdd
			];
		OutputPins.Add(PinToAdd);
	}
}

TSharedRef<SWidget> SBangoGraphNode::MakeCaseValueBox(int32 Case)
{
	using namespace BangoGraphNodeStyle;
	const FString Values = BangoNode->CaseValues.IsValidIndex(Case) ? BangoNode->CaseValues[Case] : FString();
	const bool bEmpty = Values.IsEmpty();
	const bool bHasError = BangoNode->ErrorCase == Case;
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Mono", 8);

	TSharedRef<SEditableText> Edit = SNew(SEditableText)
		.Text(FText::FromString(Values))
		.Font(Font)
		.SelectAllTextWhenFocused(true)
		.RevertTextOnEscape(true)
		.OnTextCommitted(this, &SBangoGraphNode::OnCaseCommitted, Case)
		.OnKeyDownHandler(this, &SBangoGraphNode::OnCaseKeyDown, Case);
	CaseEditBoxes[Case] = Edit;

	const FText Tip = bHasError ? FText::FromString(BangoNode->ErrorMsg)
		: LOCTEXT("CaseValuesTip", "This case's values: the switch runs this output when its value equals one of them.\nAn int like 3, a string like \"north\" or a Bango ID like @guard; separate several with commas, like 1, 2.\nRight-click the pin to remove the case.");
	TSharedRef<SBorder> Box = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(bHasError ? "WhiteBrush" : "NoBorder"))
		.BorderBackgroundColor(FSlateColor(ErrorColor))
		.Padding(bHasError ? FMargin(1.0f) : FMargin(0.0f))
		.ToolTipText(Tip)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(4.0f, 0.0f, 3.0f, 0.0f))
			[
				SNew(SBox)
				.MinDesiredWidth(28.0f)
				.MaxDesiredWidth(MaxValueWidth * 0.6f)
				.VAlign(VAlign_Center)
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([this, Case]() { return EditingCase == Case ? 1 : 0; })
					+ SWidgetSwitcher::Slot()
					[
						SNew(STextBlock)
						.Text(bEmpty ? LOCTEXT("CaseValueMissing", "value") : FText::FromString(Values))
						.Font(bEmpty ? FCoreStyle::GetDefaultFontStyle("Italic", 8) : Font)
						.ColorAndOpacity(bEmpty ? FSlateColor(EmptyColor) : FSlateColor::UseForeground())
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
					+ SWidgetSwitcher::Slot()
					[
						Edit
					]
				]
			]
		];
	Box->SetCursor(EMouseCursor::TextEditBeam);
	Box->SetOnMouseButtonDown(FPointerEventHandler::CreateLambda([this, Case](const FGeometry&, const FPointerEvent& MouseEvent)
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		BeginEditingCase(Case);
		return FReply::Handled();
	}));
	return Box;
}

void SBangoGraphNode::BeginEditingCase(int32 Case)
{
	if (!CaseEditBoxes.IsValidIndex(Case) || !CaseEditBoxes[Case].IsValid())
	{
		return;
	}
	EditingCase = Case;
	TWeakPtr<SEditableText> WeakBox = CaseEditBoxes[Case];
	// As with fields: the box shows on the next frame, so it gets keyboard focus then.
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakBox](double, float)
	{
		if (TSharedPtr<SEditableText> Box = WeakBox.Pin())
		{
			FSlateApplication::Get().SetKeyboardFocus(Box, EFocusCause::SetDirectly);
		}
		return EActiveTimerReturnType::Stop;
	}));
}

void SBangoGraphNode::OnCaseCommitted(const FText& NewText, ETextCommit::Type CommitType, int32 Case)
{
	if (EditingCase != Case)
	{
		return; // not being edited
	}
	EditingCase = INDEX_NONE;
	if (CommitType == ETextCommit::OnCleared || !BangoNode)
	{
		PendingCaseEdit = INDEX_NONE;
		return; // Escape: keep the old values
	}
	FString Error;
	if (!BangoNode->SetCaseValues(Case, NewText.ToString(), Error))
	{
		PendingCaseEdit = INDEX_NONE;
		FNotificationInfo Info(FText::FromString(Error));
		Info.ExpireDuration = 6.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
	RefreshSoon();
}

FReply SBangoGraphNode::OnCaseKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent, int32 Case)
{
	const FKey Key = KeyEvent.GetKey();
	if ((Key != EKeys::Enter && Key != EKeys::Tab) || KeyEvent.IsControlDown() || KeyEvent.IsAltDown() || KeyEvent.IsCommandDown()
		|| EditingCase != Case || !CaseEditBoxes.IsValidIndex(Case) || !CaseEditBoxes[Case].IsValid())
	{
		return FReply::Unhandled();
	}
	const int32 Num = CaseEditBoxes.Num();
	int32 Next = Case + (KeyEvent.IsShiftDown() ? -1 : 1);
	if (Next < 0 || Next >= Num)
	{
		Next = Key == EKeys::Tab ? ((Next % Num) + Num) % Num : INDEX_NONE;
	}
	PendingCaseEdit = Next == Case ? INDEX_NONE : Next;
	OnCaseCommitted(CaseEditBoxes[Case]->GetText(), ETextCommit::OnEnter, Case); // and the node rebuilds on the next frame
	return FReply::Handled();
}

TSharedRef<SWidget> SBangoGraphNode::MakeFieldsArea()
{
	EditBoxes.Reset();
	EditingField = INDEX_NONE;
	if (!BangoNode || BangoNode->Layout.Fields.Num() == 0)
	{
		return SNew(SSpacer).Size(29);
	}
	EditBoxes.SetNum(BangoNode->Layout.Fields.Num());
	CompletionAnchors.Reset();
	CompletionAnchors.SetNum(BangoNode->Layout.Fields.Num());
	CloseCompletion();

	// Like the details panel, with each row's type icon first: icons, then names, then values filling the rest.
	// The icon column is left out when no row has an icon (like a For node's Init, Condition and Step).
	bool bAnyIcon = false;
	for (const FBangoNodeField& Field : BangoNode->Layout.Fields)
	{
		FLinearColor Tint;
		bAnyIcon |= FBangoEditorStyle::GetFieldIcon(Field, Tint) != nullptr;
	}
	const int32 LabelColumn = bAnyIcon ? 1 : 0;
	const int32 ValueColumn = LabelColumn + 1;

	TSharedRef<SGridPanel> Grid = SNew(SGridPanel).FillColumn(ValueColumn, 1.0f);
	// Rows and fields differ once a text field is expanded (its Key / Namespace / Notes rows follow it).
	int32 Row = 0;
	// The widest heading: the grid is kept at least this wide, since headings don't ask the grid for width.
	float HeadingWidth = 0.0f;
	for (int32 FieldIndex = 0; FieldIndex < BangoNode->Layout.Fields.Num(); ++FieldIndex, ++Row)
	{
		const FBangoNodeField& Field = BangoNode->Layout.Fields[FieldIndex];
		if (!Field.SectionHeader.IsEmpty())
		{
			// A heading row across all the columns, with a little space above it unless it's the first row.
			// The grid shares a spanning cell's width out among the columns it spans, which would widen the icon
			// and name columns; so the heading asks for no width here, and the grid's box below makes room for it.
			const FSlateFontInfo HeadingFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
			if (FSlateApplication::IsInitialized())
			{
				const auto HeadingSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Field.SectionHeader, HeadingFont);
				HeadingWidth = FMath::Max(HeadingWidth, (float)HeadingSize.X);
			}
			Grid->AddSlot(0, Row)
				.ColumnSpan(ValueColumn + 1)
				.VAlign(VAlign_Fill)
				.Padding(0.0f, Row > 0 ? 12.0f : 0.0f, 0.0f, 2.0f)
				[
					SNew(SBox)
					.MaxDesiredWidth(0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Field.SectionHeader))
						.Font(HeadingFont)
					]
				];
			++Row;
		}
		if (bAnyIcon)
		{
			Grid->AddSlot(0, Row)
				.VAlign(VAlign_Fill)
				.Padding(0.0f, 1.0f, 5.0f, 1.0f)
				[
					MakeFieldIcon(Field)
				];
		}
		if (Field.bHideLabel)
		{
			// No label: the value takes the label's column too.
			Grid->AddSlot(LabelColumn, Row)
				.ColumnSpan(ValueColumn - LabelColumn + 1)
				.VAlign(VAlign_Fill)
				.Padding(0.0f, 1.0f)
				[
					MakeFieldValue(FieldIndex)
				];
		}
		else
		{
			Grid->AddSlot(LabelColumn, Row)
				.VAlign(VAlign_Fill)
				.Padding(0.0f, 1.0f, 8.0f, 1.0f)
				[
					MakeFieldLabel(Field)
				];
			Grid->AddSlot(ValueColumn, Row)
				.VAlign(VAlign_Fill)
				.Padding(0.0f, 1.0f)
				[
					MakeFieldValue(FieldIndex)
				];
		}
		if (BangoNode->ExpandedTextFields.Contains(FieldIndex))
		{
			Row = AddTextPartRows(Grid, Row + 1, LabelColumn, ValueColumn, FieldIndex) - 1;
		}
	}

	// A switch's Var row starts where its first case row does (that row is PinGridOffset below the top, like the
	// first pin), so the Var box and the first case box line up.
	using namespace BangoGraphNodeStyle;
	const float TopGap = BangoNode->IsSwitch() ? PinGridOffset - 2.0f - ValueRowGap : FieldsTopGap;
	return SNew(SBox)
		.MinDesiredWidth(HeadingWidth)
		.Padding(2.0f, TopGap, 2.0f, 2.0f)
		[
			Grid
		];
}

TSharedRef<SWidget> SBangoGraphNode::MakeFieldLabel(const FBangoNodeField& Field) const
{
	// The grid's slots fill their row; the name sits in the middle of it.
	return SNew(SBox)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Field.Label))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(BangoGraphNodeStyle::LabelColor))
		];
}

TSharedRef<SWidget> SBangoGraphNode::MakeFieldIcon(const FBangoNodeField& Field) const
{
	using namespace BangoGraphNodeStyle;
	FLinearColor IconTint;
	const FSlateBrush* TypeIcon = FBangoEditorStyle::GetFieldIcon(Field, IconTint);
	// A variable field's tooltip says so, with its type if known: "variable (int)".
	const FString IconTip = Field.bVariable
		? (Field.VariableType.IsEmpty() ? FString(TEXT("variable")) : FString::Printf(TEXT("variable (%s)"), *Field.VariableType))
		: Field.TypeHint;
	// Always the same width, so the names line up when some rows have no icon.
	// The grid's slot fills its cell, which can be wider than the icon (a heading spanning the columns can widen the
	// icon column) as well as taller, so the icon keeps exactly its own size here: at the left, in line with the
	// headings, and centred top to bottom.
	return SNew(SBox)
		.WidthOverride(IconSize)
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.ToolTipText(FText::FromString(IconTip))
		[
			SNew(SBox)
			.WidthOverride(IconSize)
			.HeightOverride(IconSize)
			[
				TypeIcon
					? StaticCastSharedRef<SWidget>(SNew(SImage).Image(TypeIcon).ColorAndOpacity(FSlateColor(IconTint)))
					: SNullWidget::NullWidget
			]
		];
}

/**
 * The height of one line of a value, in whichever of the value fonts is tallest. Code shows in the fixed-width font
 * and text in the regular one, and their lines aren't the same height, so without this a row's height (and the gap
 * above it) depends on what's in it.
 */
static float GetValueLineHeight()
{
	if (!FSlateApplication::IsInitialized())
	{
		return 0.0f;
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	return (float)FMath::Max3(
		(int32)Measure->GetMaxCharacterHeight(FCoreStyle::GetDefaultFontStyle("Regular", 9)),
		(int32)Measure->GetMaxCharacterHeight(FCoreStyle::GetDefaultFontStyle("Italic", 9)),
		(int32)Measure->GetMaxCharacterHeight(FCoreStyle::GetDefaultFontStyle("Mono", 9)));
}

TSharedRef<SWidget> SBangoGraphNode::MakeFieldValue(int32 FieldIndex)
{
	using namespace BangoGraphNodeStyle;
	const FBangoNodeField& Field = BangoNode->Layout.Fields[FieldIndex];

	if (!Field.bHasValue)
	{
		// A row that only shows information (like a function parameter): its icon and name say it all.
		return SNullWidget::NullWidget;
	}

	const bool bEditable = Bango::CanEditField(BangoNode->Layout, FieldIndex);
	// A string field shows (and is typed as) plain text, without quotes or escapes; see Bango::IsTextField.
	const bool bTextField = Bango::IsTextField(Field);
	const FString DisplayString = Bango::GetFieldDisplayText(Field);
	FString LiteralValue;
	FBangoTextLiteral TextLiteral;
	const bool bLocalized = Bango::IsLocalizedTextField(Field);
	const bool bLiteralText = bTextField && Field.bPresent && (bLocalized
		? Bango::ParseTextLiteral(Field.Text, TextLiteral)
		: Bango::TryGetStringLiteral(Field.Text, LiteralValue));
	if (bLocalized && bLiteralText)
	{
		LiteralValue = TextLiteral.Source;
	}
	const bool bEmptyText = bLiteralText && LiteralValue.IsEmpty();
	const bool bEmpty = !Field.bPresent || bEmptyText;
	// One line: line breaks and tabs in a text show as \n and \t (as they're typed).
	const FString OneLine = DisplayString.Replace(TEXT("\n"), TEXT("\\n"), ESearchCase::CaseSensitive).Replace(TEXT("\t"), TEXT("\\t"), ESearchCase::CaseSensitive);
	FText ValueText = FText::FromString(OneLine);
	if (bEmptyText)
	{
		ValueText = LOCTEXT("EmptyText", "empty text");
	}
	else if (!Field.bPresent)
	{
		ValueText = Field.bLocked ? LOCTEXT("Locked", "fill in the values above first")
			: Field.bReadOnly ? LOCTEXT("Required", "required") // a script parameter without a default
			: Field.bOptional ? LOCTEXT("NotSet", "not set") : LOCTEXT("Missing", "missing");
	}
	// Text reads like text; code (and an expression in a text field) in a fixed-width font.
	const FSlateFontInfo ValueFont = bLiteralText || (bTextField && !Field.bPresent)
		? FCoreStyle::GetDefaultFontStyle("Regular", 9)
		: FCoreStyle::GetDefaultFontStyle("Mono", 9);

	// What's shown when not editing: the value on one line, cut off with "..." if it's too long.
	TSharedRef<SWidget> Display = SNew(STextBlock)
		.Text(ValueText)
		.Font(bEmpty ? FCoreStyle::GetDefaultFontStyle("Italic", 9) : ValueFont)
		.ColorAndOpacity(bEmpty ? FSlateColor(EmptyColor) : FSlateColor::UseForeground())
		.OverflowPolicy(ETextOverflowPolicy::Ellipsis);

	// A field with a compile error gets a red outline, with the error as its tooltip; a field with a warning
	// gets a yellow one. The error wins if a field has both.
	const bool bHasError = BangoNode->ErrorField == FieldIndex;
	const Bango::FBangoFieldWarning* Warning = bHasError ? nullptr : BangoNode->FindFieldWarning(FieldIndex);
	const bool bOutlined = bHasError || Warning;
	const FText TextFieldTip = bLocalized
		? LOCTEXT("LocTextFieldTip", "Localized text: type it without quotes; quotes inside it are fine. \\n is a line break.\nThe arrow shows its Key, Namespace and Notes for translators.\nStart with = to use a variable or expression instead, like =ToText(_name).")
		: LOCTEXT("TextFieldTip", "Type the text without quotes; quotes inside it are fine. \\n is a line break.\nStart with = to use a variable or expression instead, like =_name.");
	// Otherwise the tooltip shows the parameter's description if it has one (like Sleep's "Seconds"), or else the
	// whole value (which may be cut off); text fields add how to type a text.
	FString ValueTip = !Field.Description.IsEmpty() ? Field.Description
		: Field.bPresent && !bEmptyText ? DisplayString : FString();
	if (bTextField && bEditable)
	{
		ValueTip += (ValueTip.IsEmpty() ? FString() : FString(TEXT("\n\n"))) + TextFieldTip.ToString();
	}
	if (Field.bReadOnly)
	{
		ValueTip += (ValueTip.IsEmpty() ? FString() : FString(TEXT("\n\n"))) + LOCTEXT("ReadOnlyDefaultTip", "Edit parameters and their defaults in the SCRIPT INPUT panel.").ToString();
	}
	const FText OutlineTip = bHasError ? FText::FromString(BangoNode->ErrorMsg)
		: Warning ? FText::FromString(Warning->Message)
		: FText::FromString(ValueTip);
	TSharedRef<SBorder> DisplayBox = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(bOutlined ? "WhiteBrush" : "NoBorder"))
		.BorderBackgroundColor(FSlateColor(bHasError ? ErrorColor : WarningColor))
		.Padding(bOutlined ? FMargin(1.0f) : FMargin(0.0f))
		.ToolTipText(OutlineTip)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(4.0f, 1.0f, 3.0f, 1.0f))
			[
				// Every value line is the same height, whatever its font.
				SNew(SBox)
				.MinDesiredHeight(GetValueLineHeight())
				.VAlign(VAlign_Center)
				[
					Display
				]
			]
		];

	// A switch's Var box is as tall as its case boxes, beside it.
	const TSharedRef<SWidget> ShownBox = BangoNode->IsSwitch()
		? StaticCastSharedRef<SWidget>(SNew(SBox).HeightOverride(CaseRowHeight)[ DisplayBox ])
		: StaticCastSharedRef<SWidget>(DisplayBox);
	TSharedRef<SWidget> Content = ShownBox;
	if (bEditable)
	{
		DisplayBox->SetOnMouseButtonDown(FPointerEventHandler::CreateSP(this, &SBangoGraphNode::OnValueClicked, FieldIndex));
		DisplayBox->SetCursor(EMouseCursor::TextEditBeam);

		TSharedRef<SEditableTextBox> EditBox = SNew(SEditableTextBox)
			.Text(FText::FromString(Bango::GetFieldEditText(Field)))
			.Font(ValueFont)
			.SelectAllTextWhenFocused(true)
			.RevertTextOnEscape(true)
			.OnTextCommitted(this, &SBangoGraphNode::OnValueCommitted, FieldIndex)
			// Autocomplete: typing may open the list; moving the cursor off the word closes it.
			.OnTextChanged_Lambda([this, FieldIndex](const FText&)
			{
				if (!bCompletionAccepting)
				{
					QueueCompletionUpdate(FieldIndex, true);
				}
			})
			.OnCursorMovedWithSelection_Lambda([this, FieldIndex](const FTextLocation& Location, const FTextSelection&)
			{
				CompletionCursor = Location.GetOffset();
				if (IsCompletionOpen() && !bCompletionAccepting)
				{
					QueueCompletionUpdate(FieldIndex, false);
				}
			})
			.OnKeyDownHandler(this, &SBangoGraphNode::OnEditBoxKeyDown, FieldIndex);
		EditBoxes[FieldIndex] = EditBox;

		TSharedRef<SMenuAnchor> Anchor = SNew(SMenuAnchor)
			.Placement(MenuPlacement_BelowAnchor)
			.Method(EPopupMethod::UseCurrentWindow) // drawn in this window, so clicking an entry doesn't move focus off the box
			[
				EditBox
			];
		CompletionAnchors[FieldIndex] = Anchor;

		Content = SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this, FieldIndex]() { return EditingField == FieldIndex ? 1 : 0; })
			+ SWidgetSwitcher::Slot()
			[
				ShownBox
			]
			+ SWidgetSwitcher::Slot()
			[
				Anchor
			];
	}

	// A localized text literal gets an arrow for its Key / Namespace / Notes rows.
	if (bLocalized && bLiteralText && bEditable)
	{
		const bool bExpanded = BangoNode->ExpandedTextFields.Contains(FieldIndex);
		Content = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				Content
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(1.0f, 0.0f))
				.ToolTipText(bExpanded ? LOCTEXT("HideTextParts", "Hide the key, namespace and notes")
					: LOCTEXT("ShowTextParts", "Show the key, namespace and notes"))
				.OnClicked(this, &SBangoGraphNode::OnToggleTextExpanded, FieldIndex)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(bExpanded ? "Icons.ChevronDown" : "Icons.ChevronRight"))
					.ColorAndOpacity(FSlateColor::UseForeground())
					.DesiredSizeOverride(FVector2D(IconSize, IconSize))
				]
			];
	}

	return SNew(SBox)
		.MinDesiredWidth(MinValueWidth)
		.MaxDesiredWidth(MaxValueWidth + (bLocalized ? IconSize + 4.0f : 0.0f))
		[
			Content
		];
}

int32 SBangoGraphNode::AddTextPartRows(const TSharedRef<SGridPanel>& Grid, int32 Row, int32 LabelColumn, int32 ValueColumn, int32 FieldIndex)
{
	using namespace BangoGraphNodeStyle;
	FBangoTextLiteral Literal;
	if (!BangoNode->Layout.Fields.IsValidIndex(FieldIndex) || !Bango::ParseTextLiteral(BangoNode->Layout.Fields[FieldIndex].Text, Literal))
	{
		return Row; // no longer a literal (like after typing =expression); nothing to show
	}

	// Translated: a LOCTEXT (and the like) is gathered for translators; an INVTEXT is shown as it is in every language.
	{
		const FText TranslatedTip = LOCTEXT("TranslatedTip", "Checked: translators translate this text (LOCTEXT), and it has a key, a namespace and notes.\nUnchecked: it's shown as it is in every language (INVTEXT), like a name or a debug message.");
		Grid->AddSlot(LabelColumn, Row)
			.VAlign(VAlign_Fill)
			.Padding(10.0f, 1.0f, 8.0f, 1.0f)
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TranslatedLabel", "Translated"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(LabelColor))
					.ToolTipText(TranslatedTip)
				]
			];
		Grid->AddSlot(ValueColumn, Row)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 1.0f)
			[
				SNew(SCheckBox)
				.IsChecked(Literal.bInvariant ? ECheckBoxState::Unchecked : ECheckBoxState::Checked)
				.ToolTipText(TranslatedTip)
				.OnCheckStateChanged(this, &SBangoGraphNode::OnTextTranslatedChanged, FieldIndex)
			];
		++Row;
	}
	if (Literal.bInvariant)
	{
		return Row; // no key, namespace or notes
	}

	struct FPartRow
	{
		ETextPart Part;
		FText Label;
		FString Value;
		FText Hint;
		FText Tip;
	};
	const FPartRow Parts[] =
	{
		{ ETextPart::Key, LOCTEXT("KeyLabel", "Key"), Literal.Key, FText(),
			LOCTEXT("KeyTip", "Identifies this text for translators. Made up for you when the text is first typed; change it to something readable if you like.\nEach key can be used for only one text in its namespace.") },
		{ ETextPart::Namespace, LOCTEXT("NamespaceLabel", "Namespace"), Literal.bHasNamespace ? Literal.Namespace : FString(), LOCTEXT("NamespaceHint", "script's namespace"),
			LOCTEXT("NamespaceTip", "Leave empty to use the script's namespace (Localization on the Script Start node). Fill in to use another one (NSLOCTEXT).") },
		{ ETextPart::Notes, LOCTEXT("NotesLabel", "Notes"), Literal.Notes, LOCTEXT("NotesHint", "none"),
			LOCTEXT("NotesTip", "Shown to translators with this text, like who's speaking or where it appears (NOTELOCTEXT).") },
	};
	for (const FPartRow& Part : Parts)
	{
		Grid->AddSlot(LabelColumn, Row)
			.VAlign(VAlign_Fill)
			.Padding(10.0f, 1.0f, 8.0f, 1.0f)
			[
				SNew(SBox)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Part.Label)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(LabelColor))
					.ToolTipText(Part.Tip)
				]
			];
		Grid->AddSlot(ValueColumn, Row)
			.VAlign(VAlign_Fill)
			.Padding(0.0f, 1.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(MinValueWidth)
				.MaxDesiredWidth(MaxValueWidth)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(Part.Value))
					.HintText(Part.Hint)
					.ToolTipText(Part.Tip)
					.Font(FCoreStyle::GetDefaultFontStyle(Part.Part == ETextPart::Key ? "Mono" : "Regular", 8))
					.SelectAllTextWhenFocused(true)
					.RevertTextOnEscape(true)
					.OnTextCommitted(this, &SBangoGraphNode::OnTextPartCommitted, FieldIndex, Part.Part)
				]
			];
		++Row;
	}
	return Row;
}

int32 SBangoGraphNode::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const EBangoNodeDebugState DebugState = BangoNode ? BangoNode->DebugState : EBangoNodeDebugState::None;
	static const FSlateRoundedBoxBrush OutlineBrush(FLinearColor::Transparent, 10.0f, FLinearColor::White, 3.0f);

	// The trail: a node the script just ran gets a pale blue outline that fades out over a second.
	constexpr double PingSeconds = 1.0;
	const double SincePing = BangoNode && BangoNode->DebugPingTime > 0.0 ? FPlatformTime::Seconds() - BangoNode->DebugPingTime : PingSeconds;
	if (SincePing < PingSeconds)
	{
		const float Fade = 1.0f - (float)(SincePing / PingSeconds);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToInflatedPaintGeometry(FVector2D(5.0f, 5.0f)), &OutlineBrush,
			ESlateDrawEffect::None, FLinearColor(0.55f, 0.85f, 1.0f, 0.9f * Fade * Fade));
	}
	if (DebugState == EBangoNodeDebugState::None)
	{
		return SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
	}

	// An outline a little outside the node: pulsing yellow where it is (like Blueprint's), a dim one along the calls on
	// the way there, red where it failed.
	FLinearColor Color;
	switch (DebugState)
	{
	case EBangoNodeDebugState::Paused:
	{
		const float Pulse = 0.5f + 0.5f * FMath::Sin((float)FPlatformTime::Seconds() * 5.0f);
		Color = FLinearColor(1.0f, 0.85f, 0.1f, 0.55f + 0.45f * Pulse);
		break;
	}
	case EBangoNodeDebugState::CallStack:
		Color = FLinearColor(1.0f, 0.85f, 0.1f, 0.4f);
		break;
	default:
		Color = FLinearColor(1.0f, 0.2f, 0.15f, 1.0f);
		break;
	}
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToInflatedPaintGeometry(FVector2D(5.0f, 5.0f)), &OutlineBrush, ESlateDrawEffect::None, Color);
	return SGraphNode::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 1, InWidgetStyle, bParentEnabled);
}

void SBangoGraphNode::MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	if (BangoNode && BangoNode->IsEntry())
	{
		return; // always node 0 at (0,0); the rest of the graph moves around it
	}
	SGraphNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);
}

TSharedRef<SWidget> SBangoGraphNode::CreateTitleWidget(TSharedPtr<SNodeTitle> NodeTitle)
{
	const TSharedRef<SWidget> Title = SGraphNode::CreateTitleWidget(NodeTitle);
	if (!BangoNode || BangoNode->GetKind() != EBangoGraphNodeKind::For)
	{
		return Title;
	}
	// The toggle sits right after the title, so it stays put when the node gets wider or narrower.
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Title
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			MakeExpressionToggle()
		];
}

TSharedRef<SWidget> SBangoGraphNode::MakeExpressionToggle()
{
	const FBangoNodeLayout& Layout = BangoNode->Layout;
	return SNew(SBox)
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked(Layout.bShowsExpression ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
			.IsEnabled(Layout.bHasSimpleForm)
			.OnCheckStateChanged(this, &SBangoGraphNode::OnShowExpressionChanged)
			.ToolTipText(Layout.bHasSimpleForm
				? LOCTEXT("ShowExpressionTip", "Simple: First and Last. Complex: the whole loop header in one row, to write a loop that isn't a simple count.")
				: LOCTEXT("ExpressionOnlyTip", "This loop isn't a simple count (int _i = First; _i <= Last; _i++, or >= with --), so it's shown as its whole header."))
			.Padding(FMargin(4.0f, 1.0f))
			[
				SNew(STextBlock)
				.Text(Layout.bShowsExpression ? LOCTEXT("ComplexFor", "(Complex)") : LOCTEXT("SimpleFor", "(Simple)"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			]
		];
}

void SBangoGraphNode::OnShowExpressionChanged(ECheckBoxState NewState)
{
	if (BangoNode)
	{
		BangoNode->bShowExpression = NewState == ECheckBoxState::Checked;
		BangoNode->UpdateLayout();
		RefreshSoon();
	}
}

FReply SBangoGraphNode::OnToggleTextExpanded(int32 FieldIndex)
{
	if (BangoNode)
	{
		if (BangoNode->ExpandedTextFields.Remove(FieldIndex) == 0)
		{
			BangoNode->ExpandedTextFields.Add(FieldIndex);
		}
		RefreshSoon();
	}
	return FReply::Handled();
}

void SBangoGraphNode::OnTextPartCommitted(const FText& NewText, ETextCommit::Type CommitType, int32 FieldIndex, ETextPart Part)
{
	FBangoTextLiteral Literal;
	if (CommitType == ETextCommit::OnCleared || !BangoNode || !BangoNode->Layout.Fields.IsValidIndex(FieldIndex)
		|| !Bango::ParseTextLiteral(BangoNode->Layout.Fields[FieldIndex].Text, Literal))
	{
		return;
	}

	const FString Typed = NewText.ToString();
	FString Error;
	switch (Part)
	{
	case ETextPart::Key:
		if (Typed.TrimStartAndEnd().IsEmpty())
		{
			Error = TEXT("A text's key can't be empty.");
		}
		Literal.Key = Typed.TrimStartAndEnd();
		break;
	case ETextPart::Namespace:
		Literal.bHasNamespace = !Typed.TrimStartAndEnd().IsEmpty();
		Literal.Namespace = Typed.TrimStartAndEnd();
		break;
	case ETextPart::Notes:
		Literal.Notes = Typed;
		Literal.bHasNotes = !Typed.IsEmpty();
		break;
	}

	SetTextLiteral(FieldIndex, Literal, Error);
}

void SBangoGraphNode::OnTextTranslatedChanged(ECheckBoxState NewState, int32 FieldIndex)
{
	FBangoTextLiteral Literal;
	if (!BangoNode || !BangoNode->Layout.Fields.IsValidIndex(FieldIndex) || !Bango::ParseTextLiteral(BangoNode->Layout.Fields[FieldIndex].Text, Literal))
	{
		return;
	}
	const bool bTranslated = NewState == ECheckBoxState::Checked;
	if (bTranslated == !Literal.bInvariant)
	{
		return;
	}
	FBangoTextLiteral NewLiteral;
	NewLiteral.Source = Literal.Source;
	NewLiteral.bInvariant = !bTranslated;
	if (bTranslated)
	{
		NewLiteral.Key = Bango::MakeTextKey(); // a text that's newly translated needs a key of its own
	}
	SetTextLiteral(FieldIndex, NewLiteral, FString());
}

void SBangoGraphNode::SetTextLiteral(int32 FieldIndex, const FBangoTextLiteral& Literal, FString Error)
{
	const FString Value = Bango::MakeTextLiteral(Literal);
	if (Error.IsEmpty() && Value.Equals(BangoNode->Layout.Fields[FieldIndex].Text, ESearchCase::CaseSensitive))
	{
		return; // unchanged (like focus just moving on)
	}
	if (Error.IsEmpty())
	{
		BangoNode->SetFieldValue(FieldIndex, Value, Error);
	}
	if (!Error.IsEmpty())
	{
		FNotificationInfo Info(FText::FromString(Error));
		Info.ExpireDuration = 6.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
	RefreshSoon();
}

FReply SBangoGraphNode::OnValueClicked(const FGeometry& Geometry, const FPointerEvent& MouseEvent, int32 FieldIndex)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !EditBoxes.IsValidIndex(FieldIndex) || !EditBoxes[FieldIndex].IsValid())
	{
		return FReply::Unhandled();
	}

	BeginEditingField(FieldIndex);
	return FReply::Handled();
}

void SBangoGraphNode::BeginEditingField(int32 FieldIndex)
{
	if (!EditBoxes.IsValidIndex(FieldIndex) || !EditBoxes[FieldIndex].IsValid())
	{
		return;
	}
	EditingField = FieldIndex;
	TWeakPtr<SEditableTextBox> WeakBox = EditBoxes[FieldIndex];
	// The edit box only becomes visible on the next frame; give it keyboard focus then. (Its text is all selected
	// when it gets focus, so typing replaces it.)
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakBox](double, float)
	{
		if (TSharedPtr<SEditableTextBox> Box = WeakBox.Pin())
		{
			FSlateApplication::Get().SetKeyboardFocus(Box, EFocusCause::SetDirectly);
		}
		return EActiveTimerReturnType::Stop;
	}));
}

int32 SBangoGraphNode::FindEditableField(int32 From, int32 Step, bool bWrap) const
{
	const int32 Num = EditBoxes.Num();
	for (int32 Offset = 1; Offset <= Num; ++Offset)
	{
		int32 Index = From + Step * Offset;
		if (Index < 0 || Index >= Num)
		{
			if (!bWrap)
			{
				return INDEX_NONE;
			}
			Index = ((Index % Num) + Num) % Num;
		}
		if (EditBoxes[Index].IsValid())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FReply SBangoGraphNode::CommitAndMoveOn(int32 FieldIndex, int32 Step, bool bWrap)
{
	const TSharedPtr<SEditableTextBox> Box = EditBoxes.IsValidIndex(FieldIndex) ? EditBoxes[FieldIndex] : nullptr;
	if (!Box.IsValid() || EditingField != FieldIndex)
	{
		return FReply::Unhandled();
	}
	// The edit can change which fields the node has (like a call's command), so the next one is found after the
	// node has rebuilt itself for it.
	PendingEditFrom = FieldIndex;
	PendingEditStep = Step;
	bPendingEditWrap = bWrap;
	OnValueCommitted(Box->GetText(), ETextCommit::OnEnter, FieldIndex); // and the node rebuilds on the next frame
	return FReply::Handled();
}

void SBangoGraphNode::ResumePendingEdit()
{
	const int32 From = PendingEditFrom;
	PendingEditFrom = INDEX_NONE;
	if (From == INDEX_NONE)
	{
		return;
	}
	const int32 Next = FindEditableField(From, PendingEditStep, bPendingEditWrap);
	if (Next != INDEX_NONE && Next != From)
	{
		BeginEditingField(Next);
	}
}

void SBangoGraphNode::OnValueCommitted(const FText& NewText, ETextCommit::Type CommitType, int32 FieldIndex)
{
	if (EditingField != FieldIndex)
	{
		return; // not being edited (focus left a box that was never opened)
	}
	EditingField = INDEX_NONE;
	CloseCompletion();
	if (CommitType == ETextCommit::OnCleared || !BangoNode)
	{
		return; // Escape: keep the old value
	}

	// What was typed, as the field's code: a text field's plain text becomes a quoted, escaped string.
	const FString Typed = NewText.ToString();
	const FString Value = BangoNode->Layout.Fields.IsValidIndex(FieldIndex) ? Bango::GetFieldValueFromTyped(BangoNode->Layout.Fields[FieldIndex], Typed) : Typed;
	FString Error;
	if (!BangoNode->SetFieldValue(FieldIndex, Value, Error))
	{
		FNotificationInfo Info(FText::FromString(Error));
		Info.ExpireDuration = 6.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
	RefreshSoon();
}

void SBangoGraphNode::RefreshSoon()
{
	TWeakPtr<SBangoGraphNode> WeakThis = StaticCastSharedRef<SBangoGraphNode>(AsShared());
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakThis](double, float)
	{
		if (TSharedPtr<SBangoGraphNode> This = WeakThis.Pin())
		{
			This->UpdateGraphNode();
			This->ResumePendingEdit(); // Enter or Tab: on to the next field
			if (This->PendingCaseEdit != INDEX_NONE)
			{
				const int32 Case = This->PendingCaseEdit;
				This->PendingCaseEdit = INDEX_NONE;
				This->BeginEditingCase(Case);
			}
		}
		return EActiveTimerReturnType::Stop;
	}));
}

TSharedPtr<SGraphNode> FBangoGraphNodeFactory::CreateNode(UEdGraphNode* Node) const
{
	if (UBangoEdGraphNode* BangoNode = Cast<UBangoEdGraphNode>(Node))
	{
		if (BangoNode->IsReroute())
		{
			return SNew(SBangoRerouteNode, BangoNode);
		}
		return SNew(SBangoGraphNode, BangoNode);
	}
	return nullptr;
}

// ---- Autocomplete in node fields ----

namespace BangoNodeCompletionPrivate
{
	static FLinearColor KindColor(EBangoCompletionKind Kind)
	{
		switch (Kind)
		{
		case EBangoCompletionKind::Parameter:
		case EBangoCompletionKind::Local:    return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("9CDCFE")));
		case EBangoCompletionKind::Global:   return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("4FC1FF")));
		case EBangoCompletionKind::Function: return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("DCDCAA")));
		case EBangoCompletionKind::Builtin:  return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("C586C0")));
		case EBangoCompletionKind::Keyword:  return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("569CD6")));
		default:                             return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("4EC9B0")));
		}
	}

	static const TCHAR* KindLetter(EBangoCompletionKind Kind)
	{
		switch (Kind)
		{
		case EBangoCompletionKind::Parameter: return TEXT("P");
		case EBangoCompletionKind::Local:     return TEXT("L");
		case EBangoCompletionKind::Global:    return TEXT("G");
		case EBangoCompletionKind::Function:  return TEXT("F");
		case EBangoCompletionKind::Builtin:   return TEXT("B");
		case EBangoCompletionKind::Keyword:   return TEXT("K");
		default:                              return TEXT("C");
		}
	}
}

bool SBangoGraphNode::FieldTakesCompletion(int32 FieldIndex, const FString& Typed) const
{
	if (!BangoNode || !BangoNode->Layout.Fields.IsValidIndex(FieldIndex))
	{
		return false;
	}
	const FBangoNodeField& Field = BangoNode->Layout.Fields[FieldIndex];
	// A text field's box holds the plain text being shown, unless it starts with = (then it's an expression).
	return !Bango::IsTextField(Field) || Typed.StartsWith(TEXT("="));
}

void SBangoGraphNode::QueueCompletionUpdate(int32 FieldIndex, bool bMayOpen)
{
	bCompletionMayOpen |= bMayOpen;
	if (bCompletionQueued)
	{
		return;
	}
	bCompletionQueued = true;
	// Next frame: by then the box's cursor has moved for the change too.
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this, FieldIndex](double, float)
	{
		bCompletionQueued = false;
		const bool bOpen = bCompletionMayOpen;
		bCompletionMayOpen = false;
		UpdateCompletion(FieldIndex, bOpen);
		return EActiveTimerReturnType::Stop;
	}));
}

void SBangoGraphNode::UpdateCompletion(int32 FieldIndex, bool bMayOpen)
{
	const TSharedPtr<SEditableTextBox> Box = EditBoxes.IsValidIndex(FieldIndex) ? EditBoxes[FieldIndex] : nullptr;
	const TSharedPtr<SMenuAnchor> Anchor = CompletionAnchors.IsValidIndex(FieldIndex) ? CompletionAnchors[FieldIndex] : nullptr;
	if (!Box.IsValid() || !Anchor.IsValid() || EditingField != FieldIndex || !Box->HasKeyboardFocus())
	{
		CloseCompletion();
		return;
	}
	const FString Typed = Box->GetText().ToString();
	const int32 CursorColumn = FMath::Clamp(CompletionCursor, 0, Typed.Len());
	int32 WordStart = INDEX_NONE;
	if (!FieldTakesCompletion(FieldIndex, Typed) || !FBangoCompletion::FindWordBeforeCursor(Typed, CursorColumn, WordStart))
	{
		CloseCompletion();
		return;
	}
	const bool bSameWord = IsCompletionOpen() && CompletionWordStart == WordStart;
	if (!bSameWord && !bMayOpen)
	{
		CloseCompletion();
		return;
	}
	if (!IsCompletionOpen())
	{
		GatherCompletionItems();
	}

	const FString Prefix = Typed.Mid(WordStart, CursorColumn - WordStart);
	const int32 WasSelected = CompletionShown.IsValidIndex(CompletionSelected) ? CompletionShown[CompletionSelected] : INDEX_NONE;
	FBangoCompletion::Filter(CompletionItems, Prefix, CompletionShown);
	// Nothing to offer, or the name is already typed in full: no list.
	if (CompletionShown.Num() == 0 || (CompletionShown.Num() == 1 && CompletionItems[CompletionShown[0]].Name.Equals(Prefix, ESearchCase::CaseSensitive)))
	{
		CloseCompletion();
		return;
	}
	CompletionSelected = FMath::Max(0, CompletionShown.IndexOfByKey(WasSelected));
	CompletionTop = FMath::Clamp(CompletionTop, FMath::Max(0, CompletionSelected - CompletionVisibleRows + 1), CompletionSelected);
	CompletionWordStart = WordStart;
	if (!Anchor->IsOpen())
	{
		Anchor->SetMenuContent(MakeCompletionPopup(FieldIndex));
		Anchor->SetIsOpen(true, /*bFocusMenu*/ false);
	}
	RebuildCompletionRows();
}

void SBangoGraphNode::GatherCompletionItems()
{
	CompletionItems.Reset();
	CompletionShown.Reset();
	CompletionSelected = 0;
	CompletionTop = 0;
	if (!BangoNode)
	{
		return;
	}
	TSet<FString> Seen;
	auto AddItem = [this, &Seen](const FBangoCompletionItem& Item)
	{
		if (!Seen.Contains(Item.Name))
		{
			Seen.Add(Item.Name);
			CompletionItems.Add(Item);
		}
	};

	const FBangoNodeLayoutContext* Context = BangoNode->LayoutContext.Get();
	if (Context)
	{
		// The variables this node can see: its graph's parameters and locals (named _x) and the file's globals.
		if (const TMap<FString, FString>* Visible = Context->VisibleVariables.Find(BangoNode->NodeID))
		{
			for (const TPair<FString, FString>& Variable : *Visible)
			{
				FBangoCompletionItem Item;
				Item.Name = Variable.Key;
				Item.Detail = Variable.Value;
				Item.Kind = Variable.Key.StartsWith(TEXT("_")) ? EBangoCompletionKind::Local : EBangoCompletionKind::Global;
				AddItem(Item);
			}
		}
		for (const FBangoCommandSignature& Function : Context->Functions)
		{
			FBangoCompletionItem Item;
			Item.Name = Function.Name;
			Item.Detail = Function.ToString();
			Item.Kind = EBangoCompletionKind::Function;
			Item.bCallable = true;
			AddItem(Item);
		}
		if (Context->Commands)
		{
			TArray<FString> Names;
			Context->Commands->GetCommandNames(Names);
			for (const FString& Name : Names)
			{
				FBangoCompletionItem Item;
				Item.Name = Name;
				Item.Kind = EBangoCompletionKind::Command;
				Item.bCallable = true;
				if (const FBangoCommandSignature* Signature = Context->Commands->FindCommand(Name))
				{
					Item.Detail = Signature->ToString();
					Item.Description = Signature->CommandDescription;
				}
				AddItem(Item);
			}
		}
	}
	TArray<FBangoCompletionItem> Builtins;
	FBangoCompletion::GetBuiltinItems(Builtins);
	FBangoCompletion::GetKeywordItems(Builtins, /*bValuesOnly*/ true); // true, false, nil: a field holds an expression
	for (const FBangoCompletionItem& Item : Builtins)
	{
		AddItem(Item);
	}
	CompletionItems.Sort([](const FBangoCompletionItem& A, const FBangoCompletionItem& B)
	{
		const int32 IgnoringCase = A.Name.Compare(B.Name, ESearchCase::IgnoreCase);
		return IgnoringCase != 0 ? IgnoringCase < 0 : A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0;
	});
}

void SBangoGraphNode::CloseCompletion()
{
	CompletionWordStart = INDEX_NONE;
	CompletionShown.Reset();
	for (const TSharedPtr<SMenuAnchor>& Anchor : CompletionAnchors)
	{
		if (Anchor.IsValid() && Anchor->IsOpen())
		{
			Anchor->SetIsOpen(false, false);
		}
	}
}

void SBangoGraphNode::MoveCompletionSelection(int32 Delta)
{
	if (CompletionShown.Num() == 0)
	{
		return;
	}
	CompletionSelected = FMath::Clamp(CompletionSelected + Delta, 0, CompletionShown.Num() - 1);
	if (CompletionSelected < CompletionTop)
	{
		CompletionTop = CompletionSelected;
	}
	else if (CompletionSelected >= CompletionTop + CompletionVisibleRows)
	{
		CompletionTop = CompletionSelected - CompletionVisibleRows + 1;
	}
	RebuildCompletionRows();
}

void SBangoGraphNode::AcceptCompletion(int32 FieldIndex)
{
	const TSharedPtr<SEditableTextBox> Box = EditBoxes.IsValidIndex(FieldIndex) ? EditBoxes[FieldIndex] : nullptr;
	if (!Box.IsValid() || !IsCompletionOpen() || !CompletionShown.IsValidIndex(CompletionSelected))
	{
		CloseCompletion();
		return;
	}
	const FBangoCompletionItem& Item = CompletionItems[CompletionShown[CompletionSelected]];
	const FString Typed = Box->GetText().ToString();
	const int32 CursorColumn = FMath::Clamp(CompletionCursor, CompletionWordStart, Typed.Len());
	// The word typed so far becomes the whole name; commands and functions get their ( too, unless one's there.
	FString Insert = Item.Name;
	if (Item.bCallable && !(CursorColumn < Typed.Len() && Typed[CursorColumn] == TEXT('(')))
	{
		Insert += TEXT("(");
	}
	const FString NewText = Typed.Left(CompletionWordStart) + Insert + Typed.Mid(CursorColumn);
	const int32 NewCursor = CompletionWordStart + Insert.Len();
	CloseCompletion();

	bCompletionAccepting = true;
	Box->SetText(FText::FromString(NewText));
	Box->GoTo(FTextLocation(0, NewCursor));
	bCompletionAccepting = false;
	CompletionCursor = NewCursor;
}

void SBangoGraphNode::RebuildCompletionRows()
{
	using namespace BangoNodeCompletionPrivate;
	if (!CompletionRows.IsValid())
	{
		return;
	}
	CompletionRows->ClearChildren();
	const FSlateFontInfo NameFont = FCoreStyle::GetDefaultFontStyle("Mono", 9);
	const FSlateFontInfo DetailFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const int32 End = FMath::Min(CompletionShown.Num(), CompletionTop + CompletionVisibleRows);
	for (int32 Row = CompletionTop; Row < End; ++Row)
	{
		const FBangoCompletionItem& Item = CompletionItems[CompletionShown[Row]];
		const bool bSelected = Row == CompletionSelected;
		const int32 FieldIndex = EditingField;
		CompletionRows->AddSlot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(bSelected ? "WhiteBrush" : "NoBorder"))
			.BorderBackgroundColor(bSelected ? FAppStyle::GetSlateColor("SelectionColor") : FSlateColor(FLinearColor::Transparent))
			.Padding(0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.IsFocusable(false) // the field keeps keyboard focus
				.ContentPadding(FMargin(4.0f, 1.0f))
				.OnClicked_Lambda([this, Row, FieldIndex]()
				{
					CompletionSelected = Row;
					AcceptCompletion(FieldIndex);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(10.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(KindLetter(Item.Kind)))
							.Font(DetailFont)
							.ColorAndOpacity(FSlateColor(KindColor(Item.Kind)))
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.Name))
						.Font(NameFont)
						.ColorAndOpacity(FSlateColor(KindColor(Item.Kind)))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(10.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.Detail))
						.Font(DetailFont)
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
				]
			]
		];
	}
	// The selected entry's description, if it has one, and how many more there are.
	const FBangoCompletionItem* Selected = CompletionShown.IsValidIndex(CompletionSelected) ? &CompletionItems[CompletionShown[CompletionSelected]] : nullptr;
	FString Footer = Selected ? Selected->Description : FString();
	if (CompletionShown.Num() > CompletionVisibleRows)
	{
		Footer += FString::Printf(TEXT("%s%d of %d"), Footer.IsEmpty() ? TEXT("") : TEXT("\n"), CompletionSelected + 1, CompletionShown.Num());
	}
	if (!Footer.IsEmpty())
	{
		CompletionRows->AddSlot()
		.AutoHeight()
		.Padding(FMargin(6.0f, 3.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Footer))
			.AutoWrapText(true)
			.Font(DetailFont)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	}
}

TSharedRef<SWidget> SBangoGraphNode::MakeCompletionPopup(int32 FieldIndex)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Menu.Background"))
		.Padding(FMargin(2.0f))
		[
			SNew(SBox)
			.WidthOverride(380.0f)
			[
				SAssignNew(CompletionRows, SVerticalBox)
			]
		];
}

FReply SBangoGraphNode::OnEditBoxKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent, int32 FieldIndex)
{
	const FKey Key = KeyEvent.GetKey();
	if (Key == EKeys::SpaceBar && KeyEvent.IsControlDown())
	{
		QueueCompletionUpdate(FieldIndex, true); // Ctrl+Space: open the list for the word at the cursor
		return FReply::Handled();
	}
	if (KeyEvent.IsControlDown() || KeyEvent.IsAltDown() || KeyEvent.IsCommandDown())
	{
		return FReply::Unhandled();
	}
	if (!IsCompletionOpen())
	{
		// Enter applies what's typed and moves on to the next field (after the last, editing ends); Tab does the
		// same but wraps around to the first, and Shift goes back instead.
		if (Key == EKeys::Enter || Key == EKeys::Tab)
		{
			return CommitAndMoveOn(FieldIndex, KeyEvent.IsShiftDown() ? -1 : 1, Key == EKeys::Tab);
		}
		return FReply::Unhandled();
	}
	if (Key == EKeys::Up || Key == EKeys::Down)
	{
		MoveCompletionSelection(Key == EKeys::Up ? -1 : 1);
		return FReply::Handled();
	}
	if (Key == EKeys::PageUp || Key == EKeys::PageDown)
	{
		MoveCompletionSelection(Key == EKeys::PageUp ? -CompletionVisibleRows : CompletionVisibleRows);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::Tab)
	{
		AcceptCompletion(FieldIndex); // and the field stays open for more typing; Enter again applies it
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		CloseCompletion(); // just the list: a second Escape cancels the edit, as before
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
