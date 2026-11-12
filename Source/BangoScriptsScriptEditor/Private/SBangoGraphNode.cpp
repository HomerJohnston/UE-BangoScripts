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
	static constexpr float MinValueWidth = 90.0f;
	static constexpr float MaxValueWidth = 240.0f;

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
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetNoBrush())
		.Padding(FMargin(0.0f, 3.0f))
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

void SBangoGraphNode::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	using namespace BangoGraphNodeStyle;
	PinToAdd->SetOwner(SharedThis(this));
	const FMargin BodyInset = GetDefault<UGraphEditorSettings>()->GetNonPinNodeBodyPadding();
	const float HalfPin = SBangoGraphPin::PinSize * 0.5f;
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

TSharedRef<SWidget> SBangoGraphNode::MakeFieldsArea()
{
	EditBoxes.Reset();
	EditingField = INDEX_NONE;
	if (!BangoNode || BangoNode->Layout.Fields.Num() == 0)
	{
		return SNew(SSpacer).Size(29);
	}
	EditBoxes.SetNum(BangoNode->Layout.Fields.Num());

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

	return SNew(SBox)
		.MinDesiredWidth(HeadingWidth)
		.Padding(2, 9, 2, 2)
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
		ValueTip += (ValueTip.IsEmpty() ? FString() : FString(TEXT("\n\n"))) + LOCTEXT("ReadOnlyDefaultTip", "Edit parameters and their defaults in the INPUTS panel.").ToString();
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

	TSharedRef<SWidget> Content = DisplayBox;
	if (bEditable)
	{
		DisplayBox->SetOnMouseButtonDown(FPointerEventHandler::CreateSP(this, &SBangoGraphNode::OnValueClicked, FieldIndex));
		DisplayBox->SetCursor(EMouseCursor::TextEditBeam);

		TSharedRef<SEditableTextBox> EditBox = SNew(SEditableTextBox)
			.Text(FText::FromString(Bango::GetFieldEditText(Field)))
			.Font(ValueFont)
			.SelectAllTextWhenFocused(true)
			.RevertTextOnEscape(true)
			.OnTextCommitted(this, &SBangoGraphNode::OnValueCommitted, FieldIndex);
		EditBoxes[FieldIndex] = EditBox;

		Content = SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this, FieldIndex]() { return EditingField == FieldIndex ? 1 : 0; })
			+ SWidgetSwitcher::Slot()
			[
				DisplayBox
			]
			+ SWidgetSwitcher::Slot()
			[
				EditBox
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

	EditingField = FieldIndex;
	TWeakPtr<SEditableTextBox> WeakBox = EditBoxes[FieldIndex];
	// The edit box only becomes visible on the next frame; give it keyboard focus then.
	RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([WeakBox](double, float)
	{
		if (TSharedPtr<SEditableTextBox> Box = WeakBox.Pin())
		{
			FSlateApplication::Get().SetKeyboardFocus(Box, EFocusCause::SetDirectly);
		}
		return EActiveTimerReturnType::Stop;
	}));
	return FReply::Handled();
}

void SBangoGraphNode::OnValueCommitted(const FText& NewText, ETextCommit::Type CommitType, int32 FieldIndex)
{
	if (EditingField != FieldIndex)
	{
		return; // not being edited (focus left a box that was never opened)
	}
	EditingField = INDEX_NONE;
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

#undef LOCTEXT_NAMESPACE
