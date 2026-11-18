#pragma once

#include "CoreMinimal.h"
#include "BangoProgram.h"

/**
 * Inputs signatures (spec section 15): the inputs a C++ class gives its inline scripts, so a script can use them
 * without writing an #inputs line. A class registers its signatures by name in its constructor, for its templates
 * only, and points its inline script properties at one in the same constructor:
 *
 *	ABangoTrigger::ABangoTrigger()
 *	{
 *		if (IsTemplate())
 *		{
 *			Bango::RegisterInputsSignature(this, TEXT("Activation"), {
 *				{ TEXT("entity _trigger"), TEXT("The trigger itself.") },
 *				{ TEXT("int _count"), TEXT("How many matching actors are inside.") },
 *			});
 *		}
 *		ActivatedScript.UseInputsSignature(this, TEXT("Activation"));
 *	}
 *
 * The script then can't have an #inputs line, and its owner passes the inputs' values in the signature's order when
 * it spawns or checks it.
 */
namespace Bango
{
	/**
	 * Registers a signature: Name's inputs for Template's class, or for everyone with a null Template (a global
	 * signature, used with UseInputsSignature(nullptr, Name)). Each input is written as in an #inputs line, without a
	 * default (`entity _who`), with an optional tooltip for the editor. Input names should start with '_': one that
	 * doesn't is reported as an error, but still works.
	 *
	 * Template must be a template (a class default object or an archetype: call it inside `if (IsTemplate())`): other
	 * objects are refused with an error, since every instance would register it again. Registering a Name again for the
	 * same class replaces it, so a subclass's constructor can replace its parent's. Safe on any thread.
	 */
	BANGOSCRIPTS_API void RegisterInputsSignature(const UObject* Template, FName Name, const TArray<FBangoInputDecl>& Inputs);

	/** Name's signature for Class (null: the global one), or null if none is registered. */
	BANGOSCRIPTS_API TSharedPtr<const FBangoInputsSignature> FindInputsSignature(const UClass* Class, FName Name);
}
