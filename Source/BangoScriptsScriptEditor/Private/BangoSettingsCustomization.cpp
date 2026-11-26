#include "BangoSettingsCustomization.h"

#include "BangoDefines.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "BangoSettingsCustomization"

TSharedRef<IDetailCustomization> FBangoSettingsCustomization::MakeInstance()
{
	return MakeShared<FBangoSettingsCustomization>();
}

void FBangoSettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<const FBangoDefineEntry*> FromPlugins;
	for (const FBangoDefineEntry& Define : Bango::GetDefines())
	{
		if (!Define.Source.IsEmpty())
		{
			FromPlugins.Add(&Define);
		}
	}
	if (FromPlugins.Num() == 0)
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Defines"));
	Category.AddCustomRow(LOCTEXT("FromPluginsSearch", "Defines from plugins"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFontBold())
		.Text(LOCTEXT("FromPlugins", "From plugins"))
		.ToolTipText(LOCTEXT("FromPluginsTooltip", "Defines other plugins give (Bango::RegisterDefines). Their code sets them, so they're shown here, not edited. A row above with the same name is left out."))
	];
	for (const FBangoDefineEntry* Define : FromPlugins)
	{
		FString Tooltip = FString::Printf(TEXT("define %s %s = %s, registered by %s"), *Define->TypeName, *Define->Name, *Define->ValueText, *Define->Source);
		if (!Define->Description.IsEmpty())
		{
			Tooltip += TEXT("\n") + Define->Description;
		}
		Category.AddCustomRow(FText::FromString(Define->Name))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(FText::FromString(Define->Name))
			.ToolTipText(FText::FromString(Tooltip))
		]
		.ValueContent()
		.MinDesiredWidth(250.0f)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(FText::FromString(FString::Printf(TEXT("%s = %s    (%s)"), *Define->TypeName, *Define->ValueText, *Define->Source)))
			.ToolTipText(FText::FromString(Tooltip))
		];
	}
}

#undef LOCTEXT_NAMESPACE
