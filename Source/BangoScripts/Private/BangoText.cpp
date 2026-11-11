#include "BangoText.h"

#include "BangoTypes.h"
#include "Internationalization/Text.h"
#include "Misc/Guid.h"

namespace Bango
{
	FText MakeLocalizedText(const FString& Namespace, const FString& Key, const FString& Source, const FString& Notes)
	{
		return FText::AsLocalizable_Advanced(FTextKey(Namespace), FTextKey(Key), FString(Source), FStringView(Notes));
	}

	FText MakeInvariantText(const FString& String)
	{
		return FText::AsCultureInvariant(FString(String));
	}

	bool FormatLocalizedText(const FText& Pattern, TConstArrayView<FBangoValue> Args, FText& OutText, FString& OutError)
	{
		FString Named;
		const int32 Needed = CountTextFormatValues(Pattern.ToString(), &Named);
		if (Needed < 0)
		{
			OutError = FString::Printf(TEXT("The text uses {%s}. Text placeholders are numbered: {0}, {1}, ..."), *Named);
			return false;
		}
		if (Needed > Args.Num())
		{
			OutError = FString::Printf(TEXT("The text uses {%d}, but only %d value(s) were given."), Needed - 1, Args.Num());
			return false;
		}

		FFormatOrderedArguments Arguments;
		Arguments.Reserve(Args.Num());
		for (const FBangoValue& Arg : Args)
		{
			if (!Arg.IsNil() && !Arg.GetType().IsArray())
			{
				switch (Arg.GetType().Base)
				{
				case EBangoBaseType::Int:    Arguments.Add(FFormatArgumentValue(Arg.GetInt())); continue;
				case EBangoBaseType::Double: Arguments.Add(FFormatArgumentValue(Arg.GetDouble())); continue;
				case EBangoBaseType::Text:   Arguments.Add(FFormatArgumentValue(Arg.GetText())); continue;
				default: break;
				}
			}
			Arguments.Add(FFormatArgumentValue(MakeInvariantText(Arg.ToDisplayString())));
		}
		OutText = FText::Format(FTextFormat(Pattern), MoveTemp(Arguments));
		return true;
	}

	FString MakeTextKey()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Digits);
	}
}
