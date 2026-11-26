#pragma once

#include "CoreMinimal.h"
#include "BangoInlineScript.h"
#include "PropertyHandle.h"

namespace Bango
{
	/**
	 * Gives each of the inline scripts StructHandle edits that has no ID (FBangoInlineScriptBase::ScriptId) a new
	 * one, and leaves the others theirs: call it in the same transaction as setting their text. An ID once given is
	 * never changed here, since a script's persistent variables are kept under it.
	 */
	inline void GiveInlineScriptIds(const TSharedPtr<IPropertyHandle>& StructHandle)
	{
		if (!StructHandle.IsValid() || !StructHandle->IsValidHandle())
		{
			return;
		}
		const TSharedPtr<IPropertyHandle> IdHandle = StructHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBangoInlineScriptBase, ScriptId));
		if (!IdHandle.IsValid() || !IdHandle->IsValidHandle())
		{
			return;
		}
		TArray<void*> RawData;
		StructHandle->AccessRawData(RawData);
		TArray<FString> Ids;
		bool bAnyMissing = false;
		for (void* Data : RawData)
		{
			const FBangoInlineScriptBase* Script = static_cast<const FBangoInlineScriptBase*>(Data);
			if (Script && Script->ScriptId.IsValid())
			{
				Ids.Add(Script->ScriptId.ToString(EGuidFormats::Digits));
			}
			else
			{
				Ids.Add(FGuid::NewGuid().ToString(EGuidFormats::Digits));
				bAnyMissing = true;
			}
		}
		if (bAnyMissing)
		{
			IdHandle->SetPerObjectValues(Ids);
		}
	}

	/** GiveInlineScriptIds, for a script changed straight in its object (no details panel). */
	inline void GiveInlineScriptId(FBangoInlineScriptBase& Script)
	{
		if (!Script.ScriptId.IsValid())
		{
			Script.ScriptId = FGuid::NewGuid();
		}
	}
}
