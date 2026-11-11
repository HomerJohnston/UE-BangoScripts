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

	const FString OwnerName = GetOwner() ? GetOwner()->GetName() : TEXT("<none>");

	if (BangoName.IsEmpty())
	{
		Bango::ReportError(FString::Printf(TEXT("Actor '%s' has a Bango ID component with no name."), *OwnerName));
		return;
	}

	UBangoActorRegistry* Registry = UBangoActorRegistry::Get(this);
	if (!Registry)
	{
		Bango::ReportError(FString::Printf(TEXT("Actor '%s': no Bango actor registry in this world, so '%s' was not registered."), *OwnerName, *BangoName));
		return;
	}

	FString Error;
	if (!Registry->Register(BangoName, GetOwner(), Error))
	{
		Bango::ReportError(Error);
		return;
	}

	bRegistered = true;
}

void UBangoIDActorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (UBangoActorRegistry* Registry = UBangoActorRegistry::Get(this))
		{
			Registry->Unregister(BangoName, GetOwner());
		}
		bRegistered = false;
	}

	Super::EndPlay(EndPlayReason);
}
