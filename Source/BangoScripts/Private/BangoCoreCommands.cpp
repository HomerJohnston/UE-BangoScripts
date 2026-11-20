// The commands that need nothing from the engine or a world, so every command table can have them (the module's,
// and tests'). Bango::RegisterBuiltinCommands registers them with the rest.

#include "BangoCommand.h"

namespace Bango
{
	bool RegisterCoreCommands(FBangoCommandTable& Commands, FString& OutError)
	{
		bool bAllAdded = true;
		auto Add = [&Commands, &OutError, &bAllAdded](FBangoCommand Command)
		{
			Command.Signature.bBuiltIn = true;
			FString Error;
			if (!Commands.Add(Command, Error))
			{
				bAllAdded = false;
				OutError = Error;
			}
		};

		// ---- Count ----
		// Count(value) -> int: an array's slots (nil ones included), a vector's parts (2 or 3), or a string's
		// characters (UTF-16 units). Spec section 6.
		{
			const FBangoTypeVar T(TEXT("T"));
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Count"))
				.TypeParam(T, { FBangoType::Vector2(), FBangoType::Vector3(), FBangoType::String() }).OrAnyArray()
				.Param(TEXT("Value"), T, TEXT("An array, a vector or a string")).Returns(FBangoType::Int()).Pure()
				.InCategory(TEXT("Arrays")).Describe(TEXT("How many slots an array has (nil ones included), parts a vector has (2 or 3), or characters a string has."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (Call.Args.Num() < 1 || Call.Args[0].IsNil())
				{
					return Call.Fail(TEXT("Argument 1 is nil."));
				}
				const FBangoValue& Value = Call.Args[0];
				const FBangoType Type = Value.GetType();
				if (Type.IsArray())
				{
					Call.Result = FBangoValue::MakeInt((int32)Value.ArrayCount());
				}
				else if (Type.IsVector())
				{
					Call.Result = FBangoValue::MakeInt(Type.GetVectorSize());
				}
				else
				{
					Call.Result = FBangoValue::MakeInt(Value.GetString().Len());
				}
				return EBangoCommandResult::Done;
			};
			Add(MoveTemp(Command));
		}

		// ---- The array commands: they change their target, an array, in place (spec section 6) ----
		// Each checks everything before it changes anything, so a call that fails leaves the array as it was.
		const FBangoTypeVar Element(TEXT("T"));
		const FBangoType Int = FBangoType::Int();
		auto Array = [&Element](const TCHAR* Name)
		{
			// `array Name(...)`: the target is an array of any type, and says what an element is.
			return FBangoCommandSignature::Make(Name).AnyTypeParam(Element, TEXT("element")).Target(Element.Array()).MutableTarget().Pure().InCategory(TEXT("Arrays"));
		};

		// array Push(value)
		{
			FBangoCommand Command;
			Command.Signature = Array(TEXT("Push")).Param(TEXT("Value"), Element).Describe(TEXT("Adds a value to the end of an array."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (Call.Args.Num() < 1)
				{
					return Call.Fail(TEXT("Push needs a value."));
				}
				FString Error;
				return Call.Target.Push(Call.Args[0], Error) ? EBangoCommandResult::Done : Call.Fail(Error);
			};
			Add(MoveTemp(Command));
		}

		// array InsertAt(value, index)
		{
			FBangoCommand Command;
			Command.Signature = Array(TEXT("InsertAt")).Param(TEXT("Value"), Element).Param(TEXT("Index"), Int, TEXT("Where it goes: the elements from there on move up"))
				.Describe(TEXT("Puts a value into an array at an index, moving the later elements up."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (Call.Args.Num() < 2 || Call.Args[1].IsNil())
				{
					return Call.Fail(TEXT("the index is nil."));
				}
				FString Error;
				return Call.Target.InsertAt(Call.Args[1].GetInt(), Call.Args[0], Error) ? EBangoCommandResult::Done : Call.Fail(Error);
			};
			Add(MoveTemp(Command));
		}

		// array Pop() -> element, array RemoveAt(index) -> element. On a line of their own the element is thrown away,
		// and may be nil; where it's used, a nil one is an error (reading nil always is).
		for (const bool bPop : { true, false })
		{
			FBangoCommand Command;
			Command.Signature = Array(bPop ? TEXT("Pop") : TEXT("RemoveAt"));
			if (!bPop)
			{
				Command.Signature.Param(TEXT("Index"), Int, TEXT("Which element: the later ones move down"));
			}
			Command.Signature.Returns(Element).Discardable().Describe(bPop
				? TEXT("Removes the last element of an array, and gives it back.")
				: TEXT("Removes the element at an index of an array, moving the later elements down, and gives it back."));
			Command.Start = [bPop](FBangoCommandCall& Call)
			{
				if (!bPop && (Call.Args.Num() < 1 || Call.Args[0].IsNil()))
				{
					return Call.Fail(TEXT("the index is nil."));
				}
				// Looked at before it's taken out, so a call that fails changes nothing.
				if (Call.bResultUsed && !Call.Target.IsNil() && Call.Target.GetType().IsArray())
				{
					const int64 Count = Call.Target.ArrayCount();
					const int64 Index = bPop ? Count - 1 : (int64)Call.Args[0].GetInt();
					const FBangoValue* Found = Index >= 0 && Index < Count ? Call.Target.GetElement(Index) : nullptr;
					if (Found && Found->IsNil())
					{
						return Call.Fail(TEXT("the element it would remove is nil, so its value can't be used."));
					}
				}
				FString Error;
				FBangoValue Removed;
				if (!(bPop ? Call.Target.Pop(Removed, Error) : Call.Target.RemoveAt(Call.Args[0].GetInt(), Removed, Error)))
				{
					return Call.Fail(Error);
				}
				Call.Result = MoveTemp(Removed);
				return EBangoCommandResult::Done;
			};
			Add(MoveTemp(Command));
		}

		return bAllAdded;
	}
}
