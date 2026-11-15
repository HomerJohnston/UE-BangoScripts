#include "BangoInlineScriptCustomization.h"

#include "BangoInlineScript.h"
#include "BangoGraph.h"
#include "BangoParser.h"
#include "BangoTextMetadata.h"
#include "BangoEditorStyle.h"
#include "BangoInlineScriptTarget.h"
#include "DetailWidgetRow.h"
#include "Dialog/SCustomDialog.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyHandle.h"
#include "SBangoTextEditor.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BangoInlineScript"

namespace BangoInlineScriptCustomizationPrivate
{
	/** About ten lines of the box's text (10 pt), and its frame. */
	constexpr float MaxBoxHeight = 180.0f;
	/** While the script's window is open: the box is darkened by this much, and the note over it is this colour. */
	constexpr float LockedDim = 0.75f;
	const FLinearColor LockedNoteColor(1.0f, 0.85f, 0.0f);
	/** The Edit button over the box's top-right corner is this opaque. */
	constexpr float EditButtonOpacity = 0.75f;
	/** Its space from the box's top and right edges, and how much further left it sits while the box's scrollbar is shown. */
	constexpr float EditButtonTop = 3.0f;
	constexpr float EditButtonRight = 2.0f;
	constexpr float EditButtonScrollBarGap = 12.0f;
	/** The Has graph layout note: how far up from the box's bottom it always sits (more over a scrollbar). */
	constexpr float LayoutNoteBottom = 4.0f;
	/** Clicking the box while it has graph layout flashes the note: this long, this many times, in the locked note's colour. */
	constexpr double LayoutNoteFlashSeconds = 0.6;
	constexpr int32 LayoutNoteFlashes = 2;
	const FLinearColor LayoutNoteBackground(0.0f, 0.0f, 0.0f, 0.6f);

	/**
	 * The script without its graph layout: no node metadata (//@) and no section headings (// ====), so it can be
	 * typed in its box. Written compact by the graph writer if it reads; otherwise each line has its metadata taken off,
	 * and the headings and reroute lines (metadata only) are dropped.
	 */
	static FString StripNodeLayout(const FString& Script)
	{
		FBangoGraphDocument Document;
		FBangoCompileError Error;
		FString Compact;
		FString WriteError;
		FBangoGraphWriteOptions Options;
		Options.bCompact = true;
		if (Bango::BuildGraph(Script, TEXT("inline"), Document, Error) && Bango::WriteGraphText(Document, Compact, WriteError, Options))
		{
			return Compact.TrimEnd(); // without the last line's line break, as a script typed in the box would be
		}
		TArray<FString> Lines;
		FBangoMetadataTracker::SplitLines(Script, Lines);
		TArray<FString> Kept;
		for (const FString& Line : Lines)
		{
			FBangoLineMetadata Meta;
			const FString Visible = FBangoMetadataTracker::SplitLine(Line, Meta).TrimEnd();
			const FString Trimmed = Visible.TrimStart();
			const bool bHeading = Trimmed.StartsWith(TEXT("//")) && !Trimmed.StartsWith(TEXT("//~")) && Bango::IsSectionHeadingComment(Trimmed.Mid(2));
			if (bHeading || (Meta.HasMetadata() && Trimmed.IsEmpty()))
			{
				continue;
			}
			Kept.Add(Visible);
		}
		return FBangoMetadataTracker::JoinLines(Kept);
	}
}

/**
 * The details panel's box for one FBangoInlineScript property: its text, typed in and applied when the box loses
 * focus (as one undoable change), kept up to date with changes made elsewhere, and the Edit button over its top-right
 * corner (which shows the window instead once it's open).
 */
class SBangoInlineScriptBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBangoInlineScriptBox) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IPropertyHandle>& InStructHandle)
	{
		using namespace BangoInlineScriptCustomizationPrivate;
		StructHandle = InStructHandle;
		SourceHandle = InStructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoInlineScript, Source));

		// The window's key: only for one object (several selected objects, each with its own script, have no window).
		TArray<UObject*> Outers;
		InStructHandle->GetOuterObjects(Outers);
		if (Outers.Num() == 1 && Outers[0])
		{
			Target = MakeShared<FBangoInlineScriptTarget>(Outers[0], InStructHandle->GeneratePathToProperty(), InStructHandle, InStructHandle->GetPropertyDisplayName());
		}

		ReadValue(Shown, bMultipleValues);

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SBox)
						.MaxDesiredHeight(MaxBoxHeight)
						[
							SNew(SWidgetSwitcher)
							.WidgetIndex_Lambda([this]() { return bMultipleValues ? 1 : 0; })
							+ SWidgetSwitcher::Slot()
							[
								SAssignNew(TextBox, SBangoTextEditor)
								.Text(FText::FromString(Shown))
								.InlineScriptEditor(true) // no metadata shown, and read-only while the script has some
								.IsReadOnly_Lambda([this]() { return IsLocked(); })
								.OnTextCommitted(this, &SBangoInlineScriptBox::OnTextCommitted)
								.ContextMenuExtender(this, &SBangoInlineScriptBox::ExtendContextMenu)
							]
							+ SWidgetSwitcher::Slot()
							.Padding(4.0f)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("MultipleValues", "Multiple Values"))
								.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
							]
						]
					]
					// While the script's window is open: the box darkened, and a note over its middle (the box can't be typed in then).
					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image(FBangoEditorStyle::GetWhiteBrush())
						.ColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, LockedDim))
						.Visibility_Lambda([this]() { return IsWindowOpen() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					]
					// A script with graph layout (node metadata) is edited in the window, or made editable here: a note at the
					// bottom says so (above and left of the scrollbars while they're shown), with how in its tooltip.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Bottom)
					.Padding(TAttribute<FMargin>::CreateLambda([this]()
					{
						const bool bVertical = TextBox.IsValid() && TextBox->IsVerticalScrollBarShown();
						const bool bHorizontal = TextBox.IsValid() && TextBox->IsHorizontalScrollBarShown();
						return FMargin(2.0f, 2.0f, 2.0f + (bVertical ? EditButtonScrollBarGap : 0.0f), LayoutNoteBottom + (bHorizontal ? EditButtonScrollBarGap : 0.0f));
					}))
					[
						SNew(SBorder)
						.BorderImage(FBangoEditorStyle::GetWhiteBrush())
						.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FMath::Lerp(LayoutNoteBackground, FLinearColor(LockedNoteColor.R, LockedNoteColor.G, LockedNoteColor.B, 0.9f), GetFlash())); })
						.Padding(FMargin(4.0f, 1.0f))
						.ToolTipText(LOCTEXT("HasLayoutTooltip", "Click Edit to use the full editor, or right-click > Make Editable Inline to remove graph layout."))
						.Visibility_Lambda([this]() { return HasLayout() && !IsWindowOpen() && !bMultipleValues ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							SNew(STextBlock)
							.Text(LOCTEXT("HasLayout", "Has graph layout"))
							.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
							.ColorAndOpacity_Lambda([this]() { return GetFlash() > 0.5f ? FSlateColor(FLinearColor::Black) : FSlateColor::UseSubduedForeground(); })
						]
					]
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(4.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("OpenInWindow", "Editor open. Click Edit to focus it."))
						.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
						.ColorAndOpacity(LockedNoteColor)
						.Visibility_Lambda([this]() { return IsWindowOpen() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					]
					// The Edit button, in the box's top-right corner (left of the scrollbar while it's shown): hover-only, and a
					// little see-through so the text shows.
					+ SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Top)
					.Padding(TAttribute<FMargin>::CreateLambda([this]()
					{
						const bool bScrollBar = TextBox.IsValid() && TextBox->IsVerticalScrollBarShown();
						return FMargin(2.0f, EditButtonTop, EditButtonRight + (bScrollBar ? EditButtonScrollBarGap : 0.0f), 2.0f);
					}))
					[
						SAssignNew(EditButton, SButton)
						.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
						.RenderOpacity(EditButtonOpacity)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.ContentPadding(FMargin(2.0f))
						.ToolTipText_Lambda([this]()
						{
							if (!Target.IsValid())
							{
								return LOCTEXT("EditManyTooltip", "Select a single object to edit its script in a window.");
							}
							return IsWindowOpen()
								? LOCTEXT("ShowWindowTooltip", "Bring this script's window to the front.")
								: LOCTEXT("EditTooltip", "Edit this script in the full graph editor, in a window of its own. While the window is open, the box here can't be typed in.");
						})
						.IsEnabled_Lambda([this]() { return Target.IsValid() && !bMultipleValues && !StructHandle->IsEditConst(); })
						.OnClicked(this, &SBangoInlineScriptBox::OnEditClicked)
						[
							SNew(SImage)
							.Image(FBangoEditorStyle::GetEditScriptIcon())
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
			]
		];

	}

	/** Keeps the box up to date with the property (undo, the window's Save, paste), except while it's being typed in. */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		if (!SourceHandle.IsValid() || !SourceHandle->IsValidHandle() || !TextBox.IsValid() || TextBox->HasAnyUserFocusOrFocusedDescendants())
		{
			return;
		}
		FString Value;
		bool bMultiple = false;
		ReadValue(Value, bMultiple);
		bMultipleValues = bMultiple;
		if (!bMultiple && !Value.Equals(Shown, ESearchCase::CaseSensitive))
		{
			Shown = Value;
			TextBox->SetText(FText::FromString(Shown));
		}
	}

private:
	TSharedPtr<IPropertyHandle> StructHandle;
	TSharedPtr<IPropertyHandle> SourceHandle;
	TSharedPtr<FBangoInlineScriptTarget> Target;
	TSharedPtr<SBangoTextEditor> TextBox;
	TSharedPtr<SButton> EditButton;
	/** When the Has graph layout note last started flashing (FSlateApplication time), or a negative number. */
	double FlashStartTime = -1.0;
	/** The text the box was last given (or that was last applied from it). */
	FString Shown;
	bool bMultipleValues = false;

	void ReadValue(FString& OutValue, bool& bOutMultiple) const
	{
		OutValue.Reset();
		bOutMultiple = false;
		if (SourceHandle.IsValid() && SourceHandle->IsValidHandle())
		{
			bOutMultiple = SourceHandle->GetValue(OutValue) == FPropertyAccess::MultipleValues;
		}
	}

	bool IsWindowOpen() const
	{
		return Target.IsValid() && FBangoInlineScriptWindows::IsOpen(Target->GetKey());
	}

	/** How lit the Has graph layout note is while it flashes: 0 (as usual) to 1 (fully lit). */
	float GetFlash() const
	{
		using namespace BangoInlineScriptCustomizationPrivate;
		if (FlashStartTime < 0.0)
		{
			return 0.0f;
		}
		const double Elapsed = FSlateApplication::Get().GetCurrentTime() - FlashStartTime;
		if (Elapsed >= LayoutNoteFlashSeconds)
		{
			return 0.0f;
		}
		return (float)FMath::Abs(FMath::Sin(Elapsed / LayoutNoteFlashSeconds * LayoutNoteFlashes * PI));
	}

	/** A left click on the box while the note is shown (it can't be typed in): the note flashes, so it's seen. Selecting still works. */
	virtual FReply OnPreviewMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && HasLayout() && !IsWindowOpen() && !bMultipleValues && !(EditButton.IsValid() && EditButton->IsHovered()))
		{
			FlashStartTime = FSlateApplication::Get().GetCurrentTime();
			// Keep painting while it flashes, in case nothing else would.
			RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double, float)
			{
				return GetFlash() > 0.0f || FSlateApplication::Get().GetCurrentTime() - FlashStartTime < BangoInlineScriptCustomizationPrivate::LayoutNoteFlashSeconds
					? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
			}));
		}
		return FReply::Unhandled();
	}

	/** The script has node metadata: it's laid out for the graph, so it's edited in the window (or made editable here). */
	bool HasLayout() const
	{
		return TextBox.IsValid() && TextBox->HasNodeMetadata();
	}

	void ExtendContextMenu(FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection(TEXT("BangoInlineScript"), LOCTEXT("InlineScriptSection", "Inline Script"));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("MakeEditableInline", "Make Editable Inline"),
			LOCTEXT("MakeEditableInlineTooltip", "Removes the script's node metadata (and section headings), so it can be typed in here. Its graph loses its layout: the nodes are laid out afresh when it's next opened in the editor."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SBangoInlineScriptBox::MakeEditableInline),
				FCanExecuteAction::CreateLambda([this]() { return HasLayout() && !IsLocked(); })));
		MenuBuilder.EndSection();
	}

	void MakeEditableInline()
	{
		using namespace BangoInlineScriptCustomizationPrivate;
		if (!HasLayout() || IsLocked() || !SourceHandle.IsValid() || !SourceHandle->IsValidHandle())
		{
			return;
		}
		TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
			.Title(LOCTEXT("MakeEditableInlineTitle", "Make Editable Inline"))
			.Content()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MakeEditableInlineMessage", "This action will remove all node metadata. The graph representation of this script will lose all layout information. Proceed?"))
				.AutoWrapText(true)
			]
			.Buttons({
				SCustomDialog::FButton(LOCTEXT("Yes", "Yes")),
				SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel")),
			});
		if (Dialog->ShowModal() != 0)
		{
			return;
		}
		FString Current;
		bool bMultiple = false;
		ReadValue(Current, bMultiple);
		if (bMultiple)
		{
			return;
		}
		Shown = StripNodeLayout(Current);
		SourceHandle->SetValue(Shown); // one undoable change
		TextBox->SetText(FText::FromString(Shown));
	}

	bool IsLocked() const
	{
		return bMultipleValues || IsWindowOpen() || !StructHandle.IsValid() || StructHandle->IsEditConst();
	}

	void OnTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
	{
		if (IsLocked() || !SourceHandle.IsValid() || !SourceHandle->IsValidHandle())
		{
			return;
		}
		const FString Text = NewText.ToString();
		FString Current;
		bool bMultiple = false;
		ReadValue(Current, bMultiple);
		if (!bMultiple && Text.Equals(Current, ESearchCase::CaseSensitive))
		{
			return;
		}
		Shown = Text;
		SourceHandle->SetValue(Text); // one undoable change, for every selected object
	}

	FReply OnEditClicked()
	{
		if (Target.IsValid())
		{
			// What's typed but not applied yet goes in first, so the window opens on it.
			if (TextBox.IsValid() && !IsLocked())
			{
				OnTextCommitted(TextBox->GetText(), ETextCommit::Default);
			}
			FBangoInlineScriptWindows::Open(Target.ToSharedRef());
		}
		return FReply::Handled();
	}
};

TSharedRef<IPropertyTypeCustomization> FBangoInlineScriptCustomization::MakeInstance()
{
	return MakeShared<FBangoInlineScriptCustomization>();
}

void FBangoInlineScriptCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils)
{
	HeaderRow
		.NameContent()
		[
			StructHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(320.0f)
		.MaxDesiredWidth(1200.0f)
		[
			SNew(SBangoInlineScriptBox, StructHandle)
		];
}

#undef LOCTEXT_NAMESPACE
