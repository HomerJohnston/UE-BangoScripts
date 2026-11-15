#include "BangoInlineScriptCustomization.h"

#include "BangoInlineScript.h"
#include "BangoGraph.h"
#include "BangoParser.h"
#include "BangoTextMetadata.h"
#include "BangoEditorStyle.h"
#include "BangoInlineScriptTarget.h"
#include "DetailWidgetRow.h"
#include "Dialog/SCustomDialog.h"
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
 * focus (as one undoable change), kept up to date with changes made elsewhere, and the Edit button on its right (which
 * shows the window instead once it's open).
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
					// A script with graph layout (node metadata) is edited in the window, or made editable here: a note says so.
					+ SOverlay::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					.Padding(2.0f)
					[
						SNew(SBorder)
						.BorderImage(FBangoEditorStyle::GetWhiteBrush())
						.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
						.Padding(FMargin(4.0f, 1.0f))
						.Visibility_Lambda([this]() { return HasLayout() && !IsWindowOpen() && !bMultipleValues ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
						[
							SNew(STextBlock)
							.Text(LOCTEXT("HasLayout", "Has graph layout: click Edit, or right-click > Make Editable Inline."))
							.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
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
				]
				// The Edit button, as tall as the box beside it.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.ContentPadding(FMargin(2.0f, 0.0f))
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
