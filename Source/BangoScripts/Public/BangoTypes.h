#pragma once

#include "CoreMinimal.h"

/** The primitive types of the Bango language. */
enum class EBangoBaseType : uint8
{
	Bool,
	Int,
	Double,
	String,
	Entity,
	/** Localized text for players (an FText): see spec section 3. */
	Text,
	/** Two floats (x, y), the same as an Unreal FVector2D. */
	Vector2,
	/** Three floats (x, y, z), the same as an Unreal FVector. */
	Vector3,

	Count // Not a type; number of entries
};

/**
 * A Bango type: a primitive with an array depth.
 * int = depth 0, int[] = depth 1, int[][] = depth 2, up to MaxArrayDepth.
 */
struct BANGOSCRIPTS_API FBangoType
{
	static constexpr uint8 MaxArrayDepth = 8;

	EBangoBaseType Base = EBangoBaseType::Int;
	uint8 ArrayDepth = 0;

	FBangoType() = default;
	explicit FBangoType(EBangoBaseType InBase, uint8 InArrayDepth = 0)
		: Base(InBase), ArrayDepth(InArrayDepth)
	{
		check(InArrayDepth <= MaxArrayDepth);
	}

	static FBangoType Bool()   { return FBangoType(EBangoBaseType::Bool); }
	static FBangoType Int()    { return FBangoType(EBangoBaseType::Int); }
	static FBangoType Double() { return FBangoType(EBangoBaseType::Double); }
	static FBangoType String() { return FBangoType(EBangoBaseType::String); }
	static FBangoType Entity() { return FBangoType(EBangoBaseType::Entity); }
	static FBangoType Text()   { return FBangoType(EBangoBaseType::Text); }
	static FBangoType Vector2() { return FBangoType(EBangoBaseType::Vector2); }
	static FBangoType Vector3() { return FBangoType(EBangoBaseType::Vector3); }

	/** ArrayOf(EBangoBaseType::Int) is int[]; ArrayOf(EBangoBaseType::Int, 2) is int[][]. */
	static FBangoType ArrayOf(EBangoBaseType ElementBase, uint8 Depth = 1) { return FBangoType(ElementBase, Depth); }

	/** An array whose elements are ElementType: ArrayOf(int[]) is int[][]. */
	static FBangoType ArrayOf(const FBangoType& ElementType) { return ElementType.MakeArrayType(); }

	bool IsArray() const { return ArrayDepth > 0; }

	/** vector2 or vector3 (not an array of them). */
	bool IsVector() const { return ArrayDepth == 0 && (Base == EBangoBaseType::Vector2 || Base == EBangoBaseType::Vector3); }

	/** How many parts a vector has: 2 for vector2, 3 for vector3, 0 for anything else. */
	int32 GetVectorSize() const { return !IsVector() ? 0 : (Base == EBangoBaseType::Vector2 ? 2 : 3); }

	/** The type of this array's elements: int[][] -> int[]. Must be an array. */
	FBangoType GetElementType() const;

	/** An array of this type: int[] -> int[][]. Must be below MaxArrayDepth. */
	FBangoType MakeArrayType() const;

	/** True if a value of type Source can be stored in this type: same type, or int -> double at the same depth (int[][] -> double[][]). */
	bool IsAssignableFrom(const FBangoType& Source) const;

	/** "int", "float[][]", ... */
	FString ToString() const;

	/** Parses "int", "float[][]", ... Returns false if the text is not a type name or is nested too deeply. */
	static bool FromString(const FString& Text, FBangoType& OutType);

	bool operator==(const FBangoType& Other) const { return Base == Other.Base && ArrayDepth == Other.ArrayDepth; }
	bool operator!=(const FBangoType& Other) const { return !(*this == Other); }

	void Serialize(FArchive& Ar);
};

inline FArchive& operator<<(FArchive& Ar, FBangoType& Type)
{
	Type.Serialize(Ar);
	return Ar;
}

/**
 * A Bango value. Every value carries its type and may be nil (undefined).
 * Arrays are values: copying an FBangoValue copies the whole array, including nested arrays.
 *
 * Accessors (GetInt etc.) assert that the value is non-nil and of the right type.
 * The interpreter must check IsNil() first and report a script error, so these asserts never fire from script mistakes.
 */
class BANGOSCRIPTS_API FBangoValue
{
public:
	/** Largest index a script can grow an array to. Writing beyond it is a script error. */
	static constexpr int64 MaxArraySize = 1000000;

	/** A nil int. Needed for containers and loading; prefer MakeNil(Type). */
	FBangoValue() = default;

	static FBangoValue MakeNil(const FBangoType& Type);
	static FBangoValue MakeBool(bool Value);
	static FBangoValue MakeInt(int32 Value);
	static FBangoValue MakeDouble(double Value);
	static FBangoValue MakeString(const FString& Value);
	static FBangoValue MakeEntity(const FString& Name);
	/** Localized text (see BangoText.h for making one from a namespace, key and source). */
	static FBangoValue MakeText(const FText& Value);
	static FBangoValue MakeVector2(double X, double Y);
	static FBangoValue MakeVector3(double X, double Y, double Z);
	/** An array whose elements are ElementType. Elements must fit that type (int fits double), or be nil. */
	static FBangoValue MakeArray(const FBangoType& ElementType, const TArray<FBangoValue>& Elements = TArray<FBangoValue>());

	const FBangoType& GetType() const { return Type; }
	bool IsNil() const { return bIsNil; }

	bool GetBool() const;
	int32 GetInt() const;
	double GetDouble() const;
	const FString& GetString() const;
	const FString& GetEntityName() const;
	const FText& GetText() const;
	/** Int or double, returned as double. */
	double GetNumberAsDouble() const;
	const TArray<FBangoValue>& GetArray() const;
	/** Part Index of a vector (0 = x, 1 = y, 2 = z). Must be a non-nil vector, and Index must be below its size. */
	double GetVectorPart(int32 Index) const;
	/** Changes part Index of a vector in place. Same rules as GetVectorPart. */
	void SetVectorPart(int32 Index, double Value);

	// ---- Array operations (spec section 6). All fail if this is nil or not an array. ----

	/** Number of slots, including nil slots (the Count command). */
	int64 ArrayCount() const;

	/** Element at Index, or nullptr if Index is out of range. Reading never grows the array. */
	const FBangoValue* GetElement(int64 Index) const;

	/** Writes Element at Index. Writing past the end grows the array and fills the gap with nil. */
	bool SetElement(int64 Index, const FBangoValue& Element, FString& OutError);

	/** Adds Element at the end. */
	bool Push(const FBangoValue& Element, FString& OutError);

	/** Inserts Element at Index, moving later elements up. Past the end, the gap is filled with nil. */
	bool InsertAt(int64 Index, const FBangoValue& Element, FString& OutError);

	/** Removes and returns the last element (which may be nil). Fails if the array is empty. */
	bool Pop(FBangoValue& OutElement, FString& OutError);

	/** Removes and returns element Index (which may be nil). Fails if Index is out of range. */
	bool RemoveAt(int64 Index, FBangoValue& OutElement, FString& OutError);

	// ---- Nested access: _a[i][j][k] ----

	/**
	 * Reads a nested element. Returns nullptr if any level is out of range or an inner array is nil.
	 * The returned element itself may be nil. Use for reads and IsNil; never grows anything.
	 */
	const FBangoValue* GetElementAtPath(TConstArrayView<int64> Indices) const;

	/**
	 * Finds a nested element to change in place, e.g. the target of `_grid[1] Push(4);`.
	 * Fails (with an error) if any level is out of range or an inner array is nil. Never grows anything.
	 */
	FBangoValue* FindElementAtPath(TConstArrayView<int64> Indices, FString& OutError);

	/**
	 * Writes a nested element: `_grid[3][2] = 7;`
	 * Every level grows as needed, and any missing or nil inner array on the way is created (empty).
	 * Everything is checked before anything changes, so a failed write leaves the array untouched.
	 * This array itself must not be nil.
	 */
	bool SetElementAtPath(TConstArrayView<int64> Indices, const FBangoValue& Element, FString& OutError);

	// ---- Conversion and utilities ----

	/** Converts to Target if allowed (same type, or int -> double). Nil stays nil, retyped to Target. */
	bool TryConvertTo(const FBangoType& Target, FBangoValue& OutValue) const;

	/** For logs and errors: 5, 2.5, "text", @john, text("Hello"), (1.0, 2.0, 3.0), [1, nil, 3], [[1], []], nil */
	FString ToDebugString() const;

	/** For showing to players (hint text etc.): strings without quotes, entities as their name, text in the current language: 5, 2.5, text, john, (1.0, 2.0), [1, 2] */
	FString ToDisplayString() const;

	/**
	 * Exact structural equality, including type and nil. Texts are equal if they show the same thing in the current
	 * language (which is also what the language's == does for text).
	 */
	bool operator==(const FBangoValue& Other) const;
	bool operator!=(const FBangoValue& Other) const { return !(*this == Other); }

	void Serialize(FArchive& Ar);

private:
	bool CheckIsArray(const TCHAR* Operation, FString& OutError) const;
	static bool CheckWriteIndex(int64 Index, const TCHAR* Operation, FString& OutError);
	/** Grows the array so Index exists (filling with nil) and returns that slot. Index must already be checked. */
	FBangoValue& GrowToIndex(int64 Index);
	bool PrepareElement(const FBangoValue& Element, FBangoValue& OutConverted, FString& OutError) const;
	void ResetPayload();

	FBangoType Type;
	bool bIsNil = true;

	bool BoolValue = false;
	int32 IntValue = 0;
	double DoubleValue = 0.0;
	FString StringValue;              // string, and the name for entity
	FText TextValue;                  // text
	TArray<FBangoValue> ArrayValue;   // elements, which may themselves be arrays
	double VectorValue[3] = { 0.0, 0.0, 0.0 };  // vector2 uses the first two
};

inline FArchive& operator<<(FArchive& Ar, FBangoValue& Value)
{
	Value.Serialize(Ar);
	return Ar;
}

namespace Bango
{
	/** Letters, digits and '_', not starting with a digit. */
	BANGOSCRIPTS_API bool IsValidIdentifier(const FString& Name);

	/** Keywords and type names that can't be used as names. */
	BANGOSCRIPTS_API bool IsReservedWord(const FString& Name);

	/** True if the name starts with '_'. Command names can't; variable names (locals and globals alike) may. */
	BANGOSCRIPTS_API bool IsLocalName(const FString& Name);

	/** A valid identifier that isn't a reserved word. (A leading '_' is allowed.) */
	BANGOSCRIPTS_API bool IsValidGlobalName(const FString& Name);

	/** A name a Bango ID can have, written `@name` in scripts: any identifier. Keywords and a leading '_' are fine, since the '@' sets it apart. */
	BANGOSCRIPTS_API bool IsValidBangoIDName(const FString& Name);

	/**
	 * Replaces %0, %1, ... in Text with Args[0], Args[1], ... (as ToDisplayString). "%%" is a literal '%'.
	 * A % followed by a number with no matching argument is an error.
	 */
	BANGOSCRIPTS_API bool FormatText(const FString& Text, TConstArrayView<FBangoValue> Args, FString& OutText, FString& OutError);

	/** How many values FormatText needs for Text: one more than its highest %n (so 3 for "%0 and %2"), or 0. "%%" doesn't count. */
	BANGOSCRIPTS_API int32 CountFormatValues(const FString& Text);

	/**
	 * How many values a text pattern (FormatText, Hint) needs: one more than its highest {n}, or 0 if it has none.
	 * A backtick escapes the next character, as in FText::Format (`{ is a literal brace). If the pattern has a
	 * named placeholder like {Name}, returns -1 and puts the name in OutNamedPlaceholder.
	 */
	BANGOSCRIPTS_API int32 CountTextFormatValues(const FString& Pattern, FString* OutNamedPlaceholder = nullptr);
}
