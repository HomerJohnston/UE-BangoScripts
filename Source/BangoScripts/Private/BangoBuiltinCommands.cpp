#include "BangoBuiltinCommands.h"

#include "BangoActorRegistry.h"
#include "BangoLog.h"
#include "BangoScriptSubsystem.h"
#include "BangoText.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace BangoBuiltinPrivate
{
	/** Formats Args[0] with Args[1...] the way Hint does. */
	static bool FormatArgs(FBangoCommandCall& Call, FString& OutText)
	{
		TArray<FBangoValue> Values;
		for (int32 i = 1; i < Call.Args.Num(); ++i)
		{
			Values.Add(Call.Args[i]);
		}
		FString Error;
		if (!Bango::FormatText(Call.Args[0].GetString(), Values, OutText, Error))
		{
			Call.Error = Error;
			return false;
		}
		return true;
	}

	/** Formats the text Args[0] with Args[1...] the way Hint and FormatText do ({0}, {1}, ...). */
	static bool FormatTextArgs(FBangoCommandCall& Call, FText& OutText)
	{
		TArray<FBangoValue> Values;
		for (int32 i = 1; i < Call.Args.Num(); ++i)
		{
			Values.Add(Call.Args[i]);
		}
		FString Error;
		if (!Bango::FormatLocalizedText(Call.Args[0].GetText(), Values, OutText, Error))
		{
			Call.Error = Error;
			return false;
		}
		return true;
	}

	static bool RequireArgs(FBangoCommandCall& Call, int32 Count)
	{
		for (int32 i = 0; i < Count && i < Call.Args.Num(); ++i)
		{
			if (Call.Args[i].IsNil())
			{
				Call.Error = FString::Printf(TEXT("Argument %d is nil."), i + 1);
				return false;
			}
		}
		return true;
	}

	static AActor* ResolveActor(FBangoCommandCall& Call, const FBangoValue& Entity)
	{
		if (Entity.IsNil())
		{
			Call.Error = TEXT("The entity is nil.");
			return nullptr;
		}
		UBangoActorRegistry* Registry = UBangoActorRegistry::Get(Call.WorldContext);
		AActor* Actor = Registry ? Registry->FindActor(Entity.GetEntityName()) : nullptr;
		if (!Actor)
		{
			Call.Error = FString::Printf(TEXT("No actor is registered with the Bango ID name '%s'."), *Entity.GetEntityName());
		}
		return Actor;
	}

	/** For a ReturnsNumber command: true if every argument is an int, so the result must be one too. */
	static bool AllInts(const FBangoCommandCall& Call)
	{
		for (const FBangoValue& Arg : Call.Args)
		{
			if (!(Arg.GetType() == FBangoType::Int()))
			{
				return false;
			}
		}
		return true;
	}

	/** Sets Call.Result to Value as an int if bInt (the value is then a whole number in the int range), otherwise as a float. */
	static void SetNumberResult(FBangoCommandCall& Call, bool bInt, double Value)
	{
		Call.Result = bInt ? FBangoValue::MakeInt((int32)Value) : FBangoValue::MakeDouble(Value);
	}

	static bool ToInt(FBangoCommandCall& Call, double Value, int32& Out)
	{
		if (!(Value >= (double)MIN_int32 && Value <= (double)MAX_int32))
		{
			Call.Error = FString::Printf(TEXT("%s is too large to convert to an int (ints go from -2147483648 to 2147483647)."), *FString::SanitizeFloat(Value));
			return false;
		}
		Out = (int32)Value;
		return true;
	}

	static void Add(FBangoCommandTable& Commands, const FBangoCommand& Command)
	{
		FString Error;
		if (!Commands.Add(Command, Error))
		{
			Bango::ReportError(FString::Printf(TEXT("Couldn't register built-in command: %s"), *Error));
		}
	}
}

namespace Bango
{
	void RegisterBuiltinCommands(FBangoCommandTable& Commands)
	{
		using namespace BangoBuiltinPrivate;
		const FBangoType Bool = FBangoType::Bool();
		const FBangoType Int = FBangoType::Int();
		const FBangoType Double = FBangoType::Double();
		const FBangoType String = FBangoType::String();
		const FBangoType Entity = FBangoType::Entity();
		const FBangoType TextType = FBangoType::Text();
		const FBangoType Vector3 = FBangoType::Vector3();

		// Hint(text, ...): shows localized text to the player, with {0}, {1}, ... filled in. Bind
		// UBangoScriptSubsystem::OnHint to show it in your own UI.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Hint")).Param(TEXT("Msg"), TextType).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")).InCategory(TEXT("Text"))
				.Describe(TEXT("Shows text to the player, with {0}, {1}, ... replaced by the extra values."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				FText Shown;
				if (!RequireArgs(Call, 1) || !FormatTextArgs(Call, Shown))
				{
					return EBangoCommandResult::Failed;
				}
				if (UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(Call.WorldContext))
				{
					Subsystem->ShowHint(Shown);
				}
				else
				{
					UE_LOG(LogBango, Display, TEXT("Hint: %s"), *Shown.ToString());
				}
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// FormatText(text, ...) -> text: like Format, for localized text. Uses FText::Format, so translators can move
		// the values around and use plural forms.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("FormatText")).Param(TEXT("Text"), TextType).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")).Returns(TextType).InCategory(TEXT("Text"))
				.Describe(TEXT("Returns the text with {0}, {1}, ... replaced by the extra values. Translators can reorder them and use plural forms."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				FText Formatted;
				if (!RequireArgs(Call, 1) || !FormatTextArgs(Call, Formatted))
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeText(Formatted);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// ToText(string) -> text: text that is never translated, like a player's name.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("ToText")).Param(TEXT("Value"), String).Returns(TextType).InCategory(TEXT("Text"))
				.Describe(TEXT("Text that shows this string as it is, in every language. For things that aren't translated, like names."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeText(Bango::MakeInvariantText(Call.Args[0].GetString()));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// ToString(text) -> string: what the text shows in the current language.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("ToString")).Param(TEXT("Value"), TextType).Returns(String).InCategory(TEXT("Text"))
				.Describe(TEXT("What the text shows in the current language, as a string. It doesn't change if the language does."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeString(Call.Args[0].GetText().ToString());
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Log(text, ...): writes to the output log only.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Log")).Param(TEXT("Msg"), String).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")).InCategory(TEXT("Text")).Describe(TEXT("Writes text to the output log, with {0}, {1}, ... replaced by the extra values."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				FString Text;
				if (!RequireArgs(Call, 1) || !FormatArgs(Call, Text))
				{
					return EBangoCommandResult::Failed;
				}
				UE_LOG(LogBango, Display, TEXT("%s"), *Text);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Format(text, ...) -> string
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Format")).Param(TEXT("Text"), String).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")).Returns(String).InCategory(TEXT("Text")).Describe(TEXT("Returns the text with {0}, {1}, ... replaced by the extra values."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				FString Text;
				if (!RequireArgs(Call, 1) || !FormatArgs(Call, Text))
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeString(Text);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Sleep(seconds): pauses the script. Sleep(0), or Sleep() with no time, waits until the next frame.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Sleep")).Optional().Param(TEXT("Time"), Double, TEXT("Seconds (none: until the next frame)"))
				.InCategory(TEXT("Time")).Describe(TEXT("Pauses the script for a number of seconds, or until the next frame if no time is given."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				// No time (or nil): the same as Sleep(0), the next frame.
				const double Seconds = Call.Args.Num() > 0 && !Call.Args[0].IsNil() ? Call.Args[0].GetDouble() : 0.0;
				if (Seconds < 0.0)
				{
					return Call.Fail(FString::Printf(TEXT("Can't sleep for a negative time (%s seconds)."), *FString::SanitizeFloat(Seconds)));
				}
				Call.LatentState.Add(FBangoValue::MakeDouble(Seconds));
				return EBangoCommandResult::Pending;
			};
			Command.Poll = [](FBangoCommandCall& Call)
			{
				const double Remaining = Call.LatentState[0].GetDouble() - Call.DeltaSeconds;
				Call.LatentState[0] = FBangoValue::MakeDouble(Remaining);
				return Remaining <= 0.0 ? EBangoCommandResult::Done : EBangoCommandResult::Pending;
			};
			Add(Commands, Command);
		}

		// Random(min, max) -> double, in [min, max]
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Random")).Param(TEXT("Min"), Double).Param(TEXT("Max"), Double).Returns(Double).InCategory(TEXT("Math")).Describe(TEXT("A random number from min to max."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 2))
				{
					return EBangoCommandResult::Failed;
				}
				const double Min = Call.Args[0].GetDouble();
				const double Max = Call.Args[1].GetDouble();
				if (Min > Max)
				{
					return Call.Fail(FString::Printf(TEXT("Min (%s) is greater than Max (%s)."), *FString::SanitizeFloat(Min), *FString::SanitizeFloat(Max)));
				}
				Call.Result = FBangoValue::MakeDouble(FMath::FRandRange(Min, Max));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// RandomInt(min, max) -> int, from min to max inclusive
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("RandomInt")).Param(TEXT("Min"), Int).Param(TEXT("Max"), Int).Returns(Int).InCategory(TEXT("Math")).Describe(TEXT("A random int from min to max, inclusive."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 2))
				{
					return EBangoCommandResult::Failed;
				}
				const int32 Min = Call.Args[0].GetInt();
				const int32 Max = Call.Args[1].GetInt();
				if (Min > Max)
				{
					return Call.Fail(FString::Printf(TEXT("Min (%d) is greater than Max (%d)."), Min, Max));
				}
				Call.Result = FBangoValue::MakeInt(FMath::RandRange(Min, Max));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Floor(value) -> int, Round(value) -> int
		for (const bool bRound : { false, true })
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(bRound ? TEXT("Round") : TEXT("Floor")).Param(TEXT("Value"), Double).Returns(Int)
				.InCategory(TEXT("Math")).Describe(bRound ? TEXT("Rounds to the nearest int (halves away from zero).") : TEXT("Rounds down to an int."));
			Command.Start = [bRound](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				const double Value = Call.Args[0].GetDouble();
				int32 Result = 0;
				if (!ToInt(Call, bRound ? FMath::RoundHalfFromZero(Value) : FMath::FloorToDouble(Value), Result))
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeInt(Result);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Min(a, b), Max(a, b) -> number: an int if both are ints, otherwise a float (spec section 7).
		for (const bool bMax : { false, true })
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(bMax ? TEXT("Max") : TEXT("Min")).NumberParam(TEXT("A")).NumberParam(TEXT("B")).ReturnsNumber()
				.InCategory(TEXT("Math")).Describe(bMax
					? TEXT("The larger of two numbers: an int if both are ints, otherwise a float.")
					: TEXT("The smaller of two numbers: an int if both are ints, otherwise a float."));
			Command.Start = [bMax](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 2))
				{
					return EBangoCommandResult::Failed;
				}
				if (AllInts(Call))
				{
					const int32 A = Call.Args[0].GetInt();
					const int32 B = Call.Args[1].GetInt();
					Call.Result = FBangoValue::MakeInt(bMax ? FMath::Max(A, B) : FMath::Min(A, B));
					return EBangoCommandResult::Done;
				}
				const double A = Call.Args[0].GetNumberAsDouble();
				const double B = Call.Args[1].GetNumberAsDouble();
				Call.Result = FBangoValue::MakeDouble(bMax ? FMath::Max(A, B) : FMath::Min(A, B));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Abs(value) -> number: an int for an int, a float for a float.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Abs")).NumberParam(TEXT("Value")).ReturnsNumber()
				.InCategory(TEXT("Math")).Describe(TEXT("The number without its sign: an int for an int, a float for a float."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				if (AllInts(Call))
				{
					const int32 Value = Call.Args[0].GetInt();
					if (Value == MIN_int32)
					{
						return Call.Fail(TEXT("-2147483648 has no positive int (ints go up to 2147483647). Pass it as a float to get 2147483648.0."));
					}
					Call.Result = FBangoValue::MakeInt(FMath::Abs(Value));
					return EBangoCommandResult::Done;
				}
				Call.Result = FBangoValue::MakeDouble(FMath::Abs(Call.Args[0].GetNumberAsDouble()));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Clamp(value, min, max) -> number: an int if all three are ints, otherwise a float.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Clamp")).NumberParam(TEXT("Value")).NumberParam(TEXT("Min")).NumberParam(TEXT("Max")).ReturnsNumber()
				.InCategory(TEXT("Math")).Describe(TEXT("The value, kept from min to max: an int if all three are ints, otherwise a float."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 3))
				{
					return EBangoCommandResult::Failed;
				}
				const double Value = Call.Args[0].GetNumberAsDouble();
				const double Min = Call.Args[1].GetNumberAsDouble();
				const double Max = Call.Args[2].GetNumberAsDouble();
				if (Min > Max)
				{
					return Call.Fail(FString::Printf(TEXT("Min (%s) is greater than Max (%s)."), *Call.Args[1].ToDebugString(), *Call.Args[2].ToDebugString()));
				}
				// Every int fits a double exactly, so for ints this gives one of the three ints back.
				SetNumberResult(Call, AllInts(Call), FMath::Clamp(Value, Min, Max));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Sin(rads), Cos(rads), SinDegrees(degrees), CosDegrees(degrees) -> float
		{
			struct FTrigCommand { const TCHAR* Name; const TCHAR* ParamName; const TCHAR* ParamDescription; bool bCos; bool bDegrees; const TCHAR* Description; };
			static const FTrigCommand TrigCommands[] =
			{
				{ TEXT("Sin"),        TEXT("Rads"),    TEXT("An angle in radians"), false, false, TEXT("The sine of an angle in radians.") },
				{ TEXT("Cos"),        TEXT("Rads"),    TEXT("An angle in radians"), true,  false, TEXT("The cosine of an angle in radians.") },
				{ TEXT("SinDegrees"), TEXT("Degrees"), TEXT("An angle in degrees"), false, true,  TEXT("The sine of an angle in degrees.") },
				{ TEXT("CosDegrees"), TEXT("Degrees"), TEXT("An angle in degrees"), true,  true,  TEXT("The cosine of an angle in degrees.") },
			};
			for (const FTrigCommand& Trig : TrigCommands)
			{
				FBangoCommand Command;
				Command.Signature = FBangoCommandSignature::Make(Trig.Name).Param(Trig.ParamName, Double, Trig.ParamDescription).Returns(Double)
					.InCategory(TEXT("Math")).Describe(Trig.Description);
				const bool bCos = Trig.bCos;
				const bool bDegrees = Trig.bDegrees;
				Command.Start = [bCos, bDegrees](FBangoCommandCall& Call)
				{
					if (!RequireArgs(Call, 1))
					{
						return EBangoCommandResult::Failed;
					}
					const double Angle = bDegrees ? FMath::DegreesToRadians(Call.Args[0].GetDouble()) : Call.Args[0].GetDouble();
					Call.Result = FBangoValue::MakeDouble(bCos ? FMath::Cos(Angle) : FMath::Sin(Angle));
					return EBangoCommandResult::Done;
				};
				Add(Commands, Command);
			}
		}

		// Time() -> float: the world's time, GetWorld()->GetTimeSeconds() (seconds since the level started; pauses with the game).
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Time")).Returns(Double).InCategory(TEXT("Time")).Describe(TEXT("Seconds since the level started."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				const UWorld* World = Call.WorldContext ? Call.WorldContext->GetWorld() : nullptr;
				if (!World)
				{
					return Call.Fail(TEXT("Time needs a game world."));
				}
				Call.Result = FBangoValue::MakeDouble(World->GetTimeSeconds());
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// ToEntity(name) -> entity: for Bango ID names that come from data. In script text, write @name instead.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("ToEntity")).Param(TEXT("Name"), String).Returns(Entity).InCategory(TEXT("Actors"))
				.Describe(TEXT("The entity with this Bango ID name, for names that come from data. In a script, write @name instead."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				const FString& Name = Call.Args[0].GetString();
				if (!Bango::IsValidBangoIDName(Name))
				{
					return Call.Fail(FString::Printf(TEXT("'%s' isn't a valid Bango ID name. Names use letters, digits and '_', and don't start with a digit."), *Name));
				}
				Call.Result = FBangoValue::MakeEntity(Name);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// entity SetPos(pos)
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("SetPos")).Target(Entity).Param(TEXT("Pos"), Vector3).InCategory(TEXT("Actors")).Describe(TEXT("Moves the actor."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				AActor* Actor = ResolveActor(Call, Call.Target);
				if (!Actor || !RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				const FBangoValue& Pos = Call.Args[0];
				Actor->SetActorLocation(FVector(Pos.GetVectorPart(0), Pos.GetVectorPart(1), Pos.GetVectorPart(2)));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// GetPos(entity) -> vector3. The entity is its argument, not a target: a command with no other arguments takes
		// what it works on as its only argument (SetPos, with a position to give, keeps a target).
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("GetPos")).Param(TEXT("Entity"), Entity).Returns(Vector3).InCategory(TEXT("Actors")).Describe(TEXT("The actor's location."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				const AActor* Actor = ResolveActor(Call, Call.Args[0]);
				if (!Actor)
				{
					return EBangoCommandResult::Failed;
				}
				const FVector Location = Actor->GetActorLocation();
				Call.Result = FBangoValue::MakeVector3(Location.X, Location.Y, Location.Z);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// entity SetHidden(hidden)
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("SetHidden")).Target(Entity).Param(TEXT("Hidden"), Bool).InCategory(TEXT("Actors")).Describe(TEXT("Hides or shows the actor in game."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				AActor* Actor = ResolveActor(Call, Call.Target);
				if (!Actor || !RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				Actor->SetActorHiddenInGame(Call.Args[0].GetBool());
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// entity DistanceTo(other) -> double
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("DistanceTo")).Target(Entity).Param(TEXT("Other"), Entity).Returns(Double).InCategory(TEXT("Actors")).Describe(TEXT("Distance between two actors."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				const AActor* A = ResolveActor(Call, Call.Target);
				const AActor* B = A ? ResolveActor(Call, Call.Args[0]) : nullptr;
				if (!A || !B)
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeDouble(FVector::Dist(A->GetActorLocation(), B->GetActorLocation()));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// IsNull(entity) -> bool: true if the entity is nil, or no actor has its Bango ID name (never registered, or since
		// destroyed). MayBeNil: a nil variable doesn't stop the script, and a missing actor isn't an error here.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("IsNull")).Param(TEXT("Entity"), Entity).MayBeNil().Returns(Bool).InCategory(TEXT("Actors"))
				.Describe(TEXT("True if the entity is nil, or no actor in the level has its Bango ID (it was never there, or it's been destroyed)."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				UBangoActorRegistry* Registry = UBangoActorRegistry::Get(Call.WorldContext);
				const AActor* Actor = Registry && Call.Args.Num() > 0 && !Call.Args[0].IsNil() ? Registry->FindActor(Call.Args[0].GetEntityName()) : nullptr;
				Call.Result = FBangoValue::MakeBool(!IsValid(Actor));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}
	}
}
