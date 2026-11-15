#include "BangoInlineScriptCustomization.h"

#include "BangoInlineScript.h"
#include "BangoInlineScriptTarget.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "SBangoTextEditor.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BangoInlineScript"

namespace BangoInlineScriptCustomizationPrivate
{
	/** About ten lines of the box's text (10 pt), and its frame. */
	constexpr float MaxBoxHeight = 180.0f;
}

/**
 * The details panel's box for one FBangoInlineScript property: its text, typed in and applied when the box loses
 * focus (as one undoable change), kept up to date with changes made elsewhere, and the Edit / Show Window button.
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
				SNew(SBox)
				.MaxDesiredHeight(MaxBoxHeight)
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([this]() { return bMultipleValues ? 1 : 0; })
					+ SWidgetSwitcher::Slot()
					[
						SAssignNew(TextBox, SBangoTextEditor)
						.Text(FText::FromString(Shown))
						.IsReadOnly_Lambda([this]() { return IsLocked(); })
						.OnTextCommitted(this, &SBangoInlineScriptBox::OnTextCommitted)
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
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OpenInWindow", "Open in a script window."))
					.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility_Lambda([this]() { return IsWindowOpen() ? EVisibility::Visible : EVisibility::Collapsed; })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text_Lambda([this]() { return IsWindowOpen() ? LOCTEXT("ShowWindow", "Show Window") : LOCTEXT("Edit", "Edit"); })
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
				]
			]
		];

		if (TextBox.IsValid())
		{
			TextBox->SetMetadataShown(false); // node positions aren't worth the room here
		}
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
