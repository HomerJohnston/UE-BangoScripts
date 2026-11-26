#include "BangoInlineScriptIdValidator.h"

#include "BangoInlineScript.h"
#include "DataValidationFixers.h"
#include "Engine/Blueprint.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/Level.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/DataValidation.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "BangoInlineScriptIdValidator"

namespace BangoInlineScriptIdValidatorPrivate
{
	/** "Trigger.OnEnter" for a component's script, "OnEnter" for the owner's own. */
	static FString DescribeWhere(const UObject* Object, const FString& Path, const TArray<UObject*>& Unit)
	{
		if (!Object || Unit.Num() == 0 || Object == Unit[0])
		{
			return Path;
		}
		FString Name = Object->GetName();
		Name.RemoveFromEnd(TEXT("_GEN_VARIABLE")); // a Blueprint's component template
		return FString::Printf(TEXT("%s.%s"), *Name, *Path);
	}
}

bool UBangoInlineScriptIdValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const
{
	return InObject != nullptr && !InObject->IsA<UClass>();
}

EDataValidationResult UBangoInlineScriptIdValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context)
{
	if (UBlueprint* Blueprint = Cast<UBlueprint>(InAsset))
	{
		// Its default object and its components' templates: one owner, as each object made from it is.
		TArray<UObject*> Unit;
		if (Blueprint->GeneratedClass)
		{
			Unit.Add(Blueprint->GeneratedClass->GetDefaultObject());
		}
		if (Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate)
				{
					Unit.Add(Node->ComponentTemplate);
				}
			}
		}
		if (UInheritableComponentHandler* Inherited = Blueprint->GetInheritableComponentHandler(false))
		{
			TArray<UActorComponent*> Templates;
			Inherited->GetAllTemplates(Templates);
			for (UActorComponent* Template : Templates)
			{
				if (Template)
				{
					Unit.Add(Template);
				}
			}
		}
		ValidateUnit(InAssetData, Unit, Blueprint->GetName(), Blueprint);
	}
	else if (UWorld* World = Cast<UWorld>(InAsset))
	{
		// Each actor kept in the level's own package (one kept in a package of its own is validated with that).
		if (World->PersistentLevel)
		{
			for (AActor* Actor : World->PersistentLevel->Actors)
			{
				if (IsValid(Actor) && !Actor->IsPackageExternal())
				{
					ValidateUnit(InAssetData, Bango::GetInlineScriptIdUnit(Actor), Actor->GetActorNameOrLabel(), nullptr);
				}
			}
		}
	}
	else if (AActor* Actor = Cast<AActor>(InAsset))
	{
		ValidateUnit(InAssetData, Bango::GetInlineScriptIdUnit(Actor), Actor->GetActorNameOrLabel(), nullptr);
	}
	else
	{
		ValidateUnit(InAssetData, { InAsset }, InAsset->GetName(), nullptr);
	}

	if (GetValidationResult() != EDataValidationResult::Invalid)
	{
		AssetPasses(InAsset);
	}
	return GetValidationResult();
}

void UBangoInlineScriptIdValidator::ValidateUnit(const FAssetData& AssetData, const TArray<UObject*>& Unit, const FString& OwnerName, UBlueprint* Blueprint)
{
	using namespace BangoInlineScriptIdValidatorPrivate;
	for (const Bango::FBangoInlineScriptIdProblem& Problem : Bango::FindInlineScriptIdProblems(Unit, true))
	{
		const FString Where = DescribeWhere(Problem.Object, Problem.Path, Unit);
		const FText Message = Problem.bMissing
			? FText::Format(LOCTEXT("Missing", "Bango: the inline script '{0}' on '{1}' has no ID, so its persistent variables would be kept for its text, and start again whenever it's edited. Give it one with the fix (or edit it once)."),
				FText::FromString(Where), FText::FromString(OwnerName))
			: FText::Format(LOCTEXT("Duplicate", "Bango: the inline scripts '{0}' and '{1}' on '{2}' have the same ID (one was copied from the other), so they'd share their persistent variables. Give '{1}' a new one with the fix."),
				FText::FromString(DescribeWhere(Problem.OtherObject, Problem.OtherPath, Unit)), FText::FromString(Where), FText::FromString(OwnerName));
		TSharedRef<FTokenizedMessage> Token = AssetMessage(AssetData, EMessageSeverity::Error, Message);

		// The fix: a new ID for this one script, found again by where it is (the object may have changed since).
		TWeakObjectPtr<UObject> WeakObject = Problem.Object;
		TWeakObjectPtr<UBlueprint> WeakBlueprint = Blueprint;
		const FString Path = Problem.Path;
		TSharedRef<UE::DataValidation::IFixer> Fixer = UE::DataValidation::MakeFix([WeakObject, WeakBlueprint, Path]()
		{
			UObject* Object = WeakObject.Get();
			FBangoInlineScriptBase* Script = Object ? Bango::FindInlineScriptAt(Object, Path) : nullptr;
			if (!Script)
			{
				return FFixResult::Failure(FText::Format(LOCTEXT("FixGone", "Couldn't give '{0}' an ID: it isn't there any more."), FText::FromString(Path)));
			}
			const FScopedTransaction Transaction(LOCTEXT("FixTransaction", "Give Bango Inline Script an ID"));
			Object->Modify();
			Script->ScriptId = FGuid::NewGuid();
			Object->MarkPackageDirty();
			if (UBlueprint* FixedBlueprint = WeakBlueprint.Get())
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(FixedBlueprint);
			}
			return FFixResult::Success(FText::Format(LOCTEXT("Fixed", "'{0}' has a new ID. Save to keep it."), FText::FromString(Path)));
		});
		Token->AddToken(Fixer->CreateToken(LOCTEXT("FixLabel", "Give it a new ID")));
	}
}

#undef LOCTEXT_NAMESPACE
