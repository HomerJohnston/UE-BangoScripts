#include "BangoNodeDecorator.h"

namespace BangoNodeDecoratorPrivate
{
	static TMap<FString, TSharedRef<IBangoNodeDecorator>>& GetDecorators()
	{
		static TMap<FString, TSharedRef<IBangoNodeDecorator>> Decorators;
		return Decorators;
	}
}

void Bango::RegisterNodeDecorator(const FString& CommandName, const TSharedRef<IBangoNodeDecorator>& Decorator)
{
	BangoNodeDecoratorPrivate::GetDecorators().Add(CommandName, Decorator);
}

void Bango::UnregisterNodeDecorator(const FString& CommandName)
{
	BangoNodeDecoratorPrivate::GetDecorators().Remove(CommandName);
}

TSharedPtr<IBangoNodeDecorator> Bango::FindNodeDecorator(const FString& CommandName)
{
	if (CommandName.IsEmpty())
	{
		return nullptr;
	}
	const TSharedRef<IBangoNodeDecorator>* Found = BangoNodeDecoratorPrivate::GetDecorators().Find(CommandName);
	return Found ? TSharedPtr<IBangoNodeDecorator>(*Found) : nullptr;
}
