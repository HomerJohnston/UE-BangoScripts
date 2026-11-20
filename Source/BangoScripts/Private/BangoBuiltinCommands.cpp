#include "BangoBuiltinCommands.h"

#include "BangoScriptPath.h"

#include "BangoActorRegistry.h"
#include "BangoLog.h"
#include "BangoScriptSubsystem.h"
#include "BangoSettings.h"
#include "BangoText.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/MovementComponent.h"

namespace BangoBuiltinPrivate
{
	/** Formats Args[0] with Args[1...] the way WriteLog and Format do. */
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

	/** Formats the text Args[0] with Args[1...] the way FormatText does ({0}, {1}, ...). */
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

	/** The actor or component an entity stands for; null, with the call's error, if it's nil or isn't there. */
	static UObject* ResolveEntity(FBangoCommandCall& Call, const FBangoValue& Entity)
	{
		if (Entity.IsNil())
		{
			Call.Error = TEXT("The entity is nil.");
			return nullptr;
		}
		UBangoActorRegistry* Registry = UBangoActorRegistry::Get(Call.WorldContext);
		UObject* Object = Registry ? Registry->FindEntity(Entity.GetEntityName()) : nullptr;
		if (!Object)
		{
			Call.Error = Bango::DescribeMissingEntity(Entity.GetEntityName());
		}
		return Object;
	}

	/**
	 * What a command that places, turns or hides things works on: an actor (OutActor), or a scene component, which has
	 * a place of its own (OutScene). Commands act on the entity itself, so a component is never taken to mean its
	 * actor. Any other component fails, with the call's error naming the command.
	 */
	static bool ResolvePlaced(FBangoCommandCall& Call, const FBangoValue& Entity, const TCHAR* Command, AActor*& OutActor, USceneComponent*& OutScene)
	{
		OutActor = nullptr;
		OutScene = nullptr;
		UObject* Object = ResolveEntity(Call, Entity);
		if (!Object)
		{
			return false;
		}
		OutActor = Cast<AActor>(Object);
		OutScene = Cast<USceneComponent>(Object);
		if (!OutActor && !OutScene)
		{
			Call.Error = FString::Printf(TEXT("'%s' is a component without a place in the world (it isn't a scene component), so %s can't be used on it."),
				*Bango::GetEntityDisplayName(Entity.GetEntityName()), Command);
			return false;
		}
		return true;
	}

	/** Where something ResolvePlaced found is, and which way it faces. */
	static FVector GetPlacedLocation(const AActor* Actor, const USceneComponent* Scene)
	{
		return Actor ? Actor->GetActorLocation() : Scene->GetComponentLocation();
	}
	static FVector GetPlacedForward(const AActor* Actor, const USceneComponent* Scene)
	{
		return Actor ? Actor->GetActorForwardVector() : Scene->GetForwardVector();
	}

	/**
	 * An actor's compass bearing (GetDir, SetDir): its forward vector flattened onto the ground (Z dropped), as degrees
	 * from 0 up to 360, clockwise seen from above: 0 is +X, 90 is +Y, as an upright actor's yaw. Fails (the call's error)
	 * if it faces straight up or down, which has no bearing.
	 */
	static bool GetCompassBearing(FBangoCommandCall& Call, const FVector& Forward, const FString& Who, double& OutBearing)
	{
		const FVector2D Flat(Forward.X, Forward.Y);
		if (Flat.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
		{
			Call.Fail(FString::Printf(TEXT("'%s' faces straight up or down, so it has no compass direction."), *Who));
			return false;
		}
		const double Degrees = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
		OutBearing = Degrees < 0.0 ? Degrees + 360.0 : Degrees;
		if (OutBearing >= 360.0)
		{
			OutBearing = 0.0;
		}
		return true;
	}

	/**
	 * A character's position, in SetPos and GetPos, is the bottom of its capsule (its feet), not its middle: a script
	 * names the spot it stands on. False for an actor that isn't a character with a capsule.
	 */
	static bool GetCharacterCapsule(const AActor* Actor, float& OutHalfHeight, float& OutRadius)
	{
		const ACharacter* Character = Cast<ACharacter>(Actor);
		const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
		if (!Capsule)
		{
			return false;
		}
		OutHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		OutRadius = FMath::Min(Capsule->GetScaledCapsuleRadius(), OutHalfHeight);
		return true;
	}

	/**
	 * Where a character standing at Feet goes (its capsule's middle). Feet + the half height, unless the ground there
	 * slopes up into the capsule's rounded bottom: then it's lifted until it rests on it, by up to the half height. Found
	 * with a sphere the capsule's width, swept down against static collision onto the spot.
	 */
	static FVector GetCharacterLocationForFeet(const AActor& Character, const FVector& Feet, float HalfHeight, float Radius)
	{
		const FVector Up = FVector::UpVector;
		const FVector Resting = Feet + Up * HalfHeight;
		UWorld* World = Character.GetWorld();
		if (!World)
		{
			return Resting;
		}
		// The capsule's bottom sphere: from a half height above where it would rest down to resting on Feet.
		const FVector Start = Feet + Up * (Radius + HalfHeight);
		const FVector End = Feet + Up * Radius;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BangoSetPos), false, &Character);
		FHitResult Hit;
		if (World->SweepSingleByObjectType(Hit, Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionShape::MakeSphere(Radius), Params)
			&& !Hit.bStartPenetrating)
		{
			return Hit.Location + Up * (HalfHeight - Radius);
		}
		return Resting; // nothing under it there (it may fall), or something in the way above: as on flat ground
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

	/** A float result: fails instead if it isn't a real number (NaN or infinite), so a bad value never reaches the script. */
	static EBangoCommandResult SetFloatResult(FBangoCommandCall& Call, double Value, const TCHAR* What)
	{
		if (!FMath::IsFinite(Value))
		{
			return Call.Fail(FString::Printf(TEXT("%s isn't a number that can be used (the result is too large, or undefined)."), What));
		}
		Call.Result = FBangoValue::MakeDouble(Value);
		return EBangoCommandResult::Done;
	}

	static FString FloatText(double Value)
	{
		return FString::SanitizeFloat(Value);
	}

	static FBangoValue MakeVector(const FVector& V)
	{
		return FBangoValue::MakeVector3(V.X, V.Y, V.Z);
	}

	static FVector GetVector(const FBangoValue& Value)
	{
		return FVector(Value.GetVectorPart(0), Value.GetVectorPart(1), Value.GetVectorPart(2));
	}

	/**
	 * The frame's length in seconds: the world's, or (with no world, as in tests) the one the script was ticked with.
	 * Pauses with the game and follows time dilation, like Time().
	 */
	static double GetFrameSeconds(const FBangoCommandCall& Call)
	{
		const UWorld* World = Call.WorldContext ? Call.WorldContext->GetWorld() : nullptr;
		return World ? (double)World->GetDeltaSeconds() : (double)Call.DeltaSeconds;
	}

	/** Registers a command that takes only plain arguments and can't pause: Run gets them, checked not to be nil. */
	static void AddSimple(FBangoCommandTable& Commands, const FBangoCommandSignature& Signature, int32 RequiredArgs,
		TFunction<EBangoCommandResult(FBangoCommandCall&)> Run)
	{
		FBangoCommand Command;
		Command.Signature = Signature;
		Command.Signature.bBuiltIn = true;
		Command.Start = [RequiredArgs, Run = MoveTemp(Run)](FBangoCommandCall& Call)
		{
			if (!RequireArgs(Call, RequiredArgs))
			{
				return EBangoCommandResult::Failed;
			}
			return Run(Call);
		};
		FString Error;
		if (!Commands.Add(Command, Error))
		{
			Bango::ReportError(FString::Printf(TEXT("Couldn't register built-in command: %s"), *Error));
		}
	}

	static void Add(FBangoCommandTable& Commands, const FBangoCommand& InCommand)
	{
		FBangoCommand Command = InCommand;
		Command.Signature.bBuiltIn = true; // every command registered here comes with the plugin
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

		// WriteScreen(text, seconds?): shows a message on screen (and in the output log), like Print String. Text a game
		// shows its players goes through commands of the game's own.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("WriteScreen"))
				.Param(TEXT("Text"), String)
				.Optional()
				.Param(TEXT("Seconds"), Double, TEXT("How long it stays on screen (none: the Write Screen Seconds setting, 5 by default)"))
				.InCategory(TEXT("Text"))
				.Describe(TEXT("Shows a message on screen for a few seconds, and writes it to the output log. For testing and debugging, like Print String."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				const FString Text = Call.Args[0].GetString();
				const double Seconds = Call.Args.Num() > 1 && !Call.Args[1].IsNil() ? Call.Args[1].GetDouble() : (double)GetDefault<UBangoSettings>()->WriteScreenSeconds;
				if (Seconds < 0.0)
				{
					return Call.Fail(FString::Printf(TEXT("Can't show a message for a negative time (%s seconds)."), *FString::SanitizeFloat(Seconds)));
				}
				if (UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(Call.WorldContext))
				{
					Subsystem->WriteScreen(Text, (float)Seconds);
				}
				else
				{
					UE_LOG(LogBango, Display, TEXT("WriteScreen: %s"), *Text);
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

		// WriteLog(text, ...): writes to the output log only. (Log is the logarithm.)
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("WriteLog")).Param(TEXT("Msg"), String).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")).InCategory(TEXT("Text")).Describe(TEXT("Writes text to the output log, with {0}, {1}, ... replaced by the extra values."));
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
				.InCategory(TEXT("Flow Control")).Describe(TEXT("Pauses the script for a number of seconds, or until the next frame if no time is given."));
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

		// ---- More math (spec section 7) ----

		// Ceil(value) -> int: rounds up.
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Ceil")).Param(TEXT("Value"), Double).Returns(Int)
			.InCategory(TEXT("Math")).Describe(TEXT("Rounds up to an int.")), 1, [](FBangoCommandCall& Call)
			{
				int32 Result = 0;
				if (!ToInt(Call, FMath::CeilToDouble(Call.Args[0].GetDouble()), Result))
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeInt(Result);
				return EBangoCommandResult::Done;
			});

		// Sign(value) -> number: -1, 0 or 1, an int for an int and a float for a float.
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Sign")).NumberParam(TEXT("Value")).ReturnsNumber()
			.InCategory(TEXT("Math")).Describe(TEXT("-1 if the number is negative, 1 if it's positive, 0 if it's 0: an int for an int, a float for a float.")), 1,
			[](FBangoCommandCall& Call)
			{
				const double Value = Call.Args[0].GetNumberAsDouble();
				SetNumberResult(Call, AllInts(Call), Value > 0.0 ? 1.0 : (Value < 0.0 ? -1.0 : 0.0));
				return EBangoCommandResult::Done;
			});

		// Sqrt(value) -> float
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Sqrt")).Param(TEXT("Value"), Double).Returns(Double)
			.InCategory(TEXT("Math")).Describe(TEXT("The square root. The number can't be negative.")), 1, [](FBangoCommandCall& Call)
			{
				const double Value = Call.Args[0].GetDouble();
				if (Value < 0.0)
				{
					return Call.Fail(FString::Printf(TEXT("Can't take the square root of a negative number (%s)."), *FloatText(Value)));
				}
				return SetFloatResult(Call, FMath::Sqrt(Value), TEXT("The square root"));
			});

		// Pow(base, exponent) -> float. Always a float: 2 to the -1 is 0.5, and big powers don't fit an int.
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Pow")).Param(TEXT("Base"), Double).Param(TEXT("Exponent"), Double).Returns(Double)
			.InCategory(TEXT("Math")).Describe(TEXT("The base raised to the power of the exponent (Pow(2, 3) is 8.0).")), 2, [](FBangoCommandCall& Call)
			{
				const double Base = Call.Args[0].GetDouble();
				const double Exponent = Call.Args[1].GetDouble();
				if (Base < 0.0 && Exponent != FMath::RoundToDouble(Exponent))
				{
					return Call.Fail(FString::Printf(TEXT("A negative base (%s) can't be raised to a power that isn't a whole number (%s)."), *FloatText(Base), *FloatText(Exponent)));
				}
				if (Base == 0.0 && Exponent < 0.0)
				{
					return Call.Fail(FString::Printf(TEXT("0 can't be raised to a negative power (%s): that divides by 0."), *FloatText(Exponent)));
				}
				return SetFloatResult(Call, FMath::Pow(Base, Exponent), TEXT("The power"));
			});

		// Log(value, base = 10) -> float, Ln(value) -> float
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Log")).Param(TEXT("Value"), Double).Optional().Param(TEXT("Base"), Double, TEXT("Base (none: 10)")).Returns(Double)
			.InCategory(TEXT("Math")).Describe(TEXT("The logarithm: the power the base must be raised to to give the value. The base is 10 if none is given. To write to the output log, use WriteLog.")), 1,
			[](FBangoCommandCall& Call)
			{
				const double Value = Call.Args[0].GetDouble();
				const double Base = Call.Args.Num() > 1 && !Call.Args[1].IsNil() ? Call.Args[1].GetDouble() : 10.0;
				if (Value <= 0.0)
				{
					return Call.Fail(FString::Printf(TEXT("The logarithm needs a number greater than 0, but this is %s."), *FloatText(Value)));
				}
				if (Base <= 0.0 || Base == 1.0)
				{
					return Call.Fail(FString::Printf(TEXT("A logarithm's base must be greater than 0 and not 1, but this is %s."), *FloatText(Base)));
				}
				return SetFloatResult(Call, FMath::Loge(Value) / FMath::Loge(Base), TEXT("The logarithm"));
			});
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Ln")).Param(TEXT("Value"), Double).Returns(Double)
			.InCategory(TEXT("Math")).Describe(TEXT("The natural logarithm (base e).")), 1, [](FBangoCommandCall& Call)
			{
				const double Value = Call.Args[0].GetDouble();
				if (Value <= 0.0)
				{
					return Call.Fail(FString::Printf(TEXT("The natural logarithm needs a number greater than 0, but this is %s."), *FloatText(Value)));
				}
				return SetFloatResult(Call, FMath::Loge(Value), TEXT("The natural logarithm"));
			});

		// Lerp(a, b, alpha) -> float. Alpha isn't clamped: 2 goes past B, like Unreal's Lerp.
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Lerp")).Param(TEXT("A"), Double).Param(TEXT("B"), Double).Param(TEXT("Alpha"), Double, TEXT("0 gives A, 1 gives B"))
			.Returns(Double).InCategory(TEXT("Math")).Describe(TEXT("Blends from A to B: Alpha 0 gives A, 1 gives B, 0.5 halfway between.")), 3,
			[](FBangoCommandCall& Call)
			{
				const double A = Call.Args[0].GetDouble();
				return SetFloatResult(Call, A + (Call.Args[1].GetDouble() - A) * Call.Args[2].GetDouble(), TEXT("Lerp's result"));
			});

		// MapRange(value, inMin, inMax, outMin, outMax) -> float, clamped to the out range (Unreal's MapRangeClamped).
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("MapRange")).Param(TEXT("Value"), Double).Param(TEXT("InMin"), Double).Param(TEXT("InMax"), Double)
			.Param(TEXT("OutMin"), Double).Param(TEXT("OutMax"), Double).Returns(Double).InCategory(TEXT("Math"))
			.Describe(TEXT("Where the value is from InMin to InMax, as the same place from OutMin to OutMax, kept in that range.")), 5,
			[](FBangoCommandCall& Call)
			{
				const double InMin = Call.Args[1].GetDouble();
				const double InMax = Call.Args[2].GetDouble();
				if (InMin == InMax)
				{
					return Call.Fail(FString::Printf(TEXT("InMin and InMax are both %s, so there's no range to map from."), *FloatText(InMin)));
				}
				const double Alpha = FMath::Clamp((Call.Args[0].GetDouble() - InMin) / (InMax - InMin), 0.0, 1.0);
				const double OutMin = Call.Args[3].GetDouble();
				return SetFloatResult(Call, OutMin + (Call.Args[4].GetDouble() - OutMin) * Alpha, TEXT("MapRange's result"));
			});

		// InterpTo(current, target, speed) -> float: one frame's step towards the target (Unreal's FInterpTo).
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("InterpTo")).Param(TEXT("Current"), Double).Param(TEXT("Target"), Double)
			.Param(TEXT("Speed"), Double, TEXT("How fast it closes in (0: jumps to the target)")).Returns(Double).InCategory(TEXT("Math"))
			.Describe(TEXT("A step from Current towards Target for this frame, fast when far and slowing down as it gets close. Call it every frame (after Sleep()) to move smoothly.")), 3,
			[](FBangoCommandCall& Call)
			{
				const double Speed = Call.Args[2].GetDouble();
				if (Speed < 0.0)
				{
					return Call.Fail(FString::Printf(TEXT("The speed can't be negative (%s)."), *FloatText(Speed)));
				}
				return SetFloatResult(Call, FMath::FInterpTo(Call.Args[0].GetDouble(), Call.Args[1].GetDouble(), GetFrameSeconds(Call), Speed), TEXT("InterpTo's result"));
			});

		// DeltaTime() -> float: the frame's length in seconds.
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("DeltaTime")).Returns(Double).InCategory(TEXT("Time"))
			.Describe(TEXT("How long the last frame took, in seconds. Multiply a speed by it to move the same distance per second at any frame rate.")), 0,
			[](FBangoCommandCall& Call)
			{
				Call.Result = FBangoValue::MakeDouble(GetFrameSeconds(Call));
				return EBangoCommandResult::Done;
			});

		// Tan, Asin, Acos, Atan, Atan2, and their ...Degrees versions (which take or give degrees).
		{
			enum class ETrig : uint8 { Tan, Asin, Acos, Atan, Atan2 };
			struct FTrig2Command { const TCHAR* Name; ETrig Op; bool bDegrees; const TCHAR* Description; };
			static const FTrig2Command Trig2Commands[] =
			{
				{ TEXT("Tan"),          ETrig::Tan,   false, TEXT("The tangent of an angle in radians.") },
				{ TEXT("TanDegrees"),   ETrig::Tan,   true,  TEXT("The tangent of an angle in degrees.") },
				{ TEXT("Asin"),         ETrig::Asin,  false, TEXT("The angle, in radians, whose sine is the value (from -1 to 1).") },
				{ TEXT("AsinDegrees"),  ETrig::Asin,  true,  TEXT("The angle, in degrees, whose sine is the value (from -1 to 1).") },
				{ TEXT("Acos"),         ETrig::Acos,  false, TEXT("The angle, in radians, whose cosine is the value (from -1 to 1).") },
				{ TEXT("AcosDegrees"),  ETrig::Acos,  true,  TEXT("The angle, in degrees, whose cosine is the value (from -1 to 1).") },
				{ TEXT("Atan"),         ETrig::Atan,  false, TEXT("The angle, in radians, whose tangent is the value.") },
				{ TEXT("AtanDegrees"),  ETrig::Atan,  true,  TEXT("The angle, in degrees, whose tangent is the value.") },
				{ TEXT("Atan2"),        ETrig::Atan2, false, TEXT("The angle, in radians, of the direction (X, Y) from the X axis: from -pi to pi. Note Y comes first.") },
				{ TEXT("Atan2Degrees"), ETrig::Atan2, true,  TEXT("The angle, in degrees, of the direction (X, Y) from the X axis: from -180 to 180. Note Y comes first.") },
			};
			for (const FTrig2Command& Trig : Trig2Commands)
			{
				FBangoCommandSignature Signature = FBangoCommandSignature::Make(Trig.Name);
				if (Trig.Op == ETrig::Atan2)
				{
					Signature.Param(TEXT("Y"), Double).Param(TEXT("X"), Double);
				}
				else if (Trig.Op == ETrig::Tan)
				{
					Signature.Param(Trig.bDegrees ? TEXT("Degrees") : TEXT("Rads"), Double, Trig.bDegrees ? TEXT("An angle in degrees") : TEXT("An angle in radians"));
				}
				else
				{
					Signature.Param(TEXT("Value"), Double);
				}
				Signature.Returns(Double).InCategory(TEXT("Math")).Describe(Trig.Description);
				const ETrig Op = Trig.Op;
				const bool bDegrees = Trig.bDegrees;
				const FString Name = Trig.Name;
				AddSimple(Commands, Signature, Op == ETrig::Atan2 ? 2 : 1, [Op, bDegrees, Name](FBangoCommandCall& Call)
				{
					const double Value = Call.Args[0].GetDouble();
					if ((Op == ETrig::Asin || Op == ETrig::Acos) && (Value < -1.0 || Value > 1.0))
					{
						return Call.Fail(FString::Printf(TEXT("%s needs a value from -1 to 1, but this is %s."), *Name, *FloatText(Value)));
					}
					double Result = 0.0;
					switch (Op)
					{
					case ETrig::Tan:   Result = FMath::Tan(bDegrees ? FMath::DegreesToRadians(Value) : Value); break;
					case ETrig::Asin:  Result = FMath::Asin(Value); break;
					case ETrig::Acos:  Result = FMath::Acos(Value); break;
					case ETrig::Atan:  Result = FMath::Atan(Value); break;
					case ETrig::Atan2: Result = FMath::Atan2(Value, Call.Args[1].GetDouble()); break;
					}
					if (bDegrees && Op != ETrig::Tan)
					{
						Result = FMath::RadiansToDegrees(Result);
					}
					return SetFloatResult(Call, Result, *Name);
				});
			}
		}

		// Pi() -> float
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Pi")).Returns(Double).InCategory(TEXT("Math"))
			.Describe(TEXT("Pi, 3.14159...: half a turn in radians.")), 0, [](FBangoCommandCall& Call)
			{
				Call.Result = FBangoValue::MakeDouble(UE_DOUBLE_PI);
				return EBangoCommandResult::Done;
			});

		// RandomBool() -> bool, Chance(probability) -> bool
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("RandomBool")).Returns(Bool).InCategory(TEXT("Math"))
			.Describe(TEXT("true or false, at random (each half the time).")), 0, [](FBangoCommandCall& Call)
			{
				Call.Result = FBangoValue::MakeBool(FMath::RandBool());
				return EBangoCommandResult::Done;
			});
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Chance")).Param(TEXT("Probability"), Double, TEXT("From 0 (never) to 1 (always)")).Returns(Bool)
			.InCategory(TEXT("Math")).Describe(TEXT("true at random, as often as the probability says: Chance(0.3) is true 30% of the time.")), 1,
			[](FBangoCommandCall& Call)
			{
				const double Probability = Call.Args[0].GetDouble();
				if (Probability < 0.0 || Probability > 1.0)
				{
					return Call.Fail(FString::Printf(TEXT("The probability must be from 0 to 1 (0.3 is 30%%), but this is %s."), *FloatText(Probability)));
				}
				// FRand is in [0, 1): 0 is never true and 1 always is.
				Call.Result = FBangoValue::MakeBool(FMath::FRand() < Probability);
				return EBangoCommandResult::Done;
			});

		// IsNearlyEqual(a, b, tolerance = 0.0001) -> bool
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("IsNearlyEqual")).Param(TEXT("A"), Double).Param(TEXT("B"), Double)
			.Optional().Param(TEXT("Tolerance"), Double, TEXT("How far apart they can be (none: 0.0001)")).Returns(Bool).InCategory(TEXT("Math"))
			.Describe(TEXT("True if the two numbers are at most the tolerance apart. Use it instead of == for floats that come from math.")), 2,
			[](FBangoCommandCall& Call)
			{
				const double Tolerance = Call.Args.Num() > 2 && !Call.Args[2].IsNil() ? Call.Args[2].GetDouble() : 0.0001;
				if (Tolerance < 0.0)
				{
					return Call.Fail(FString::Printf(TEXT("The tolerance can't be negative (%s)."), *FloatText(Tolerance)));
				}
				Call.Result = FBangoValue::MakeBool(FMath::Abs(Call.Args[0].GetDouble() - Call.Args[1].GetDouble()) <= Tolerance);
				return EBangoCommandResult::Done;
			});

		// ---- Vectors (vector3) ----

		// Length(v) -> float
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Length")).Param(TEXT("Vector"), Vector3).Returns(Double).InCategory(TEXT("Vectors"))
			.Describe(TEXT("The vector's length.")), 1, [](FBangoCommandCall& Call)
			{
				return SetFloatResult(Call, GetVector(Call.Args[0]).Size(), TEXT("The length"));
			});

		// Distance(a, b) -> float
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Distance")).Param(TEXT("A"), Vector3).Param(TEXT("B"), Vector3).Returns(Double).InCategory(TEXT("Vectors"))
			.Describe(TEXT("The distance between two points. For two actors, use DistanceTo.")), 2, [](FBangoCommandCall& Call)
			{
				return SetFloatResult(Call, FVector::Dist(GetVector(Call.Args[0]), GetVector(Call.Args[1])), TEXT("The distance"));
			});

		// Normalize(v) -> vector3
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Normalize")).Param(TEXT("Vector"), Vector3).Returns(Vector3).InCategory(TEXT("Vectors"))
			.Describe(TEXT("The vector made 1 long, keeping its direction. [0, 0, 0] has no direction, so it's an error.")), 1, [](FBangoCommandCall& Call)
			{
				const FVector V = GetVector(Call.Args[0]);
				const double Length = V.Size();
				if (!(Length > UE_DOUBLE_SMALL_NUMBER) || !FMath::IsFinite(Length))
				{
					return Call.Fail(FString::Printf(TEXT("Can't normalize %s: it has no direction (it's [0, 0, 0], or too small)."), *Call.Args[0].ToDebugString()));
				}
				Call.Result = MakeVector(V / Length);
				return EBangoCommandResult::Done;
			});

		// Dot(a, b) -> float, Cross(a, b) -> vector3
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Dot")).Param(TEXT("A"), Vector3).Param(TEXT("B"), Vector3).Returns(Double).InCategory(TEXT("Vectors"))
			.Describe(TEXT("The dot product. For two directions 1 long: 1 if they're the same, 0 at right angles, -1 if opposite.")), 2, [](FBangoCommandCall& Call)
			{
				return SetFloatResult(Call, FVector::DotProduct(GetVector(Call.Args[0]), GetVector(Call.Args[1])), TEXT("The dot product"));
			});
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("Cross")).Param(TEXT("A"), Vector3).Param(TEXT("B"), Vector3).Returns(Vector3).InCategory(TEXT("Vectors"))
			.Describe(TEXT("The cross product: a vector at right angles to both.")), 2, [](FBangoCommandCall& Call)
			{
				Call.Result = MakeVector(FVector::CrossProduct(GetVector(Call.Args[0]), GetVector(Call.Args[1])));
				return EBangoCommandResult::Done;
			});

		// LerpVector(a, b, alpha) -> vector3
		AddSimple(Commands, FBangoCommandSignature::Make(TEXT("LerpVector")).Param(TEXT("A"), Vector3).Param(TEXT("B"), Vector3).Param(TEXT("Alpha"), Double, TEXT("0 gives A, 1 gives B"))
			.Returns(Vector3).InCategory(TEXT("Vectors")).Describe(TEXT("Blends from A to B: Alpha 0 gives A, 1 gives B, 0.5 halfway between.")), 3,
			[](FBangoCommandCall& Call)
			{
				const FVector A = GetVector(Call.Args[0]);
				Call.Result = MakeVector(A + (GetVector(Call.Args[1]) - A) * Call.Args[2].GetDouble());
				return EBangoCommandResult::Done;
			});

		// Exec(path, args...) -> int: spawns a script file, like UBangoScriptSubsystem::Spawn; the result is its handle's id.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Exec")).Param(TEXT("Path"), String, TEXT("The script: relative to this script's folder (\"Intro\", \"../Shared/Greet\"), or a full asset path (\"/Game/Quests/Intro\")"))
				.Variadic(TEXT("Arg "), 1).Returns(Int).Discardable().InCategory(TEXT("Flow Control"))
				.Describe(TEXT("Starts another script, which runs on its own alongside this one. The extra values are its parameters, in order. It runs straight away until it first pauses; then this script carries on. Gives back the new script's id."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(Call.WorldContext);
				if (!Subsystem)
				{
					return Call.Fail(TEXT("Exec needs a game world to start the script in."));
				}
				TArray<FBangoValue> ScriptArgs;
				for (int32 i = 1; i < Call.Args.Num(); ++i)
				{
					ScriptArgs.Add(Call.Args[i]);
				}
				// Relative to the calling script's folder (a script spawned from text has none: then relative to /Game).
				const FString Path = Call.Args[0].GetString();
				const FString Caller = Subsystem->GetScriptPath(Call.InstanceId);
				const FString Folder = Caller.IsEmpty() ? FString(Bango::DefaultScriptFolder()) : Bango::GetScriptPathFolder(Caller);
				FString Resolved;
				FString Error;
				if (!Bango::ResolveScriptPath(Path, Folder, Resolved, Error))
				{
					return Call.Fail(Error);
				}
				const FBangoScriptHandle Handle = Subsystem->SpawnChild(Call.InstanceId, Resolved, ScriptArgs, Error); // its child, for Abort's Children
				if (!Handle.IsValid())
				{
					return Call.Fail(FString::Printf(TEXT("Couldn't start '%s': %s"), *Path, Error.IsEmpty() ? TEXT("it didn't start.") : *Error));
				}
				Call.Result = FBangoValue::MakeInt(Handle.Id);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// Abort(Script = this one, Children = false): stops a running script (UBangoScriptSubsystem::Terminate).
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("Abort"))
				.Optional()
				.Param(TEXT("Script"), Int, TEXT("The script's id (from Exec or spawn). Leave it out, or nil, for this script")).MayBeNil()
				.Param(TEXT("Children"), Bool, TEXT("Also stop the scripts it started (with Exec or spawn), and the ones they started, and so on"))
				.InCategory(TEXT("Flow Control"))
				.Describe(TEXT("Stops a running script, or this one if no id is given. With Children true, also stops every script it started (with Exec or spawn) that's still running, and theirs. Does nothing for a script that has already ended (but still stops its children, with Children true)."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(Call.WorldContext);
				if (!Subsystem)
				{
					return Call.Fail(TEXT("Abort needs a game world, with its running scripts."));
				}
				FBangoScriptHandle Handle;
				Handle.Id = Call.Args.Num() > 0 && !Call.Args[0].IsNil() ? Call.Args[0].GetInt() : (int32)Call.InstanceId;
				const bool bChildren = Call.Args.Num() > 1 && !Call.Args[1].IsNil() && Call.Args[1].GetBool();
				Subsystem->Terminate(Handle, bChildren); // if that's this script, it stops as soon as this command returns
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// IsRunning(Script) -> bool: UBangoScriptSubsystem::IsRunning.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("IsRunning"))
				.Param(TEXT("Script"), Int, TEXT("The script's id (from Exec or spawn)"))
				.Returns(Bool).InCategory(TEXT("Flow Control"))
				.Describe(TEXT("True while the script with this id (from Exec or spawn) is running or waiting; false once it has ended, for any reason."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(Call.WorldContext);
				if (!Subsystem)
				{
					return Call.Fail(TEXT("IsRunning needs a game world, with its running scripts."));
				}
				FBangoScriptHandle Handle;
				Handle.Id = Call.Args[0].GetInt();
				Call.Result = FBangoValue::MakeBool(Subsystem->IsRunning(Handle));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// WaitFor(Script): latent, until that script ends.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("WaitFor"))
				.Param(TEXT("Script"), Int, TEXT("The script's id (from Exec or spawn)"))
				.InCategory(TEXT("Flow Control"))
				.Describe(TEXT("Waits until the script with this id (from Exec or spawn) has ended, for any reason. Carries on at once if it already has."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(Call.WorldContext);
				if (!Subsystem)
				{
					return Call.Fail(TEXT("WaitFor needs a game world, with its running scripts."));
				}
				FBangoScriptHandle Handle;
				Handle.Id = Call.Args[0].GetInt();
				if (Handle.Id == (int32)Call.InstanceId)
				{
					return Call.Fail(TEXT("A script can't wait for itself to end: it would wait forever."));
				}
				if (!Subsystem->IsRunning(Handle))
				{
					return EBangoCommandResult::Done;
				}
				Call.LatentState.Add(FBangoValue::MakeInt(Handle.Id));
				return EBangoCommandResult::Pending;
			};
			Command.Poll = [](FBangoCommandCall& Call)
			{
				UBangoScriptSubsystem* Subsystem = UBangoScriptSubsystem::Get(Call.WorldContext);
				FBangoScriptHandle Handle;
				Handle.Id = Call.LatentState.Num() > 0 ? Call.LatentState[0].GetInt() : 0;
				return Subsystem && Subsystem->IsRunning(Handle) ? EBangoCommandResult::Pending : EBangoCommandResult::Done;
			};
			Add(Commands, Command);
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
			Command.Signature = FBangoCommandSignature::Make(TEXT("SetPos")).Target(Entity).Param(TEXT("Pos"), Vector3).InCategory(TEXT("Actors"))
				.Describe(TEXT("Moves the actor, or the scene component (in world space). A character stands on Pos: its feet go there, lifted onto a slope if the ground rises under it."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				AActor* Actor = nullptr;
				USceneComponent* Scene = nullptr;
				if (!ResolvePlaced(Call, Call.Target, TEXT("SetPos"), Actor, Scene) || !RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				const FBangoValue& Pos = Call.Args[0];
				FVector Location(Pos.GetVectorPart(0), Pos.GetVectorPart(1), Pos.GetVectorPart(2));
				if (Scene)
				{
					Scene->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
					return EBangoCommandResult::Done;
				}
				float HalfHeight = 0.0f;
				float Radius = 0.0f;
				if (GetCharacterCapsule(Actor, HalfHeight, Radius))
				{
					Location = GetCharacterLocationForFeet(*Actor, Location, HalfHeight, Radius);
				}
				Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// GetPos(entity) -> vector3. The entity is its argument, not a target: a command with no other arguments takes
		// what it works on as its only argument (SetPos, with a position to give, keeps a target).
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("GetPos")).Param(TEXT("Entity"), Entity).Returns(Vector3).InCategory(TEXT("Actors"))
				.Describe(TEXT("The actor's or scene component's location (world space). For a character, where its feet are (the bottom of its capsule), as SetPos takes it."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				AActor* Actor = nullptr;
				USceneComponent* Scene = nullptr;
				if (!RequireArgs(Call, 1) || !ResolvePlaced(Call, Call.Args[0], TEXT("GetPos"), Actor, Scene))
				{
					return EBangoCommandResult::Failed;
				}
				FVector Location = GetPlacedLocation(Actor, Scene);
				float HalfHeight = 0.0f;
				float Radius = 0.0f;
				if (Actor && GetCharacterCapsule(Actor, HalfHeight, Radius))
				{
					Location -= FVector::UpVector * HalfHeight;
				}
				Call.Result = FBangoValue::MakeVector3(Location.X, Location.Y, Location.Z);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// GetDir(entity) -> float: the actor's compass bearing. Like GetPos, the entity is its only argument.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("GetDir")).Param(TEXT("Entity"), Entity).Returns(Double).InCategory(TEXT("Actors"))
				.Describe(TEXT("The actor's or scene component's compass bearing, in degrees from 0 up to 360: which way it faces, seen from above. 0 is the world's +X, 90 is +Y (turning clockwise from above), as an upright actor's yaw."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				AActor* Actor = nullptr;
				USceneComponent* Scene = nullptr;
				double Bearing = 0.0;
				if (!RequireArgs(Call, 1) || !ResolvePlaced(Call, Call.Args[0], TEXT("GetDir"), Actor, Scene)
					|| !GetCompassBearing(Call, GetPlacedForward(Actor, Scene), Bango::GetEntityDisplayName(Call.Args[0].GetEntityName()), Bearing))
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeDouble(Bearing);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// entity SetDir(bearing): turns the actor about the world's up axis to face that compass bearing.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("SetDir")).Target(Entity).Param(TEXT("Bearing"), Double).InCategory(TEXT("Actors"))
				.Describe(TEXT("Turns the actor or scene component about the world's up axis so it faces Bearing, a compass bearing in degrees (as GetDir gives), the short way round. Its tilt (pitch and roll) is kept."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				AActor* Actor = nullptr;
				USceneComponent* Scene = nullptr;
				double Current = 0.0;
				if (!ResolvePlaced(Call, Call.Target, TEXT("SetDir"), Actor, Scene) || !RequireArgs(Call, 1)
					|| !GetCompassBearing(Call, GetPlacedForward(Actor, Scene), Bango::GetEntityDisplayName(Call.Target.GetEntityName()), Current))
				{
					return EBangoCommandResult::Failed;
				}
				if (Scene)
				{
					const double SceneDelta = FMath::FindDeltaAngleDegrees(Current, Call.Args[0].GetNumberAsDouble());
					if (!FMath::IsNearlyZero(SceneDelta))
					{
						Scene->SetWorldRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(SceneDelta)) * Scene->GetComponentQuat(), false, nullptr, ETeleportType::TeleportPhysics);
					}
					return EBangoCommandResult::Done;
				}
				// The short way round: -180 to 180 degrees. Bearings turn clockwise from above, as yaw does about +Z.
				const double Delta = FMath::FindDeltaAngleDegrees(Current, Call.Args[0].GetNumberAsDouble());
				if (FMath::IsNearlyZero(Delta))
				{
					return EBangoCommandResult::Done;
				}
				const FQuat Turn(FVector::UpVector, FMath::DegreesToRadians(Delta));
				Actor->SetActorRotation(Turn * Actor->GetActorQuat(), ETeleportType::TeleportPhysics);
				// A pawn that turns with its controller (as the player's usually does) would be turned back by it next
				// frame: turn the controller the same way.
				if (const APawn* Pawn = Cast<APawn>(Actor))
				{
					AController* Controller = Pawn->GetController();
					if (Controller && Pawn->bUseControllerRotationYaw)
					{
						FRotator Control = Controller->GetControlRotation();
						Control.Yaw += Delta;
						Controller->SetControlRotation(Control);
					}
				}
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// GetVelocity(entity) -> vector3: the actor's velocity, read from what SetVelocity would set. Like GetPos, the
		// entity is its only argument.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("GetVelocity")).Param(TEXT("Entity"), Entity).Returns(Vector3).InCategory(TEXT("Actors"))
				.Describe(TEXT("The actor's velocity, in cm per second (world space): its character movement's, its physics-simulating root's, or its movement component's, as SetVelocity sets. Otherwise its root's velocity, which is [0, 0, 0] for an actor nothing moves. A movement component's own velocity, or a scene component's (its physics velocity if it simulates physics)."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				if (!RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				UObject* Object = ResolveEntity(Call, Call.Args[0]);
				if (!Object)
				{
					return EBangoCommandResult::Failed;
				}
				if (const UMovementComponent* OwnMovement = Cast<UMovementComponent>(Object))
				{
					Call.Result = MakeVector(OwnMovement->Velocity);
					return EBangoCommandResult::Done;
				}
				if (const USceneComponent* Scene = Cast<USceneComponent>(Object))
				{
					const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Scene);
					Call.Result = MakeVector(Primitive && Primitive->IsSimulatingPhysics() ? Primitive->GetPhysicsLinearVelocity() : Scene->GetComponentVelocity());
					return EBangoCommandResult::Done;
				}
				const AActor* Actor = Cast<AActor>(Object);
				if (!Actor)
				{
					return Call.Fail(FString::Printf(TEXT("'%s' is a component that doesn't move (not a movement or scene component), so GetVelocity can't be used on it."),
						*Bango::GetEntityDisplayName(Call.Args[0].GetEntityName())));
				}
				FVector Velocity = Actor->GetVelocity(); // the root's: for an actor none of the below move
				if (const ACharacter* Character = Cast<ACharacter>(Actor); Character && Character->GetCharacterMovement())
				{
					Velocity = Character->GetCharacterMovement()->Velocity;
				}
				else if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); Root && Root->IsSimulatingPhysics())
				{
					Velocity = Root->GetPhysicsLinearVelocity();
				}
				else if (const UMovementComponent* Movement = Actor->FindComponentByClass<UMovementComponent>())
				{
					Velocity = Movement->Velocity;
				}
				Call.Result = MakeVector(Velocity);
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// entity SetVelocity(velocity): what moves it decides how: a character's movement, a simulating root, or another
		// movement component (a projectile's, a floating pawn's).
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("SetVelocity")).Target(Entity).Param(TEXT("Velocity"), Vector3).InCategory(TEXT("Actors"))
				.Describe(TEXT("Sets the actor's velocity, in cm per second (world space). A character keeps moving from it under its own movement rules (friction, gravity, its input), and one on the ground given an upward velocity leaves the ground. A physics-simulating actor gets it as its linear velocity. Fails for an actor with nothing that moves it. On a component: a movement component's velocity, or a physics-simulating one's linear velocity."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				UObject* Object = ResolveEntity(Call, Call.Target);
				if (!Object || !RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				const FVector Velocity = GetVector(Call.Args[0]);

				// A component: the movement component itself, or a primitive that simulates physics.
				if (UMovementComponent* OwnMovement = Cast<UMovementComponent>(Object))
				{
					OwnMovement->Velocity = Velocity;
					if (UCharacterMovementComponent* CharacterMovement = Cast<UCharacterMovementComponent>(OwnMovement); CharacterMovement && Velocity.Z > 0.0 && CharacterMovement->IsMovingOnGround())
					{
						CharacterMovement->SetMovementMode(MOVE_Falling);
					}
					OwnMovement->UpdateComponentVelocity();
					return EBangoCommandResult::Done;
				}
				if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Object); Primitive && Primitive->IsSimulatingPhysics())
				{
					Primitive->SetPhysicsLinearVelocity(Velocity);
					return EBangoCommandResult::Done;
				}
				AActor* Actor = Cast<AActor>(Object);
				if (!Actor)
				{
					return Call.Fail(FString::Printf(TEXT("'%s' is a component that nothing moves (not a movement component, or one simulating physics), so it has no velocity to set."),
						*Bango::GetEntityDisplayName(Call.Target.GetEntityName())));
				}

				// A character: its movement's velocity. On the ground, an upward velocity would be lost to the ground
				// snapping it back down, so it falls (jumps) instead, as launching a character does.
				if (const ACharacter* Character = Cast<ACharacter>(Actor))
				{
					if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
					{
						Movement->Velocity = Velocity;
						if (Velocity.Z > 0.0 && Movement->IsMovingOnGround())
						{
							Movement->SetMovementMode(MOVE_Falling);
						}
						Movement->UpdateComponentVelocity();
						return EBangoCommandResult::Done;
					}
				}
				// Physics: a root that simulates it.
				if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); Root && Root->IsSimulatingPhysics())
				{
					Root->SetPhysicsLinearVelocity(Velocity);
					return EBangoCommandResult::Done;
				}
				// Another movement component (projectile, floating pawn, ...).
				if (UMovementComponent* Movement = Actor->FindComponentByClass<UMovementComponent>())
				{
					Movement->Velocity = Velocity;
					Movement->UpdateComponentVelocity();
					return EBangoCommandResult::Done;
				}
				Call.Fail(FString::Printf(TEXT("'%s' has nothing that moves it (no character movement, physics-simulating root or movement component), so it has no velocity to set."), *Actor->GetName()));
				return EBangoCommandResult::Failed;
			};
			Add(Commands, Command);
		}

		// entity SetHidden(hidden)
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("SetHidden")).Target(Entity).Param(TEXT("Hidden"), Bool).InCategory(TEXT("Actors")).Describe(TEXT("Hides or shows the actor in game, or the scene component (and only it, not its children)."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				AActor* Actor = nullptr;
				USceneComponent* Scene = nullptr;
				if (!ResolvePlaced(Call, Call.Target, TEXT("SetHidden"), Actor, Scene) || !RequireArgs(Call, 1))
				{
					return EBangoCommandResult::Failed;
				}
				if (Scene)
				{
					Scene->SetHiddenInGame(Call.Args[0].GetBool());
					return EBangoCommandResult::Done;
				}
				Actor->SetActorHiddenInGame(Call.Args[0].GetBool());
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// entity DistanceTo(other) -> double
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("DistanceTo")).Target(Entity).Param(TEXT("Other"), Entity).Returns(Double).InCategory(TEXT("Actors")).Describe(TEXT("Distance between two actors or scene components (their locations)."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				AActor* ActorA = nullptr;
				USceneComponent* SceneA = nullptr;
				AActor* ActorB = nullptr;
				USceneComponent* SceneB = nullptr;
				if (!RequireArgs(Call, 1) || !ResolvePlaced(Call, Call.Target, TEXT("DistanceTo"), ActorA, SceneA)
					|| !ResolvePlaced(Call, Call.Args[0], TEXT("DistanceTo"), ActorB, SceneB))
				{
					return EBangoCommandResult::Failed;
				}
				Call.Result = FBangoValue::MakeDouble(FVector::Dist(GetPlacedLocation(ActorA, SceneA), GetPlacedLocation(ActorB, SceneB)));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}

		// IsNull(entity) -> bool: true if the entity is nil, or no actor has its Bango ID name (never registered, or since
		// destroyed). MayBeNil: a nil variable doesn't stop the script, and a missing actor isn't an error here.
		{
			FBangoCommand Command;
			Command.Signature = FBangoCommandSignature::Make(TEXT("IsNull")).Param(TEXT("Entity"), Entity).MayBeNil().Returns(Bool).InCategory(TEXT("Actors"))
				.Describe(TEXT("True if the entity is nil, or what it stands for isn't there: no actor in the level has its Bango ID (it was never there, or it's been destroyed), or the component's been removed."));
			Command.Start = [](FBangoCommandCall& Call)
			{
				UBangoActorRegistry* Registry = UBangoActorRegistry::Get(Call.WorldContext);
				const UObject* Object = Registry && Call.Args.Num() > 0 && !Call.Args[0].IsNil() ? Registry->FindEntity(Call.Args[0].GetEntityName()) : nullptr;
				Call.Result = FBangoValue::MakeBool(!IsValid(Object));
				return EBangoCommandResult::Done;
			};
			Add(Commands, Command);
		}
	}
}
