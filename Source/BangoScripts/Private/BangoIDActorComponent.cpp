#include "BangoIDActorComponent.h"

#include "BangoActorRegistry.h"
#include "BangoLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UBangoIDActorComponent::UBangoIDActorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Registered in InitializeComponent, not BeginPlay: Unreal gives no order to actors' BeginPlays, so a script
	// started in one actor's could otherwise run before another actor's Bango ID was registered.
	bWantsInitializeComponent = true;
}

void UBangoIDActorComponent::InitializeComponent()
{
	Super::InitializeComponent();
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		RegisterName();
	}
}

void UBangoIDActorComponent::UninitializeComponent()
{
	UnregisterName(); // one that never began play (so never ends it), or was removed from its actor
	Super::UninitializeComponent();
}

void UBangoIDActorComponent::BeginPlay()
{
	Super::BeginPlay();
	RegisterName(); // already done, unless it wasn't initialized first
}

void UBangoIDActorComponent::RegisterName()
{
	if (bTriedRegistering)
	{
		return;
	}

	// No name is fine: the actor is still usable in scripts (by its Level or Automatic ID, made when first used).
	// Not counted as a try: it may be given an Automatic ID later, and that is registered then.
	if (BangoName.IsEmpty() && AutomaticID.IsEmpty())
	{
		return;
	}
	bTriedRegistering = true;

	const FString OwnerName = GetOwner() ? GetOwner()->GetActorNameOrLabel() : TEXT("<none>");

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
	UnregisterName();
	Super::EndPlay(EndPlayReason);
}

void UBangoIDActorComponent::UnregisterName()
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
}
