#include "BangoCallChecks.h"

#include "BangoContainers.h"
#include "BangoGraphLayout.h"
#include "BangoParser.h"
#include "BangoReflectedCall.h"
#include "BangoScriptReferences.h"
#include "BangoTypes.h"
#include "GameFramework/Actor.h"

namespace BangoCallChecks
{
	const AActor* FindTarget(const FString& TargetEntity, const AActor* Self)
	{
		if (TargetEntity.IsEmpty() || Bango::IsPlayerEntityName(TargetEntity))
		{
			return nullptr;
		}
		if (TargetEntity.Equals(Bango::GetSelfEntityName(), ESearchCase::CaseSensitive))
		{
			return Self;
		}
		return BangoScriptReferences::FindLevelActor(TargetEntity);
	}

	bool GetTargetFunctions(const FString& TargetEntity, const AActor* Self, TArray<FBangoCallableFunction>& OutFunctions, FString& OutLabel)
	{
		const AActor* Actor = FindTarget(TargetEntity, Self);
		if (!Actor)
		{
			return false;
		}
		Bango::GetBangoCallableFunctions(Actor->GetClass(), OutFunctions);
		FString ClassName = Actor->GetClass()->GetName();
		ClassName.RemoveFromEnd(TEXT("_C"));
		const bool bSelf = TargetEntity.Equals(Bango::GetSelfEntityName(), ESearchCase::CaseSensitive);
		OutLabel = FString::Printf(TEXT("%s (%s)"), bSelf ? TEXT("self") : *(FString(TEXT("@")) + TargetEntity), *ClassName);
		return true;
	}

	bool CheckCalls(const FBangoProgram& Program, const AActor* Self, const FString& FileName, FBangoCompileError& OutError)
	{
		// Each target is looked up once, however many calls it has.
		TBangoNameMap<TPair<TArray<FBangoCallableFunction>, FString>> Known; // case-sensitive, as Bango IDs are
		TBangoNameMap<bool> Unknown;
		for (const FBangoReflectedCallSite& Site : Program.ReflectedCalls)
		{
			if (Site.TargetEntity.IsEmpty() || Unknown.Contains(Site.TargetEntity))
			{
				continue;
			}
			TPair<TArray<FBangoCallableFunction>, FString>* Target = Known.Find(Site.TargetEntity);
			if (!Target)
			{
				TPair<TArray<FBangoCallableFunction>, FString> Found;
				if (!GetTargetFunctions(Site.TargetEntity, Self, Found.Key, Found.Value))
				{
					Unknown.Add(Site.TargetEntity, true);
					continue;
				}
				Target = &Known.Add(Site.TargetEntity, MoveTemp(Found));
			}
			FString Message;
			if (!Bango::CheckReflectedCall(Site, Target->Key, Target->Value, Message))
			{
				OutError = FBangoCompileError();
				OutError.FileName = FileName;
				OutError.Line = Site.Line;
				OutError.Column = Site.Column;
				OutError.Message = Message;
				return false;
			}
		}
		return true;
	}

	void SetUpLayoutContext(FBangoNodeLayoutContext& Context, TFunction<const AActor*()> GetSelf)
	{
		Context.FindCallTarget = [GetSelf](const FString& TargetEntity, TArray<FBangoCallableFunction>& OutFunctions, FString& OutLabel)
		{
			return GetTargetFunctions(TargetEntity, GetSelf ? GetSelf() : nullptr, OutFunctions, OutLabel);
		};
	}
}
