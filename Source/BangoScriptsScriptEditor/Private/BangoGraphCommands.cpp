#include "BangoGraphCommands.h"

#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "BangoGraphCommands"

FBangoGraphCommands::FBangoGraphCommands()
	: TCommands<FBangoGraphCommands>(TEXT("BangoGraph"), LOCTEXT("BangoGraphContext", "Bango Graph"), NAME_None, FAppStyle::GetAppStyleSetName())
{
}

void FBangoGraphCommands::RegisterCommands()
{
	UI_COMMAND(ToggleNodesDisabled, "Disable Nodes", "Turn the selected nodes off, so they do nothing when the script runs (or back on, if they're all off). Disabled nodes are written between /* and */ in the text.",
		EUserInterfaceActionType::Button, FInputChord(EModifierKey::Control, EKeys::Slash));
}

#undef LOCTEXT_NAMESPACE
