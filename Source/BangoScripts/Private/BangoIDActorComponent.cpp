#include "BangoIDActorComponent.h"

#include "BangoActorRegistry.h"
#include "BangoLog.h"
#include "GameFramework/Actor.h"

UBangoIDActorComponent::UBangoIDActorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UBangoIDActorComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterName();
}

void UBangoIDActorComponent::RegisterName()
{
	if (bTriedRegistering)
	{
		return;
	}
	bTriedRegistering = true;

	const FString OwnerName = GetOwner() ? GetOwner()->GetActorNameOrLabel() : TEXT("<none>");

	if (BangoName.IsEmpty() && AutomaticID.IsEmpty())
	{
		Bango::ReportError(FString::Printf(TEXT("Actor '%s' has a Bango ID component with no name."), *OwnerName));
		return;
	}

	UBangoActorRegistry* Registry = UBangoActorRegistry::Get(this);
	if (!Registry)
	{
		Bango::ReportError(FString::Printf(TEXT("Actor '%s': no Bango actor registry in this world, so its Bango ID was not registered."), *OwnerName));
		return;
	}

	// The automatic ID first, so a name given as well (which shouldn't happen) is the one scripts get from now on.
	for (const FString& Name : { AutomaticID, BangoName.ToString() })
	{
		if (Name.IsEmpty())
		{
			continue;
		}
		FString Error;
		if (!Registry->Register(Name, GetOwner(), Error))
		{
			Bango::ReportError(Error);
			continue;
		}
		RegisteredNames.Add(Name);
		bRegistered = true;
	}
}

void UBangoIDActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UBangoActorRegistry* Registry = UBangoActorRegistry::Get(this))
	{
		for (const FString& Name : RegisteredNames)
		{
			Registry->Unregister(Name, GetOwner());
		}
	}
	RegisteredNames.Reset();
	bRegistered = false;
	bTriedRegistering = false;

	Super::EndPlay(EndPlayReason);
}
