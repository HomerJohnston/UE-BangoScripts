#include "BangoActorRegistry.h"

#include "BangoIDActorComponent.h"
#include "BangoTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UBangoActorRegistry* UBangoActorRegistry::Get(const UObject* WorldContext)
{
	if (!WorldContext || !GEngine)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<UBangoActorRegistry>() : nullptr;
}

bool UBangoActorRegistry::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UBangoActorRegistry::Register(const FString& Name, AActor* Actor, FString& OutError)
{
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Can't register Bango ID '%s': no actor."), *Name);
		return false;
	}

	if (!Bango::IsValidBangoIDName(Name))
	{
		OutError = FString::Printf(TEXT("Actor '%s' has an invalid Bango ID name '%s'. Use letters, digits and '_', not starting with a digit (scripts write it as @%s)."),
			*Actor->GetName(), *Name, *Name);
		return false;
	}

	if (const TWeakObjectPtr<AActor>* Existing = Actors.Find(Name))
	{
		const AActor* ExistingActor = Existing->Get();
		if (ExistingActor && ExistingActor != Actor)
		{
			OutError = FString::Printf(TEXT("Actor '%s' uses Bango ID name '%s', which is already used by actor '%s'."),
				*Actor->GetName(), *Name, *ExistingActor->GetName());
			return false;
		}
	}

	Actors.Add(Name, Actor);
	return true;
}

void UBangoActorRegistry::Unregister(const FString& Name, const AActor* Actor)
{
	if (const TWeakObjectPtr<AActor>* Existing = Actors.Find(Name))
	{
		// Also removes stale entries whose actor is already gone.
		if (!Existing->IsValid() || Existing->Get() == Actor)
		{
			Actors.Remove(Name);
		}
	}
}

AActor* UBangoActorRegistry::FindActor(const FString& Name) const
{
	const TWeakObjectPtr<AActor>* Found = Actors.Find(Name);
	return Found ? Found->Get() : nullptr;
}

namespace Bango
{
	bool ActorToEntity(const AActor* Actor, FBangoValue& OutValue, FString& OutError)
	{
		if (!Actor)
		{
			OutValue = FBangoValue::MakeNil(FBangoType::Entity());
			return true;
		}
		const UBangoIDActorComponent* Component = Actor->FindComponentByClass<UBangoIDActorComponent>();
		if (!Component)
		{
			OutError = FString::Printf(TEXT("Actor '%s' has no Bango ID component, so it can't be passed to a script as an entity."), *Actor->GetName());
			return false;
		}
		if (!Component->IsRegistered())
		{
			OutError = FString::Printf(TEXT("Actor '%s' has a Bango ID component, but its name '%s' isn't registered (it may be empty, invalid or used by another actor)."),
				*Actor->GetName(), *Component->BangoName);
			return false;
		}
		OutValue = FBangoValue::MakeEntity(Component->BangoName);
		return true;
	}
}
