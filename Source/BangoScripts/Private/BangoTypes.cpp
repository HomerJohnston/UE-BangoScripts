#include "BangoTypes.h"

#include "Serialization/Archive.h"

// ---------------------------------------------------------------------------
// FBangoType
// ---------------------------------------------------------------------------

namespace
{
	const TCHAR* BaseTypeName(EBangoBaseType Base)
	{
		switch (Base)
		{
		case EBangoBaseType::Bool:   return TEXT("bool");
		case EBangoBaseType::Int:    return TEXT("int");
		case EBangoBaseType::Double: return TEXT("float"); // a 64-bit double underneath
		case EBangoBaseType::String: return TEXT("string");
		case EBangoBaseType::Entity: return TEXT("entity");
		case EBangoBaseType::Text:   return TEXT("text");
		case EBangoBaseType::Vector2: return TEXT("vector2");
		case EBangoBaseType::Vector3: return TEXT("vector3");
		default:                     return TEXT("<invalid>");
		}
	}
}

FBangoType FBangoType::GetElementType() const
{
	check(IsArray());
	return FBangoType(Base, (uint8)(ArrayDepth - 1));
}

FBangoType FBangoType::MakeArrayType() const
{
	check(ArrayDepth < MaxArrayDepth);
	return FBangoType(Base, (uint8)(ArrayDepth + 1));
}

bool FBangoType::IsAssignableFrom(const FBangoType& Source) const
{
	if (*this == Source)
	{
		return true;
	}
	return ArrayDepth == Source.ArrayDepth
		&& Base == EBangoBaseType::Double
		&& Source.Base == EBangoBaseType::Int;
}

FString FBangoType::ToString() const
{
	FString Result = BaseTypeName(Base);
	for (uint8 i = 0; i < ArrayDepth; ++i)
	{
		Result += TEXT("[]");
	}
	return Result;
}

bool FBangoType::FromString(const FString& Text, FBangoType& OutType)
{
	FString BaseText = Text;
	int32 Depth = 0;
	while (BaseText.EndsWith(TEXT("[]"), ESearchCase::CaseSensitive))
	{
		BaseText.LeftChopInline(2);
		++Depth;
	}
	if (Depth > MaxArrayDepth)
	{
		return false;
	}

	for (uint8 i = 0; i < (uint8)EBangoBaseType::Count; ++i)
	{
		const EBangoBaseType Base = (EBangoBaseType)i;
		if (BaseText.Equals(BaseTypeName(Base), ESearchCase::CaseSensitive))
		{
			OutType = FBangoType(Base, (uint8)Depth);
			return true;
		}
	}
	return false;
}

void FBangoType::Serialize(FArchive& Ar)
{
	uint8 BaseByte = (uint8)Base;
	Ar << BaseByte;
	Ar << ArrayDepth;

	if (Ar.IsLoading())
	{
		if (BaseByte >= (uint8)EBangoBaseType::Count || ArrayDepth > MaxArrayDepth)
		{
			Ar.SetError();
			BaseByte = (uint8)EBangoBaseType::Int;
			ArrayDepth = 0;
		}
		Base = (EBangoBaseType)BaseByte;
	}
}

// ---------------------------------------------------------------------------
// FBangoValue: construction and access
// ---------------------------------------------------------------------------

FBangoValue FBangoValue::MakeNil(const FBangoType& InType)
{
	FBangoValue V;
	V.Type = InType;
	V.bIsNil = true;
	return V;
}

FBangoValue FBangoValue::MakeBool(bool Value)
{
	FBangoValue V;
	V.Type = FBangoType::Bool();
	V.bIsNil = false;
	V.BoolValue = Value;
	return V;
}

FBangoValue FBangoValue::MakeInt(int32 Value)
{
	FBangoValue V;
	V.Type = FBangoType::Int();
	V.bIsNil = false;
	V.IntValue = Value;
	return V;
}

FBangoValue FBangoValue::MakeDouble(double Value)
{
	FBangoValue V;
	V.Type = FBangoType::Double();
	V.bIsNil = false;
	V.DoubleValue = Value;
	return V;
}

FBangoValue FBangoValue::MakeString(const FString& Value)
{
	FBangoValue V;
	V.Type = FBangoType::String();
	V.bIsNil = false;
	V.StringValue = Value;
	return V;
}

FBangoValue FBangoValue::MakeEntity(const FString& Name)
{
	FBangoValue V;
	V.Type = FBangoType::Entity();
	V.bIsNil = false;
	V.StringValue = Name;
	return V;
}

FBangoValue FBangoValue::MakeText(const FText& Value)
{
	FBangoValue V;
	V.Type = FBangoType::Text();
	V.bIsNil = false;
	V.TextValue = Value;
	return V;
}

FBangoValue FBangoValue::MakeVector2(double X, double Y)
{
	FBangoValue V;
	V.Type = FBangoType::Vector2();
	V.bIsNil = false;
	V.VectorValue[0] = X;
	V.VectorValue[1] = Y;
	return V;
}

FBangoValue FBangoValue::MakeVector3(double X, double Y, double Z)
{
	FBangoValue V;
	V.Type = FBangoType::Vector3();
	V.bIsNil = false;
	V.VectorValue[0] = X;
	V.VectorValue[1] = Y;
	V.VectorValue[2] = Z;
	return V;
}

FBangoValue FBangoValue::MakeArray(const FBangoType& ElementType, const TArray<FBangoValue>& Elements)
{
	FBangoValue V;
	V.Type = ElementType.MakeArrayType();
	V.bIsNil = false;
	V.ArrayValue.Reserve(Elements.Num());

	for (const FBangoValue& Element : Elements)
	{
		FBangoValue Converted;
		verifyf(Element.TryConvertTo(ElementType, Converted), TEXT("MakeArray: element of type %s does not fit %s"), *Element.GetType().ToString(), *V.Type.ToString());
		V.ArrayValue.Add(MoveTemp(Converted));
	}
	return V;
}

bool FBangoValue::GetBool() const
{
	check(!bIsNil && Type == FBangoType::Bool());
	return BoolValue;
}

int32 FBangoValue::GetInt() const
{
	check(!bIsNil && Type == FBangoType::Int());
	return IntValue;
}

double FBangoValue::GetDouble() const
{
	check(!bIsNil && Type == FBangoType::Double());
	return DoubleValue;
}

const FString& FBangoValue::GetString() const
{
	check(!bIsNil && Type == FBangoType::String());
	return StringValue;
}

const FString& FBangoValue::GetEntityName() const
{
	check(!bIsNil && Type == FBangoType::Entity());
	return StringValue;
}

const FText& FBangoValue::GetText() const
{
	check(!bIsNil && Type == FBangoType::Text());
	return TextValue;
}

double FBangoValue::GetNumberAsDouble() const
{
	check(!bIsNil && !Type.IsArray());
	if (Type.Base == EBangoBaseType::Int)
	{
		return (double)IntValue;
	}
	check(Type.Base == EBangoBaseType::Double);
	return DoubleValue;
}

const TArray<FBangoValue>& FBangoValue::GetArray() const
{
	check(!bIsNil && Type.IsArray());
	return ArrayValue;
}

double FBangoValue::GetVectorPart(int32 Index) const
{
	check(!bIsNil && Index >= 0 && Index < Type.GetVectorSize());
	return VectorValue[Index];
}

void FBangoValue::SetVectorPart(int32 Index, double Value)
{
	check(!bIsNil && Index >= 0 && Index < Type.GetVectorSize());
	VectorValue[Index] = Value;
}

// ---------------------------------------------------------------------------
// FBangoValue: arrays
// ---------------------------------------------------------------------------

bool FBangoValue::CheckIsArray(const TCHAR* Operation, FString& OutError) const
{
	if (!Type.IsArray())
	{
		OutError = FString::Printf(TEXT("%s needs an array, but got %s."), Operation, *Type.ToString());
		return false;
	}
	if (bIsNil)
	{
		OutError = FString::Printf(TEXT("%s on an array that is nil."), Operation);
		return false;
	}
	return true;
}

bool FBangoValue::CheckWriteIndex(int64 Index, const TCHAR* Operation, FString& OutError)
{
	if (Index < 0)
	{
		OutError = FString::Printf(TEXT("%s: array index %lld is negative."), Operation, Index);
		return false;
	}
	if (Index >= MaxArraySize)
	{
		OutError = FString::Printf(TEXT("%s: array index %lld is past the limit of %lld elements."), Operation, Index, MaxArraySize);
		return false;
	}
	return true;
}

FBangoValue& FBangoValue::GrowToIndex(int64 Index)
{
	check(!bIsNil && Type.IsArray() && Index >= 0 && Index < MaxArraySize);
	if (ArrayValue.Num() <= Index)
	{
		const FBangoValue NilElement = MakeNil(Type.GetElementType());
		ArrayValue.Reserve((int32)Index + 1);
		while (ArrayValue.Num() <= Index)
		{
			ArrayValue.Add(NilElement);
		}
	}
	return ArrayValue[(int32)Index];
}

bool FBangoValue::PrepareElement(const FBangoValue& Element, FBangoValue& OutConverted, FString& OutError) const
{
	if (!Element.TryConvertTo(Type.GetElementType(), OutConverted))
	{
		OutError = FString::Printf(TEXT("Can't put a %s into a %s."), *Element.GetType().ToString(), *Type.ToString());
		return false;
	}
	return true;
}

int64 FBangoValue::ArrayCount() const
{
	check(!bIsNil && Type.IsArray());
	return ArrayValue.Num();
}

const FBangoValue* FBangoValue::GetElement(int64 Index) const
{
	check(!bIsNil && Type.IsArray());
	if (Index < 0 || Index >= ArrayValue.Num())
	{
		return nullptr;
	}
	return &ArrayValue[(int32)Index];
}

bool FBangoValue::SetElement(int64 Index, const FBangoValue& Element, FString& OutError)
{
	if (!CheckIsArray(TEXT("Setting an array element"), OutError)
		|| !CheckWriteIndex(Index, TEXT("Setting an array element"), OutError))
	{
		return false;
	}

	FBangoValue Converted;
	if (!PrepareElement(Element, Converted, OutError))
	{
		return false;
	}

	GrowToIndex(Index) = MoveTemp(Converted);
	return true;
}

bool FBangoValue::Push(const FBangoValue& Element, FString& OutError)
{
	if (!CheckIsArray(TEXT("Push"), OutError))
	{
		return false;
	}
	if (ArrayValue.Num() >= MaxArraySize)
	{
		OutError = FString::Printf(TEXT("Push: array is at the limit of %lld elements."), MaxArraySize);
		return false;
	}

	FBangoValue Converted;
	if (!PrepareElement(Element, Converted, OutError))
	{
		return false;
	}
	ArrayValue.Add(MoveTemp(Converted));
	return true;
}

bool FBangoValue::InsertAt(int64 Index, const FBangoValue& Element, FString& OutError)
{
	if (!CheckIsArray(TEXT("InsertAt"), OutError))
	{
		return false;
	}
	if (!CheckWriteIndex(Index, TEXT("InsertAt"), OutError))
	{
		return false;
	}
	if (ArrayValue.Num() >= MaxArraySize)
	{
		OutError = FString::Printf(TEXT("InsertAt: array would pass the limit of %lld elements."), MaxArraySize);
		return false;
	}

	FBangoValue Converted;
	if (!PrepareElement(Element, Converted, OutError))
	{
		return false;
	}

	if (Index >= ArrayValue.Num())
	{
		// Past the end: fill the gap with nil, then the element lands at Index.
		const FBangoValue NilElement = MakeNil(Type.GetElementType());
		while (ArrayValue.Num() < Index)
		{
			ArrayValue.Add(NilElement);
		}
		ArrayValue.Add(MoveTemp(Converted));
	}
	else
	{
		ArrayValue.Insert(MoveTemp(Converted), (int32)Index);
	}
	return true;
}

bool FBangoValue::Pop(FBangoValue& OutElement, FString& OutError)
{
	if (!CheckIsArray(TEXT("Pop"), OutError))
	{
		return false;
	}
	if (ArrayValue.Num() == 0)
	{
		OutError = TEXT("Pop on an empty array.");
		return false;
	}
	OutElement = ArrayValue.Pop();
	return true;
}

bool FBangoValue::RemoveAt(int64 Index, FBangoValue& OutElement, FString& OutError)
{
	if (!CheckIsArray(TEXT("RemoveAt"), OutError))
	{
		return false;
	}
	if (Index < 0 || Index >= ArrayValue.Num())
	{
		OutError = FString::Printf(TEXT("RemoveAt: index %lld is out of range (array has %d elements)."), Index, ArrayValue.Num());
		return false;
	}
	OutElement = MoveTemp(ArrayValue[(int32)Index]);
	ArrayValue.RemoveAt((int32)Index);
	return true;
}

// ---------------------------------------------------------------------------
// FBangoValue: nested access
// ---------------------------------------------------------------------------

namespace
{
	FString PathToString(TConstArrayView<int64> Indices, int32 Count)
	{
		FString Result;
		for (int32 i = 0; i < Count; ++i)
		{
			Result += FString::Printf(TEXT("[%lld]"), Indices[i]);
		}
		return Result;
	}
}

const FBangoValue* FBangoValue::GetElementAtPath(TConstArrayView<int64> Indices) const
{
	if (Indices.Num() == 0 || Indices.Num() > Type.ArrayDepth || bIsNil)
	{
		return nullptr;
	}

	const FBangoValue* Current = this;
	for (int32 Level = 0; Level < Indices.Num(); ++Level)
	{
		if (Current->bIsNil)
		{
			return nullptr;
		}
		Current = Current->GetElement(Indices[Level]);
		if (!Current)
		{
			return nullptr;
		}
	}
	return Current;
}

FBangoValue* FBangoValue::FindElementAtPath(TConstArrayView<int64> Indices, FString& OutError)
{
	if (!CheckIsArray(TEXT("Indexing"), OutError))
	{
		return nullptr;
	}
	if (Indices.Num() == 0 || Indices.Num() > Type.ArrayDepth)
	{
		OutError = FString::Printf(TEXT("%d indices used on a %s."), Indices.Num(), *Type.ToString());
		return nullptr;
	}

	FBangoValue* Current = this;
	for (int32 Level = 0; Level < Indices.Num(); ++Level)
	{
		if (Current->bIsNil)
		{
			OutError = FString::Printf(TEXT("%s is nil."), *PathToString(Indices, Level));
			return nullptr;
		}
		const int64 Index = Indices[Level];
		if (Index < 0 || Index >= Current->ArrayValue.Num())
		{
			OutError = FString::Printf(TEXT("Index %s is out of range (that array has %d elements)."),
				*PathToString(Indices, Level + 1), Current->ArrayValue.Num());
			return nullptr;
		}
		Current = &Current->ArrayValue[(int32)Index];
	}
	return Current;
}

bool FBangoValue::SetElementAtPath(TConstArrayView<int64> Indices, const FBangoValue& Element, FString& OutError)
{
	if (!CheckIsArray(TEXT("Setting an array element"), OutError))
	{
		return false;
	}
	if (Indices.Num() == 0 || Indices.Num() > Type.ArrayDepth)
	{
		OutError = FString::Printf(TEXT("%d indices used on a %s."), Indices.Num(), *Type.ToString());
		return false;
	}

	// Check everything first, so a failed write changes nothing.
	for (const int64 Index : Indices)
	{
		if (!CheckWriteIndex(Index, TEXT("Setting an array element"), OutError))
		{
			return false;
		}
	}
	const FBangoType TargetType(Type.Base, (uint8)(Type.ArrayDepth - Indices.Num()));
	FBangoValue Converted;
	if (!Element.TryConvertTo(TargetType, Converted))
	{
		OutError = FString::Printf(TEXT("Can't put a %s into %s, which holds %s."),
			*Element.GetType().ToString(), *PathToString(Indices, Indices.Num()), *TargetType.ToString());
		return false;
	}

	// Walk down, growing each level and creating missing inner arrays.
	FBangoValue* Current = this;
	for (int32 Level = 0; Level < Indices.Num() - 1; ++Level)
	{
		FBangoValue& Slot = Current->GrowToIndex(Indices[Level]);
		if (Slot.bIsNil)
		{
			Slot = MakeArray(Current->Type.GetElementType().GetElementType());
		}
		Current = &Slot;
	}
	Current->GrowToIndex(Indices.Last()) = MoveTemp(Converted);
	return true;
}

// ---------------------------------------------------------------------------
// FBangoValue: conversion and utilities
// ---------------------------------------------------------------------------

bool FBangoValue::TryConvertTo(const FBangoType& Target, FBangoValue& OutValue) const
{
	if (Type == Target)
	{
		OutValue = *this;
		return true;
	}
	if (!Target.IsAssignableFrom(Type))
	{
		return false;
	}

	// Only int -> double at the same depth (int[][] -> double[][]) gets here.
	if (bIsNil)
	{
		OutValue = MakeNil(Target);
		return true;
	}
	if (!Type.IsArray())
	{
		OutValue = MakeDouble((double)IntValue);
		return true;
	}

	const FBangoType TargetElement = Target.GetElementType();
	FBangoValue Result = MakeArray(TargetElement);
	Result.ArrayValue.Reserve(ArrayValue.Num());
	for (const FBangoValue& Element : ArrayValue)
	{
		FBangoValue Converted;
		verify(Element.TryConvertTo(TargetElement, Converted));
		Result.ArrayValue.Add(MoveTemp(Converted));
	}
	OutValue = MoveTemp(Result);
	return true;
}

FString FBangoValue::ToDebugString() const
{
	if (bIsNil)
	{
		return TEXT("nil");
	}
	if (Type.IsArray())
	{
		TArray<FString> Parts;
		Parts.Reserve(ArrayValue.Num());
		for (const FBangoValue& Element : ArrayValue)
		{
			Parts.Add(Element.ToDebugString());
		}
		return FString::Printf(TEXT("[%s]"), *FString::Join(Parts, TEXT(", ")));
	}

	switch (Type.Base)
	{
	case EBangoBaseType::Bool:   return BoolValue ? TEXT("true") : TEXT("false");
	case EBangoBaseType::Int:    return FString::Printf(TEXT("%d"), IntValue);
	case EBangoBaseType::Double: return FString::SanitizeFloat(DoubleValue);
	case EBangoBaseType::String: return FString::Printf(TEXT("\"%s\""), *StringValue.ReplaceCharWithEscapedChar());
	case EBangoBaseType::Entity: return FString::Printf(TEXT("@%s"), *StringValue);
	case EBangoBaseType::Text:   return FString::Printf(TEXT("text(\"%s\")"), *TextValue.ToString().ReplaceCharWithEscapedChar());
	case EBangoBaseType::Vector2:
	case EBangoBaseType::Vector3:
	{
		TArray<FString> Parts;
		for (int32 i = 0; i < Type.GetVectorSize(); ++i)
		{
			Parts.Add(FString::SanitizeFloat(VectorValue[i]));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(", ")));
	}
	default:                     return TEXT("<invalid>");
	}
}

FString FBangoValue::ToDisplayString() const
{
	if (bIsNil)
	{
		return TEXT("nil");
	}
	if (Type.IsArray())
	{
		TArray<FString> Parts;
		Parts.Reserve(ArrayValue.Num());
		for (const FBangoValue& Element : ArrayValue)
		{
			Parts.Add(Element.ToDisplayString());
		}
		return FString::Printf(TEXT("[%s]"), *FString::Join(Parts, TEXT(", ")));
	}
	switch (Type.Base)
	{
	case EBangoBaseType::String:
	case EBangoBaseType::Entity:
		return StringValue;
	case EBangoBaseType::Text:
		return TextValue.ToString();
	default:
		return ToDebugString();
	}
}

bool FBangoValue::operator==(const FBangoValue& Other) const
{
	if (Type != Other.Type || bIsNil != Other.bIsNil)
	{
		return false;
	}
	if (bIsNil)
	{
		return true;
	}
	if (Type.IsArray())
	{
		return ArrayValue == Other.ArrayValue;
	}

	switch (Type.Base)
	{
	case EBangoBaseType::Bool:   return BoolValue == Other.BoolValue;
	case EBangoBaseType::Int:    return IntValue == Other.IntValue;
	case EBangoBaseType::Double: return DoubleValue == Other.DoubleValue;
	case EBangoBaseType::String:
	case EBangoBaseType::Entity: return StringValue.Equals(Other.StringValue, ESearchCase::CaseSensitive);
	case EBangoBaseType::Text:   return TextValue.ToString().Equals(Other.TextValue.ToString(), ESearchCase::CaseSensitive);
	case EBangoBaseType::Vector2:
	case EBangoBaseType::Vector3:
		for (int32 i = 0; i < Type.GetVectorSize(); ++i)
		{
			if (VectorValue[i] != Other.VectorValue[i])
			{
				return false;
			}
		}
		return true;
	default:                     return false;
	}
}

void FBangoValue::ResetPayload()
{
	BoolValue = false;
	IntValue = 0;
	DoubleValue = 0.0;
	StringValue.Reset();
	TextValue = FText::GetEmpty();
	ArrayValue.Reset();
	VectorValue[0] = VectorValue[1] = VectorValue[2] = 0.0;
}

void FBangoValue::Serialize(FArchive& Ar)
{
	Ar << Type;
	Ar << bIsNil;

	if (Ar.IsLoading())
	{
		ResetPayload();
	}
	if (bIsNil)
	{
		return;
	}

	if (Type.IsArray())
	{
		Ar << ArrayValue;
		return;
	}

	switch (Type.Base)
	{
	case EBangoBaseType::Bool:   Ar << BoolValue; break;
	case EBangoBaseType::Int:    Ar << IntValue; break;
	case EBangoBaseType::Double: Ar << DoubleValue; break;
	case EBangoBaseType::String:
	case EBangoBaseType::Entity: Ar << StringValue; break;
	case EBangoBaseType::Text:   Ar << TextValue; break; // keeps where a localized text came from, so it shows in the language of the game that loads it
	case EBangoBaseType::Vector2:
	case EBangoBaseType::Vector3:
		for (int32 i = 0; i < Type.GetVectorSize(); ++i)
		{
			Ar << VectorValue[i];
		}
		break;
	default:                     Ar.SetError(); break;
	}
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

namespace Bango
{
	bool IsValidIdentifier(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return false;
		}
		for (int32 i = 0; i < Name.Len(); ++i)
		{
			const TCHAR C = Name[i];
			const bool bLetter = (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || C == TEXT('_');
			const bool bDigit = C >= TEXT('0') && C <= TEXT('9');
			if (!bLetter && !(bDigit && i > 0))
			{
				return false;
			}
		}
		return true;
	}

	bool IsReservedWord(const FString& Name)
	{
		static const TCHAR* Words[] =
		{
			TEXT("if"), TEXT("else"), TEXT("while"), TEXT("for"), TEXT("forEach"), TEXT("in"),
			TEXT("break"), TEXT("continue"), TEXT("return"), TEXT("label"), TEXT("goto"),
			TEXT("function"), TEXT("global"), TEXT("true"), TEXT("false"), TEXT("nil"),
			TEXT("bool"), TEXT("int"), TEXT("float"), TEXT("double"), TEXT("string"), TEXT("entity"), TEXT("void"),
			TEXT("text"), TEXT("vector2"), TEXT("vector3"), TEXT("params"), TEXT("IsDefined"),
			TEXT("LOCTEXT"), TEXT("NSLOCTEXT"), TEXT("NOTELOCTEXT"), TEXT("NSNOTELOCTEXT"),
		};
		for (const TCHAR* Word : Words)
		{
			if (Name.Equals(Word, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	bool IsLocalName(const FString& Name)
	{
		return Name.StartsWith(TEXT("_"), ESearchCase::CaseSensitive);
	}

	bool IsValidGlobalName(const FString& Name)
	{
		return IsValidIdentifier(Name) && !IsLocalName(Name) && !IsReservedWord(Name);
	}

	bool IsValidBangoIDName(const FString& Name)
	{
		return IsValidIdentifier(Name);
	}

	int32 CountTextFormatValues(const FString& Pattern, FString* OutNamedPlaceholder)
	{
		int32 Needed = 0;
		for (int32 i = 0; i < Pattern.Len(); ++i)
		{
			if (Pattern[i] == TEXT('`'))
			{
				++i; // the next character is literal
				continue;
			}
			if (Pattern[i] != TEXT('{'))
			{
				continue;
			}
			int32 Close = i + 1;
			while (Close < Pattern.Len() && Pattern[Close] != TEXT('}'))
			{
				++Close;
			}
			if (Close >= Pattern.Len())
			{
				break; // an unclosed { is shown as it is
			}
			FString Inside;
			int64 Index = 0;
			bool bDigits = Close > i + 1 && Close - i - 1 <= 9;
			for (int32 j = i + 1; j < Close; ++j)
			{
				Inside.AppendChar(Pattern[j]);
				bDigits &= Pattern[j] >= TEXT('0') && Pattern[j] <= TEXT('9');
				Index = Index * 10 + (Pattern[j] - TEXT('0'));
			}
			if (!bDigits)
			{
				if (OutNamedPlaceholder)
				{
					*OutNamedPlaceholder = Inside;
				}
				return -1;
			}
			Needed = FMath::Max(Needed, (int32)Index + 1);
			i = Close;
		}
		return Needed;
	}

	bool FormatText(const FString& Text, TConstArrayView<FBangoValue> Args, FString& OutText, FString& OutError)
	{
		OutText.Reset();
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const TCHAR C = Text[i];
			if (C != TEXT('%') || i + 1 >= Text.Len())
			{
				OutText.AppendChar(C);
				continue;
			}
			const TCHAR Next = Text[i + 1];
			if (Next == TEXT('%'))
			{
				OutText.AppendChar(TEXT('%'));
				++i;
				continue;
			}
			if (Next < TEXT('0') || Next > TEXT('9'))
			{
				OutText.AppendChar(C);
				continue;
			}
			int32 Index = 0;
			int32 j = i + 1;
			while (j < Text.Len() && Text[j] >= TEXT('0') && Text[j] <= TEXT('9') && Index < 100000)
			{
				Index = Index * 10 + (Text[j] - TEXT('0'));
				++j;
			}
			if (Index >= Args.Num())
			{
				OutError = FString::Printf(TEXT("The text uses %%%d, but only %d value(s) were given."), Index, Args.Num());
				return false;
			}
			OutText += Args[Index].ToDisplayString();
			i = j - 1;
		}
		return true;
	}

	int32 CountFormatValues(const FString& Text)
	{
		// Reads %n exactly as FormatText does.
		int32 Count = 0;
		for (int32 i = 0; i + 1 < Text.Len(); ++i)
		{
			if (Text[i] != TEXT('%'))
			{
				continue;
			}
			const TCHAR Next = Text[i + 1];
			if (Next == TEXT('%'))
			{
				++i;
				continue;
			}
			if (Next < TEXT('0') || Next > TEXT('9'))
			{
				continue;
			}
			int32 Index = 0;
			int32 j = i + 1;
			while (j < Text.Len() && Text[j] >= TEXT('0') && Text[j] <= TEXT('9') && Index < 100000)
			{
				Index = Index * 10 + (Text[j] - TEXT('0'));
				++j;
			}
			Count = FMath::Max(Count, Index + 1);
			i = j - 1;
		}
		return Count;
	}
}
