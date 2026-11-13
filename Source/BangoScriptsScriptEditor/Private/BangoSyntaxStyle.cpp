// Bango text editor: one text style per token kind.
// See "Bango Text Editor Spec", section 3.

#include "BangoSyntaxStyle.h"

#include "Styling/CoreStyle.h"

namespace BangoSyntaxStylePrivate
{
	struct FKindStyle
	{
		const TCHAR* Hex;
		bool bBold;
		bool bItalic;
	};

	// The default palette, from the spec. Later this comes from settings.
	static FKindStyle GetDefaultKindStyle(EBangoSyntaxKind Kind)
	{
		switch (Kind)
		{
		case EBangoSyntaxKind::Keyword:            return { TEXT("C792EA"), true,  false };
		case EBangoSyntaxKind::JumpKeyword:        return { TEXT("F78C6C"), true,  false };
		case EBangoSyntaxKind::LabelKeyword:       return { TEXT("B48EF0"), true,  false };
		case EBangoSyntaxKind::LabelName:          return { TEXT("B48EF0"), false, false };
		case EBangoSyntaxKind::DeclKeyword:        return { TEXT("82AAFF"), true,  false };
		case EBangoSyntaxKind::Type:               return { TEXT("4EC9B0"), false, false };
		case EBangoSyntaxKind::Constant:           return { TEXT("569CD6"), false, false };
		case EBangoSyntaxKind::Nil:                return { TEXT("569CD6"), false, true  };
		case EBangoSyntaxKind::Preprocessor:       return { TEXT("A0A0A0"), true,  false };
		case EBangoSyntaxKind::Local:              return { TEXT("9CDCFE"), false, false };
		case EBangoSyntaxKind::Global:             return { TEXT("FFCB6B"), true,  false };
		case EBangoSyntaxKind::Entity:             return { TEXT("FF9CAC"), false, false };
		case EBangoSyntaxKind::Number:             return { TEXT("B5CEA8"), false, false };
		case EBangoSyntaxKind::String:             return { TEXT("CE9178"), false, false };
		case EBangoSyntaxKind::StringEscape:       return { TEXT("D7BA7D"), false, false };
		case EBangoSyntaxKind::Placeholder:        return { TEXT("D7BA7D"), true,  false };
		case EBangoSyntaxKind::LocMacro:           return { TEXT("DCDCAA"), false, false };
		case EBangoSyntaxKind::LocKey:             return { TEXT("808080"), false, false };
		case EBangoSyntaxKind::LocNotes:           return { TEXT("6A9955"), false, true  };
		case EBangoSyntaxKind::VectorPart:         return { TEXT("89DDFF"), false, false };
		case EBangoSyntaxKind::Call:               return { TEXT("DCDCAA"), false, false };
		case EBangoSyntaxKind::Builtin:            return { TEXT("4FC1FF"), false, false };
		case EBangoSyntaxKind::Comment:            return { TEXT("6A9955"), false, true  };
		case EBangoSyntaxKind::Metadata:           return { TEXT("3A3A3A"), false, false };
		case EBangoSyntaxKind::DisconnectedMarker: return { TEXT("6A9955"), false, false };
		case EBangoSyntaxKind::DisabledMarker:     return { TEXT("6A9955"), true,  false };
		case EBangoSyntaxKind::Structure:          return { TEXT("808080"), false, false };
		case EBangoSyntaxKind::Operator:           return { TEXT("D4D4D4"), false, false };
		case EBangoSyntaxKind::Punctuation:        return { TEXT("A0A0A0"), false, false };
		case EBangoSyntaxKind::Error:              return { TEXT("F44747"), false, false };
		case EBangoSyntaxKind::Command:            return { TEXT("61AFEF"), false, false };
		case EBangoSyntaxKind::Function:           return { TEXT("8FB8DE"), false, false };
		case EBangoSyntaxKind::Parameter:          return { TEXT("9CDCFE"), false, true  };
		case EBangoSyntaxKind::UndeclaredGlobal:   return { TEXT("FFCB6B"), true,  false };
		case EBangoSyntaxKind::Default:
		default:                                  return { TEXT("D4D4D4"), false, false };
		}
	}

	static FName GetTypefaceName(bool bBold, bool bItalic)
	{
		if (bBold && bItalic) { return TEXT("BoldItalic"); }
		if (bBold)            { return TEXT("Bold"); }
		if (bItalic)          { return TEXT("Italic"); }
		return TEXT("Regular");
	}
}

FBangoSyntaxStyle::FBangoSyntaxStyle(float FontSize, const UObject* FontObject)
	: BackgroundColor(FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("1E1E1E"))))
{
	using namespace BangoSyntaxStylePrivate;

	const FTextBlockStyle& Base = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

	for (int32 KindIndex = 0; KindIndex < NumKinds; ++KindIndex)
	{
		const FKindStyle KindStyle = GetDefaultKindStyle(static_cast<EBangoSyntaxKind>(KindIndex));
		const FLinearColor Colour = FLinearColor::FromSRGBColor(FColor::FromHex(KindStyle.Hex));

		for (int32 Variant = 0; Variant < 4; ++Variant)
		{
			// Disconnected code is faded and in italics, whatever its kind. Disabled code (between the markers) is
			// faded more, but not italic, so the two can be told apart; code that's both is both.
			const bool bDisconnectedVariant = (Variant & 1) != 0;
			const bool bDisabledVariant = (Variant & 2) != 0;
			const bool bItalic = KindStyle.bItalic || bDisconnectedVariant;

			FSlateFontInfo Font = FontObject != nullptr
				? FSlateFontInfo(FontObject, FontSize, GetTypefaceName(KindStyle.bBold, bItalic))
				: FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), FontSize);

			FLinearColor RunColour = Colour;
			if (bDisconnectedVariant)
			{
				RunColour.A *= 0.5f;
			}
			if (bDisabledVariant)
			{
				RunColour.A *= 0.4f;
			}

			Styles[KindIndex][Variant] = FTextBlockStyle(Base)
				.SetFont(Font)
				.SetColorAndOpacity(FSlateColor(RunColour));
		}
	}
}

const FTextBlockStyle& FBangoSyntaxStyle::GetStyle(EBangoSyntaxKind Kind, bool bDisconnected, bool bDisabled) const
{
	const int32 KindIndex = FMath::Clamp(static_cast<int32>(Kind), 0, NumKinds - 1);
	return Styles[KindIndex][(bDisconnected ? 1 : 0) | (bDisabled ? 2 : 0)];
}
