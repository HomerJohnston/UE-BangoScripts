#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"

/** The Bango graph editor's own commands (the generic ones, like Delete and Copy, come from FGenericCommands). */
class FBangoGraphCommands : public TCommands<FBangoGraphCommands>
{
public:
	FBangoGraphCommands();

	virtual void RegisterCommands() override;

	/**
	 * Disables the selected nodes, or enables them if they're all disabled already (Ctrl+/). A disabled node is
	 * written between slash-star and star-slash, and does nothing when the script runs (spec section 1).
	 */
	TSharedPtr<FUICommandInfo> ToggleNodesDisabled;
};
