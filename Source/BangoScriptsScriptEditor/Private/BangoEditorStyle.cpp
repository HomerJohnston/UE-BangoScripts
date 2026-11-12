#include "BangoEditorStyle.h"

#include "BangoGraphLayout.h"
#include "SBangoGraphPin.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FBangoEditorStyle::StyleSet;

namespace BangoEditorStylePrivate
{
	static const FName StyleSetName(TEXT("BangoEditorStyle"));

	/** Type icons: brush name suffix and SVG file name (in Resources/Icons). */
	static const TCHAR* const TypeIcons[][2] =
	{
		{ TEXT("Bool"),   TEXT("TypeBool") },
		{ TEXT("Int"),    TEXT("TypeInt") },
		{ TEXT("Double"), TEXT("TypeDouble") },
		{ TEXT("String"), TEXT("TypeString") },
		{ TEXT("Text"),   TEXT("TypeText") },
		{ TEXT("Entity"), TEXT("TypeEntity") },
		{ TEXT("Array"),  TEXT("TypeArray") },
		{ TEXT("Any"),    TEXT("TypeAny") },
		{ TEXT("Variable"), TEXT("TypeVariable") },
		{ TEXT("Vector2"), TEXT("TypeVector2") },
		{ TEXT("Vector3"), TEXT("TypeVector3") },
	};

	// Tints, close to Blueprint's pin colours.
	static const FLinearColor BoolColor(0.85f, 0.20f, 0.20f);
	static const FLinearColor IntColor(0.12f, 0.85f, 0.65f);
	static const FLinearColor DoubleColor(0.55f, 0.95f, 0.25f);
	static const FLinearColor StringColor(0.95f, 0.25f, 0.75f);
	static const FLinearColor EntityColor(0.20f, 0.55f, 1.00f);
	static const FLinearColor TextColor(0.95f, 0.55f, 0.40f); // text: like a string, but warmer, so the two are told apart
	static const FLinearColor Vector3Color(1.00f, 0.78f, 0.15f); // Blueprint's Vector gold
	static const FLinearColor Vector2Color(0.85f, 0.70f, 0.30f); // a duller gold, so the two are told apart
	static const FLinearColor AnyColor(0.70f, 0.70f, 0.70f);
	static const FLinearColor NumberColor = FMath::Lerp(IntColor, DoubleColor, 0.5f); // an int or a float (a NumberParam)
}

void FBangoEditorStyle::Initialize()
{
	using namespace BangoEditorStylePrivate;
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(StyleSetName);
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BangoScripts"));
	StyleSet->SetContentRoot(Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources")) : FString());

	const FVector2D Icon12(12.0f, 12.0f);
	for (const auto& Icon : TypeIcons)
	{
		StyleSet->Set(FName(*FString::Printf(TEXT("Bango.Type.%s"), Icon[0])),
			new FSlateVectorImageBrush(StyleSet->RootToContentDir(FString(TEXT("Icons/")) + Icon[1], TEXT(".svg")), Icon12));
	}

	// The pin circles, at the size pins are drawn (a reroute's dot draws the brush at its own size, so this matters there).
	const FVector2D PinIconSize(SBangoGraphPin::PinSize, SBangoGraphPin::PinSize);
	StyleSet->Set("Bango.Pin.Connected", new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("Icons/PinConnected"), TEXT(".svg")), PinIconSize));
	StyleSet->Set("Bango.Pin.Disconnected", new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("Icons/PinDisconnected"), TEXT(".svg")), PinIconSize));
	StyleSet->Set("Bango.White", new FSlateColorBrush(FLinearColor::White));

	// A Bango script (the scripts' folder tree and tabs), from Resources/ClassIcons.
	StyleSet->Set(GetScriptIconName(), new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("ClassIcons/Icon_Script"), TEXT(".svg")), FVector2D(16.0f, 16.0f)));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FBangoEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
		StyleSet.Reset();
	}
}

const FSlateBrush* FBangoEditorStyle::GetTypeIcon(const FString& TypeHint, FLinearColor& OutTint)
{
	using namespace BangoEditorStylePrivate;
	OutTint = AnyColor;
	if (!StyleSet.IsValid() || TypeHint.IsEmpty())
	{
		return nullptr;
	}

	// "int[][]" -> base "int", array. "array" and "element" are what the array commands use.
	FString Base = TypeHint;
	bool bArray = false;
	while (Base.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive))
	{
		Base.LeftChopInline(2);
		bArray = true;
	}
	if (Base.Equals(TEXT("array"), ESearchCase::CaseSensitive))
	{
		return StyleSet->GetBrush("Bango.Type.Array");
	}

	const TCHAR* Icon = TEXT("Any");
	if (Base.Equals(TEXT("bool"), ESearchCase::CaseSensitive))        { Icon = TEXT("Bool");   OutTint = BoolColor; }
	else if (Base.Equals(TEXT("int"), ESearchCase::CaseSensitive))    { Icon = TEXT("Int");    OutTint = IntColor; }
	else if (Base.Equals(TEXT("float"), ESearchCase::CaseSensitive))  { Icon = TEXT("Double"); OutTint = DoubleColor; }
	else if (Base.Equals(TEXT("number"), ESearchCase::CaseSensitive)) { Icon = TEXT("Double"); OutTint = NumberColor; }
	else if (Base.Equals(TEXT("string"), ESearchCase::CaseSensitive)) { Icon = TEXT("String"); OutTint = StringColor; }
	else if (Base.Equals(TEXT("entity"), ESearchCase::CaseSensitive)) { Icon = TEXT("Entity"); OutTint = EntityColor; }
	else if (Base.Equals(TEXT("text"), ESearchCase::CaseSensitive))   { Icon = TEXT("Text");   OutTint = TextColor; }
	else if (Base.Equals(TEXT("vector2"), ESearchCase::CaseSensitive)) { Icon = TEXT("Vector2"); OutTint = Vector2Color; }
	else if (Base.Equals(TEXT("vector3"), ESearchCase::CaseSensitive)) { Icon = TEXT("Vector3"); OutTint = Vector3Color; }

	if (bArray)
	{
		Icon = TEXT("Array");
	}
	return StyleSet->GetBrush(FName(*FString::Printf(TEXT("Bango.Type.%s"), Icon)));
}

const FSlateBrush* FBangoEditorStyle::GetVariableIcon(const FString& VariableType, FLinearColor& OutTint)
{
	using namespace BangoEditorStylePrivate;
	OutTint = AnyColor;
	if (!StyleSet.IsValid())
	{
		return nullptr;
	}
	if (!VariableType.IsEmpty())
	{
		GetTypeIcon(VariableType, OutTint); // only for its colour
	}
	return StyleSet->GetBrush("Bango.Type.Variable");
}

const FSlateBrush* FBangoEditorStyle::GetFieldIcon(const FBangoNodeField& Field, FLinearColor& OutTint)
{
	if (Field.bHideIcon)
	{
		OutTint = FLinearColor::White;
		return nullptr;
	}
	return Field.bVariable ? GetVariableIcon(Field.VariableType, OutTint) : GetTypeIcon(Field.TypeHint, OutTint);
}

FLinearColor FBangoEditorStyle::GetWireColor()
{
	return FLinearColor(0.6f, 0.6f, 0.6f);
}

FLinearColor FBangoEditorStyle::GetDisconnectedPinColor()
{
	return FLinearColor(0.25f, 0.25f, 0.25f);
}

const FSlateBrush* FBangoEditorStyle::GetPinBrush(bool bConnected)
{
	if (!StyleSet.IsValid())
	{
		return FAppStyle::GetBrush(bConnected ? "Graph.Pin.Connected" : "Graph.Pin.Disconnected");
	}
	return StyleSet->GetBrush(bConnected ? "Bango.Pin.Connected" : "Bango.Pin.Disconnected");
}

const FSlateBrush* FBangoEditorStyle::GetScriptIcon()
{
	return StyleSet.IsValid() ? StyleSet->GetBrush(GetScriptIconName()) : FAppStyle::GetBrush("GraphEditor.EventGraph_16x");
}

FName FBangoEditorStyle::GetStyleSetName()
{
	return BangoEditorStylePrivate::StyleSetName;
}

FName FBangoEditorStyle::GetScriptIconName()
{
	return TEXT("Bango.ScriptIcon");
}

const FSlateBrush* FBangoEditorStyle::GetWhiteBrush()
{
	static const FSlateColorBrush Fallback(FLinearColor::White);
	return StyleSet.IsValid() ? StyleSet->GetBrush("Bango.White") : &Fallback;
}
