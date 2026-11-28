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

const FName Bango::NodePins::In(TEXT("In"));
const FName Bango::NodePins::Then(TEXT("Then"));
const FName Bango::NodePins::Body(TEXT("Body"));
const FName Bango::NodePins::Default(TEXT("Default"));

FName Bango::NodePins::Entry(int32 Entry)
{
	return FName(*FString::Printf(TEXT("Entry%d"), Entry));
}

FName Bango::NodePins::Statement(int32 Entry, int32 Index)
{
	return FName(*FString::Printf(TEXT("Entry%d.Statement%d"), Entry, Index));
}
