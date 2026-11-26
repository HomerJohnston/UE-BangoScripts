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

// What a string, entity, text or array value holds besides its inline part. A value has one only once it's been given
// such contents, so the values the VM handles most (bools and numbers) never make, copy or free any of this.
struct FBangoValueHeap
{
	FString String;              // string, and the name for entity
	FText Text;                  // text
	TArray<FBangoValue> Array;   // elements, which may themselves be arrays
};

void FBangoValue::CopyHeap(const FBangoValue& Other)
{
	if (!Other.Heap)
	{
		if (Heap)
		{
			FreeHeap();
		}
	}
	else if (Heap)
	{
		*Heap = *Other.Heap;
	}
	else
	{
		Heap = new FBangoValueHeap(*Other.Heap);
	}
}

void FBangoValue::FreeHeap()
{
	delete Heap;
	Heap = nullptr;
}

FString& FBangoValue::HeapString()
{
	if (!Heap)
	{
		Heap = new FBangoValueHeap();
	}
	return Heap->String;
}

const FString& FBangoValue::HeapString() const
{
	static const FString Empty;
	return Heap ? Heap->String : Empty;
}

FText& FBangoValue::HeapText()
{
	if (!Heap)
	{
		Heap = new FBangoValueHeap();
	}
	return Heap->Text;
}

const FText& FBangoValue::HeapText() const
{
	static const FText Empty;
	return Heap ? Heap->Text : Empty;
}

TArray<FBangoValue>& FBangoValue::HeapArray()
{
	if (!Heap)
	{
		Heap = new FBangoValueHeap();
	}
	return Heap->Array;
}

const TArray<FBangoValue>& FBangoValue::HeapArray() const
{
	static const TArray<FBangoValue> Empty;
	return Heap ? Heap->Array : Empty;
}





FBangoValue FBangoValue::MakeString(const FString& Value)
{
	FBangoValue V;
	V.Type = FBangoType::String();
	V.bIsNil = false;
	V.HeapString() = Value;
	return V;
}

FBangoValue FBangoValue::MakeEntity(const FString& Name)
{
	FBangoValue V;
	V.Type = FBangoType::Entity();
	V.bIsNil = false;
	V.HeapString() = Name;
	return V;
}

FBangoValue FBangoValue::MakeText(const FText& Value)
{
	FBangoValue V;
	V.Type = FBangoType::Text();
	V.bIsNil = false;
	V.HeapText() = Value;
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
	V.HeapArray().Reserve(Elements.Num());

	for (const FBangoValue& Element : Elements)
	{
		FBangoValue Converted;
		verifyf(Element.TryConvertTo(ElementType, Converted), TEXT("MakeArray: element of type %s does not fit %s"), *Element.GetType().ToString(), *V.Type.ToString());
		V.HeapArray().Add(MoveTemp(Converted));
	}
	return V;
}




const FString& FBangoValue::GetString() const
{
	check(!bIsNil && Type == FBangoType::String());
	return HeapString();
}

const FString& FBangoValue::GetEntityName() const
{
	check(!bIsNil && Type == FBangoType::Entity());
	return HeapString();
}

const FText& FBangoValue::GetText() const
{
	check(!bIsNil && Type == FBangoType::Text());
	return HeapText();
}


const TArray<FBangoValue>& FBangoValue::GetArray() const
{
	check(!bIsNil && Type.IsArray());
	return HeapArray();
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
	if (HeapArray().Num() <= Index)
	{
		const FBangoValue NilElement = MakeNil(Type.GetElementType());
		HeapArray().Reserve((int32)Index + 1);
		while (HeapArray().Num() <= Index)
		{
			HeapArray().Add(NilElement);
		}
	}
	return HeapArray()[(int32)Index];
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
	return HeapArray().Num();
}

const FBangoValue* FBangoValue::GetElement(int64 Index) const
{
	check(!bIsNil && Type.IsArray());
	if (Index < 0 || Index >= HeapArray().Num())
	{
		return nullptr;
	}
	return &HeapArray()[(int32)Index];
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
	if (HeapArray().Num() >= MaxArraySize)
	{
		OutError = FString::Printf(TEXT("Push: array is at the limit of %lld elements."), MaxArraySize);
		return false;
	}

	FBangoValue Converted;
	if (!PrepareElement(Element, Converted, OutError))
	{
		return false;
	}
	HeapArray().Add(MoveTemp(Converted));
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
	if (HeapArray().Num() >= MaxArraySize)
	{
		OutError = FString::Printf(TEXT("InsertAt: array would pass the limit of %lld elements."), MaxArraySize);
		return false;
	}

	FBangoValue Converted;
	if (!PrepareElement(Element, Converted, OutError))
	{
		return false;
	}

	if (Index >= HeapArray().Num())
	{
		// Past the end: fill the gap with nil, then the element lands at Index.
		const FBangoValue NilElement = MakeNil(Type.GetElementType());
		while (HeapArray().Num() < Index)
		{
			HeapArray().Add(NilElement);
		}
		HeapArray().Add(MoveTemp(Converted));
	}
	else
	{
		HeapArray().Insert(MoveTemp(Converted), (int32)Index);
	}
	return true;
}

bool FBangoValue::Pop(FBangoValue& OutElement, FString& OutError)
{
	if (!CheckIsArray(TEXT("Pop"), OutError))
	{
		return false;
	}
	if (HeapArray().Num() == 0)
	{
		OutError = TEXT("Pop on an empty array.");
		return false;
	}
	OutElement = HeapArray().Pop();
	return true;
}

bool FBangoValue::RemoveAt(int64 Index, FBangoValue& OutElement, FString& OutError)
{
	if (!CheckIsArray(TEXT("RemoveAt"), OutError))
	{
		return false;
	}
	if (Index < 0 || Index >= HeapArray().Num())
	{
		OutError = FString::Printf(TEXT("RemoveAt: index %lld is out of range (array has %d elements)."), Index, HeapArray().Num());
		return false;
	}
	OutElement = MoveTemp(HeapArray()[(int32)Index]);
	HeapArray().RemoveAt((int32)Index);
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
		if (Index < 0 || Index >= Current->HeapArray().Num())
		{
			OutError = FString::Printf(TEXT("Index %s is out of range (that array has %d elements)."),
				*PathToString(Indices, Level + 1), Current->HeapArray().Num());
			return nullptr;
		}
		Current = &Current->HeapArray()[(int32)Index];
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
	Result.HeapArray().Reserve(HeapArray().Num());
	for (const FBangoValue& Element : HeapArray())
	{
		FBangoValue Converted;
		verify(Element.TryConvertTo(TargetElement, Converted));
		Result.HeapArray().Add(MoveTemp(Converted));
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
		Parts.Reserve(HeapArray().Num());
		for (const FBangoValue& Element : HeapArray())
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
	case EBangoBaseType::String: return FString::Printf(TEXT("\"%s\""), *HeapString().ReplaceCharWithEscapedChar());
	case EBangoBaseType::Entity:
		if (Bango::GetEntityIDKind(HeapString()) == Bango::EBangoEntityIDKind::Named)
		{
			return FString::Printf(TEXT("@%s"), *HeapString());
		}
		return Bango::GetEntityDisplayName(HeapString()); // player, self, or an actor without a Bango ID name
	case EBangoBaseType::Text:   return FString::Printf(TEXT("text(\"%s\")"), *HeapText().ToString().ReplaceCharWithEscapedChar());
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
		Parts.Reserve(HeapArray().Num());
		for (const FBangoValue& Element : HeapArray())
		{
			Parts.Add(Element.ToDisplayString());
		}
		return FString::Printf(TEXT("[%s]"), *FString::Join(Parts, TEXT(", ")));
	}
	switch (Type.Base)
	{
	case EBangoBaseType::String:
		return HeapString();
	case EBangoBaseType::Entity:
		return Bango::GetEntityDisplayName(HeapString());
	case EBangoBaseType::Text:
		return HeapText().ToString();
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
		return HeapArray() == Other.HeapArray();
	}

	switch (Type.Base)
	{
	case EBangoBaseType::Bool:   return BoolValue == Other.BoolValue;
	case EBangoBaseType::Int:    return IntValue == Other.IntValue;
	case EBangoBaseType::Double: return DoubleValue == Other.DoubleValue;
	case EBangoBaseType::String:
	case EBangoBaseType::Entity: return HeapString().Equals(Other.HeapString(), ESearchCase::CaseSensitive);
	case EBangoBaseType::Text:   return HeapText().ToString().Equals(Other.HeapText().ToString(), ESearchCase::CaseSensitive);
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
	if (Heap)
	{
		FreeHeap();
	}
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
		Ar << HeapArray();
		return;
	}

	switch (Type.Base)
	{
	case EBangoBaseType::Bool:   Ar << BoolValue; break;
	case EBangoBaseType::Int:    Ar << IntValue; break;
	case EBangoBaseType::Double: Ar << DoubleValue; break;
	case EBangoBaseType::String:
	case EBangoBaseType::Entity: Ar << HeapString(); break;
	case EBangoBaseType::Text:   Ar << HeapText(); break; // keeps where a localized text came from, so it shows in the language of the game that loads it
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

	const FString& GetPlayerEntityName()
	{
		static const FString Name(TEXT("#player"));
		return Name;
	}

	bool IsPlayerEntityName(const FString& Name)
	{
		return Name.Equals(GetPlayerEntityName(), ESearchCase::CaseSensitive);
	}

	const FString& GetSelfEntityName()
	{
		static const FString Name(TEXT("#self"));
		return Name;
	}

	bool IsSelfEntityName(const FString& Name)
	{
		return Name.Equals(GetSelfEntityName(), ESearchCase::CaseSensitive);
	}

	const TCHAR* GetEntityKeyword(const FString& Name)
	{
		return IsPlayerEntityName(Name) ? TEXT("player") : IsSelfEntityName(Name) ? TEXT("self") : nullptr;
	}

	const TCHAR* GetRenamedTextMacro(const FString& Name)
	{
		// v0.133 renamed Unreal's five macros to Bango's three.
		static const TCHAR* const Renames[][2] =
		{
			{ TEXT("LOCTEXT"),       TEXT("TEXT: TEXT(\"Key\", \"Text\")") },
			{ TEXT("NOTELOCTEXT"),   TEXT("TEXT, with the notes last: TEXT(\"Key\", \"Text\", \"Notes\")") },
			{ TEXT("NSLOCTEXT"),     TEXT("TEXT_NS: TEXT_NS(\"Namespace\", \"Key\", \"Text\")") },
			{ TEXT("NSNOTELOCTEXT"), TEXT("TEXT_NS, with the notes last: TEXT_NS(\"Namespace\", \"Key\", \"Text\", \"Notes\")") },
			{ TEXT("INVTEXT"),       TEXT("TEXT_INV: TEXT_INV(\"Text\")") },
		};
		for (const auto& Rename : Renames)
		{
			if (Name.Equals(Rename[0], ESearchCase::CaseSensitive))
			{
				return Rename[1];
			}
		}
		return nullptr;
	}

	bool IsReservedWord(const FString& Name)
	{
		static const TCHAR* Words[] =
		{
			TEXT("if"), TEXT("else"), TEXT("while"), TEXT("for"), TEXT("forEach"), TEXT("in"), TEXT("sequence"), TEXT("spawn"), TEXT("waitUntil"), TEXT("switch"), TEXT("case"), TEXT("default"),
			TEXT("break"), TEXT("continue"), TEXT("return"), TEXT("label"), TEXT("goto"),
			TEXT("function"), TEXT("global"), TEXT("scriptVar"), TEXT("persistent"), TEXT("true"), TEXT("false"), TEXT("nil"),
			TEXT("bool"), TEXT("int"), TEXT("float"), TEXT("string"), TEXT("entity"), TEXT("void"),
			TEXT("text"), TEXT("vector2"), TEXT("vector3"), TEXT("isNil"), TEXT("call"), TEXT("player"), TEXT("self"),
			TEXT("TEXT"), TEXT("TEXT_NS"), TEXT("TEXT_INV"),
		};
		if (GetRenamedTextMacro(Name))
		{
			return true; // the old names, so that using one says what it's called now
		}
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
		return IsValidIdentifier(Name) && !IsReservedWord(Name);
	}

	bool IsValidBangoIDName(const FString& Name)
	{
		return IsValidIdentifier(Name);
	}

	EBangoEntityIDKind GetEntityIDKind(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return EBangoEntityIDKind::Invalid;
		}
		int32 Bar = INDEX_NONE;
		if (Name.FindLastChar(TEXT('|'), Bar))
		{
			// A component: its actor's ID (of another kind), then its name.
			const EBangoEntityIDKind OwnerKind = GetEntityIDKind(Name.Left(Bar));
			const bool bOwnerValid = OwnerKind != EBangoEntityIDKind::Invalid && OwnerKind != EBangoEntityIDKind::Component;
			return bOwnerValid && Bar + 1 < Name.Len() ? EBangoEntityIDKind::Component : EBangoEntityIDKind::Invalid;
		}
		switch (Name[0])
		{
		case TEXT('#'):
			return GetEntityKeyword(Name) ? EBangoEntityIDKind::Keyword : EBangoEntityIDKind::Invalid;
		case TEXT('%'):
			return Name.Len() > 1 ? EBangoEntityIDKind::Level : EBangoEntityIDKind::Invalid;
		case TEXT('$'):
		{
			if (Name.Len() != 33)
			{
				return EBangoEntityIDKind::Invalid;
			}
			for (int32 i = 1; i < Name.Len(); ++i)
			{
				const TCHAR C = Name[i];
				const bool bHex = (C >= TEXT('0') && C <= TEXT('9')) || (C >= TEXT('A') && C <= TEXT('F')) || (C >= TEXT('a') && C <= TEXT('f'));
				if (!bHex)
				{
					return EBangoEntityIDKind::Invalid;
				}
			}
			return EBangoEntityIDKind::Automatic;
		}
		default:
			return IsValidBangoIDName(Name) ? EBangoEntityIDKind::Named : EBangoEntityIDKind::Invalid;
		}
	}

	FString MakeLevelEntityID(const FString& ActorPath)
	{
		return FString(TEXT("%")) + ActorPath;
	}

	FString MakeAutomaticEntityID(const FString& GuidDigits)
	{
		return FString(TEXT("$")) + GuidDigits;
	}

	FString MakeComponentEntityID(const FString& OwnerID, const FString& ComponentName)
	{
		return OwnerID + TEXT("|") + ComponentName;
	}

	bool SplitComponentEntityID(const FString& Name, FString& OutOwnerID, FString& OutComponentName)
	{
		int32 Bar = INDEX_NONE;
		if (GetEntityIDKind(Name) != EBangoEntityIDKind::Component || !Name.FindLastChar(TEXT('|'), Bar))
		{
			return false;
		}
		OutOwnerID = Name.Left(Bar);
		OutComponentName = Name.Mid(Bar + 1);
		return true;
	}

	FString GetLevelEntityPath(const FString& Name)
	{
		return GetEntityIDKind(Name) == EBangoEntityIDKind::Level ? Name.Mid(1) : FString();
	}

	namespace EntityNamesPrivate
	{
		TMap<FString, FString>& GetDisplayNames()
		{
			static TMap<FString, FString> DisplayNames;
			return DisplayNames;
		}
	}

	void SetEntityDisplayName(const FString& Name, const FString& DisplayName)
	{
		const EBangoEntityIDKind Kind = GetEntityIDKind(Name);
		if ((Kind == EBangoEntityIDKind::Level || Kind == EBangoEntityIDKind::Automatic || Kind == EBangoEntityIDKind::Component) && !DisplayName.IsEmpty())
		{
			EntityNamesPrivate::GetDisplayNames().Add(Name, DisplayName);
		}
	}

	FString GetEntityDisplayName(const FString& Name)
	{
		if (const TCHAR* Keyword = GetEntityKeyword(Name))
		{
			return Keyword;
		}
		const EBangoEntityIDKind Kind = GetEntityIDKind(Name);
		if (Kind != EBangoEntityIDKind::Level && Kind != EBangoEntityIDKind::Automatic && Kind != EBangoEntityIDKind::Component)
		{
			return Name;
		}
		if (const FString* Known = EntityNamesPrivate::GetDisplayNames().Find(Name))
		{
			return *Known;
		}
		FString OwnerID;
		FString ComponentName;
		if (SplitComponentEntityID(Name, OwnerID, ComponentName))
		{
			return FString::Printf(TEXT("%s (%s)"), *GetEntityDisplayName(OwnerID), *ComponentName);
		}
		if (Kind == EBangoEntityIDKind::Level)
		{
			// The path's last part, after its last '.' or ':': the actor's object name.
			int32 Cut = INDEX_NONE;
			for (int32 i = Name.Len() - 1; i > 0; --i)
			{
				if (Name[i] == TEXT('.') || Name[i] == TEXT(':'))
				{
					Cut = i;
					break;
				}
			}
			return Cut != INDEX_NONE ? Name.Mid(Cut + 1) : Name.Mid(1);
		}
		return TEXT("something that's no longer here");
	}

	FString DescribeMissingEntity(const FString& Name)
	{
		if (IsPlayerEntityName(Name))
		{
			return TEXT("There's no player pawn right now (no local player, or it isn't possessing a pawn).");
		}
		switch (GetEntityIDKind(Name))
		{
		case EBangoEntityIDKind::Level:
			return FString::Printf(TEXT("The actor '%s' isn't in the level any more (it was destroyed, or its level isn't loaded)."), *GetEntityDisplayName(Name));
		case EBangoEntityIDKind::Automatic:
			return FString::Printf(TEXT("'%s' isn't in the level any more (it was destroyed or removed, or its level isn't loaded)."), *GetEntityDisplayName(Name));
		case EBangoEntityIDKind::Component:
			return FString::Printf(TEXT("The component '%s' isn't there any more (it was removed, or its actor is gone)."), *GetEntityDisplayName(Name));
		default:
			return FString::Printf(TEXT("No actor is registered with the Bango ID name '%s'. Check the name on the actor's Bango ID component, and that the actor is in the level."), *Name);
		}
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

	/**
	 * Reads the placeholder at Text[Start] (a '{'): {digits}, its closing '}' and index, as FText::Format does.
	 * False if it isn't one ({Name}, {}, or no closing '}'), in which case the '{' is just a character.
	 */
	static bool ReadNumberedPlaceholder(const FString& Text, int32 Start, int32& OutClose, int32& OutIndex)
	{
		int32 Close = Start + 1;
		int64 Index = 0;
		while (Close < Text.Len() && Text[Close] >= TEXT('0') && Text[Close] <= TEXT('9') && Close - Start <= 9)
		{
			Index = Index * 10 + (Text[Close] - TEXT('0'));
			++Close;
		}
		if (Close == Start + 1 || Close >= Text.Len() || Text[Close] != TEXT('}'))
		{
			return false;
		}
		OutClose = Close;
		OutIndex = (int32)Index;
		return true;
	}

	bool FormatText(const FString& Text, TConstArrayView<FBangoValue> Args, FString& OutText, FString& OutError)
	{
		// One pass, with FText::Format's rules: {0}, {1}, ... are the values; a backtick makes the next character
		// literal (`{ `} ``); anything else, including {Name} and a '{' with no '}', is copied as it is.
		OutText.Reset();
		OutText.Reserve(Text.Len());
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const TCHAR C = Text[i];
			if (C == TEXT('`') && i + 1 < Text.Len())
			{
				OutText.AppendChar(Text[++i]);
				continue;
			}
			int32 Close = 0;
			int32 Index = 0;
			if (C != TEXT('{') || !ReadNumberedPlaceholder(Text, i, Close, Index))
			{
				OutText.AppendChar(C);
				continue;
			}
			if (Index >= Args.Num())
			{
				OutError = FString::Printf(TEXT("The text uses {%d}, but only %d value(s) were given."), Index, Args.Num());
				return false;
			}
			OutText += Args[Index].ToDisplayString();
			i = Close;
		}
		return true;
	}

	int32 CountFormatValues(const FString& Text)
	{
		// Reads {n} exactly as FormatText does.
		int32 Count = 0;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			if (Text[i] == TEXT('`'))
			{
				++i; // the next character is literal
				continue;
			}
			int32 Close = 0;
			int32 Index = 0;
			if (Text[i] == TEXT('{') && ReadNumberedPlaceholder(Text, i, Close, Index))
			{
				Count = FMath::Max(Count, Index + 1);
				i = Close;
			}
		}
		return Count;
	}
}
