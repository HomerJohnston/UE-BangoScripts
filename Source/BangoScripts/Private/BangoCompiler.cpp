#include "BangoCompiler.h"
#include "BangoText.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace BangoCompilerPrivate
{
	// -----------------------------------------------------------------------
	// Compile-time types
	// -----------------------------------------------------------------------

	/** "an int", "a string[]" */
	static FString Article(const FBangoType& Type)
	{
		const FString Name = Type.ToString();
		const bool bVowel = Type.Base == EBangoBaseType::Int || Type.Base == EBangoBaseType::Entity;
		return FString::Printf(TEXT("%s %s"), bVowel ? TEXT("an") : TEXT("a"), *Name);
	}

	enum class ETypeKind : uint8
	{
		Known,       // a real type
		Unknown,     // a global this script didn't declare: checked when the script runs
		Nil,         // the nil literal, typed by where it's used
		EmptyArray,  // [] with no type known yet (only while inferring an array literal's type)
		Void,        // a call that returns nothing
	};

	struct FExprType
	{
		ETypeKind Kind = ETypeKind::Void;
		FBangoType Type;

		static FExprType MakeKnown(const FBangoType& InType) { FExprType T; T.Kind = ETypeKind::Known; T.Type = InType; return T; }
		static FExprType MakeUnknown() { FExprType T; T.Kind = ETypeKind::Unknown; return T; }
		static FExprType MakeNil() { FExprType T; T.Kind = ETypeKind::Nil; return T; }
		static FExprType MakeEmptyArray() { FExprType T; T.Kind = ETypeKind::EmptyArray; return T; }
		static FExprType MakeVoid() { return FExprType(); }

		bool IsKnown() const { return Kind == ETypeKind::Known; }
		bool IsUnknown() const { return Kind == ETypeKind::Unknown; }
		bool Is(const FBangoType& Other) const { return Kind == ETypeKind::Known && Type == Other; }
		bool IsKnownArray() const { return Kind == ETypeKind::Known && Type.IsArray(); }
		bool IsNumber() const { return Kind == ETypeKind::Known && !Type.IsArray() && (Type.Base == EBangoBaseType::Int || Type.Base == EBangoBaseType::Double); }
		/** 2 or 3 for a known vector2 / vector3, otherwise 0. */
		int32 VectorSize() const { return Kind == ETypeKind::Known ? Type.GetVectorSize() : 0; }

		FString Describe() const
		{
			switch (Kind)
			{
			case ETypeKind::Known:      return Article(Type);
			case ETypeKind::Unknown:    return TEXT("a value whose type is only known when it runs (an undeclared global, or a 'call')");
			case ETypeKind::Nil:        return TEXT("nil");
			case ETypeKind::EmptyArray: return TEXT("an empty array");
			default:                    return TEXT("nothing (the call doesn't return a value)");
			}
		}
	};


	// -----------------------------------------------------------------------
	// Compiler state
	// -----------------------------------------------------------------------

	struct FLocalVar
	{
		FString Name;
		FBangoType Type;
		int32 Slot = 0;
	};

	struct FScope
	{
		int32 BlockId = 0;
		TArray<FLocalVar> Vars;
	};

	struct FLoop
	{
		int32 ContinueTarget = -1;  // -1 until known (for loops: the step comes after the body)
		TArray<int32> BreakJumps;
		TArray<int32> ContinueJumps;
	};

	struct FLabel
	{
		int32 Address = 0;
		int32 BlockId = 0;
		int32 Line = 0;
		/** The locals (slots) in scope at the label, for ResolveGotos: a goto mustn't jump past a declaration. */
		TArray<int32> SlotsInScope;
	};

	struct FGotoFixup
	{
		int32 Instruction = 0;
		FString Name;
		int32 BlockId = 0;
		FBangoSourceLocation Loc;
		const FBangoStmt* Stmt = nullptr;
		TArray<int32> SlotsInScope;
	};

	/** A local (or loop variable) declared somewhere in the graph being compiled, for out-of-scope errors. */
	struct FGraphLocal
	{
		int32 Line = 0;
		bool bLoopVar = false;
		bool bDisabled = false;
	};

	struct FPlaceInfo
	{
		int32 PlaceIndex = 0;
		int32 NumIndexes = 0;
		FExprType Type;
		FString Text;  // "_x", "_grid[...]", "_pos.x"
		/** Its last index picks a vector's part or a string's character (`_v[i]`, `_names[0][2]`): never nil. */
		bool bElement = false;
	};

	static const TCHAR* PartName(int32 Part)
	{
		return Part == 0 ? TEXT("x") : Part == 1 ? TEXT("y") : TEXT("z");
	}

	enum class EPlaceUse : uint8
	{
		Read,
		Write,
		Check,  // isNil
	};

	class FCompiler
	{
	public:
		FCompiler(const FBangoScript& InScript, const IBangoCommandLookup& InCommands, FBangoProgram& InProgram, FBangoCompileError& InError)
			: Script(InScript), Commands(InCommands), Program(InProgram), Error(InError)
		{
		}

		/** For Bango::ParseFixedValue: true if Expr is a fixed value (a literal, a negated number, or an array of those). */
		static bool IsFixedValueExpr(const FBangoExpr& Expr) { return IsFixedValue(Expr); }

		/** A condition (Bango::CompileCondition): the main graph returns a bool. */
		bool bCondition = false;
		/** An inline script's inputs signature: its inputs are the main graph's parameters, and #inputs is an error. */
		const FBangoInputsSignature* InputsSignature = nullptr;

		bool Run()
		{
			Program = FBangoProgram();
			Program.FileName = Script.FileName;
			Program.bIsCondition = bCondition;

			if (!CheckDeclarations() || !CollectGlobals() || !CollectFunctions())
			{
				return false;
			}

			// Top level
			if (!CompileFunctionBody(0, nullptr, Script.TopLevel))
			{
				return false;
			}

			for (int32 i = 0; i < Script.Functions.Num(); ++i)
			{
				if (!CompileFunctionBody(i + 1, Script.Functions[i].Get(), Script.Functions[i]->Body))
				{
					return false;
				}
			}
			if (bFailed)
			{
				return false;
			}
			// Now every function's code is there, each waitUntil's condition can be followed into the functions it calls.
			for (FBangoWaitSet& Set : Program.WaitSets)
			{
				FBangoDependencies Found = Bango::FindDependencies(Program, Set.Function, Set.CondBegin, Set.CondEnd);
				Set.Globals = MoveTemp(Found.Globals);
				Set.PollReason = MoveTemp(Found.PollReason);
				const bool bCanChange = Set.Globals.Num() > 0 || !Set.PollReason.IsEmpty();
				if (bCanChange && Set.TimeoutCondIP >= 0)
				{
					// A timeout that's a condition: what it reads wakes the script too.
					FBangoDependencies Timeout = Bango::FindDependencies(Program, Set.Function, Set.TimeoutCondBegin, Set.TimeoutCondEnd);
					if (Timeout.Globals.Num() == 0 && Timeout.PollReason.IsEmpty())
					{
						FBangoSourceLocation Loc;
						Loc.Line = Set.Line;
						Loc.Column = Set.Column;
						Fail(Loc, TEXT("This waitUntil's timeout is a condition that can't change while the script waits: it reads no global, and calls no command that could answer differently later (locals and inputs don't change while a script waits). So its block would run at once, or never. Read a global in it, call a command like Time(), or give the timeout a number of seconds."));
						Error.NodeID = Set.NodeID;
						return false;
					}
					for (const FString& Global : Timeout.Globals)
					{
						Set.Globals.AddUnique(Global);
					}
					Set.Globals.Sort();
					if (Set.PollReason.IsEmpty())
					{
						Set.PollReason = MoveTemp(Timeout.PollReason);
					}
				}
				if (!bCanChange)
				{
					// Nothing it reads can change while the script waits there: it would carry on at once, or never.
					FBangoSourceLocation Loc;
					Loc.Line = Set.Line;
					Loc.Column = Set.Column;
					Fail(Loc, Set.TimeoutIP >= 0
						? TEXT("This waitUntil's condition can't change while the script waits: it reads no global, and calls no command that could answer differently later (locals and inputs don't change while a script waits). So it would carry on at once, or always wait for its whole timeout. Read a global in it, or use Sleep.")
						: TEXT("This waitUntil's condition can't change while the script waits: it reads no global, and calls no command that could answer differently later (locals and inputs don't change while a script waits). So it would carry on at once, or wait forever. Read a global in it, or use an if."));
					Error.NodeID = Set.NodeID;
					return false;
				}
			}
			if (bCondition && Program.Functions.Num() > 0)
			{
				Program.ConditionDependencies = Bango::FindDependencies(Program, 0, 0, Program.Functions[0].Code.Num());
			}
			// Last, once everything that reads the code has: the common runs of instructions, for the VM to run in one go.
			Bango::FuseInstructions(Program);
			return true;
		}

	private:
		const FBangoScript& Script;

		/** Every text literal's "namespace\nkey", and the first literal that used it (spec section 3, keys must be unique). */
		TMap<FString, const FBangoLiteralExpr*> TextKeys;

		/** The value of a text literal: localized text with its namespace (the script's LOCTEXT_NAMESPACE for LOCTEXT) and key. */
		bool MakeTextValue(const FBangoLiteralExpr& Lit, FBangoValue& OutValue)
		{
			if (Lit.bTextInvariant)
			{
				// INVTEXT: never translated, so it needs no namespace or key.
				OutValue = FBangoValue::MakeText(Bango::MakeInvariantText(Lit.StringValue));
				return true;
			}
			FString Namespace;
			if (Lit.bTextHasNamespace)
			{
				Namespace = Lit.TextNamespace;
			}
			else if (Script.bHasLocNamespace)
			{
				Namespace = Script.LocNamespace;
			}
			else
			{
				return Fail(Lit.Loc, TEXT("This text needs the script's namespace: add the line #LOCTEXT_NAMESPACE \"Bango.Intro\" at the top of the script (under its description). (NSLOCTEXT and NSNOTELOCTEXT give their own namespace instead.)"));
			}

			const FString Id = Namespace + TEXT("\n") + Lit.TextKey;
			if (const FBangoLiteralExpr* const* Earlier = TextKeys.Find(Id))
			{
				const FBangoLiteralExpr& First = **Earlier;
				if (!First.StringValue.Equals(Lit.StringValue, ESearchCase::CaseSensitive) || !First.TextNotes.Equals(Lit.TextNotes, ESearchCase::CaseSensitive))
				{
					const FBangoSourceLocation& KeyLoc = Lit.TextArgLocs.IsValidIndex(Lit.bTextHasNamespace ? 1 : 0) ? Lit.TextArgLocs[Lit.bTextHasNamespace ? 1 : 0] : Lit.Loc;
					return Fail(KeyLoc, FString::Printf(TEXT("The key '%s' is already used in namespace '%s' for a different text, on line %d. Every text needs its own key."), *Lit.TextKey, *Namespace, First.Loc.Line));
				}
			}
			else
			{
				TextKeys.Add(Id, &Lit);
			}
			OutValue = FBangoValue::MakeText(Bango::MakeLocalizedText(Namespace, Lit.TextKey, Lit.StringValue, Lit.TextNotes));
			return true;
		}
		const IBangoCommandLookup& Commands;
		FBangoProgram& Program;
		FBangoCompileError& Error;
		bool bFailed = false;
		/** While DeclareScriptParams declares the signature's inputs (DeclareLocal's check for them doesn't apply). */
		bool bDeclaringSignature = false;

		TBangoNameMap<FBangoType> DeclaredGlobals;
		TBangoNameMap<int32> FunctionIndexes;
		/**
		 * The main graph's inputs and script variables (their slots are in its frame), which the script's functions see
		 * too. Filled while the main graph is compiled, which is before any function is.
		 */
		TArray<FLocalVar> ScriptVars;
		/** The main graph's locals and loop variables, which functions don't see: for the error that says so. */
		TBangoNameMap<FGraphLocal> MainGraphLocals;

		// Per function
		FBangoFunctionCode* Fn = nullptr;
		int32 FnIndex = 0;
		const FBangoFunction* FnAst = nullptr;
		TArray<FScope> Scopes;
		TArray<FLoop> Loops;
		TArray<int32> BlockParents;
		/** The blocks (BlockParents indexes) that are spawn blocks: a goto can't jump out of one, or into one. */
		TArray<int32> SpawnBlockIds;
		/** The blocks that are a for or forEach loop (its header's scope, which holds its body): a goto can't jump into one. */
		TArray<int32> LoopBlockIds;
		/** For each spawn block being compiled, inside each other: how many loops were open when it started. */
		TArray<int32> SpawnLoopBases;
		TBangoNameMap<FLabel> Labels;
		/** Labels in disabled statements, by name, with their lines: a goto to one gets its own error. */
		TBangoNameMap<int32> DisabledLabels;
		TArray<FGotoFixup> Gotos;
		const FBangoStmt* CurrentStmt = nullptr;
		FBangoSourceLocation CurrentLoc;

		/** True while compiling array literal elements only to learn their types. */
		bool bInferring = false;
		/** Calls whose type parameters were worked out ahead of compiling their arguments (InferTypeParams). */
		TMap<const FBangoCallExpr*, TArray<FExprType>> InferredTypeParams;

		// ---- Errors ----

		bool Fail(const FBangoSourceLocation& Loc, const FString& Message)
		{
			if (!bFailed)
			{
				bFailed = true;
				Error.Line = Loc.Line;
				Error.Column = Loc.Column;
				Error.Message = Message;
				Error.NodeID = -1;
				if (CurrentStmt && CurrentStmt->Node.bHasMetadata && CurrentStmt->Node.Line == Loc.Line)
				{
					Error.NodeID = CurrentStmt->Node.NodeID;
				}
			}
			return false;
		}

		// ---- Pools ----

		int32 AddConstant(const FBangoValue& Value)
		{
			for (int32 i = 0; i < Program.Constants.Num(); ++i)
			{
				if (Program.Constants[i] == Value)
				{
					return i;
				}
			}
			return Program.Constants.Add(Value);
		}

		int32 AddName(const FString& Name)
		{
			for (int32 i = 0; i < Program.Names.Num(); ++i)
			{
				if (Program.Names[i].Equals(Name, ESearchCase::CaseSensitive))
				{
					return i;
				}
			}
			return Program.Names.Add(Name);
		}

		int32 AddType(const FBangoType& Type)
		{
			for (int32 i = 0; i < Program.Types.Num(); ++i)
			{
				if (Program.Types[i] == Type)
				{
					return i;
				}
			}
			return Program.Types.Add(Type);
		}

		int32 AddPlace(EBangoPlaceKind Kind, int32 Index, int32 NumIndexes, int32 Part = -1)
		{
			for (int32 i = 0; i < Fn->Places.Num(); ++i)
			{
				const FBangoPlace& Existing = Fn->Places[i];
				if (Existing.Kind == Kind && Existing.Index == Index && Existing.NumIndexes == NumIndexes && Existing.Part == Part)
				{
					return i;
				}
			}
			FBangoPlace Place;
			Place.Kind = Kind;
			Place.Index = Index;
			Place.NumIndexes = NumIndexes;
			Place.Part = Part;
			return Fn->Places.Add(Place);
		}

		int32 AllocSlot(const FBangoType& Type, const FString& Name)
		{
			Fn->SlotNames.Add(Name);
			Fn->SlotNodeIDs.Add(LoopNodeForSlots);
			SlotLines.Add(CurrentLoc.Line);
			Fn->SlotStartValues.Add(FBangoValue::MakeNil(Type));
			return Fn->SlotTypes.Add(Type);
		}

		/**
		 * For reloading (FBangoFunctionCode::SlotNodeIDs): the loop whose slots are being made, or the local
		 * declaration node whose slot is (its NodeID; -1 without metadata, matched by name), or -1.
		 */
		int64 LoopNodeForSlots = -1;
		/** The line each slot of the graph being compiled was declared on (for goto errors). */
		TArray<int32> SlotLines;
		/** Every local and loop variable declared in the graph being compiled, in scope here or not (CollectGraphLocals). */
		TBangoNameMap<FGraphLocal> GraphLocals;

		/** The value AllocSlot gives slots made while compiling Stmt's loop header. */
		static int64 LoopSlotNode(const FBangoStmt& Stmt)
		{
			return Stmt.Node.bHasMetadata ? Stmt.Node.NodeID : -2;
		}

		// ---- Emitting ----

		int32 Here() const { return Fn->Code.Num(); }

		int32 Emit(EBangoOp Op, int32 A = 0, int32 B = 0, int32 C = 0)
		{
			FBangoInstruction Instruction;
			Instruction.Op = Op;
			Instruction.A = A;
			Instruction.B = B;
			Instruction.C = C;

			FBangoDebugLoc Debug;
			Debug.Line = CurrentLoc.Line;
			Debug.Column = CurrentLoc.Column;
			if (CurrentStmt && CurrentStmt->Node.bHasMetadata)
			{
				Debug.NodeID = CurrentStmt->Node.NodeID;
			}

			Fn->Debug.Add(Debug);
			return Fn->Code.Add(Instruction);
		}

		void PatchJump(int32 Instruction, int32 Target)
		{
			Fn->Code[Instruction].A = Target;
		}

		void EmitConvertIfNeeded(const FBangoType& Target, const FExprType& Source)
		{
			if (Source.IsUnknown() || (Source.IsKnown() && Source.Type != Target))
			{
				Emit(EBangoOp::Convert, AddType(Target));
			}
		}

		// ---- Type rules ----

		bool CanAssign(const FBangoType& Target, const FExprType& Source, const FBangoSourceLocation& Loc, const FString& What)
		{
			switch (Source.Kind)
			{
			case ETypeKind::Known:
				if (Target.IsAssignableFrom(Source.Type))
				{
					return true;
				}
				return Fail(Loc, FString::Printf(TEXT("%s needs %s, but this is %s."), *What, *Article(Target), *Source.Describe()));
			case ETypeKind::Unknown:
			case ETypeKind::Nil:
				return true;
			case ETypeKind::EmptyArray:
				if (Target.IsArray())
				{
					return true;
				}
				return Fail(Loc, FString::Printf(TEXT("%s needs %s, but this is an array."), *What, *Article(Target)));
			default:
				return Fail(Loc, FString::Printf(TEXT("%s needs %s, but this call doesn't return a value."), *What, *Article(Target)));
			}
		}

		bool CheckCondition(const FExprType& Type, const FBangoSourceLocation& Loc)
		{
			if (Type.Is(FBangoType::Bool()) || Type.IsUnknown())
			{
				return true;
			}
			return Fail(Loc, FString::Printf(TEXT("A condition must be a bool, but this is %s. There is no automatic conversion to bool."), *Type.Describe()));
		}

		bool CheckUsableValue(const FExprType& Type, const FBangoSourceLocation& Loc, const TCHAR* Where)
		{
			switch (Type.Kind)
			{
			case ETypeKind::Void:
				return Fail(Loc, FString::Printf(TEXT("This call doesn't return a value, so it can't be used %s."), Where));
			case ETypeKind::Nil:
				return Fail(Loc, FString::Printf(TEXT("nil can't be used %s. To check whether something is nil, use isNil(...)."), Where));
			case ETypeKind::EmptyArray:
				return Fail(Loc, FString::Printf(TEXT("An array can't be used %s."), Where));
			default:
				return true;
			}
		}

		static const TCHAR* BinaryOpText(EBangoBinaryOp Op)
		{
			switch (Op)
			{
			case EBangoBinaryOp::Add:          return TEXT("+");
			case EBangoBinaryOp::Subtract:     return TEXT("-");
			case EBangoBinaryOp::Multiply:     return TEXT("*");
			case EBangoBinaryOp::Divide:       return TEXT("/");
			case EBangoBinaryOp::Modulo:       return TEXT("%");
			case EBangoBinaryOp::Less:         return TEXT("<");
			case EBangoBinaryOp::LessEqual:    return TEXT("<=");
			case EBangoBinaryOp::Greater:      return TEXT(">");
			case EBangoBinaryOp::GreaterEqual: return TEXT(">=");
			case EBangoBinaryOp::Equal:        return TEXT("==");
			case EBangoBinaryOp::NotEqual:     return TEXT("!=");
			case EBangoBinaryOp::And:          return TEXT("&&");
			case EBangoBinaryOp::Or:           return TEXT("||");
			default:                           return TEXT("?");
			}
		}

		static EBangoOp BinaryOpCode(EBangoBinaryOp Op)
		{
			switch (Op)
			{
			case EBangoBinaryOp::Add:          return EBangoOp::Add;
			case EBangoBinaryOp::Subtract:     return EBangoOp::Subtract;
			case EBangoBinaryOp::Multiply:     return EBangoOp::Multiply;
			case EBangoBinaryOp::Divide:       return EBangoOp::Divide;
			case EBangoBinaryOp::Modulo:       return EBangoOp::Modulo;
			case EBangoBinaryOp::Less:         return EBangoOp::Less;
			case EBangoBinaryOp::LessEqual:    return EBangoOp::LessEqual;
			case EBangoBinaryOp::Greater:      return EBangoOp::Greater;
			case EBangoBinaryOp::GreaterEqual: return EBangoOp::GreaterEqual;
			case EBangoBinaryOp::Equal:        return EBangoOp::Equal;
			case EBangoBinaryOp::NotEqual:     return EBangoOp::NotEqual;
			default:                           return EBangoOp::Nop;
			}
		}

		/** The result type of Left Op Right (not && / ||). */
		bool BinaryResult(EBangoBinaryOp Op, const FExprType& Left, const FExprType& Right, const FBangoSourceLocation& Loc, FExprType& OutType)
		{
			const TCHAR* OpText = BinaryOpText(Op);
			const bool bEquality = Op == EBangoBinaryOp::Equal || Op == EBangoBinaryOp::NotEqual;

			if (Left.Kind == ETypeKind::Nil || Right.Kind == ETypeKind::Nil)
			{
				if (bEquality)
				{
					return Fail(Loc, TEXT("Comparing with nil isn't allowed, because comparing reads the value. Use isNil(...) instead."));
				}
				return Fail(Loc, FString::Printf(TEXT("nil can't be used with '%s'."), OpText));
			}
			for (const FExprType* Side : { &Left, &Right })
			{
				if (Side->Kind == ETypeKind::Void)
				{
					return Fail(Loc, FString::Printf(TEXT("One side of '%s' is a call that doesn't return a value."), OpText));
				}
				if (Side->Kind == ETypeKind::EmptyArray || Side->IsKnownArray())
				{
					return Fail(Loc, FString::Printf(TEXT("Arrays can't be used with '%s'."), OpText));
				}
			}

			if (Left.VectorSize() > 0 || Right.VectorSize() > 0)
			{
				return VectorBinaryResult(Op, Left, Right, Loc, OutType);
			}

			const bool bLeftNumber = Left.IsNumber() || Left.IsUnknown();
			const bool bRightNumber = Right.IsNumber() || Right.IsUnknown();

			// '+' joins two strings; otherwise it's arithmetic like the operators below.
			if (Op == EBangoBinaryOp::Add)
			{
				if (Left.Is(FBangoType::Text()) || Right.Is(FBangoType::Text()))
				{
					return Fail(Loc, TEXT("Texts can't be joined with '+': other languages may need the parts in another order. Put the parts in one text with placeholders instead: FormatText(LOCTEXT(\"Key\", \"{0} {1}\"), _a, _b)."));
				}
				const bool bLeftString = Left.Is(FBangoType::String());
				const bool bRightString = Right.Is(FBangoType::String());
				if (bLeftString || bRightString)
				{
					if ((bLeftString || Left.IsUnknown()) && (bRightString || Right.IsUnknown()))
					{
						OutType = FExprType::MakeKnown(FBangoType::String());
						return true;
					}
					return Fail(Loc, FString::Printf(TEXT("'+' can join two strings, but this is %s + %s. Convert the number to a string first."),
						*Left.Describe(), *Right.Describe()));
				}
			}

			switch (Op)
			{
			case EBangoBinaryOp::Add:
			case EBangoBinaryOp::Subtract:
			case EBangoBinaryOp::Multiply:
			case EBangoBinaryOp::Divide:
			case EBangoBinaryOp::Modulo:
				if (!bLeftNumber || !bRightNumber)
				{
					return Fail(Loc, FString::Printf(TEXT("'%s' needs numbers, but this is %s %s %s."), OpText, *Left.Describe(), OpText, *Right.Describe()));
				}
				if (Left.IsUnknown() || Right.IsUnknown())
				{
					OutType = FExprType::MakeUnknown();
				}
				else if (Left.Type.Base == EBangoBaseType::Int && Right.Type.Base == EBangoBaseType::Int)
				{
					OutType = FExprType::MakeKnown(FBangoType::Int());
				}
				else
				{
					OutType = FExprType::MakeKnown(FBangoType::Double());
				}
				return true;

			case EBangoBinaryOp::Less:
			case EBangoBinaryOp::LessEqual:
			case EBangoBinaryOp::Greater:
			case EBangoBinaryOp::GreaterEqual:
				if (!bLeftNumber || !bRightNumber)
				{
					return Fail(Loc, FString::Printf(TEXT("'%s' compares numbers, but this is %s %s %s."), OpText, *Left.Describe(), OpText, *Right.Describe()));
				}
				OutType = FExprType::MakeKnown(FBangoType::Bool());
				return true;

			case EBangoBinaryOp::Equal:
			case EBangoBinaryOp::NotEqual:
				if (Left.IsKnown() && Right.IsKnown())
				{
					const bool bBothNumbers = Left.IsNumber() && Right.IsNumber();
					if (!bBothNumbers && Left.Type != Right.Type)
					{
						return Fail(Loc, FString::Printf(TEXT("'%s' compares two values of the same type, but this is %s %s %s."), OpText, *Left.Describe(), OpText, *Right.Describe()));
					}
				}
				OutType = FExprType::MakeKnown(FBangoType::Bool());
				return true;

			default:
				return Fail(Loc, TEXT("Unexpected operator."));
			}
		}

		/** BinaryResult when at least one side is a known vector. */
		bool VectorBinaryResult(EBangoBinaryOp Op, const FExprType& Left, const FExprType& Right, const FBangoSourceLocation& Loc, FExprType& OutType)
		{
			const TCHAR* OpText = BinaryOpText(Op);
			const FExprType& Vector = Left.VectorSize() > 0 ? Left : Right;
			const bool bLeftVector = Left.VectorSize() > 0;
			const bool bRightVector = Right.VectorSize() > 0;
			const bool bLeftScalar = Left.IsNumber() || Left.IsUnknown();
			const bool bRightScalar = Right.IsNumber() || Right.IsUnknown();
			const FString Shown = FString::Printf(TEXT("%s %s %s"), *Left.Describe(), OpText, *Right.Describe());

			switch (Op)
			{
			case EBangoBinaryOp::Add:
			case EBangoBinaryOp::Subtract:
				if ((bLeftVector || Left.IsUnknown()) && (bRightVector || Right.IsUnknown()) && (!bLeftVector || !bRightVector || Left.Type == Right.Type))
				{
					OutType = FExprType::MakeKnown(Vector.Type);
					return true;
				}
				return Fail(Loc, FString::Printf(TEXT("'%s' works on two vectors of the same kind, but this is %s. To change every part by a number, use a vector, like _v + [1, 1, 1]."), OpText, *Shown));

			case EBangoBinaryOp::Multiply:
				if ((bLeftVector && bRightScalar) || (bLeftScalar && bRightVector))
				{
					OutType = FExprType::MakeKnown(Vector.Type);
					return true;
				}
				if (bLeftVector && bRightVector)
				{
					return Fail(Loc, TEXT("Two vectors can't be multiplied with '*'. Multiply a vector by a number instead, like _v * 2."));
				}
				return Fail(Loc, FString::Printf(TEXT("'*' multiplies a vector by a number, but this is %s."), *Shown));

			case EBangoBinaryOp::Divide:
				if (bLeftVector && bRightScalar)
				{
					OutType = FExprType::MakeKnown(Vector.Type);
					return true;
				}
				if (!bLeftVector)
				{
					return Fail(Loc, FString::Printf(TEXT("A vector can be divided by a number, but nothing can be divided by a vector (this is %s)."), *Shown));
				}
				return Fail(Loc, FString::Printf(TEXT("'/' divides a vector by a number, but this is %s."), *Shown));

			case EBangoBinaryOp::Modulo:
				return Fail(Loc, TEXT("'%' doesn't work on vectors."));

			case EBangoBinaryOp::Less:
			case EBangoBinaryOp::LessEqual:
			case EBangoBinaryOp::Greater:
			case EBangoBinaryOp::GreaterEqual:
				return Fail(Loc, FString::Printf(TEXT("Vectors can't be compared with '%s'. Compare their parts instead, like _a.x %s _b.x."), OpText, OpText));

			case EBangoBinaryOp::Equal:
			case EBangoBinaryOp::NotEqual:
				if ((bLeftVector || Left.IsUnknown()) && (bRightVector || Right.IsUnknown()) && (!bLeftVector || !bRightVector || Left.Type == Right.Type))
				{
					OutType = FExprType::MakeKnown(FBangoType::Bool());
					return true;
				}
				return Fail(Loc, FString::Printf(TEXT("'%s' compares two values of the same type, but this is %s."), OpText, *Shown));

			default:
				return Fail(Loc, TEXT("Unexpected operator."));
			}
		}

		// ---- Where declarations may be (spec section 4) ----

		/** A value allowed as a local's starting value: a literal, a negated number, or an array of those. */
		static bool IsFixedValue(const FBangoExpr& Expr)
		{
			if (Expr.Kind == EBangoExprKind::Literal || Expr.Kind == EBangoExprKind::Empty)
			{
				return true; // an empty value fails later, as missing
			}
			if (const FBangoUnaryExpr* Unary = Expr.As<FBangoUnaryExpr>())
			{
				const FBangoLiteralExpr* Operand = Unary->Operand.IsValid() ? Unary->Operand->As<FBangoLiteralExpr>() : nullptr;
				return Unary->Op == EBangoUnaryOp::Negate && Operand
					&& (Operand->LiteralKind == EBangoLiteralKind::Int || Operand->LiteralKind == EBangoLiteralKind::Double);
			}
			if (const FBangoArrayLiteralExpr* ArrayExpr = Expr.As<FBangoArrayLiteralExpr>())
			{
				for (const FBangoExprPtr& Element : ArrayExpr->Elements)
				{
					if (!Element.IsValid() || !IsFixedValue(*Element))
					{
						return false;
					}
				}
				return true;
			}
			return false;
		}

		bool FailMisplacedDecl(const FBangoVarDeclStmt& Decl, bool bInFunction)
		{
			CurrentStmt = &Decl;
			if (Decl.bGlobal && Decl.bDefault)
			{
				return Fail(Decl.Loc, FString::Printf(TEXT("A global's default ('?=') goes in its declaration at the top of the file, after the settings lines (#inputs and the others) and before everything else: move 'global %s %s ?= ...;' up there. To set it here, each time this runs, write '=': 'global %s %s = ...;'."),
					*Decl.Type.ToString(), *Decl.Name, *Decl.Type.ToString(), *Decl.Name));
			}
			if (Decl.bGlobal)
			{
				return Fail(Decl.Loc, FString::Printf(TEXT("A global declared in the code must be given a value, like 'global %s %s = ...;' (it's set to it each time this runs). To only declare it, put 'global %s %s;' at the top of the file, after the settings lines (#inputs and the others) and before everything else%s."),
					*Decl.Type.ToString(), *Decl.Name, *Decl.Type.ToString(), *Decl.Name, bInFunction ? TEXT(", not in a function") : TEXT("")));
			}
			return Fail(Decl.Loc, FString::Printf(TEXT("scriptVar declarations go at the top of their %s, before its code: move 'scriptVar %s %s;' up there. Or drop 'scriptVar' to declare a local here, which exists from here to the end of its block."),
				bInFunction ? TEXT("function") : TEXT("script"), *Decl.Type.ToString(), *Decl.Name));
		}

		/**
		 * A scriptVar declaration, or a global's without a value or with a default (?=), nested in a block (they only
		 * go at the top). Locals, and globals set in the code (`global bool b = true;`), can be declared in blocks; a
		 * for header isn't in a block.
		 */
		bool CheckNoDeclsInBlock(const FBangoBlock& Block, bool bInFunction)
		{
			for (const FBangoStmtPtr& Stmt : Block)
			{
				if (!CheckNoDeclsInStmt(*Stmt, bInFunction))
				{
					return false;
				}
			}
			return true;
		}

		bool CheckNoDeclsInStmt(const FBangoStmt& Stmt, bool bInFunction)
		{
			switch (Stmt.Kind)
			{
			case EBangoStmtKind::VarDecl:
			{
				const FBangoVarDeclStmt& Decl = *Stmt.As<FBangoVarDeclStmt>();
				return Decl.IsLocal() || Decl.IsGlobalAssignment() || FailMisplacedDecl(Decl, bInFunction);
			}
			case EBangoStmtKind::If:
			{
				const FBangoIfStmt& If = *Stmt.As<FBangoIfStmt>();
				return CheckNoDeclsInBlock(If.Then, bInFunction) && CheckNoDeclsInBlock(If.Else, bInFunction);
			}
			case EBangoStmtKind::While:
				return CheckNoDeclsInBlock(Stmt.As<FBangoWhileStmt>()->Body, bInFunction);
			case EBangoStmtKind::For:
				return CheckNoDeclsInBlock(Stmt.As<FBangoForStmt>()->Body, bInFunction);
			case EBangoStmtKind::ForEach:
				return CheckNoDeclsInBlock(Stmt.As<FBangoForEachStmt>()->Body, bInFunction);
			case EBangoStmtKind::Sequence:
				for (const FBangoBlock& Block : Stmt.As<FBangoSequenceStmt>()->Blocks)
				{
					if (!CheckNoDeclsInBlock(Block, bInFunction))
					{
						return false;
					}
				}
				return true;
			case EBangoStmtKind::Spawn:
				return CheckNoDeclsInBlock(Stmt.As<FBangoSpawnStmt>()->Body, bInFunction);
			case EBangoStmtKind::WaitUntil:
				return CheckNoDeclsInBlock(Stmt.As<FBangoWaitUntilStmt>()->TimeoutBlock, bInFunction);
			case EBangoStmtKind::Switch:
			{
				const FBangoSwitchStmt& Switch = *Stmt.As<FBangoSwitchStmt>();
				for (const FBangoSwitchCase& Case : Switch.Cases)
				{
					if (!CheckNoDeclsInBlock(Case.Body, bInFunction))
					{
						return false;
					}
				}
				return CheckNoDeclsInBlock(Switch.Default, bInFunction);
			}
			default:
				return true;
			}
		}

		/**
		 * Global and scriptVar declarations at the top aren't graph nodes (globals and script variables are listed in the
		 * editor's side panel), so they have no metadata. (A local's declaration is a node, and so is a global's in the code.)
		 */
		bool CheckDeclHasNoMetadata(const FBangoVarDeclStmt& Decl)
		{
			if (Decl.Node.bHasMetadata)
			{
				CurrentStmt = nullptr; // the error isn't on a node
				return Fail(Decl.Loc, TEXT("A global or scriptVar declaration isn't a graph node, so it can't have node metadata: remove the //@ comment."));
			}
			return true;
		}

		/** A scriptVar declaration: no metadata, and its starting value must be a fixed value. */
		bool CheckScriptVarDecl(const FBangoVarDeclStmt& Decl)
		{
			if (!CheckDeclHasNoMetadata(Decl))
			{
				return false;
			}
			if (Decl.Init.IsValid() && !IsFixedValue(*Decl.Init))
			{
				CurrentStmt = &Decl;
				return Fail(Decl.Init->Loc, FString::Printf(TEXT("A scriptVar's starting value must be a fixed value: a number, a string, true, false, nil, a Bango ID like @john, a text, or an array of those. Drop 'scriptVar' to declare a local, which can be given any value where it's declared: '%s %s = ...;'."),
					*Decl.Type.ToString(), *Decl.Name));
			}
			return true;
		}

		/**
		 * Globals are declared at the top of the file (after the settings lines (#inputs and the others), before the main graph's script
		 * variables and anything else, functions included), once each, with a fixed default (?=) or none; or in the
		 * code, anywhere, with a value (`global bool b = true;`, a statement), any number of times. Script variables
		 * (scriptVar) are declared only at the top of their graph, before its code, with fixed starting values. Locals
		 * (plain declarations) can be anywhere, so they aren't checked here; nor are loop variables.
		 */
		bool CheckDeclarations()
		{
			int32 FirstFunctionLine = MAX_int32;
			for (const TUniquePtr<FBangoFunction>& Function : Script.Functions)
			{
				FirstFunctionLine = FMath::Min(FirstFunctionLine, Function->Loc.Line);
			}

			TMap<FString, int32> GlobalLines;
			bool bInLead = true;
			bool bSeenScriptVar = false;
			for (const FBangoStmtPtr& Stmt : Script.TopLevel)
			{
				if (Stmt->Kind == EBangoStmtKind::Params)
				{
					continue;
				}
				const FBangoVarDeclStmt* Decl = Stmt->As<FBangoVarDeclStmt>();
				if (!Decl || Decl->IsLocal() || Decl->IsGlobalAssignment())
				{
					// The script's code (a local's declaration is code, and so is a global's with a value): no more
					// globals or scriptVars after it.
					bInLead = false;
					if (!CheckNoDeclsInStmt(*Stmt, false))
					{
						return false;
					}
					continue;
				}
				if (!bInLead || Decl->Loc.Line > FirstFunctionLine)
				{
					return FailMisplacedDecl(*Decl, false);
				}
				CurrentStmt = Decl;
				if (Decl->bGlobal)
				{
					if (!CheckDeclHasNoMetadata(*Decl))
					{
						return false;
					}
					if (bSeenScriptVar)
					{
						return Fail(Decl->Loc, FString::Printf(TEXT("Global declarations come before the script's scriptVars: move 'global %s %s;' above them."), *Decl->Type.ToString(), *Decl->Name));
					}
					// A default (used only if the global doesn't exist yet or is nil) is fixed, like a scriptVar's.
					if (Decl->Init.IsValid() && !IsFixedValue(*Decl->Init))
					{
						CurrentStmt = Decl;
						return Fail(Decl->Init->Loc, FString::Printf(TEXT("A global's default must be a fixed value: a number, a string, true, false, nil, a Bango ID like @john, a text, or an array of those. It's only used if the global doesn't exist yet or is nil. To set it to something worked out, set it in the code: 'global %s %s = ...;'."),
							*Decl->Type.ToString(), *Decl->Name));
					}
					if (const int32* Line = GlobalLines.Find(Decl->Name))
					{
						return Fail(Decl->Loc, FString::Printf(TEXT("Global '%s' is already declared on line %d."), *Decl->Name, *Line));
					}
					GlobalLines.Add(Decl->Name, Decl->Loc.Line);
				}
				else
				{
					bSeenScriptVar = true;
					if (!CheckScriptVarDecl(*Decl))
					{
						return false;
					}
				}
			}

			for (const TUniquePtr<FBangoFunction>& Function : Script.Functions)
			{
				bool bInFunctionLead = true;
				for (const FBangoStmtPtr& Stmt : Function->Body)
				{
					const FBangoVarDeclStmt* Decl = Stmt->As<FBangoVarDeclStmt>();
					if (!Decl || Decl->IsLocal() || Decl->IsGlobalAssignment())
					{
						bInFunctionLead = false;
						if (!CheckNoDeclsInStmt(*Stmt, true))
						{
							return false;
						}
						continue;
					}
					if (!bInFunctionLead || Decl->bGlobal)
					{
						return FailMisplacedDecl(*Decl, true);
					}
					if (!CheckScriptVarDecl(*Decl))
					{
						return false;
					}
				}
			}
			CurrentStmt = nullptr;
			return true;
		}

		// ---- File-level collection ----

		bool CollectGlobalsInBlock(const FBangoBlock& Block)
		{
			for (const FBangoStmtPtr& Stmt : Block)
			{
				if (!CollectGlobalsInStmt(*Stmt))
				{
					return false;
				}
			}
			return true;
		}

		bool CollectGlobalsInStmt(const FBangoStmt& Stmt)
		{
			switch (Stmt.Kind)
			{
			case EBangoStmtKind::VarDecl:
			{
				const FBangoVarDeclStmt& Decl = *Stmt.As<FBangoVarDeclStmt>();
				if (!Decl.bGlobal)
				{
					return true;
				}
				if (const FBangoType* Existing = DeclaredGlobals.Find(Decl.Name))
				{
					if (*Existing != Decl.Type)
					{
						CurrentStmt = &Stmt;
						return Fail(Stmt.Loc, FString::Printf(TEXT("Global '%s' is declared as %s here, but as %s elsewhere in this script."),
							*Decl.Name, *Decl.Type.ToString(), *Existing->ToString()));
					}
					return true;
				}
				DeclaredGlobals.Add(Decl.Name, Decl.Type);
				FBangoGlobalDecl Global;
				Global.Name = Decl.Name;
				Global.Type = Decl.Type;
				Global.Line = Stmt.Loc.Line;
				if (Decl.Init.IsValid() && Decl.bDefault)
				{
					CurrentStmt = &Stmt;
					if (!EvaluateConstant(*Decl.Init, Decl.Type, Decl.Name, Global.Default))
					{
						return false;
					}
					Global.bHasDefault = true;
				}
				Program.Globals.Add(Global);
				return true;
			}
			case EBangoStmtKind::If:
			{
				const FBangoIfStmt& If = *Stmt.As<FBangoIfStmt>();
				return CollectGlobalsInBlock(If.Then) && CollectGlobalsInBlock(If.Else);
			}
			case EBangoStmtKind::While:
				return CollectGlobalsInBlock(Stmt.As<FBangoWhileStmt>()->Body);
			case EBangoStmtKind::For:
				return CollectGlobalsInBlock(Stmt.As<FBangoForStmt>()->Body);
			case EBangoStmtKind::ForEach:
				return CollectGlobalsInBlock(Stmt.As<FBangoForEachStmt>()->Body);
			case EBangoStmtKind::Sequence:
				for (const FBangoBlock& Block : Stmt.As<FBangoSequenceStmt>()->Blocks)
				{
					if (!CollectGlobalsInBlock(Block))
					{
						return false;
					}
				}
				return true;
			case EBangoStmtKind::Spawn:
				return CollectGlobalsInBlock(Stmt.As<FBangoSpawnStmt>()->Body);
			case EBangoStmtKind::WaitUntil:
				return CollectGlobalsInBlock(Stmt.As<FBangoWaitUntilStmt>()->TimeoutBlock);
			case EBangoStmtKind::Switch:
			{
				const FBangoSwitchStmt& Switch = *Stmt.As<FBangoSwitchStmt>();
				for (const FBangoSwitchCase& Case : Switch.Cases)
				{
					if (!CollectGlobalsInBlock(Case.Body))
					{
						return false;
					}
				}
				return CollectGlobalsInBlock(Switch.Default);
			}
			default:
				return true;
			}
		}

		bool CollectGlobals()
		{
			if (!CollectGlobalsInBlock(Script.TopLevel))
			{
				return false;
			}
			for (const TUniquePtr<FBangoFunction>& Function : Script.Functions)
			{
				if (!CollectGlobalsInBlock(Function->Body))
				{
					return false;
				}
			}
			return true;
		}

		/** For error messages: a function or command whose name matches when case is ignored. */
		FString FindCallableNameIgnoringCase(const FString& Name) const
		{
			for (const auto& Pair : FunctionIndexes)
			{
				if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase))
				{
					return Pair.Key;
				}
			}
			return Commands.FindNameIgnoringCase(Name);
		}

		bool CollectFunctions()
		{
			Program.Functions.AddDefaulted(1 + Script.Functions.Num());

			for (int32 i = 0; i < Script.Functions.Num(); ++i)
			{
				const FBangoFunction& Function = *Script.Functions[i];
				CurrentStmt = nullptr;

				if (FunctionIndexes.Contains(Function.Name))
				{
					return Fail(Function.Loc, FString::Printf(TEXT("There is already a function named '%s' in this script."), *Function.Name));
				}
				if (Commands.FindCommand(Function.Name))
				{
					return Fail(Function.Loc, FString::Printf(TEXT("'%s' is already a command, so it can't be used as a function name."), *Function.Name));
				}

				for (int32 p = 0; p < Function.Params.Num(); ++p)
				{
					for (int32 q = 0; q < p; ++q)
					{
						if (Function.Params[p].Name.Equals(Function.Params[q].Name, ESearchCase::CaseSensitive))
						{
							return Fail(Function.Params[p].Loc, FString::Printf(TEXT("Two parameters are named '%s'."), *Function.Params[p].Name));
						}
					}
				}

				FunctionIndexes.Add(Function.Name, i + 1);
				FBangoFunctionCode& Code = Program.Functions[i + 1];
				Code.Name = Function.Name;
				Code.bReturnsVoid = Function.bReturnsVoid;
				Code.ReturnType = Function.ReturnType;
				Code.NumParams = Function.Params.Num();
			}
			return true;
		}

		// ---- Functions ----

		bool CompileFunctionBody(int32 FunctionIndex, const FBangoFunction* Ast, const FBangoBlock& Body)
		{
			Fn = &Program.Functions[FunctionIndex];
			FnIndex = FunctionIndex;
			FnAst = Ast;
			Scopes.Reset();
			Loops.Reset();
			BlockParents.Reset();
			SpawnBlockIds.Reset();
			LoopBlockIds.Reset();
			SpawnLoopBases.Reset();
			Labels = TBangoNameMap<FLabel>();
			DisabledLabels = TBangoNameMap<int32>();
			Gotos.Reset();
			SlotLines.Reset();
			GraphLocals = TBangoNameMap<FGraphLocal>();
			CurrentStmt = nullptr;
			CurrentLoc = Ast ? Ast->Loc : FBangoSourceLocation();
			CollectGraphLocals(Body, false);

			PushScope();
			if (!Ast && !DeclareScriptParams(Body))
			{
				return false;
			}
			if (Ast)
			{
				for (const FBangoParam& Param : Ast->Params)
				{
					if (!CheckNotAGlobal(Param.Name, Param.Loc) || !CheckNotAScriptVar(Param.Name, Param.Loc))
					{
						return false;
					}
					FLocalVar Var;
					Var.Name = Param.Name;
					Var.Type = Param.Type;
					Var.Slot = AllocSlot(Param.Type, Param.Name);
					Scopes.Last().Vars.Add(Var);
				}
			}

			for (const FBangoStmtPtr& Stmt : Body)
			{
				if (!CompileStmt(*Stmt))
				{
					return false;
				}
			}

			CurrentStmt = nullptr;
			if (!Ast)
			{
				MainGraphLocals = GraphLocals;
			}
			if (!Ast)
			{
				// A condition must say true or false; reaching the end is an error when it runs.
				Emit(bCondition ? EBangoOp::ErrorMissingReturn : EBangoOp::Halt);
			}
			else if (Ast->bReturnsVoid)
			{
				Emit(EBangoOp::Return);
			}
			else
			{
				Emit(EBangoOp::ErrorMissingReturn);
			}
			PopScope();

			return ResolveGotos();
		}

		/** Evaluates an inputs default (a literal, negated number or array of those) as a value of Type. */
		bool EvaluateConstant(const FBangoExpr& Expr, const FBangoType& Type, const FString& ParamName, FBangoValue& OutValue)
		{
			FBangoValue Raw;
			if (const FBangoLiteralExpr* Lit = Expr.As<FBangoLiteralExpr>())
			{
				switch (Lit->LiteralKind)
				{
				case EBangoLiteralKind::Bool:   Raw = FBangoValue::MakeBool(Lit->BoolValue); break;
				case EBangoLiteralKind::Int:    Raw = FBangoValue::MakeInt((int32)Lit->IntValue); break;
				case EBangoLiteralKind::Double: Raw = FBangoValue::MakeDouble(Lit->DoubleValue); break;
				case EBangoLiteralKind::String: Raw = FBangoValue::MakeString(Lit->StringValue); break;
				case EBangoLiteralKind::Entity: Raw = FBangoValue::MakeEntity(Lit->StringValue); break;
				case EBangoLiteralKind::Text:
					if (!MakeTextValue(*Lit, Raw))
					{
						return false;
					}
					break;
				default:                        Raw = FBangoValue::MakeNil(Type); break;
				}
			}
			else if (const FBangoUnaryExpr* Unary = Expr.As<FBangoUnaryExpr>())
			{
				const FBangoLiteralExpr* Operand = Unary->Operand->As<FBangoLiteralExpr>();
				check(Operand);
				Raw = Operand->LiteralKind == EBangoLiteralKind::Int ? FBangoValue::MakeInt((int32)-Operand->IntValue) : FBangoValue::MakeDouble(-Operand->DoubleValue);
			}
			else if (const FBangoArrayLiteralExpr* VectorExpr = Type.IsVector() ? Expr.As<FBangoArrayLiteralExpr>() : nullptr)
			{
				if (!CheckVectorLiteralSize(*VectorExpr, Type))
				{
					return false;
				}
				double Parts[3] = { 0.0, 0.0, 0.0 };
				for (int32 i = 0; i < VectorExpr->Elements.Num(); ++i)
				{
					const FBangoExpr& Element = *VectorExpr->Elements[i];
					FBangoValue PartValue;
					if (!CheckVectorPartNotNil(Element, Type, i) || !EvaluateConstant(Element, FBangoType::Double(), ParamName, PartValue))
					{
						return false;
					}
					Parts[i] = PartValue.GetDouble();
				}
				Raw = Type.GetVectorSize() == 2 ? FBangoValue::MakeVector2(Parts[0], Parts[1]) : FBangoValue::MakeVector3(Parts[0], Parts[1], Parts[2]);
			}
			else if (const FBangoArrayLiteralExpr* ArrayExpr = Expr.As<FBangoArrayLiteralExpr>())
			{
				if (!Type.IsArray())
				{
					return Fail(Expr.Loc, FString::Printf(TEXT("The default for '%s' must be %s, but it is an array."), *ParamName, *Article(Type)));
				}
				const FBangoType ElementType = Type.GetElementType();
				Raw = FBangoValue::MakeArray(ElementType);
				for (const FBangoExprPtr& Element : ArrayExpr->Elements)
				{
					FBangoValue ElementValue;
					if (!EvaluateConstant(*Element, ElementType, ParamName, ElementValue))
					{
						return false;
					}
					FString PushError;
					if (!Raw.Push(ElementValue, PushError))
					{
						return Fail(Element->Loc, FString::Printf(TEXT("Default for '%s': %s"), *ParamName, *PushError));
					}
				}
			}
			else
			{
				return Fail(Expr.Loc, FString::Printf(TEXT("The default for '%s' must be a fixed value."), *ParamName));
			}

			if (!Raw.TryConvertTo(Type, OutValue))
			{
				return Fail(Expr.Loc, FString::Printf(TEXT("The default for '%s' must be %s, but it is %s."),
					*ParamName, *Article(Type), *Article(Raw.GetType())));
			}
			return true;
		}

		/** Declares the inputs line's parameters (or the inputs signature's) as the first slots of the top level. */
		bool DeclareScriptParams(const FBangoBlock& Body)
		{
			const FBangoParamsStmt* ParamsStmt = Body.Num() > 0 ? Body[0]->As<FBangoParamsStmt>() : nullptr;
			if (InputsSignature)
			{
				if (ParamsStmt)
				{
					return Fail(ParamsStmt->Loc, FString::Printf(TEXT("This script's inputs are given by %s: %s. Remove the #inputs line; the script can use them as they are."),
						*InputsSignature->Label, *InputsSignature->GetNames()));
				}
				// They come from C++, so errors about them point at the start of the script.
				FBangoSourceLocation Start;
				Start.Line = 1;
				Start.Column = 1;
				bDeclaringSignature = true;
				for (int32 i = 0; i < InputsSignature->Params.Num(); ++i)
				{
					const FBangoScriptParamInfo& Input = InputsSignature->Params[i];
					int32 Slot = 0;
					if (!DeclareLocal(Input.Name, Input.Type, Start, Slot))
					{
						return false;
					}
					check(Slot == i);
					Program.Params.Add(Input);
					ScriptVars.Add(Scopes.Last().Vars.Last());
				}
				bDeclaringSignature = false;
				Fn->NumParams = InputsSignature->Params.Num();
				return true;
			}
			if (!ParamsStmt)
			{
				return true;
			}
			CurrentStmt = ParamsStmt;
			CurrentLoc = ParamsStmt->Loc;
			Program.bHasParamsLine = true;
			Program.ParamsLoc.Line = ParamsStmt->Loc.Line;
			Program.ParamsLoc.Column = ParamsStmt->Loc.Column;
			Program.ParamsLoc.NodeID = ParamsStmt->Node.bHasMetadata ? ParamsStmt->Node.NodeID : -1;

			for (int32 i = 0; i < ParamsStmt->Params.Num(); ++i)
			{
				const FBangoScriptParamDecl& Decl = ParamsStmt->Params[i];
				for (int32 j = 0; j < i; ++j)
				{
					if (ParamsStmt->Params[j].Name.Equals(Decl.Name, ESearchCase::CaseSensitive))
					{
						return Fail(Decl.Loc, FString::Printf(TEXT("Two parameters are named '%s'."), *Decl.Name));
					}
				}

				FBangoScriptParamInfo Info;
				Info.Name = Decl.Name;
				Info.Type = Decl.Type;
				if (Decl.Default)
				{
					Info.bHasDefault = true;
					if (!EvaluateConstant(*Decl.Default, Decl.Type, Decl.Name, Info.Default))
					{
						return false;
					}
				}

				int32 Slot = 0;
				if (!DeclareLocal(Decl.Name, Decl.Type, Decl.Loc, Slot))
				{
					return false;
				}
				check(Slot == i);
				Program.Params.Add(MoveTemp(Info));
				ScriptVars.Add(Scopes.Last().Vars.Last());
			}
			Fn->NumParams = ParamsStmt->Params.Num();
			return true;
		}

		bool ResolveGotos()
		{
			for (const FGotoFixup& Goto : Gotos)
			{
				CurrentStmt = Goto.Stmt;
				const FLabel* Label = Labels.Find(Goto.Name);
				if (!Label)
				{
					if (const int32* DisabledLine = DisabledLabels.Find(Goto.Name))
					{
						return Fail(Goto.Loc, FString::Printf(TEXT("Label '%s' (line %d) is disabled, so 'goto %s' has nowhere to go. Enable the label, or disable this goto too."),
							*Goto.Name, *DisabledLine, *Goto.Name));
					}
					return Fail(Goto.Loc, FString::Printf(TEXT("There is no label '%s' in this %s."), *Goto.Name, FnAst ? TEXT("function") : TEXT("script's top level")));
				}
				// The blocks the jump leaves (the goto's, out to the innermost block that holds both) and the ones it
				// enters (from there in to the label's). Leaving blocks is fine, except a spawn block. Entering an if's,
				// a while's, a switch's or a timeout's block is fine too: nothing is set up on the way in. Not a for or
				// forEach loop (its header gives its variable, and a forEach its array, their values), or a spawn block
				// (it's a separate copy's code).
				TArray<int32> GotoBlocks;
				for (int32 Block = Goto.BlockId; Block >= 0; Block = BlockParents[Block])
				{
					GotoBlocks.Add(Block);
				}
				int32 Shared = Label->BlockId;
				bool bEntersLoop = false;
				bool bEntersSpawn = false;
				for (; Shared >= 0 && !GotoBlocks.Contains(Shared); Shared = BlockParents[Shared])
				{
					bEntersLoop |= LoopBlockIds.Contains(Shared);
					bEntersSpawn |= SpawnBlockIds.Contains(Shared);
				}
				bool bLeavesSpawn = false;
				for (const int32 Block : GotoBlocks)
				{
					if (Block == Shared)
					{
						break;
					}
					bLeavesSpawn |= SpawnBlockIds.Contains(Block);
				}
				if (bLeavesSpawn)
				{
					return Fail(Goto.Loc, FString::Printf(TEXT("'goto %s' would jump out of a spawn block (label '%s' is on line %d, outside it). The spawned copy runs on its own: use 'return;' to end it."),
						*Goto.Name, *Goto.Name, Label->Line));
				}
				if (bEntersSpawn)
				{
					return Fail(Goto.Loc, FString::Printf(TEXT("'goto %s' jumps into a spawn block (label '%s' is on line %d, inside a spawn block this goto is not in). A spawn block is run by a separate copy of the script, so nothing outside it can jump into it."),
						*Goto.Name, *Goto.Name, Label->Line));
				}
				if (bEntersLoop)
				{
					return Fail(Goto.Loc, FString::Printf(TEXT("'goto %s' jumps into a for or forEach loop (label '%s' is on line %d, inside a loop this goto is not in), which would skip the loop's start, where its variable gets its value. goto can jump into an if's, a while's, a switch's or a timeout's block, but not into a for or forEach loop."),
						*Goto.Name, *Goto.Name, Label->Line));
				}
				// A local in scope at the label but not at the goto is declared between them: the jump would skip
				// giving it its value.
				for (const int32 Slot : Label->SlotsInScope)
				{
					if (!Goto.SlotsInScope.Contains(Slot))
					{
						return Fail(Goto.Loc, FString::Printf(TEXT("'goto %s' jumps past the declaration of local '%s' (line %d), so it would have no value at label '%s'. Declare it before the goto, or after the label."),
							*Goto.Name, *Fn->SlotNames[Slot], SlotLines.IsValidIndex(Slot) ? SlotLines[Slot] : 0, *Goto.Name));
					}
				}
				PatchJump(Goto.Instruction, Label->Address);
			}
			return true;
		}

		// ---- Scopes ----

		void PushScope()
		{
			FScope Scope;
			Scope.BlockId = BlockParents.Num();
			BlockParents.Add(Scopes.Num() > 0 ? Scopes.Last().BlockId : -1);
			Scopes.Add(MoveTemp(Scope));
		}

		void PopScope()
		{
			Scopes.Pop();
		}

		/** The slots of every local in scope here (for goto checks). */
		TArray<int32> GetSlotsInScope() const
		{
			TArray<int32> Slots;
			for (const FScope& Scope : Scopes)
			{
				for (const FLocalVar& Var : Scope.Vars)
				{
					Slots.Add(Var.Slot);
				}
			}
			return Slots;
		}

		/** Notes every local and loop variable declared in Block (and its blocks), in GraphLocals. */
		void CollectGraphLocals(const FBangoBlock& Block, bool bDisabled)
		{
			for (const FBangoStmtPtr& Stmt : Block)
			{
				CollectGraphLocals(*Stmt, bDisabled || Stmt->bDisabled);
			}
		}

		void NoteGraphLocal(const FString& Name, int32 Line, bool bLoopVar, bool bDisabled)
		{
			if (!Name.IsEmpty() && !GraphLocals.Find(Name))
			{
				FGraphLocal Info;
				Info.Line = Line;
				Info.bLoopVar = bLoopVar;
				Info.bDisabled = bDisabled;
				GraphLocals.Add(Name, Info);
			}
		}

		void CollectGraphLocals(const FBangoStmt& Stmt, bool bDisabled)
		{
			switch (Stmt.Kind)
			{
			case EBangoStmtKind::VarDecl:
			{
				const FBangoVarDeclStmt& Decl = *Stmt.As<FBangoVarDeclStmt>();
				if (!Decl.bGlobal)
				{
					NoteGraphLocal(Decl.Name, Decl.Loc.Line, false, bDisabled);
				}
				break;
			}
			case EBangoStmtKind::If:
				CollectGraphLocals(Stmt.As<FBangoIfStmt>()->Then, bDisabled);
				CollectGraphLocals(Stmt.As<FBangoIfStmt>()->Else, bDisabled);
				break;
			case EBangoStmtKind::While:
				CollectGraphLocals(Stmt.As<FBangoWhileStmt>()->Body, bDisabled);
				break;
			case EBangoStmtKind::For:
			{
				const FBangoForStmt& For = *Stmt.As<FBangoForStmt>();
				if (const FBangoVarDeclStmt* Decl = For.Init.IsValid() ? For.Init->As<FBangoVarDeclStmt>() : nullptr)
				{
					NoteGraphLocal(Decl->Name, Stmt.Loc.Line, true, bDisabled);
				}
				CollectGraphLocals(For.Body, bDisabled);
				break;
			}
			case EBangoStmtKind::ForEach:
				NoteGraphLocal(Stmt.As<FBangoForEachStmt>()->VarName, Stmt.Loc.Line, true, bDisabled);
				CollectGraphLocals(Stmt.As<FBangoForEachStmt>()->Body, bDisabled);
				break;
			case EBangoStmtKind::Sequence:
				for (const FBangoBlock& Block : Stmt.As<FBangoSequenceStmt>()->Blocks)
				{
					CollectGraphLocals(Block, bDisabled);
				}
				break;
			case EBangoStmtKind::Spawn:
			{
				const FBangoSpawnStmt& Spawn = *Stmt.As<FBangoSpawnStmt>();
				if (Spawn.bDeclaresId)
				{
					NoteGraphLocal(Spawn.IdName, Stmt.Loc.Line, false, bDisabled);
				}
				CollectGraphLocals(Spawn.Body, bDisabled);
				break;
			}
			case EBangoStmtKind::WaitUntil:
				CollectGraphLocals(Stmt.As<FBangoWaitUntilStmt>()->TimeoutBlock, bDisabled);
				break;
			case EBangoStmtKind::Switch:
				for (const FBangoSwitchCase& Case : Stmt.As<FBangoSwitchStmt>()->Cases)
				{
					CollectGraphLocals(Case.Body, bDisabled);
				}
				CollectGraphLocals(Stmt.As<FBangoSwitchStmt>()->Default, bDisabled);
				break;
			default:
				break;
			}
		}

		const FLocalVar* FindLocal(const FString& Name) const
		{
			for (int32 s = Scopes.Num() - 1; s >= 0; --s)
			{
				for (const FLocalVar& Var : Scopes[s].Vars)
				{
					if (Var.Name.Equals(Name, ESearchCase::CaseSensitive))
					{
						return &Var;
					}
				}
			}
			return nullptr;
		}

		/** A local, parameter or loop variable can't have the name of a global this file declares: which would it mean? */
		bool CheckNotAGlobal(const FString& Name, const FBangoSourceLocation& Loc)
		{
			if (DeclaredGlobals.Find(Name))
			{
				return Fail(Loc, FString::Printf(TEXT("'%s' is a global this file declares, so it can't also be a local, a parameter or a loop's variable here. Rename one of them."), *Name));
			}
			return true;
		}

		/** In the main graph of a script with an inputs signature, a local can't have an input's name. */
		bool CheckNotAnInput(const FString& Name, const FBangoSourceLocation& Loc)
		{
			if (InputsSignature && !FnAst && !bDeclaringSignature
				&& InputsSignature->Params.ContainsByPredicate([&Name](const FBangoScriptParamInfo& Input) { return Input.Name.Equals(Name, ESearchCase::CaseSensitive); }))
			{
				return Fail(Loc, FString::Printf(TEXT("'%s' is an input given by %s, so it can't be declared here too. Use a different name."), *Name, *InputsSignature->Label));
			}
			return true;
		}

		/** One of the script's inputs or script variables, if this is a function (which sees them). */
		const FLocalVar* FindScriptVar(const FString& Name) const
		{
			if (!FnAst)
			{
				return nullptr; // the main graph's own are in its scopes
			}
			return ScriptVars.FindByPredicate([&Name](const FLocalVar& Var) { return Var.Name.Equals(Name, ESearchCase::CaseSensitive); });
		}

		/** A function sees the script's inputs and script variables, so it can't declare anything with one's name. */
		bool CheckNotAScriptVar(const FString& Name, const FBangoSourceLocation& Loc)
		{
			if (const FLocalVar* Var = FindScriptVar(Name))
			{
				return Fail(Loc, FString::Printf(TEXT("'%s' is one of this script's %s, which its functions see too, so function '%s' can't declare its own '%s'. Rename one of them, or use the script's here."),
					*Name, Var->Slot < Program.Functions[0].NumParams ? TEXT("inputs") : TEXT("script variables"), *FnAst->Name, *Name));
			}
			return true;
		}

		bool DeclareLocal(const FString& Name, const FBangoType& Type, const FBangoSourceLocation& Loc, int32& OutSlot)
		{
			if (!CheckNotAnInput(Name, Loc) || !CheckNotAScriptVar(Name, Loc))
			{
				return false;
			}
			if (FindLocal(Name))
			{
				return Fail(Loc, FString::Printf(TEXT("'%s' is already declared (as a local, a parameter or a loop's variable). Use a different name."), *Name));
			}
			if (!CheckNotAGlobal(Name, Loc))
			{
				return false;
			}
			FLocalVar Var;
			Var.Name = Name;
			Var.Type = Type;
			Var.Slot = AllocSlot(Type, Name);
			Scopes.Last().Vars.Add(Var);
			OutSlot = Var.Slot;
			return true;
		}

		// ---- Statements ----

		bool CompileBlock(const FBangoBlock& Block)
		{
			PushScope();
			for (const FBangoStmtPtr& Stmt : Block)
			{
				if (!CompileStmt(*Stmt))
				{
					return false;
				}
			}
			PopScope();
			return true;
		}

		/** Remembers the labels in a disabled statement (and in its blocks), for ResolveGotos' error. */
		void NoteDisabledLabels(const FBangoStmt& Stmt)
		{
			if (const FBangoLabelStmt* Label = Stmt.As<FBangoLabelStmt>())
			{
				if (!DisabledLabels.Find(Label->Name))
				{
					DisabledLabels.Add(Label->Name, Label->Loc.Line);
				}
			}
			const auto NoteBlock = [this](const FBangoBlock& Block)
			{
				for (const FBangoStmtPtr& Inner : Block)
				{
					NoteDisabledLabels(*Inner);
				}
			};
			if (const FBangoIfStmt* If = Stmt.As<FBangoIfStmt>())
			{
				NoteBlock(If->Then);
				NoteBlock(If->Else);
			}
			else if (const FBangoWhileStmt* While = Stmt.As<FBangoWhileStmt>())
			{
				NoteBlock(While->Body);
			}
			else if (const FBangoForStmt* For = Stmt.As<FBangoForStmt>())
			{
				NoteBlock(For->Body);
			}
			else if (const FBangoForEachStmt* ForEach = Stmt.As<FBangoForEachStmt>())
			{
				NoteBlock(ForEach->Body);
			}
			else if (const FBangoSequenceStmt* Sequence = Stmt.As<FBangoSequenceStmt>())
			{
				for (const FBangoBlock& Block : Sequence->Blocks)
				{
					NoteBlock(Block);
				}
			}
			else if (const FBangoSwitchStmt* Switch = Stmt.As<FBangoSwitchStmt>())
			{
				for (const FBangoSwitchCase& Case : Switch->Cases)
				{
					NoteBlock(Case.Body);
				}
				NoteBlock(Switch->Default);
			}
			else if (const FBangoSpawnStmt* Spawn = Stmt.As<FBangoSpawnStmt>())
			{
				NoteBlock(Spawn->Body);
			}
			else if (const FBangoWaitUntilStmt* Wait = Stmt.As<FBangoWaitUntilStmt>())
			{
				NoteBlock(Wait->TimeoutBlock);
			}
		}

		bool CompileStmt(const FBangoStmt& Stmt)
		{
			if (Stmt.bDisabled)
			{
				// A disabled node: no code, so execution goes straight on to the next statement (spec section 1).
				NoteDisabledLabels(Stmt);
				return true;
			}
			CurrentStmt = &Stmt;
			CurrentLoc = Stmt.Loc;
			const int32 Begin = Here();
			if (!CompileStmtBody(Stmt))
			{
				return false;
			}
			// Where a reloaded script can carry on (FBangoStatementRange).
			const FBangoVarDeclStmt* LocalDecl = Stmt.As<FBangoVarDeclStmt>();
			const bool bSimple = Stmt.Kind == EBangoStmtKind::Assign || Stmt.Kind == EBangoStmtKind::IncDec || Stmt.Kind == EBangoStmtKind::Call
				|| (LocalDecl && LocalDecl->IsLocal());
			if (bSimple && Stmt.Node.bHasMetadata && Here() > Begin)
			{
				FBangoStatementRange Range;
				Range.NodeID = Stmt.Node.NodeID;
				Range.Begin = Begin;
				Range.End = Here();
				Fn->Statements.Add(Range);
			}
			return true;
		}

		/** Compiles a statement without making it the current node (for the parts of a for header). */
		bool CompileStmtBody(const FBangoStmt& Stmt)
		{
			switch (Stmt.Kind)
			{
			case EBangoStmtKind::VarDecl:  return CompileVarDecl(*Stmt.As<FBangoVarDeclStmt>());
			case EBangoStmtKind::Assign:   return CompileAssign(*Stmt.As<FBangoAssignStmt>());
			case EBangoStmtKind::IncDec:   return CompileIncDec(*Stmt.As<FBangoIncDecStmt>());
			case EBangoStmtKind::Call:
			{
				FExprType Unused;
				return CompileCall(*Stmt.As<FBangoCallStmt>()->Call->As<FBangoCallExpr>(), false, Unused);
			}
			case EBangoStmtKind::If:       return CompileIf(*Stmt.As<FBangoIfStmt>());
			case EBangoStmtKind::While:    return CompileWhile(*Stmt.As<FBangoWhileStmt>());
			case EBangoStmtKind::For:      return CompileFor(*Stmt.As<FBangoForStmt>());
			case EBangoStmtKind::ForEach:  return CompileForEach(*Stmt.As<FBangoForEachStmt>());
			case EBangoStmtKind::Sequence: return CompileSequence(*Stmt.As<FBangoSequenceStmt>());
			case EBangoStmtKind::Switch:   return CompileSwitch(*Stmt.As<FBangoSwitchStmt>());
			case EBangoStmtKind::Spawn:    return CompileSpawn(*Stmt.As<FBangoSpawnStmt>());
			case EBangoStmtKind::Break:
			case EBangoStmtKind::Continue: return CompileBreakContinue(Stmt);
			case EBangoStmtKind::Return:   return CompileReturn(*Stmt.As<FBangoReturnStmt>());
			case EBangoStmtKind::WaitUntil: return CompileWaitUntil(*Stmt.As<FBangoWaitUntilStmt>());
			case EBangoStmtKind::Label:    return CompileLabel(*Stmt.As<FBangoLabelStmt>());
			case EBangoStmtKind::Goto:     return CompileGoto(*Stmt.As<FBangoGotoStmt>());
			case EBangoStmtKind::Params:
				// The parameters were declared when the top level started; the line itself does nothing.
				Emit(EBangoOp::Nop);
				return true;
			case EBangoStmtKind::Reroute:
				return true; // only a bend in a wire: no code at all
			default:                       return Fail(Stmt.Loc, TEXT("Unexpected statement."));
			}
		}

		bool CompileVarDecl(const FBangoVarDeclStmt& Decl)
		{
			if (Decl.bGlobal && !Decl.IsGlobalAssignment())
			{
				// Globals are declared when the instance starts, which gives one its default if it doesn't exist yet
				// or is nil (FBangoGlobalDecl, checked in CollectGlobals). The line itself does nothing.
				Emit(EBangoOp::Nop);
				return true;
			}
			if (Decl.bGlobal)
			{
				// `global bool b = true;`: declared when the instance starts, like one at the top (CollectGlobals); the
				// line sets it. It's the global even where a local of the same name is visible.
				const FBangoType* Declared = DeclaredGlobals.Find(Decl.Name);
				const FBangoType& Type = Declared ? *Declared : Decl.Type;
				FExprType ValueType;
				if (!CompileExpr(*Decl.Init, &Type, ValueType)
					|| !CanAssign(Type, ValueType, Decl.Init->Loc, FString::Printf(TEXT("Global '%s'"), *Decl.Name)))
				{
					return false;
				}
				Emit(EBangoOp::StorePlace, AddPlace(EBangoPlaceKind::Global, AddName(Decl.Name), 0));
				return true;
			}

			// The new local isn't visible in its own initializer.
			if (!CheckNotAnInput(Decl.Name, Decl.Loc) || !CheckNotAScriptVar(Decl.Name, Decl.Loc))
			{
				return false;
			}
			if (FindLocal(Decl.Name))
			{
				return Fail(Decl.Loc, FString::Printf(TEXT("'%s' is already declared (as a local, a parameter or a loop's variable). Use a different name."), *Decl.Name));
			}
			if (Decl.Init)
			{
				FExprType ValueType;
				if (!CompileExpr(*Decl.Init, &Decl.Type, ValueType)
					|| !CanAssign(Decl.Type, ValueType, Decl.Init->Loc, FString::Printf(TEXT("'%s'"), *Decl.Name)))
				{
					return false;
				}
			}
			else
			{
				Emit(EBangoOp::PushNil, AddType(Decl.Type));
			}

			// A local's slot is told apart by its declaration node, for reloading (a for header's, by its loop's).
			const int64 SlotNodeBefore = LoopNodeForSlots;
			if (Decl.IsLocal() && LoopNodeForSlots == -1)
			{
				LoopNodeForSlots = Decl.Node.bHasMetadata ? Decl.Node.NodeID : -1;
			}
			int32 Slot = 0;
			const bool bDeclared = DeclareLocal(Decl.Name, Decl.Type, Decl.Loc, Slot);
			LoopNodeForSlots = SlotNodeBefore;
			if (!bDeclared)
			{
				return false;
			}
			Emit(EBangoOp::StorePlace, AddPlace(EBangoPlaceKind::Local, Slot, 0));
			// A scriptVar has a fixed starting value (CheckScriptVarDecl), kept for reloading. A local a reload adds
			// starts as nil: its value may be worked out where it's declared, which a reload can't do.
			if (Decl.Init && Decl.bScriptVar && IsFixedValue(*Decl.Init))
			{
				FBangoValue StartValue;
				if (!EvaluateConstant(*Decl.Init, Decl.Type, Decl.Name, StartValue))
				{
					return false;
				}
				Fn->SlotStartValues[Slot] = StartValue;
			}
			if (Decl.bScriptVar && !FnAst)
			{
				ScriptVars.Add(Scopes.Last().Vars.Last());
			}
			return true;
		}

		bool CompileAssign(const FBangoAssignStmt& Assign)
		{
			FPlaceInfo Place;
			if (!ResolvePlace(*Assign.Target, EPlaceUse::Write, Place))
			{
				return false;
			}
			const FBangoType& PlaceType = Place.Type.Type;  // always known for writes

			if (Assign.Op == EBangoAssignOp::Assign)
			{
				const FBangoLiteralExpr* NilLit = Assign.Value->As<FBangoLiteralExpr>();
				if (Assign.Target->Kind == EBangoExprKind::Member && NilLit && NilLit->LiteralKind == EBangoLiteralKind::Nil)
				{
					return Fail(Assign.Value->Loc, TEXT("A vector's parts can't be nil. To clear the whole vector, assign nil to the vector itself."));
				}
				FExprType ValueType;
				if (!CompileExpr(*Assign.Value, &PlaceType, ValueType)
					|| !CanAssign(PlaceType, ValueType, Assign.Value->Loc, FString::Printf(TEXT("'%s'"), *Place.Text)))
				{
					return false;
				}
				Emit(EBangoOp::StorePlace, Place.PlaceIndex);
				return true;
			}

			EBangoBinaryOp Op = EBangoBinaryOp::Add;
			switch (Assign.Op)
			{
			case EBangoAssignOp::Add:      Op = EBangoBinaryOp::Add; break;
			case EBangoAssignOp::Subtract: Op = EBangoBinaryOp::Subtract; break;
			case EBangoAssignOp::Multiply: Op = EBangoBinaryOp::Multiply; break;
			default:                       Op = EBangoBinaryOp::Divide; break;
			}

			if (Place.NumIndexes > 0)
			{
				Emit(EBangoOp::DupN, Place.NumIndexes);
			}
			Emit(EBangoOp::LoadPlace, Place.PlaceIndex);

			FExprType ValueType;
			FExprType ResultType;
			// `_v += [1, 0, 0];`: the array literal is a vector of the place's kind.
			const bool bVectorStep = PlaceType.IsVector() && (Op == EBangoBinaryOp::Add || Op == EBangoBinaryOp::Subtract);
			if (!CompileExpr(*Assign.Value, bVectorStep ? &PlaceType : nullptr, ValueType)
				|| !CheckUsableValue(ValueType, Assign.Value->Loc, TEXT("here"))
				|| !BinaryResult(Op, Place.Type, ValueType, Assign.Loc, ResultType)
				|| !CanAssign(PlaceType, ResultType, Assign.Loc, FString::Printf(TEXT("The result of '%s=' stored in '%s'"), BinaryOpText(Op), *Place.Text)))
			{
				return false;
			}
			Emit(BinaryOpCode(Op));
			Emit(EBangoOp::StorePlace, Place.PlaceIndex);
			return true;
		}

		bool CompileIncDec(const FBangoIncDecStmt& Stmt)
		{
			FPlaceInfo Place;
			if (!ResolvePlace(*Stmt.Target, EPlaceUse::Write, Place))
			{
				return false;
			}
			if (!Place.Type.Is(FBangoType::Int()))
			{
				return Fail(Stmt.Target->Loc, FString::Printf(TEXT("'%s' only works on an int, but '%s' is %s."),
					Stmt.bIncrement ? TEXT("++") : TEXT("--"), *Place.Text, *Place.Type.Describe()));
			}
			if (Place.NumIndexes > 0)
			{
				Emit(EBangoOp::DupN, Place.NumIndexes);
			}
			Emit(EBangoOp::LoadPlace, Place.PlaceIndex);
			Emit(EBangoOp::PushConst, AddConstant(FBangoValue::MakeInt(1)));
			Emit(Stmt.bIncrement ? EBangoOp::Add : EBangoOp::Subtract);
			Emit(EBangoOp::StorePlace, Place.PlaceIndex);
			return true;
		}

		bool CompileIf(const FBangoIfStmt& If)
		{
			FExprType ConditionType;
			if (!CompileExpr(*If.Condition, nullptr, ConditionType) || !CheckCondition(ConditionType, If.Condition->Loc))
			{
				return false;
			}
			const int32 JumpToElse = Emit(EBangoOp::JumpIfFalse);
			if (!CompileBlock(If.Then))
			{
				return false;
			}

			if (!If.bHasElse)
			{
				PatchJump(JumpToElse, Here());
				return true;
			}

			CurrentStmt = &If;
			CurrentLoc = If.Loc;
			const int32 JumpToEnd = Emit(EBangoOp::Jump);
			PatchJump(JumpToElse, Here());
			if (!CompileBlock(If.Else))
			{
				return false;
			}
			PatchJump(JumpToEnd, Here());
			return true;
		}

		void FinishLoop(int32 BreakTarget)
		{
			FLoop Loop = Loops.Pop();
			for (const int32 Jump : Loop.BreakJumps)
			{
				PatchJump(Jump, BreakTarget);
			}
		}

		/**
		 * `spawn { ... }`: SpawnBlock starts a new instance at the block (with a copy of this frame), then this one jumps
		 * over it. The block ends with Halt: the copy is done.
		 */
		bool CompileSpawn(const FBangoSpawnStmt& Spawn)
		{
			if (bCondition)
			{
				return Fail(Spawn.Loc, TEXT("A condition can't spawn: it only works out its answer."));
			}
			// `int _t = spawn`: declares a local for the id first, told apart by the spawn's node for reloading. It's
			// hidden while the block is compiled: the copy starts before the id is stored, so it would have no value there.
			FLocalVar DeclaredId;
			if (Spawn.bDeclaresId)
			{
				if (!CanAssign(Spawn.IdType, FExprType::MakeKnown(FBangoType::Int()), Spawn.IdNameLoc, FString::Printf(TEXT("The spawned copy's id (an int) stored in '%s'"), *Spawn.IdName)))
				{
					return false;
				}
				const int64 SlotNodeBefore = LoopNodeForSlots;
				LoopNodeForSlots = Spawn.Node.bHasMetadata ? Spawn.Node.NodeID : -1;
				int32 Slot = 0;
				const bool bDeclared = DeclareLocal(Spawn.IdName, Spawn.IdType, Spawn.IdNameLoc, Slot);
				LoopNodeForSlots = SlotNodeBefore;
				if (!bDeclared)
				{
					return false;
				}
				DeclaredId = Scopes.Last().Vars.Last();
			}
			// `_id = spawn`: the place's indexes (if any) first, then SpawnBlock pushes the copy's id, then it's stored.
			FPlaceInfo IdPlace;
			if (Spawn.IdTarget)
			{
				if (!ResolvePlace(*Spawn.IdTarget, EPlaceUse::Write, IdPlace))
				{
					return false;
				}
				if (!CanAssign(IdPlace.Type.Type, FExprType::MakeKnown(FBangoType::Int()), Spawn.IdTarget->Loc, FString::Printf(TEXT("The spawned copy's id (an int) stored in '%s'"), *IdPlace.Text)))
				{
					return false;
				}
			}
			const int32 SpawnAt = Emit(EBangoOp::SpawnBlock, 0, Spawn.IdTarget ? 1 : 0);
			if (Spawn.IdTarget)
			{
				Emit(EBangoOp::StorePlace, IdPlace.PlaceIndex);
			}
			const int32 JumpOver = Emit(EBangoOp::Jump);
			PatchJump(SpawnAt, Here());
			SpawnBlockIds.Add(BlockParents.Num()); // the block CompileBlock is about to open
			SpawnLoopBases.Add(Loops.Num());
			if (Spawn.bDeclaresId)
			{
				Scopes.Last().Vars.Pop(); // not visible in the block (see above)
			}
			const bool bCompiled = CompileBlock(Spawn.Body);
			if (Spawn.bDeclaresId)
			{
				Scopes.Last().Vars.Add(DeclaredId);
			}
			SpawnLoopBases.Pop();
			if (!bCompiled)
			{
				return false;
			}
			CurrentStmt = &Spawn;
			CurrentLoc = Spawn.Loc;
			Emit(EBangoOp::Halt);
			PatchJump(JumpOver, Here());
			return true;
		}

		/** A sequence's blocks, one after another: it only groups statements, so it needs no code of its own. */
		bool CompileSequence(const FBangoSequenceStmt& Sequence)
		{
			for (const FBangoBlock& Block : Sequence.Blocks)
			{
				if (!CompileBlock(Block))
				{
					return false;
				}
			}
			return true;
		}

		/**
		 * A case value as a constant of the switch value's type: an int literal (or a negated one), a string literal
		 * or an entity literal (@name). Anything else, even a constant expression, is an error: case values are fixed.
		 */
		bool EvaluateCaseValue(const FBangoExpr& Expr, const FBangoType& Type, FBangoValue& OutValue)
		{
			const TCHAR* Wanted = Type.Base == EBangoBaseType::Int ? TEXT("an int, like 3 or -1")
				: Type.Base == EBangoBaseType::String ? TEXT("a string in quotes, like \"north\"")
				: TEXT("a Bango ID, like @guard");
			const FBangoLiteralExpr* Lit = Expr.As<FBangoLiteralExpr>();
			bool bNegate = false;
			if (const FBangoUnaryExpr* Unary = Expr.As<FBangoUnaryExpr>())
			{
				Lit = Unary->Op == EBangoUnaryOp::Negate && Unary->Operand.IsValid() ? Unary->Operand->As<FBangoLiteralExpr>() : nullptr;
				bNegate = Lit != nullptr;
				if (Lit && Lit->LiteralKind != EBangoLiteralKind::Int)
				{
					Lit = nullptr;
				}
			}
			if (!Lit)
			{
				return Fail(Expr.Loc, FString::Printf(TEXT("A case value must be a fixed value written out (here, %s), not a name or a calculation."), Wanted));
			}
			if (Type.Base == EBangoBaseType::Int && Lit->LiteralKind == EBangoLiteralKind::Int)
			{
				OutValue = FBangoValue::MakeInt(bNegate ? (int32)-Lit->IntValue : (int32)Lit->IntValue);
				return true;
			}
			if (Type.Base == EBangoBaseType::String && Lit->LiteralKind == EBangoLiteralKind::String)
			{
				OutValue = FBangoValue::MakeString(Lit->StringValue);
				return true;
			}
			if (Type.Base == EBangoBaseType::Entity && Lit->LiteralKind == EBangoLiteralKind::Entity)
			{
				OutValue = FBangoValue::MakeEntity(Lit->StringValue);
				return true;
			}
			return Fail(Expr.Loc, FString::Printf(TEXT("This switch is on %s, so each case value must be %s."), *Article(Type), Wanted));
		}

		/**
		 * The value is evaluated once and kept on the stack while the cases are tested in order; the first equal
		 * value pops it and jumps to its case. No match pops it and goes to the default, or past the switch. Each case
		 * block jumps past the rest. A switch isn't a loop, so break and continue act on the loop around it.
		 */
		bool CompileSwitch(const FBangoSwitchStmt& Switch)
		{
			FExprType ValueType;
			if (!CompileExpr(*Switch.Value, nullptr, ValueType))
			{
				return false;
			}
			if (ValueType.IsUnknown())
			{
				return Fail(Switch.Value->Loc, TEXT("switch needs to know the value's type. This global isn't declared in this script, so declare it first, like 'global int stage;'."));
			}
			const bool bAllowed = ValueType.IsKnown() && !ValueType.Type.IsArray()
				&& (ValueType.Type.Base == EBangoBaseType::Int || ValueType.Type.Base == EBangoBaseType::String || ValueType.Type.Base == EBangoBaseType::Entity);
			if (!bAllowed)
			{
				return Fail(Switch.Value->Loc, FString::Printf(TEXT("switch works on an int, a string or an entity, but this is %s."), *ValueType.Describe()));
			}

			struct FCaseJumps { TArray<int32> Jumps; };
			TArray<FCaseJumps> CaseJumps;
			TArray<FBangoValue> Seen;
			for (const FBangoSwitchCase& Case : Switch.Cases)
			{
				FCaseJumps& Jumps = CaseJumps.AddDefaulted_GetRef();
				for (const FBangoExprPtr& ValueExpr : Case.Values)
				{
					FBangoValue Value;
					if (!EvaluateCaseValue(*ValueExpr, ValueType.Type, Value))
					{
						return false;
					}
					if (Seen.Contains(Value))
					{
						return Fail(ValueExpr->Loc, TEXT("This value is already in an earlier case of this switch. Each value can only be in one case."));
					}
					Seen.Add(Value);

					Emit(EBangoOp::DupN, 1);
					Emit(EBangoOp::PushConst, AddConstant(Value));
					Emit(EBangoOp::Equal);
					const int32 JumpToNext = Emit(EBangoOp::JumpIfFalse);
					Emit(EBangoOp::Pop);
					Jumps.Jumps.Add(Emit(EBangoOp::Jump));
					PatchJump(JumpToNext, Here());
				}
			}
			Emit(EBangoOp::Pop);
			TArray<int32> JumpsToEnd;
			const int32 JumpToDefault = Emit(EBangoOp::Jump);

			for (int32 i = 0; i < Switch.Cases.Num(); ++i)
			{
				for (const int32 Jump : CaseJumps[i].Jumps)
				{
					PatchJump(Jump, Here());
				}
				if (!CompileBlock(Switch.Cases[i].Body))
				{
					return false;
				}
				CurrentStmt = &Switch;
				CurrentLoc = Switch.Loc;
				JumpsToEnd.Add(Emit(EBangoOp::Jump));
			}

			PatchJump(JumpToDefault, Here());
			if (Switch.bHasDefault && !CompileBlock(Switch.Default))
			{
				return false;
			}
			for (const int32 Jump : JumpsToEnd)
			{
				PatchJump(Jump, Here());
			}
			return true;
		}

		bool CompileWhile(const FBangoWhileStmt& While)
		{
			const int32 Start = Here();
			FExprType ConditionType;
			if (!CompileExpr(*While.Condition, nullptr, ConditionType) || !CheckCondition(ConditionType, While.Condition->Loc))
			{
				return false;
			}
			const int32 JumpToEnd = Emit(EBangoOp::JumpIfFalse);

			FLoop Loop;
			Loop.ContinueTarget = Start;
			Loops.Add(MoveTemp(Loop));
			if (!CompileBlock(While.Body))
			{
				return false;
			}
			CurrentStmt = &While;
			CurrentLoc = While.Loc;
			Emit(EBangoOp::Jump, Start);
			PatchJump(JumpToEnd, Here());
			FinishLoop(Here());
			return true;
		}

		/**
		 * `waitUntil (c);` is
		 *     L: WaitCheck #n
		 *        <c>
		 *        WaitResult #n -> L
		 * WaitResult carries on if c is true; else the script pauses there, and runs from L again once something c reads
		 * has changed (FBangoWaitSet, filled in by Bango::FindDependencies when every function has been compiled).
		 *
		 * `waitUntil (c) heldFor (h) timeout (s, i) { block }` (each part optional) is
		 *        <h> <s> <i>           (those it has)
		 *        WaitBegin #n          (notes them: the hold's seconds, and starts the timeout)
		 *     L: WaitCheck #n
		 *        <c>
		 *        WaitResult #n -> L    (false with the time up: goes to T. True, but not for h seconds yet: pauses too)
		 *        Jump E                (from here on, only with a timeout)
		 *     T: block                 (the set's TimeoutIP)
		 *     E:
		 */
		bool CompileWaitUntil(const FBangoWaitUntilStmt& Wait)
		{
			if (bCondition)
			{
				return Fail(Wait.Loc, TEXT("A condition can't use waitUntil: a condition must answer at once."));
			}
			FBangoWaitSet Set;
			Set.Function = FnIndex;
			Set.Line = Wait.Loc.Line;
			Set.Column = Wait.Loc.Column;
			Set.NodeID = Wait.Node.bHasMetadata ? Wait.Node.NodeID : -1;
			const int32 SetIndex = Program.WaitSets.Add(Set);

			int32 BeginFlags = 0;
			const FBangoType Seconds = FBangoType::Double();
			if (Wait.bHasHold)
			{
				FExprType HoldType;
				if (!CompileExpr(*Wait.Hold, &Seconds, HoldType)
					|| !CanAssign(Seconds, HoldType, Wait.Hold->Loc, TEXT("heldFor's seconds")))
				{
					return false;
				}
				EmitConvertIfNeeded(Seconds, HoldType);
				Program.WaitSets[SetIndex].bHold = true;
				BeginFlags |= Bango::WaitHasHold;
			}
			// A timeout is a number of seconds, or a condition that ends the wait when it's true: its value's type
			// says which, so it's compiled once just to learn that (and that code thrown away).
			bool bTimeoutIsCondition = false;
			if (Wait.bHasTimeout && !IsNilLiteral(*Wait.Timeout))
			{
				const int32 Mark = Here();
				const bool bWasInferring = bInferring;
				bInferring = true;
				FExprType Probe;
				const bool bProbed = CompileExpr(*Wait.Timeout, nullptr, Probe);
				bInferring = bWasInferring;
				Fn->Code.SetNum(Mark);
				Fn->Debug.SetNum(Mark);
				if (!bProbed)
				{
					return false;
				}
				if (Probe.IsUnknown())
				{
					return Fail(Wait.Timeout->Loc, TEXT("A timeout is a number of seconds, or a condition (a bool) that ends the wait when it's true, and this value's type is only known when the script runs (an undeclared global, or a 'call'). Declare the global in this script, like 'global bool alarmRaised;' or 'global float patience;'."));
				}
				bTimeoutIsCondition = Probe.Is(FBangoType::Bool());
				if (!bTimeoutIsCondition && !Probe.IsNumber())
				{
					return Fail(Wait.Timeout->Loc, FString::Printf(TEXT("A timeout is a number of seconds, or a condition (a bool) that ends the wait when it's true, but this is %s."), *Probe.Describe()));
				}
			}
			if (Wait.bHasTimeout)
			{
				if (!bTimeoutIsCondition)
				{
					FExprType TimeoutType;
					if (!CompileExpr(*Wait.Timeout, &Seconds, TimeoutType)
						|| !CanAssign(Seconds, TimeoutType, Wait.Timeout->Loc, TEXT("A timeout's seconds")))
					{
						return false;
					}
					EmitConvertIfNeeded(Seconds, TimeoutType);
					BeginFlags |= Bango::WaitHasTimeout;
				}
				if (Wait.IgnoreHold)
				{
					const FBangoType Flag = FBangoType::Bool();
					FExprType IgnoreType;
					if (!CompileExpr(*Wait.IgnoreHold, &Flag, IgnoreType)
						|| !CanAssign(Flag, IgnoreType, Wait.IgnoreHold->Loc, TEXT("A timeout's second value (IgnoreHold)")))
					{
						return false;
					}
					EmitConvertIfNeeded(Flag, IgnoreType);
					BeginFlags |= Bango::WaitHasIgnoreHold;
				}
			}
			if (BeginFlags != 0)
			{
				Emit(EBangoOp::WaitBegin, SetIndex, BeginFlags);
			}

			const int32 Check = Emit(EBangoOp::WaitCheck, SetIndex);
			const int32 CondBegin = Here();
			FExprType ConditionType;
			if (!CompileExpr(*Wait.Condition, nullptr, ConditionType) || !CheckCondition(ConditionType, Wait.Condition->Loc))
			{
				return false;
			}
			Program.WaitSets[SetIndex].CondBegin = CondBegin;
			Program.WaitSets[SetIndex].CondEnd = Here();
			Emit(EBangoOp::WaitResult, SetIndex, Check);

			// A reloaded script can switch to a new version while it waits here, and checks the new condition
			// (FBangoInstance::TryReload: a paused waitUntil's frame is at its WaitCheck).
			if (Wait.Node.bHasMetadata)
			{
				FBangoStatementRange Range;
				Range.NodeID = Wait.Node.NodeID;
				Range.Begin = Check;
				Range.End = Check;
				Fn->Statements.Add(Range);
			}
			if (Wait.bHasTimeout)
			{
				const int32 JumpOver = Emit(EBangoOp::Jump);
				if (bTimeoutIsCondition)
				{
					// Reached from WaitResult while the waitUntil's own condition is false: true runs on into the
					// block, false pauses.
					CurrentStmt = &Wait;
					CurrentLoc = Wait.Timeout->Loc;
					Program.WaitSets[SetIndex].TimeoutCondIP = Emit(EBangoOp::WaitTimeoutCheck, SetIndex, Check);
					Program.WaitSets[SetIndex].TimeoutCondBegin = Here();
					FExprType TimeoutType;
					if (!CompileExpr(*Wait.Timeout, nullptr, TimeoutType) || !CheckCondition(TimeoutType, Wait.Timeout->Loc))
					{
						return false;
					}
					Program.WaitSets[SetIndex].TimeoutCondEnd = Here();
					Emit(EBangoOp::WaitTimeoutResult, SetIndex, Check);
				}
				Program.WaitSets[SetIndex].TimeoutIP = Here();
				if (!CompileBlock(Wait.TimeoutBlock))
				{
					return false;
				}
				PatchJump(JumpOver, Here());
			}
			return true;
		}

		bool CompileFor(const FBangoForStmt& For)
		{
			PushScope();  // holds a variable declared in the header
			LoopBlockIds.Add(Scopes.Last().BlockId);

			LoopNodeForSlots = LoopSlotNode(For);
			const bool bInitOk = !For.Init || CompileStmtBody(*For.Init);
			LoopNodeForSlots = -1;
			if (!bInitOk)
			{
				return false;
			}

			const int32 Start = Here();
			int32 JumpToEnd = -1;
			if (For.Condition)
			{
				FExprType ConditionType;
				if (!CompileExpr(*For.Condition, nullptr, ConditionType) || !CheckCondition(ConditionType, For.Condition->Loc))
				{
					return false;
				}
				JumpToEnd = Emit(EBangoOp::JumpIfFalse);
			}

			Loops.Add(FLoop());
			if (!CompileBlock(For.Body))
			{
				return false;
			}

			// continue lands on the step
			CurrentStmt = &For;
			CurrentLoc = For.Loc;
			const int32 StepAddress = Here();
			for (const int32 Jump : Loops.Last().ContinueJumps)
			{
				PatchJump(Jump, StepAddress);
			}
			if (For.Step && !CompileStmtBody(*For.Step))
			{
				return false;
			}
			Emit(EBangoOp::Jump, Start);

			if (JumpToEnd >= 0)
			{
				PatchJump(JumpToEnd, Here());
			}
			FinishLoop(Here());
			PopScope();
			return true;
		}

		bool CompileForEach(const FBangoForEachStmt& ForEach)
		{
			FExprType ArrayType;
			if (!CompileExpr(*ForEach.Array, nullptr, ArrayType))
			{
				return false;
			}
			if (ArrayType.IsUnknown())
			{
				// A name that's no local, parameter or input is read as a global, and one this script doesn't declare
				// has no type here. Often it was meant to be an input the script wasn't given, so say what it has.
				const FBangoNameExpr* Named = ForEach.Array->As<FBangoNameExpr>();
				if (!Named)
				{
					return Fail(ForEach.Array->Loc, TEXT("forEach needs to know the array's type, and this comes from a global that isn't declared in this script. Declare it first, like 'global string[] questLog;'."));
				}
				FString Why;
				if (FnAst)
				{
					Why = TEXT("It isn't a local or a parameter of this function, or one of the script's inputs or script variables. ");
				}
				else if (InputsSignature && InputsSignature->Params.Num() > 0)
				{
					Why = FString::Printf(TEXT("It isn't a local or one of the inputs this script is given (%s). "), *InputsSignature->GetNames());
				}
				else
				{
					Why = TEXT("It isn't a local, and this script is given no inputs. ");
				}
				return Fail(ForEach.Array->Loc, FString::Printf(TEXT("forEach needs to know the type of '%s'. %sSo it's read as a global, and this script doesn't declare one of that name: if it is a global, declare it first, like 'global string[] %s;'."),
					*Named->Name, *Why, *Named->Name));
			}
			if (!ArrayType.IsKnownArray())
			{
				return Fail(ForEach.Array->Loc, FString::Printf(TEXT("forEach loops over an array, but this is %s."), *ArrayType.Describe()));
			}

			PushScope();
			LoopBlockIds.Add(Scopes.Last().BlockId);
			LoopNodeForSlots = LoopSlotNode(ForEach);
			const int32 ArraySlot = AllocSlot(ArrayType.Type, TEXT("(forEach array)"));
			const int32 PositionSlot = AllocSlot(FBangoType::Int(), TEXT("(forEach position)"));
			check(PositionSlot == ArraySlot + 1);

			Emit(EBangoOp::StorePlace, AddPlace(EBangoPlaceKind::Local, ArraySlot, 0));
			Emit(EBangoOp::PushConst, AddConstant(FBangoValue::MakeInt(0)));
			Emit(EBangoOp::StorePlace, AddPlace(EBangoPlaceKind::Local, PositionSlot, 0));

			int32 VarSlot = 0;
			const bool bDeclared = DeclareLocal(ForEach.VarName, ArrayType.Type.GetElementType(), ForEach.Loc, VarSlot);
			LoopNodeForSlots = -1;
			if (!bDeclared)
			{
				return false;
			}

			const int32 Start = Emit(EBangoOp::ForEachNext, ArraySlot, VarSlot, 0);

			FLoop Loop;
			Loop.ContinueTarget = Start;
			Loops.Add(MoveTemp(Loop));
			if (!CompileBlock(ForEach.Body))
			{
				return false;
			}
			CurrentStmt = &ForEach;
			CurrentLoc = ForEach.Loc;
			Emit(EBangoOp::Jump, Start);
			Fn->Code[Start].C = Here();
			FinishLoop(Here());
			PopScope();
			return true;
		}

		bool CompileBreakContinue(const FBangoStmt& Stmt)
		{
			const bool bBreak = Stmt.Kind == EBangoStmtKind::Break;
			if (SpawnLoopBases.Num() > 0 && Loops.Num() <= SpawnLoopBases.Last() && Loops.Num() > 0)
			{
				return Fail(Stmt.Loc, FString::Printf(TEXT("'%s' can't leave a spawn block: the spawned copy runs on its own, outside the loop. Use 'return;' to end it."),
					bBreak ? TEXT("break") : TEXT("continue")));
			}
			if (Loops.Num() == 0 || (SpawnLoopBases.Num() > 0 && Loops.Num() <= SpawnLoopBases.Last()))
			{
				return Fail(Stmt.Loc, FString::Printf(TEXT("'%s' can only be used inside a while, for or forEach loop."), bBreak ? TEXT("break") : TEXT("continue")));
			}
			FLoop& Loop = Loops.Last();
			if (bBreak)
			{
				Loop.BreakJumps.Add(Emit(EBangoOp::Jump));
			}
			else if (Loop.ContinueTarget >= 0)
			{
				Emit(EBangoOp::Jump, Loop.ContinueTarget);
			}
			else
			{
				Loop.ContinueJumps.Add(Emit(EBangoOp::Jump));
			}
			return true;
		}

		bool CompileReturn(const FBangoReturnStmt& Return)
		{
			if (SpawnLoopBases.Num() > 0)
			{
				// In a spawn block, return ends the spawned copy (and only it).
				if (Return.Value)
				{
					return Fail(Return.Value->Loc, TEXT("In a spawn block, 'return;' ends the spawned copy, so it can't give back a value."));
				}
				Emit(EBangoOp::Halt);
				return true;
			}
			if (!FnAst && bCondition)
			{
				// A condition's main graph returns its answer.
				if (!Return.Value)
				{
					return Fail(Return.Loc, TEXT("A condition must return true or false, like 'return _count > 3;'."));
				}
				const FBangoType Bool = FBangoType::Bool();
				FExprType ValueType;
				if (!CompileExpr(*Return.Value, &Bool, ValueType) || !CanAssign(Bool, ValueType, Return.Value->Loc, TEXT("The condition")))
				{
					return false;
				}
				EmitConvertIfNeeded(Bool, ValueType);
				Emit(EBangoOp::ReturnValue);
				return true;
			}
			if (!FnAst)
			{
				if (Return.Value)
				{
					return Fail(Return.Value->Loc, TEXT("The top level of a script can't return a value. Use 'return;' to end the script."));
				}
				Emit(EBangoOp::Halt);
				return true;
			}

			if (FnAst->bReturnsVoid)
			{
				if (Return.Value)
				{
					return Fail(Return.Value->Loc, FString::Printf(TEXT("Function '%s' is void, so it can't return a value."), *FnAst->Name));
				}
				Emit(EBangoOp::Return);
				return true;
			}

			if (!Return.Value)
			{
				return Fail(Return.Loc, FString::Printf(TEXT("Function '%s' must return %s."), *FnAst->Name, *Article(FnAst->ReturnType)));
			}
			FExprType ValueType;
			if (!CompileExpr(*Return.Value, &FnAst->ReturnType, ValueType)
				|| !CanAssign(FnAst->ReturnType, ValueType, Return.Value->Loc, FString::Printf(TEXT("Function '%s'"), *FnAst->Name)))
			{
				return false;
			}
			EmitConvertIfNeeded(FnAst->ReturnType, ValueType);
			Emit(EBangoOp::ReturnValue);
			return true;
		}

		bool CompileLabel(const FBangoLabelStmt& Label)
		{
			if (Label.Name.IsEmpty())
			{
				return Fail(Label.NameLoc, TEXT("This label has no name. Fill it in."));
			}
			if (const FLabel* Existing = Labels.Find(Label.Name))
			{
				return Fail(Label.Loc, FString::Printf(TEXT("Label '%s' is already defined on line %d."), *Label.Name, Existing->Line));
			}
			FLabel Info;
			Info.Address = Here();
			Info.BlockId = Scopes.Last().BlockId;
			Info.Line = Label.Loc.Line;
			Info.SlotsInScope = GetSlotsInScope();
			Labels.Add(Label.Name, Info);
			return true;
		}

		bool CompileGoto(const FBangoGotoStmt& Goto)
		{
			if (Goto.Name.IsEmpty())
			{
				return Fail(Goto.NameLoc, TEXT("This goto doesn't say which label to jump to. Fill it in."));
			}
			FGotoFixup Fixup;
			Fixup.Instruction = Emit(EBangoOp::Jump);
			Fixup.Name = Goto.Name;
			Fixup.BlockId = Scopes.Last().BlockId;
			Fixup.Loc = Goto.Loc;
			Fixup.Stmt = &Goto;
			Fixup.SlotsInScope = GetSlotsInScope();
			Gotos.Add(Fixup);
			return true;
		}

		// ---- Places ----

		/** Compiles a Name or an Index chain rooted at a Name, maybe ending in a vector part: emits the index expressions and finds the place. */
		bool ResolvePlace(const FBangoExpr& Expr, EPlaceUse Use, FPlaceInfo& Out)
		{
			if (Expr.Kind == EBangoExprKind::Empty)
			{
				return Fail(Expr.Loc, TEXT("The variable is missing. Fill it in."));
			}
			TArray<const FBangoExpr*> IndexExprs;
			const FBangoExpr* Current = &Expr;
			const FBangoMemberExpr* Member = Current->As<FBangoMemberExpr>();
			if (Member)
			{
				Current = Member->Object.Get();
			}
			while (const FBangoIndexExpr* IndexExpr = Current->As<FBangoIndexExpr>())
			{
				IndexExprs.Insert(IndexExpr->Index.Get(), 0);
				Current = IndexExpr->Array.Get();
			}
			const FBangoNameExpr* Root = Current->As<FBangoNameExpr>();
			check(Root);

			EBangoPlaceKind Kind = EBangoPlaceKind::Local;
			int32 Index = 0;
			FExprType RootType;

			if (const FLocalVar* Local = FindLocal(Root->Name))
			{
				Kind = EBangoPlaceKind::Local;
				Index = Local->Slot;
				RootType = FExprType::MakeKnown(Local->Type);
			}
			else if (const FLocalVar* ScriptVar = FindScriptVar(Root->Name))
			{
				// In a function: one of the script's inputs or script variables, in the main graph's frame.
				Kind = EBangoPlaceKind::Script;
				Index = ScriptVar->Slot;
				RootType = FExprType::MakeKnown(ScriptVar->Type);
			}
			else if (const FGraphLocal* Elsewhere = GraphLocals.Find(Root->Name))
			{
				// A local of this graph, but not visible here. Its name hides any global of that name in this graph.
				return Fail(Root->Loc, Elsewhere->bDisabled
					? FString::Printf(TEXT("'%s' is declared on line %d, but that declaration is disabled."), *Root->Name, Elsewhere->Line)
					: Elsewhere->bLoopVar
					? FString::Printf(TEXT("'%s' is the variable of the loop on line %d, and only exists inside that loop. (If you meant a global called '%s', rename one of them: in a graph, a local's name hides a global's.)"), *Root->Name, Elsewhere->Line, *Root->Name)
					: FString::Printf(TEXT("'%s' is a local declared on line %d, and isn't visible here: a local exists from its declaration to the end of its block. Use it there, or declare it further out. (If you meant a global called '%s', rename one of them: in a graph, a local's name hides a global's.)"), *Root->Name, Elsewhere->Line, *Root->Name));
			}
			else if (const FGraphLocal* MainLocal = FnAst ? MainGraphLocals.Find(Root->Name) : nullptr)
			{
				// A local of the main graph: it only exists in its block there, so a function can't use it.
				return Fail(Root->Loc, FString::Printf(TEXT("'%s' is %s on line %d of the script's main graph, which function '%s' doesn't see: functions see the script's inputs and script variables, not its locals. Make it a script variable ('scriptVar' at the top of the script), or pass it to the function as a parameter. (If you meant a global called '%s', rename one of them.)"),
					*Root->Name, MainLocal->bLoopVar ? TEXT("the variable of the loop") : TEXT("a local declared"), MainLocal->Line, *FnAst->Name, *Root->Name));
			}
			else
			{
				// Not a local, parameter or loop variable here: a global (declared in this file or not), with or without a leading '_'.
				Kind = EBangoPlaceKind::Global;
				Index = AddName(Root->Name);
				if (const FBangoType* Declared = DeclaredGlobals.Find(Root->Name))
				{
					RootType = FExprType::MakeKnown(*Declared);
				}
				else if (Use == EPlaceUse::Write)
				{
					return Fail(Root->Loc, FString::Printf(TEXT("'%s' isn't declared in this script, so it can't be changed here. Declare it at the top: as a local, like 'int %s;', or as a global, like 'global int %s;'."),
						*Root->Name, *Root->Name, *Root->Name));
				}
				else
				{
					RootType = FExprType::MakeUnknown();
				}
			}

			// One index more than its array levels, on a vector or a string: a part or a character (`_v[1]`, `_names[0][2]`).
			const bool bIndexableElement = RootType.IsKnown()
				&& (RootType.Type.Base == EBangoBaseType::Vector2 || RootType.Type.Base == EBangoBaseType::Vector3 || RootType.Type.Base == EBangoBaseType::String);
			const bool bElement = bIndexableElement && IndexExprs.Num() == RootType.Type.ArrayDepth + 1;
			if (RootType.IsKnown() && IndexExprs.Num() > RootType.Type.ArrayDepth && !bElement)
			{
				if (!RootType.Type.IsArray() && !bIndexableElement)
				{
					return Fail(Root->Loc, FString::Printf(TEXT("'%s' is %s, so it can't be indexed: only arrays, vectors and strings can be."), *Root->Name, *RootType.Describe()));
				}
				return Fail(Root->Loc, FString::Printf(TEXT("'%s' is %s, so it can be indexed at most %d time(s)."),
					*Root->Name, *RootType.Describe(), (int32)RootType.Type.ArrayDepth + (bIndexableElement ? 1 : 0)));
			}
			if (bElement && Member)
			{
				return Fail(Member->Loc, FString::Printf(TEXT("'%s' is a %s, so it has no parts."),
					*Root->Name, RootType.Type.Base == EBangoBaseType::String ? TEXT("string's character, a string") : TEXT("vector's part, a float")));
			}
			if (bElement && !CheckElementIndex(*IndexExprs.Last(), RootType.Type.Base))
			{
				return false;
			}

			for (const FBangoExpr* IndexExpr : IndexExprs)
			{
				FExprType IndexType;
				if (!CompileExpr(*IndexExpr, nullptr, IndexType))
				{
					return false;
				}
				if (!IndexType.Is(FBangoType::Int()) && !IndexType.IsUnknown())
				{
					return Fail(IndexExpr->Loc, FString::Printf(TEXT("An array index must be an int, but this is %s."), *IndexType.Describe()));
				}
			}

			Out.NumIndexes = IndexExprs.Num();
			Out.bElement = bElement;
			Out.Text = Root->Name;
			for (int32 i = 0; i < IndexExprs.Num(); ++i)
			{
				Out.Text += TEXT("[...]");
			}
			if (bElement)
			{
				Out.Type = FExprType::MakeKnown(RootType.Type.Base == EBangoBaseType::String ? FBangoType::String() : FBangoType::Double());
			}
			else if (RootType.IsKnown())
			{
				Out.Type = FExprType::MakeKnown(FBangoType(RootType.Type.Base, (uint8)(RootType.Type.ArrayDepth - IndexExprs.Num())));
			}
			else
			{
				Out.Type = RootType;
			}

			int32 Part = -1;
			if (Member)
			{
				if (!CheckVectorPart(Out.Type, *Member, Out.Text))
				{
					return false;
				}
				Part = Member->Part;
				Out.Text += FString::Printf(TEXT(".%s"), PartName(Part));
				Out.Type = FExprType::MakeKnown(FBangoType::Double());
			}
			Out.PlaceIndex = AddPlace(Kind, Index, IndexExprs.Num(), bElement ? FBangoPlace::ElementPart : Part);
			return true;
		}

		/**
		 * An index into a vector or a string: an int; one written as a number must be in range (a vector's parts are
		 * 0 to 1 or 2; nothing is negative). One worked out is checked when it runs.
		 */
		bool CheckElementIndex(const FBangoExpr& IndexExpr, EBangoBaseType Base)
		{
			const FBangoLiteralExpr* Literal = IndexExpr.As<FBangoLiteralExpr>();
			int64 Value = 0;
			bool bConstant = false;
			if (Literal && Literal->LiteralKind == EBangoLiteralKind::Int)
			{
				Value = Literal->IntValue;
				bConstant = true;
			}
			else if (const FBangoUnaryExpr* Unary = IndexExpr.As<FBangoUnaryExpr>(); Unary && Unary->Op == EBangoUnaryOp::Negate)
			{
				const FBangoLiteralExpr* Inner = Unary->Operand->As<FBangoLiteralExpr>();
				if (Inner && Inner->LiteralKind == EBangoLiteralKind::Int)
				{
					Value = -Inner->IntValue;
					bConstant = true;
				}
			}
			if (!bConstant)
			{
				return true;
			}
			if (Value < 0)
			{
				return Fail(IndexExpr.Loc, FString::Printf(TEXT("Index %lld is negative. Indexes start at 0."), Value));
			}
			const int32 Size = Base == EBangoBaseType::Vector2 ? 2 : Base == EBangoBaseType::Vector3 ? 3 : -1;
			if (Size > 0 && Value >= Size)
			{
				return Fail(IndexExpr.Loc, FString::Printf(TEXT("A %s has %d parts, [0] to [%d] (x to %s), so it has no [%lld]."),
					Size == 2 ? TEXT("vector2") : TEXT("vector3"), Size, Size - 1, Size == 2 ? TEXT("y") : TEXT("z"), Value));
			}
			return true;
		}

		/** The thing before `.x` must be a vector that has that part. Unknown globals are checked when the script runs. */
		bool CheckVectorPart(const FExprType& ObjectType, const FBangoMemberExpr& Member, const FString& ObjectText)
		{
			if (ObjectType.IsUnknown())
			{
				return true;
			}
			if (!CheckUsableValue(ObjectType, Member.Object->Loc, TEXT("here")))
			{
				return false;
			}
			const int32 Size = ObjectType.VectorSize();
			if (Size == 0)
			{
				const FString What = ObjectText.IsEmpty() ? FString(TEXT("This")) : FString::Printf(TEXT("'%s'"), *ObjectText);
				return Fail(Member.MemberLoc, FString::Printf(TEXT("%s is %s, which has no parts. Only vector2 and vector3 have .x and .y (and .z)."), *What, *ObjectType.Describe()));
			}
			if (Member.Part >= Size)
			{
				return Fail(Member.MemberLoc, TEXT("A vector2 only has x and y. Use a vector3 for z."));
			}
			return true;
		}

		bool CheckVectorLiteralSize(const FBangoArrayLiteralExpr& ArrayExpr, const FBangoType& VectorType)
		{
			const int32 Size = VectorType.GetVectorSize();
			if (ArrayExpr.Elements.Num() == Size)
			{
				return true;
			}
			return Fail(ArrayExpr.Loc, FString::Printf(TEXT("A %s needs exactly %d numbers, like %s, but this has %d."),
				*VectorType.ToString(), Size, Size == 2 ? TEXT("[1, 2]") : TEXT("[1, 2, 3]"), ArrayExpr.Elements.Num()));
		}

		bool CheckVectorPartNotNil(const FBangoExpr& Element, const FBangoType& VectorType, int32 Part)
		{
			const FBangoLiteralExpr* Lit = Element.As<FBangoLiteralExpr>();
			if (Lit && Lit->LiteralKind == EBangoLiteralKind::Nil)
			{
				return Fail(Element.Loc, FString::Printf(TEXT("A vector's parts can't be nil (this is the %s of %s)."), PartName(Part), *Article(VectorType)));
			}
			return true;
		}

		/** `[1, _f, 3]` where a vector is expected. */
		bool CompileVectorLiteral(const FBangoArrayLiteralExpr& ArrayExpr, const FBangoType& VectorType, FExprType& OutType)
		{
			if (!CheckVectorLiteralSize(ArrayExpr, VectorType))
			{
				return false;
			}
			const FBangoType Double = FBangoType::Double();
			for (int32 i = 0; i < ArrayExpr.Elements.Num(); ++i)
			{
				const FBangoExpr& Element = *ArrayExpr.Elements[i];
				FExprType T;
				if (!CheckVectorPartNotNil(Element, VectorType, i)
					|| !CompileExpr(Element, &Double, T)
					|| !CanAssign(Double, T, Element.Loc, FString::Printf(TEXT("The %s of %s"), PartName(i), *Article(VectorType))))
				{
					return false;
				}
				// No Convert: MakeVector takes ints and floats, and checks values the compiler couldn't type.
			}
			Emit(EBangoOp::MakeVector, ArrayExpr.Elements.Num());
			OutType = FExprType::MakeKnown(VectorType);
			return true;
		}

		/** `_pos.x` on a place, or `_who GetPos().x` on any other value. */
		bool CompileMember(const FBangoMemberExpr& Member, FExprType& OutType)
		{
			if (IsPlaceExpr(*Member.Object))
			{
				FPlaceInfo Place;
				if (!ResolvePlace(Member, EPlaceUse::Read, Place))
				{
					return false;
				}
				Emit(EBangoOp::LoadPlace, Place.PlaceIndex);
				OutType = Place.Type;
				return true;
			}
			FExprType ObjectType;
			if (!CompileExpr(*Member.Object, nullptr, ObjectType) || !CheckVectorPart(ObjectType, Member, FString()))
			{
				return false;
			}
			CurrentLoc = Member.MemberLoc;
			Emit(EBangoOp::GetPart, Member.Part);
			OutType = FExprType::MakeKnown(FBangoType::Double());
			return true;
		}

		static bool IsPlaceExpr(const FBangoExpr& Expr)
		{
			const FBangoExpr* Current = &Expr;
			while (const FBangoIndexExpr* IndexExpr = Current->As<FBangoIndexExpr>())
			{
				Current = IndexExpr->Array.Get();
			}
			return Current->Kind == EBangoExprKind::Name;
		}

		// ---- Expressions ----

		bool CompileExpr(const FBangoExpr& Expr, const FBangoType* Expected, FExprType& OutType)
		{
			const FBangoSourceLocation SavedLoc = CurrentLoc;
			CurrentLoc = Expr.Loc;
			const bool bOk = CompileExprInner(Expr, Expected, OutType);
			CurrentLoc = SavedLoc;
			return bOk && !bFailed;
		}

		bool CompileExprInner(const FBangoExpr& Expr, const FBangoType* Expected, FExprType& OutType)
		{
			switch (Expr.Kind)
			{
			case EBangoExprKind::Literal:      return CompileLiteral(*Expr.As<FBangoLiteralExpr>(), Expected, OutType);
			case EBangoExprKind::ArrayLiteral: return CompileArrayLiteral(*Expr.As<FBangoArrayLiteralExpr>(), Expected, OutType);
			case EBangoExprKind::Name:
			case EBangoExprKind::Index:
				if (IsPlaceExpr(Expr))
				{
					FPlaceInfo Place;
					if (!ResolvePlace(Expr, EPlaceUse::Read, Place))
					{
						return false;
					}
					Emit(EBangoOp::LoadPlace, Place.PlaceIndex);
					OutType = Place.Type;
					return true;
				}
				return CompileIndexOfValue(*Expr.As<FBangoIndexExpr>(), OutType);
			case EBangoExprKind::Unary:        return CompileUnary(*Expr.As<FBangoUnaryExpr>(), OutType);
			case EBangoExprKind::Binary:       return CompileBinary(*Expr.As<FBangoBinaryExpr>(), OutType);
			case EBangoExprKind::Call:         return CompileCall(*Expr.As<FBangoCallExpr>(), true, OutType);
			case EBangoExprKind::Member:       return CompileMember(*Expr.As<FBangoMemberExpr>(), OutType);
			case EBangoExprKind::Empty:        return Fail(Expr.Loc, TEXT("This value is missing. Fill it in."));
			case EBangoExprKind::IsNil:
			{
				FPlaceInfo Place;
				if (!ResolvePlace(*Expr.As<FBangoIsNilExpr>()->Operand, EPlaceUse::Check, Place))
				{
					return false;
				}
				if (Place.bElement)
				{
					return Fail(Expr.Loc, TEXT("A vector's part or a string's character is never nil (an index outside it is an error), so isNil can't check it. Check the vector or string itself, and its Count."));
				}
				Emit(EBangoOp::IsNilPlace, Place.PlaceIndex);
				OutType = FExprType::MakeKnown(FBangoType::Bool());
				return true;
			}
			default:
				return Fail(Expr.Loc, TEXT("Unexpected expression."));
			}
		}

		bool CompileLiteral(const FBangoLiteralExpr& Lit, const FBangoType* Expected, FExprType& OutType)
		{
			switch (Lit.LiteralKind)
			{
			case EBangoLiteralKind::Bool:
				Emit(EBangoOp::PushConst, AddConstant(FBangoValue::MakeBool(Lit.BoolValue)));
				OutType = FExprType::MakeKnown(FBangoType::Bool());
				return true;
			case EBangoLiteralKind::Int:
				Emit(EBangoOp::PushConst, AddConstant(FBangoValue::MakeInt((int32)Lit.IntValue)));
				OutType = FExprType::MakeKnown(FBangoType::Int());
				return true;
			case EBangoLiteralKind::Double:
				Emit(EBangoOp::PushConst, AddConstant(FBangoValue::MakeDouble(Lit.DoubleValue)));
				OutType = FExprType::MakeKnown(FBangoType::Double());
				return true;
			case EBangoLiteralKind::String:
				Emit(EBangoOp::PushConst, AddConstant(FBangoValue::MakeString(Lit.StringValue)));
				OutType = FExprType::MakeKnown(FBangoType::String());
				return true;
			case EBangoLiteralKind::Entity:
				Emit(EBangoOp::PushConst, AddConstant(FBangoValue::MakeEntity(Lit.StringValue)));
				OutType = FExprType::MakeKnown(FBangoType::Entity());
				return true;
			case EBangoLiteralKind::Text:
			{
				FBangoValue Text;
				if (!MakeTextValue(Lit, Text))
				{
					return false;
				}
				Emit(EBangoOp::PushConst, AddConstant(Text));
				OutType = FExprType::MakeKnown(FBangoType::Text());
				return true;
			}
			case EBangoLiteralKind::Nil:
				if (Expected)
				{
					Emit(EBangoOp::PushNil, AddType(*Expected));
				}
				else if (!bInferring)
				{
					return Fail(Lit.Loc, TEXT("nil can only be assigned, passed or returned where its type is known."));
				}
				OutType = FExprType::MakeNil();
				return true;
			default:
				return Fail(Lit.Loc, TEXT("Unexpected literal."));
			}
		}

		/** Unifies element types of an array literal with no expected type. */
		bool UnifyElementTypes(const TArray<FExprType>& Types, const FBangoSourceLocation& Loc, FExprType& OutElement)
		{
			bool bHave = false;
			FBangoType Result;
			bool bSawEmptyArray = false;

			for (const FExprType& T : Types)
			{
				switch (T.Kind)
				{
				case ETypeKind::Known:
					if (!bHave)
					{
						Result = T.Type;
						bHave = true;
					}
					else if (Result != T.Type)
					{
						const bool bNumbers = Result.ArrayDepth == T.Type.ArrayDepth
							&& (Result.Base == EBangoBaseType::Int || Result.Base == EBangoBaseType::Double)
							&& (T.Type.Base == EBangoBaseType::Int || T.Type.Base == EBangoBaseType::Double);
						if (!bNumbers)
						{
							return Fail(Loc, FString::Printf(TEXT("All elements of an array must have the same type, but this array mixes %s and %s."),
								*Article(Result), *Article(T.Type)));
						}
						Result = FBangoType(EBangoBaseType::Double, Result.ArrayDepth);
					}
					break;
				case ETypeKind::Unknown:
					return Fail(Loc, TEXT("This array's type can't be worked out, because it contains a global that isn't declared in this script. Declare the global, or declare a variable with the array's type and assign the array to it."));
				case ETypeKind::Void:
					return Fail(Loc, TEXT("An array element is a call that doesn't return a value."));
				case ETypeKind::EmptyArray:
					bSawEmptyArray = true;
					break;
				default:
					break;  // nil fits any type
				}
			}

			if (!bHave)
			{
				OutElement = FExprType::MakeEmptyArray();
				return true;  // caller decides whether that's an error
			}
			if (bSawEmptyArray && !Result.IsArray())
			{
				return Fail(Loc, FString::Printf(TEXT("This array mixes %s and arrays."), *Article(Result)));
			}
			OutElement = FExprType::MakeKnown(Result);
			return true;
		}

		bool CompileArrayLiteral(const FBangoArrayLiteralExpr& ArrayExpr, const FBangoType* Expected, FExprType& OutType)
		{
			FBangoType ElementType;

			if (Expected && Expected->IsVector())
			{
				return CompileVectorLiteral(ArrayExpr, *Expected, OutType);
			}
			if (Expected && Expected->IsArray())
			{
				ElementType = Expected->GetElementType();
			}
			else if (Expected)
			{
				return Fail(ArrayExpr.Loc, FString::Printf(TEXT("Expected %s here, but this is an array."), *Article(*Expected)));
			}
			else
			{
				// Learn the element types by compiling once, then throw that code away and compile again with the type known.
				const int32 Mark = Here();
				const bool bWasInferring = bInferring;
				bInferring = true;
				TArray<FExprType> Types;
				for (const FBangoExprPtr& Element : ArrayExpr.Elements)
				{
					FExprType T;
					if (!CompileExpr(*Element, nullptr, T))
					{
						return false;
					}
					Types.Add(T);
				}
				bInferring = bWasInferring;
				Fn->Code.SetNum(Mark);
				Fn->Debug.SetNum(Mark);

				FExprType Unified;
				if (!UnifyElementTypes(Types, ArrayExpr.Loc, Unified))
				{
					return false;
				}
				if (!Unified.IsKnown())
				{
					if (bInferring)
					{
						OutType = FExprType::MakeEmptyArray();
						return true;
					}
					return Fail(ArrayExpr.Loc, TEXT("This array's type can't be worked out here. Assign it to a variable with a declared type first, like 'int[] _a = [];'."));
				}
				if (Unified.Type.ArrayDepth >= FBangoType::MaxArrayDepth)
				{
					return Fail(ArrayExpr.Loc, FString::Printf(TEXT("Arrays can be nested at most %d levels deep."), (int32)FBangoType::MaxArrayDepth));
				}
				ElementType = Unified.Type;
			}

			for (const FBangoExprPtr& Element : ArrayExpr.Elements)
			{
				FExprType T;
				if (!CompileExpr(*Element, &ElementType, T) || !CanAssign(ElementType, T, Element->Loc, TEXT("This array element")))
				{
					return false;
				}
			}
			Emit(EBangoOp::MakeArray, ArrayExpr.Elements.Num(), AddType(ElementType));
			OutType = FExprType::MakeKnown(ElementType.MakeArrayType());
			return true;
		}

		/** Indexing a value that isn't a variable, e.g. `GetList()[0]`. */
		bool CompileIndexOfValue(const FBangoIndexExpr& IndexExpr, FExprType& OutType)
		{
			FExprType ArrayType;
			if (!CompileExpr(*IndexExpr.Array, nullptr, ArrayType) || !CheckUsableValue(ArrayType, IndexExpr.Array->Loc, TEXT("as an array")))
			{
				return false;
			}
			const bool bElement = ArrayType.IsKnown() && !ArrayType.Type.IsArray()
				&& (ArrayType.Type.Base == EBangoBaseType::Vector2 || ArrayType.Type.Base == EBangoBaseType::Vector3 || ArrayType.Type.Base == EBangoBaseType::String);
			if (ArrayType.IsKnown() && !ArrayType.Type.IsArray() && !bElement)
			{
				return Fail(IndexExpr.Loc, FString::Printf(TEXT("Only arrays, vectors and strings can be indexed, but this is %s."), *ArrayType.Describe()));
			}
			if (bElement && !CheckElementIndex(*IndexExpr.Index, ArrayType.Type.Base))
			{
				return false;
			}
			FExprType IndexType;
			if (!CompileExpr(*IndexExpr.Index, nullptr, IndexType))
			{
				return false;
			}
			if (!IndexType.Is(FBangoType::Int()) && !IndexType.IsUnknown())
			{
				return Fail(IndexExpr.Index->Loc, FString::Printf(TEXT("An array index must be an int, but this is %s."), *IndexType.Describe()));
			}
			Emit(EBangoOp::Index);
			if (bElement)
			{
				OutType = FExprType::MakeKnown(ArrayType.Type.Base == EBangoBaseType::String ? FBangoType::String() : FBangoType::Double());
			}
			else
			{
				OutType = ArrayType.IsKnown() ? FExprType::MakeKnown(ArrayType.Type.GetElementType()) : FExprType::MakeUnknown();
			}
			return true;
		}

		bool CompileUnary(const FBangoUnaryExpr& Unary, FExprType& OutType)
		{
			FExprType OperandType;
			if (!CompileExpr(*Unary.Operand, nullptr, OperandType) || !CheckUsableValue(OperandType, Unary.Operand->Loc, TEXT("here")))
			{
				return false;
			}
			if (Unary.Op == EBangoUnaryOp::Not)
			{
				if (!OperandType.Is(FBangoType::Bool()) && !OperandType.IsUnknown())
				{
					return Fail(Unary.Loc, FString::Printf(TEXT("'!' needs a bool, but this is %s."), *OperandType.Describe()));
				}
				Emit(EBangoOp::Not);
				OutType = FExprType::MakeKnown(FBangoType::Bool());
				return true;
			}
			if (!OperandType.IsNumber() && !OperandType.IsUnknown() && OperandType.VectorSize() == 0)
			{
				return Fail(Unary.Loc, FString::Printf(TEXT("'-' needs a number or a vector, but this is %s."), *OperandType.Describe()));
			}
			Emit(EBangoOp::Negate);
			OutType = OperandType;
			return true;
		}

		bool CompileBinary(const FBangoBinaryExpr& Binary, FExprType& OutType)
		{
			if (Binary.Op == EBangoBinaryOp::And || Binary.Op == EBangoBinaryOp::Or)
			{
				// `a || b` is
				//        TryUnknown -> R
				//        <a>
				//        EndTryUnknown
				//        JumpIfTrueOrPop L     (true: that's the answer, b isn't worked out)
				//   R:   <b>
				//        ResolveUnknown R, ||
				//   L:
				// A global that isn't set (it doesn't exist, or is nil) is unknown, not an error straight away: if a
				// reads one, the script goes on at R with a noted as unknown, and ResolveUnknown lets a true b settle
				// it (unknown || true is true). If b doesn't, a's unset global stops the script as it would have (or
				// makes a condition Unset, or keeps a waitUntil waiting). An unknown b isn't caught here: with a
				// false, nothing can settle it. `&&` is the same with false: unknown && false is false.
				const bool bAnd = Binary.Op == EBangoBinaryOp::And;
				const int32 Try = Emit(EBangoOp::TryUnknown);
				FExprType LeftType;
				if (!CompileExpr(*Binary.Left, nullptr, LeftType))
				{
					return false;
				}
				if (!LeftType.Is(FBangoType::Bool()) && !LeftType.IsUnknown())
				{
					return Fail(Binary.Left->Loc, FString::Printf(TEXT("'%s' needs bools, but this is %s."), bAnd ? TEXT("&&") : TEXT("||"), *LeftType.Describe()));
				}
				Emit(EBangoOp::EndTryUnknown);
				const int32 Jump = Emit(bAnd ? EBangoOp::JumpIfFalseOrPop : EBangoOp::JumpIfTrueOrPop);
				const int32 Right = Here();
				PatchJump(Try, Right);
				FExprType RightType;
				if (!CompileExpr(*Binary.Right, nullptr, RightType))
				{
					return false;
				}
				if (!RightType.Is(FBangoType::Bool()) && !RightType.IsUnknown())
				{
					return Fail(Binary.Right->Loc, FString::Printf(TEXT("'%s' needs bools, but this is %s."), bAnd ? TEXT("&&") : TEXT("||"), *RightType.Describe()));
				}
				if (RightType.IsUnknown())
				{
					// The result must be a bool either way.
					Emit(EBangoOp::Convert, AddType(FBangoType::Bool()));
				}
				Emit(EBangoOp::ResolveUnknown, Right, bAnd ? 0 : 1);
				PatchJump(Jump, Here());
				OutType = FExprType::MakeKnown(FBangoType::Bool());
				return true;
			}

			FExprType LeftType;
			FExprType RightType;
			const bool bLeftIsNil = Binary.Left->Kind == EBangoExprKind::Literal && Binary.Left->As<FBangoLiteralExpr>()->LiteralKind == EBangoLiteralKind::Nil;
			const bool bRightIsNil = Binary.Right->Kind == EBangoExprKind::Literal && Binary.Right->As<FBangoLiteralExpr>()->LiteralKind == EBangoLiteralKind::Nil;
			if (bLeftIsNil || bRightIsNil)
			{
				FExprType NilType = FExprType::MakeNil();
				FExprType Unused;
				return BinaryResult(Binary.Op, NilType, NilType, Binary.Loc, Unused);
			}
			// `_pos + [0, 0, 100]`: an array literal next to a vector is a vector of the same kind.
			const FBangoType* LeftExpected = nullptr;
			const FBangoType* RightExpected = nullptr;
			FBangoType OtherVector;
			const bool bLeftLiteral = Binary.Left->Kind == EBangoExprKind::ArrayLiteral;
			const bool bRightLiteral = Binary.Right->Kind == EBangoExprKind::ArrayLiteral;
			if (bLeftLiteral != bRightLiteral && ProbeVectorType(bLeftLiteral ? *Binary.Right : *Binary.Left, OtherVector))
			{
				(bLeftLiteral ? LeftExpected : RightExpected) = &OtherVector;
			}
			if (!CompileExpr(*Binary.Left, LeftExpected, LeftType)
				|| !CompileExpr(*Binary.Right, RightExpected, RightType)
				|| !BinaryResult(Binary.Op, LeftType, RightType, Binary.Loc, OutType))
			{
				return false;
			}
			Emit(BinaryOpCode(Binary.Op));
			return true;
		}

		/** True if Expr has a known vector type. Compiles it to find out, then throws that code away. */
		bool ProbeVectorType(const FBangoExpr& Expr, FBangoType& OutType)
		{
			const int32 Mark = Here();
			FExprType T;
			const bool bOk = CompileExpr(Expr, nullptr, T);
			Fn->Code.SetNum(Mark);
			Fn->Debug.SetNum(Mark);
			if (!bOk || T.VectorSize() == 0)
			{
				return false;
			}
			OutType = T.Type;
			return true;
		}

		// ---- Calls ----

		/**
		 * NilOk (optional): the arguments a command takes even if nil (MayBeNil), so a nil variable there is passed on as nil.
		 * Signature (optional): the command's, for its parameters whose type is a type parameter (TypeParam). TypeArgs
		 * then has an entry per type parameter, in and out: what each is in this call. In: Known for one the call's
		 * target has settled, Unknown for one it will settle when it runs, and Void for the rest, which the arguments
		 * settle here (BindTypeParams).
		 */
		bool CompileArgs(const FBangoCallExpr& Call, const TArray<FBangoType>& ParamTypes, const FString& What, const TArray<bool>* NilOk = nullptr,
			const FBangoCommandSignature* Signature = nullptr, TArray<FExprType>* TypeArgs = nullptr)
		{
			const bool bTypeParams = Signature && Signature->TypeParams.Num() > 0;
			// The types of the arguments given for type parameters, as they're compiled (when none needs to know its
			// type first: see InferTypeParams).
			TArray<FTypeParamArg> TypeParamArgs;
			TArray<bool> Settled; // the type parameters the target settled: nil can be passed for those
			if (bTypeParams)
			{
				check(TypeArgs && TypeArgs->Num() == Signature->TypeParams.Num());
				for (const FExprType& TypeArg : *TypeArgs)
				{
					Settled.Add(TypeArg.Kind != ETypeKind::Void);
				}
				TypeParamArgs.SetNum(FMath::Min(Call.Args.Num(), ParamTypes.Num()));
				if (NeedsTypesFirst(Call, *Signature, *TypeArgs) && !InferTypeParams(Call, *Signature, What, *TypeArgs))
				{
					return false;
				}
			}

			for (int32 i = 0; i < Call.Args.Num() && i < ParamTypes.Num(); ++i)
			{
				FExprType ArgType;
				bool bArgOk = false;
				const int32 TypeVar = bTypeParams ? Signature->Params[i].TypeVar : INDEX_NONE;
				if (TypeVar != INDEX_NONE)
				{
					const FExprType Bound = (*TypeArgs)[TypeVar];
					const FString ArgWhat = FString::Printf(TEXT("Argument %d of %s"), i + 1, *What);
					if (IsNilLiteral(*Call.Args[i]) && !(Settled[TypeVar] && Bound.IsKnown()))
					{
						// Its type comes from its arguments, and nil has none.
						return Fail(Call.Args[i]->Loc, TEXT("nil can only be assigned, passed or returned where its type is known."));
					}
					if (Bound.IsKnown())
					{
						// Its type parameter is known already: a written-out [1, 2, 3] becomes a vector, an int a float.
						const FBangoType Wanted = FBangoCommandSignature::GetTypeOf(Bound.Type, Signature->Params[i].TypeVarDepth);
						if (!CompileExpr(*Call.Args[i], &Wanted, ArgType) || !CanAssign(Wanted, ArgType, Call.Args[i]->Loc, ArgWhat))
						{
							return false;
						}
						EmitConvertIfNeeded(Wanted, ArgType);
						continue;
					}
					// Otherwise as it is: the call works its type parameters out from its arguments when it runs, and
					// converts them (an undeclared global is only checked then).
					if (!CompileExpr(*Call.Args[i], nullptr, ArgType) || !CheckUsableValue(ArgType, Call.Args[i]->Loc, TEXT("as an argument here")))
					{
						return false;
					}
					TypeParamArgs[i].bGiven = true;
					TypeParamArgs[i].Type = ArgType;
					continue;
				}
				if (NilOk && NilOk->IsValidIndex(i) && (*NilOk)[i] && IsPlaceExpr(*Call.Args[i]))
				{
					// Like isNil: a nil variable, a global nobody made or an element past the end is passed on as nil.
					FPlaceInfo Place;
					bArgOk = ResolvePlace(*Call.Args[i], EPlaceUse::Read, Place);
					if (bArgOk)
					{
						Emit(EBangoOp::LoadPlace, Place.PlaceIndex, 1);
						ArgType = Place.Type;
					}
				}
				else
				{
					bArgOk = CompileExpr(*Call.Args[i], &ParamTypes[i], ArgType);
				}
				if (!bArgOk
					|| !CanAssign(ParamTypes[i], ArgType, Call.Args[i]->Loc, FString::Printf(TEXT("Argument %d of %s"), i + 1, *What)))
				{
					return false;
				}
				EmitConvertIfNeeded(ParamTypes[i], ArgType);
			}
			// Whatever is still open is settled by the arguments as they were compiled.
			return !bTypeParams || BindTypeParams(Call, *Signature, What, TypeParamArgs, *TypeArgs);
		}

		// ---- Type parameters (FBangoCommandSignature::TypeParam) ----

		/** An argument given for a parameter whose type is a type parameter. */
		struct FTypeParamArg
		{
			/** Its type is in Type (not a written-out array, whose type depends on the others'). */
			bool bGiven = false;
			FExprType Type;
			/** A written-out array, like [1, 2, 3]: its number of elements. It's a vector if the type parameter says so. */
			int32 LiteralCount = INDEX_NONE;
		};

		static bool IsNilLiteral(const FBangoExpr& Expr)
		{
			const FBangoLiteralExpr* Literal = Expr.As<FBangoLiteralExpr>();
			return Literal && Literal->LiteralKind == EBangoLiteralKind::Nil;
		}

		/**
		 * True if an argument for a type parameter that's still open is a written-out array: what it is depends on
		 * its type parameter (`Lerp([0, 0, 0], _to, 0.5)`: a vector3, as _to is), so the others' types are needed first.
		 */
		static bool NeedsTypesFirst(const FBangoCallExpr& Call, const FBangoCommandSignature& Signature, const TArray<FExprType>& TypeArgs)
		{
			for (int32 i = 0; i < Call.Args.Num() && i < Signature.Params.Num(); ++i)
			{
				const int32 TypeVar = Signature.Params[i].TypeVar;
				if (TypeVar != INDEX_NONE && TypeArgs[TypeVar].Kind == ETypeKind::Void && Call.Args[i]->As<FBangoArrayLiteralExpr>())
				{
					return true;
				}
			}
			return false;
		}

		/**
		 * Works out a call's open type parameters before its arguments are compiled, by compiling the arguments
		 * once and throwing that code away (as an array literal does for its elements). Kept per call, so a call
		 * inside another's arguments is worked out once however often it's compiled.
		 */
		bool InferTypeParams(const FBangoCallExpr& Call, const FBangoCommandSignature& Signature, const FString& What, TArray<FExprType>& TypeArgs)
		{
			if (const TArray<FExprType>* Found = InferredTypeParams.Find(&Call))
			{
				TypeArgs = *Found;
				return true;
			}
			TArray<FTypeParamArg> Args;
			Args.SetNum(FMath::Min(Call.Args.Num(), Signature.Params.Num()));
			const int32 Mark = Here();
			const bool bWasInferring = bInferring;
			bInferring = true;
			bool bOk = true;
			for (int32 i = 0; bOk && i < Args.Num(); ++i)
			{
				const int32 TypeVar = Signature.Params[i].TypeVar;
				if (TypeVar == INDEX_NONE || TypeArgs[TypeVar].Kind != ETypeKind::Void || IsNilLiteral(*Call.Args[i]))
				{
					continue;
				}
				const FBangoTypeParam& TypeParam = Signature.TypeParams[TypeVar];
				const FBangoArrayLiteralExpr* Literal = Call.Args[i]->As<FBangoArrayLiteralExpr>();
				if (Literal && Signature.Params[i].TypeVarDepth == 0 && !TypeParam.bAnyArray && !TypeParam.bAnyType)
				{
					Args[i].LiteralCount = Literal->Elements.Num(); // a vector, if anything
					continue;
				}
				// An array whose type can't be worked out ([]) says nothing here; compiling it for real says so.
				bOk = CompileExpr(*Call.Args[i], nullptr, Args[i].Type)
					&& (Args[i].Type.Kind != ETypeKind::Void || CheckUsableValue(Args[i].Type, Call.Args[i]->Loc, TEXT("as an argument here")));
				Args[i].bGiven = true;
			}
			bInferring = bWasInferring;
			Fn->Code.SetNum(Mark);
			Fn->Debug.SetNum(Mark);
			if (!bOk || !BindTypeParams(Call, Signature, What, Args, TypeArgs))
			{
				return false;
			}
			InferredTypeParams.Add(&Call, TypeArgs);
			return true;
		}

		/**
		 * What each of the signature's open type parameters (Void in TypeArgs) is in this call, from its arguments'
		 * types. An argument that's an array of one (T.Array()) says what it is, exactly, and the others must fit
		 * that. Without one, they must all be the same allowed type, an int counting as a float beside one (or where
		 * only floats are allowed). Unknown where it's only known when it runs: an undeclared global among them and
		 * no type that settles it.
		 */
		bool BindTypeParams(const FBangoCallExpr& Call, const FBangoCommandSignature& Signature, const FString& What, const TArray<FTypeParamArg>& Args, TArray<FExprType>& TypeArgs)
		{
			for (int32 Var = 0; Var < Signature.TypeParams.Num(); ++Var)
			{
				if (TypeArgs[Var].Kind != ETypeKind::Void)
				{
					continue; // settled already
				}
				const FBangoTypeParam& TypeParam = Signature.TypeParams[Var];
				bool bHave = false;
				bool bFromArray = false;
				FBangoType Type;
				int32 FirstArg = INDEX_NONE;
				bool bUnknown = false;

				// The arrays of it say what it is.
				for (int32 i = 0; i < Args.Num(); ++i)
				{
					const int32 Depth = Signature.Params[i].TypeVarDepth;
					if (Signature.Params[i].TypeVar != Var || Depth == 0 || !Args[i].bGiven)
					{
						continue;
					}
					if (!Args[i].Type.IsKnown())
					{
						bUnknown |= Args[i].Type.IsUnknown();
						continue;
					}
					const FBangoType ArgType = Args[i].Type.Type;
					const FBangoType Inner(ArgType.Base, (uint8)FMath::Max<int32>(ArgType.ArrayDepth - Depth, 0));
					if (ArgType.ArrayDepth < Depth || !TypeParam.Allows(Inner))
					{
						return Fail(Call.Args[i]->Loc, FString::Printf(TEXT("Argument %d of %s needs %s, but this is %s."), i + 1, *What, *TypeParam.Describe(Depth), *Args[i].Type.Describe()));
					}
					if (bHave && !(Inner == Type))
					{
						return Fail(Call.Args[i]->Loc, FString::Printf(TEXT("Argument %d of %s is %s, but argument %d ('%s') is %s. They must go together: %s"),
							i + 1, *What, *Args[i].Type.Describe(), FirstArg + 1, *Signature.Params[FirstArg].Name, *Args[FirstArg].Type.Describe(), *Signature.ToString()));
					}
					if (!bHave)
					{
						bHave = true;
						bFromArray = true;
						Type = Inner;
						FirstArg = i;
					}
				}

				for (int32 i = 0; i < Args.Num(); ++i)
				{
					if (Signature.Params[i].TypeVar != Var || Signature.Params[i].TypeVarDepth != 0 || !Args[i].bGiven)
					{
						continue;
					}
					if (!Args[i].Type.IsKnown())
					{
						bUnknown |= Args[i].Type.IsUnknown();
						continue; // an empty array or nil says nothing
					}
					if (bFromArray)
					{
						if (!Type.IsAssignableFrom(Args[i].Type.Type))
						{
							return Fail(Call.Args[i]->Loc, FString::Printf(TEXT("Argument %d of %s needs %s, but this is %s."), i + 1, *What, *Article(Type), *Args[i].Type.Describe()));
						}
						continue;
					}
					FBangoType ArgType;
					if (!TypeParam.Resolve(Args[i].Type.Type, ArgType))
					{
						return Fail(Call.Args[i]->Loc, FString::Printf(TEXT("Argument %d of %s needs %s, but this is %s."), i + 1, *What, *TypeParam.Describe(), *Args[i].Type.Describe()));
					}
					if (!bHave)
					{
						bHave = true;
						Type = ArgType;
						FirstArg = i;
						continue;
					}
					FBangoType Unified;
					if (!FBangoTypeParam::Unify(Type, ArgType, Unified))
					{
						return Fail(Call.Args[i]->Loc, FString::Printf(TEXT("Argument %d of %s is %s, but argument %d ('%s') is %s. They must be the same type: %s"),
							i + 1, *What, *Args[i].Type.Describe(), FirstArg + 1, *Signature.Params[FirstArg].Name, *Args[FirstArg].Type.Describe(), *Signature.ToString()));
					}
					Type = Unified;
				}
				for (int32 i = 0; bHave && !Type.IsVector() && i < Args.Num(); ++i)
				{
					// A written-out array beside something that isn't a vector.
					if (Signature.Params[i].TypeVar != Var || Args[i].LiteralCount == INDEX_NONE)
					{
						continue;
					}
					if (TypeParam.Allows(FBangoType::Vector2()) || TypeParam.Allows(FBangoType::Vector3()))
					{
						return Fail(Call.Args[i]->Loc, FString::Printf(TEXT("Argument %d of %s is an array of %d value(s), but argument %d ('%s') is %s. They must be the same type: %s"),
							i + 1, *What, Args[i].LiteralCount, FirstArg + 1, *Signature.Params[FirstArg].Name, *Args[FirstArg].Type.Describe(), *Signature.ToString()));
					}
					return Fail(Call.Args[i]->Loc, FString::Printf(TEXT("Argument %d of %s needs %s, but this is an array."), i + 1, *What, *TypeParam.Describe()));
				}
				if (!bHave)
				{
					// Only written-out arrays say what it is: a vector of that many numbers.
					for (int32 i = 0; i < Args.Num() && !bHave; ++i)
					{
						if (Signature.Params[i].TypeVar != Var || Args[i].LiteralCount == INDEX_NONE)
						{
							continue;
						}
						const FBangoType Vector = Args[i].LiteralCount == 2 ? FBangoType::Vector2() : FBangoType::Vector3();
						if ((Args[i].LiteralCount != 2 && Args[i].LiteralCount != 3) || !TypeParam.Allows(Vector))
						{
							const bool bVectors = TypeParam.Allows(FBangoType::Vector2()) || TypeParam.Allows(FBangoType::Vector3());
							return Fail(Call.Args[i]->Loc, bVectors
								? FString::Printf(TEXT("Argument %d of %s needs %s, but this is an array of %d value(s)."), i + 1, *What, *TypeParam.Describe(), Args[i].LiteralCount)
								: FString::Printf(TEXT("Argument %d of %s needs %s, but this is an array."), i + 1, *What, *TypeParam.Describe()));
						}
						bHave = true;
						Type = Vector;
					}
				}
				// Ints beside an undeclared global: a float if the global turns out to be one.
				const bool bMayBecomeFloat = bHave && !bFromArray && bUnknown && Type.Base == EBangoBaseType::Int && TypeParam.Allows(FBangoType(EBangoBaseType::Double, Type.ArrayDepth));
				TypeArgs[Var] = bHave && !bMayBecomeFloat ? FExprType::MakeKnown(Type) : FExprType::MakeUnknown();
			}
			return true;
		}

		/**
		 * `target call Name(args)`: a Blueprint function on the target's actor, found and called when it runs. Its
		 * arguments are converted to the function's parameters then, and its result's type is only known then too.
		 */
		bool CompileReflectedCall(const FBangoCallExpr& Call, bool bValueNeeded, FExprType& OutType)
		{
			const FBangoType EntityType = FBangoType::Entity();
			FExprType TargetType;
			if (!CompileExpr(*Call.Target, &EntityType, TargetType)
				|| !CanAssign(EntityType, TargetType, Call.Target->Loc, FString::Printf(TEXT("The target of 'call %s'"), *Call.Name)))
			{
				return false;
			}
			EmitConvertIfNeeded(EntityType, TargetType);
			for (const FBangoExprPtr& Arg : Call.Args)
			{
				// A plain nil fills an actor parameter with none.
				const FBangoLiteralExpr* Literal = Arg->As<FBangoLiteralExpr>();
				if (Literal && Literal->LiteralKind == EBangoLiteralKind::Nil)
				{
					Emit(EBangoOp::PushNil, AddType(FBangoType::Entity()));
					continue;
				}
				FExprType ArgType;
				if (!CompileExpr(*Arg, nullptr, ArgType) || !CheckUsableValue(ArgType, Arg->Loc, TEXT("as an argument here")))
				{
					return false;
				}
			}
			CurrentLoc = Call.Loc;
			Emit(EBangoOp::CallReflected, AddName(Call.Name), Call.Args.Num(), bValueNeeded ? 1 : 0);
			if (!bInferring) // an array's elements are compiled twice; this is the time that's kept
			{
				FBangoReflectedCallSite& Site = Program.ReflectedCalls.AddDefaulted_GetRef();
				const FBangoLiteralExpr* TargetLiteral = Call.Target->As<FBangoLiteralExpr>();
				Site.TargetEntity = TargetLiteral && TargetLiteral->LiteralKind == EBangoLiteralKind::Entity ? TargetLiteral->StringValue : FString();
				Site.Name = Call.Name;
				Site.NumArgs = Call.Args.Num();
				Site.bResultUsed = bValueNeeded;
				Site.Line = Call.NameLoc.Line;
				Site.Column = Call.NameLoc.Column;
			}
			OutType = bValueNeeded ? FExprType::MakeUnknown() : FExprType::MakeVoid();
			return true;
		}

		bool CompileCall(const FBangoCallExpr& Call, bool bValueNeeded, FExprType& OutType)
		{
			if (Call.bReflect)
			{
				return CompileReflectedCall(Call, bValueNeeded, OutType);
			}

			// Script function
			if (const int32* FunctionIndex = FunctionIndexes.Find(Call.Name))
			{
				const FBangoFunction& Function = *Script.Functions[*FunctionIndex - 1];
				if (Call.Target)
				{
					return Fail(Call.NameLoc, FString::Printf(TEXT("'%s' is a function in this script, and functions can't be called with a target."), *Call.Name));
				}
				if (Call.Args.Num() != Function.Params.Num())
				{
					return Fail(Call.NameLoc, FString::Printf(TEXT("Function '%s' takes %d argument(s), but %d were given."), *Call.Name, Function.Params.Num(), Call.Args.Num()));
				}
				TArray<FBangoType> ParamTypes;
				for (const FBangoParam& Param : Function.Params)
				{
					ParamTypes.Add(Param.Type);
				}
				if (!CompileArgs(Call, ParamTypes, FString::Printf(TEXT("'%s'"), *Call.Name)))
				{
					return false;
				}
				CurrentLoc = Call.Loc;
				Emit(EBangoOp::CallFunction, *FunctionIndex, Call.Args.Num());
				return FinishCall(Call, !Function.bReturnsVoid, Function.ReturnType, bValueNeeded, OutType);
			}

			// C++ command
			const FBangoCommandSignature* Signature = Commands.FindCommand(Call.Name);
			if (!Signature)
			{
				const FString Suggestion = FindCallableNameIgnoringCase(Call.Name);
				if (!Suggestion.IsEmpty())
				{
					return Fail(Call.NameLoc, FString::Printf(TEXT("Unknown command or function '%s'. Did you mean '%s'? Names are case-sensitive."), *Call.Name, *Suggestion));
				}
				return Fail(Call.NameLoc, FString::Printf(TEXT("Unknown command or function '%s'. Names are case-sensitive."), *Call.Name));
			}

			if (Call.Name == TEXT("Log") && Signature->Name == TEXT("Log"))
			{
				// Log used to write to the output log (spec v0.37): say so, rather than a puzzling type error.
				const FBangoLiteralExpr* First = Call.Args.Num() > 0 ? Call.Args[0]->As<FBangoLiteralExpr>() : nullptr;
				if ((!bValueNeeded && !bInferring) || (First && First->LiteralKind == EBangoLiteralKind::String))
				{
					return Fail(Call.NameLoc, TEXT("'Log' is the logarithm (Log(100) is 2.0). To write to the output log, use WriteLog(\"...\")."));
				}
			}
			if (Signature->bHasTarget && !Call.Target)
			{
				return Fail(Call.NameLoc, FString::Printf(TEXT("'%s' needs a target: %s"), *Call.Name, *Signature->ToString()));
			}
			if (!bValueNeeded && !bInferring && Signature->MustUseResult())
			{
				return FailUnusedResult(Call);
			}
			if (!Signature->bHasTarget && Call.Target)
			{
				return Fail(Call.NameLoc, FString::Printf(TEXT("'%s' doesn't take a target: %s"), *Call.Name, *Signature->ToString()));
			}
			const int32 Required = Signature->GetNumRequiredParams();
			if (Call.Args.Num() < Required || (!Signature->bVariadic && Call.Args.Num() > Signature->Params.Num()))
			{
				return Fail(Call.NameLoc, FString::Printf(TEXT("Wrong number of arguments for '%s' (%d given): %s"), *Call.Name, Call.Args.Num(), *Signature->ToString()));
			}
			if (!CheckPlaceholders(Call, *Signature))
			{
				return false;
			}

			// What each of its type parameters is in this call: Void until something settles it.
			TArray<FExprType> TypeArgs;
			TypeArgs.Init(FExprType::MakeVoid(), Signature->TypeParams.Num());
			const bool bTargetTypeParam = Signature->TypeParams.IsValidIndex(Signature->TargetTypeVar);
			FPlaceInfo TargetPlace; // for a command that changes its target in place
			if (Call.Target)
			{
				FExprType TargetType;
				const FString TargetWhat = FString::Printf(TEXT("The target of '%s'"), *Call.Name);
				if (Signature->bMutableTarget)
				{
					// The target is where the result goes back, so it's a place: its indexes are worked out here, once.
					if (!IsPlaceExpr(*Call.Target))
					{
						return Fail(Call.Target->Loc, FString::Printf(TEXT("%s changes its target, so the target must be a variable, a global or an array element."), *Call.Name));
					}
					if (!ResolvePlace(*Call.Target, EPlaceUse::Write, TargetPlace))
					{
						return false;
					}
					if (Fn->Places[TargetPlace.PlaceIndex].Part != -1)
					{
						return Fail(Call.Target->Loc, FString::Printf(TEXT("%s changes its target, so the target must be a variable, a global or an array element, not a part of a vector or a character of a string ('%s')."),
							*Call.Name, *TargetPlace.Text));
					}
					TargetType = TargetPlace.Type;
					if (!bTargetTypeParam && !TargetType.Is(Signature->TargetType))
					{
						return Fail(Call.Target->Loc, FString::Printf(TEXT("%s needs %s to change, but '%s' is %s."), *TargetWhat, *Article(Signature->TargetType), *TargetPlace.Text, *TargetType.Describe()));
					}
				}
				else if (bTargetTypeParam)
				{
					if (!CompileExpr(*Call.Target, nullptr, TargetType) || !CheckUsableValue(TargetType, Call.Target->Loc, TEXT("as a target here")))
					{
						return false;
					}
				}
				else
				{
					if (!CompileExpr(*Call.Target, &Signature->TargetType, TargetType) || !CanAssign(Signature->TargetType, TargetType, Call.Target->Loc, TargetWhat))
					{
						return false;
					}
					EmitConvertIfNeeded(Signature->TargetType, TargetType);
				}

				if (bTargetTypeParam)
				{
					// The target says what its type parameter is, exactly (an array is never converted to fit a value).
					const FBangoTypeParam& TypeParam = Signature->TypeParams[Signature->TargetTypeVar];
					const int32 Depth = Signature->TargetTypeVarDepth;
					if (TargetType.IsKnown())
					{
						const FBangoType Inner(TargetType.Type.Base, (uint8)FMath::Max<int32>(TargetType.Type.ArrayDepth - Depth, 0));
						if (TargetType.Type.ArrayDepth < Depth || !TypeParam.Allows(Inner))
						{
							return Fail(Call.Target->Loc, Signature->bMutableTarget
								? FString::Printf(TEXT("%s needs %s as its target, but '%s' is %s."), *Call.Name, *TypeParam.Describe(Depth), *TargetPlace.Text, *TargetType.Describe())
								: FString::Printf(TEXT("%s needs %s, but this is %s."), *TargetWhat, *TypeParam.Describe(Depth), *TargetType.Describe()));
						}
						TypeArgs[Signature->TargetTypeVar] = FExprType::MakeKnown(Inner);
					}
					else
					{
						TypeArgs[Signature->TargetTypeVar] = FExprType::MakeUnknown(); // an undeclared global: when it runs
					}
				}
			}

			TArray<FBangoType> ParamTypes;
			TArray<bool> NilOk;
			for (const FBangoCommandParam& Param : Signature->Params)
			{
				ParamTypes.Add(Param.Type);
				NilOk.Add(Param.bMayBeNil);
			}
			if (!CompileArgs(Call, ParamTypes, FString::Printf(TEXT("'%s'"), *Call.Name), &NilOk, Signature, &TypeArgs))
			{
				return false;
			}
			for (int32 i = Signature->Params.Num(); i < Call.Args.Num(); ++i)
			{
				// An extra value may be a plain nil (WriteLog shows it as "nil"). The graph editor saves a placeholder
				// that was left without a value this way.
				const FBangoLiteralExpr* Literal = Call.Args[i]->As<FBangoLiteralExpr>();
				if (Literal && Literal->LiteralKind == EBangoLiteralKind::Nil)
				{
					Emit(EBangoOp::PushNil, AddType(FBangoType::String()));
					continue;
				}
				FExprType ArgType;
				if (!CompileExpr(*Call.Args[i], nullptr, ArgType) || !CheckUsableValue(ArgType, Call.Args[i]->Loc, TEXT("as an argument here")))
				{
					return false;
				}
			}

			CurrentLoc = Call.Loc;
			const int32 CallFlags = (Signature->bPure ? Bango::CallIsPure : 0) | (bValueNeeded ? Bango::CallResultUsed : 0);
			if (Signature->bMutableTarget)
			{
				Emit(EBangoOp::CallCommandInPlace, TargetPlace.PlaceIndex, AddName(Call.Name), Call.Args.Num() | (CallFlags << Bango::InPlaceFlagShift));
			}
			else
			{
				Emit(EBangoOp::CallCommand, AddName(Call.Name), Call.Args.Num(), (Call.Target ? Bango::CallHasTarget : 0) | CallFlags);
			}
			if (Signature->bReturnsValue && TypeArgs.IsValidIndex(Signature->ReturnTypeVar))
			{
				const FExprType& TypeArg = TypeArgs[Signature->ReturnTypeVar];
				return FinishCall(Call, true, TypeArg.IsKnown() ? FExprType::MakeKnown(FBangoCommandSignature::GetTypeOf(TypeArg.Type, Signature->ReturnTypeVarDepth)) : FExprType::MakeUnknown(),
					bValueNeeded, OutType);
			}
			return FinishCall(Call, Signature->bReturnsValue, Signature->ReturnType, bValueNeeded, OutType);
		}

		/**
		 * For a command whose text has {0}, {1}, ... placeholders (WriteLog, Format, FormatText): when the text is written out as a
		 * string, every placeholder it uses needs a value after it. (A text worked out while the script runs is
		 * only checked then.)
		 */
		bool CheckPlaceholders(const FBangoCallExpr& Call, const FBangoCommandSignature& Signature)
		{
			if (!Signature.bVariadic || !Call.Args.IsValidIndex(Signature.PlaceholderParam))
			{
				return true;
			}
			const FBangoLiteralExpr* Text = Call.Args[Signature.PlaceholderParam]->As<FBangoLiteralExpr>();
			if (!Text || (Text->LiteralKind != EBangoLiteralKind::String && Text->LiteralKind != EBangoLiteralKind::Text))
			{
				return true;
			}
			// Both use {0}, {1}, ... with backtick escapes; only a text's may not have named ones ({Name}), which FText::Format would fail on.
			const bool bText = Text->LiteralKind == EBangoLiteralKind::Text;
			FString Named;
			const int32 Needed = bText ? Bango::CountTextFormatValues(Text->StringValue, &Named) : Bango::CountFormatValues(Text->StringValue);
			const FBangoSourceLocation& SourceLoc = bText && Text->TextArgLocs.IsValidIndex(Text->GetTextSourceArgIndex()) ? Text->TextArgLocs[Text->GetTextSourceArgIndex()] : Text->Loc;
			if (Needed < 0)
			{
				return Fail(SourceLoc, FString::Printf(TEXT("This text uses {%s}. Text placeholders are numbered: {0}, {1}, ... (write `{ for a brace that isn't a placeholder)."), *Named));
			}
			const int32 Given = Call.Args.Num() - Signature.Params.Num();
			if (Needed <= Given)
			{
				return true;
			}
			const FString Values = Given == 0 ? FString(TEXT("no values come")) : Given == 1 ? FString(TEXT("only 1 value comes")) : FString::Printf(TEXT("only %d values come"), Given);
			const FString Placeholder = FString::Printf(TEXT("{%d}"), Needed - 1);
			return Fail(SourceLoc, FString::Printf(TEXT("This text uses %s, but %s after it. Add a value for every placeholder (nil shows as \"nil\")."), *Placeholder, *Values));
		}

		/** A call whose result must be used, on a line of its own. */
		bool FailUnusedResult(const FBangoCallExpr& Call)
		{
			return Fail(Call.NameLoc, FString::Printf(TEXT("'%s' gives a value that must be used, like '_result = %s(...);'. On a line of its own its value would be thrown away."), *Call.Name, *Call.Name));
		}

		bool FinishCall(const FBangoCallExpr& Call, bool bReturnsValue, const FBangoType& ReturnType, bool bValueNeeded, FExprType& OutType)
		{
			return FinishCall(Call, bReturnsValue, FExprType::MakeKnown(ReturnType), bValueNeeded, OutType);
		}

		bool FinishCall(const FBangoCallExpr& Call, bool bReturnsValue, const FExprType& ReturnType, bool bValueNeeded, FExprType& OutType)
		{
			if (bReturnsValue)
			{
				OutType = ReturnType;
				if (!bValueNeeded)
				{
					Emit(EBangoOp::Pop);
				}
				return true;
			}
			OutType = FExprType::MakeVoid();
			if (bValueNeeded && !bInferring)
			{
				return Fail(Call.NameLoc, FString::Printf(TEXT("'%s' doesn't return a value, so it can't be used here."), *Call.Name));
			}
			return true;
		}
	};
}

namespace Bango
{
	bool CompileScript(const FBangoScript& Script, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError,
		const FBangoInputsSignature* Signature)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Bango_Compile); // (the trace scopes are compiled out of Shipping builds)
		OutError = FBangoCompileError();
		OutError.FileName = Script.FileName;
		BangoCompilerPrivate::FCompiler Compiler(Script, Commands, OutProgram, OutError);
		Compiler.InputsSignature = Signature;
		return Compiler.Run();
	}

	bool CompileSource(const FString& Source, const FString& FileName, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError,
		const FBangoInputsSignature* Signature)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Bango_CompileSource); // parsing, then Bango_Compile
		FBangoScript Script;
		if (!ParseScript(Source, FileName, Script, OutError))
		{
			return false;
		}
		return CompileScript(Script, Commands, OutProgram, OutError, Signature);
	}

	bool ParseInputsSignature(const TArray<FBangoInputDecl>& Inputs, FBangoInputsSignature& OutSignature, FString& OutError, TArray<FString>& OutWarnings)
	{
		OutSignature.Params.Reset();
		OutSignature.Tooltips.Reset();
		for (const FBangoInputDecl& Input : Inputs)
		{
			// Read as an #inputs line would be, so the types and names follow the same rules.
			FBangoScript Script;
			FBangoCompileError ParseError;
			const FBangoParamsStmt* Params = nullptr;
			if (ParseScript(FString(TEXT("#inputs (")) + Input.Decl + TEXT(")"), TEXT("signature"), Script, ParseError))
			{
				Params = Script.GetParams();
			}
			if (!Params || Params->Params.Num() != 1)
			{
				OutError = FString::Printf(TEXT("Input '%s' should be a type and a name, like 'entity _who'%s"), *Input.Decl,
					Params ? TEXT(" (one input per entry).") : *(FString(TEXT(": ")) + ParseError.Message));
				return false;
			}
			const FBangoScriptParamDecl& Decl = Params->Params[0];
			if (Decl.Default)
			{
				OutError = FString::Printf(TEXT("Input '%s' can't have a default: its owner always passes its value."), *Input.Decl);
				return false;
			}
			if (OutSignature.Params.ContainsByPredicate([&Decl](const FBangoScriptParamInfo& Other) { return Other.Name.Equals(Decl.Name, ESearchCase::CaseSensitive); }))
			{
				OutError = FString::Printf(TEXT("Two inputs are named '%s'."), *Decl.Name);
				return false;
			}
			if (!Decl.Name.StartsWith(TEXT("_"), ESearchCase::CaseSensitive))
			{
				OutWarnings.Add(FString::Printf(TEXT("Predefined script inputs should begin with an underscore: '%s'. Please rename it to '_%s'."), *Decl.Name, *Decl.Name));
			}
			FBangoScriptParamInfo Info;
			Info.Name = Decl.Name;
			Info.Type = Decl.Type;
			OutSignature.Params.Add(MoveTemp(Info));
			OutSignature.Tooltips.Add(Input.Tooltip);
		}
		return true;
	}

	const TArray<FString>& GetValueTypeNames()
	{
		static const TArray<FString> Names = []()
		{
			const TCHAR* const Types[] = { TEXT("bool"), TEXT("int"), TEXT("float"), TEXT("string"), TEXT("entity"), TEXT("text"), TEXT("vector2"), TEXT("vector3") };
			TArray<FString> Result;
			for (const TCHAR* Type : Types)
			{
				Result.Add(Type);
			}
			for (const TCHAR* Type : Types)
			{
				Result.Add(FString(Type) + TEXT("[]"));
			}
			return Result;
		}();
		return Names;
	}

	bool ParseFixedValue(const FString& TypeName, const FString& Text, FBangoValue& OutValue, FString& OutError)
	{
		OutValue = FBangoValue();
		FBangoType Type;
		const FString TrimmedType = TypeName.TrimStartAndEnd();
		if (!FBangoType::FromString(TrimmedType, Type))
		{
			OutError = FString::Printf(TEXT("'%s' isn't a type a value can have (bool, int, float, string, entity, text, vector2, vector3, or an array of one, like int[])."), *TypeName);
			return false;
		}
		const FString Value = Text.TrimStartAndEnd();
		if (Value.IsEmpty())
		{
			OutValue = FBangoValue::MakeNil(Type);
			return true;
		}
		if (Value.Contains(TEXT("\n")) || Value.Contains(TEXT("\r")))
		{
			OutError = TEXT("A value is written on one line.");
			return false;
		}

		// Read and checked as a script variable's starting value is: `scriptVar <type> _value = <text>;`.
		static const TCHAR* const VarName = TEXT("_value");
		FBangoScript Script;
		FBangoCompileError Error;
		const FString Source = FString::Printf(TEXT("scriptVar %s %s = %s;"), *TrimmedType, VarName, *Value);
		const FBangoVarDeclStmt* Decl = nullptr;
		if (ParseScript(Source, TEXT("value"), Script, Error) && Script.TopLevel.Num() == 1 && Script.Functions.Num() == 0)
		{
			Decl = Script.TopLevel[0]->As<FBangoVarDeclStmt>();
		}
		if (!Decl || !Decl->Init.IsValid() || Decl->Init->Kind == EBangoExprKind::Empty)
		{
			OutError = Error.Message.IsEmpty() ? FString(TEXT("This doesn't read as one value.")) : FString::Printf(TEXT("This doesn't read as a value: %s"), *Error.Message);
			return false;
		}
		if (!BangoCompilerPrivate::FCompiler::IsFixedValueExpr(*Decl->Init))
		{
			OutError = TEXT("This must be a fixed value: a number, a string, true, false, nil, a Bango ID like @john, a text, or an array of those. A name, a call or a calculation isn't one.");
			return false;
		}
		// `self` is whoever a script belongs to: a value on its own has no script, so it would never be anyone.
		TFunction<bool(const FBangoExpr&)> UsesSelf = [&UsesSelf](const FBangoExpr& Expr) -> bool
		{
			if (const FBangoLiteralExpr* Literal = Expr.As<FBangoLiteralExpr>())
			{
				return Literal->LiteralKind == EBangoLiteralKind::Entity && Literal->StringValue.Equals(Bango::GetSelfEntityName(), ESearchCase::CaseSensitive);
			}
			if (const FBangoArrayLiteralExpr* ArrayExpr = Expr.As<FBangoArrayLiteralExpr>())
			{
				for (const FBangoExprPtr& Element : ArrayExpr->Elements)
				{
					if (Element.IsValid() && UsesSelf(*Element))
					{
						return true;
					}
				}
			}
			return false;
		};
		if (UsesSelf(*Decl->Init))
		{
			OutError = TEXT("'self' is the actor a script belongs to, so it can't be written in a value outside one. Name the actor with its Bango ID, like @john.");
			return false;
		}
		const FBangoCommandTable NoCommands;
		FBangoProgram Program;
		if (!CompileScript(Script, NoCommands, Program, Error))
		{
			// Said about the value, not the variable it was read as.
			OutError = Error.Message.Replace(*FString::Printf(TEXT("'%s'"), VarName), TEXT("This value"), ESearchCase::CaseSensitive);
			if (OutError.Contains(TEXT("LOCTEXT_NAMESPACE"), ESearchCase::CaseSensitive))
			{
				OutError = TEXT("A LOCTEXT takes its namespace from a script, and there's none here. Write the text as INVTEXT(\"...\") (never translated) or NSLOCTEXT(\"Namespace\", \"Key\", \"...\").");
			}
			return false;
		}
		const FBangoFunctionCode* Main = Program.Functions.Num() > 0 ? &Program.Functions[0] : nullptr; // the main graph comes first
		const int32 Slot = Main ? Main->SlotNames.IndexOfByPredicate([](const FString& Name) { return Name.Equals(VarName, ESearchCase::CaseSensitive); }) : INDEX_NONE;
		if (!Main || !Main->SlotStartValues.IsValidIndex(Slot))
		{
			OutError = TEXT("Internal error: the value wasn't found after it was read.");
			return false;
		}
		OutValue = Main->SlotStartValues[Slot];
		if (OutValue.IsNil())
		{
			OutValue = FBangoValue::MakeNil(Type); // written as nil
		}
		return true;
	}

	bool AddInstanceInputs(const FBangoInputsSignature* Signature, const TArray<FBangoInputDecl>& InstanceInputs, FBangoInputsSignature& OutInputs, FString& OutError,
		TArray<FString>& OutWarnings)
	{
		FBangoInputsSignature Own;
		FString ParseError;
		if (!ParseInputsSignature(InstanceInputs, Own, ParseError, OutWarnings))
		{
			OutError = FString::Printf(TEXT("This object's own inputs (SetInstanceInputs): %s"), *ParseError);
			return false;
		}
		OutInputs = Signature ? *Signature : FBangoInputsSignature();
		OutInputs.SignatureLabel = Signature ? Signature->Label : FString();
		OutInputs.NumInstanceInputs = Own.Params.Num();
		OutInputs.Label = Signature ? Signature->Label + TEXT(" and this object") : FString(TEXT("this object"));
		OutInputs.Tooltips.SetNum(OutInputs.Params.Num()); // one per input, whatever the signature had
		for (int32 i = 0; i < Own.Params.Num(); ++i)
		{
			const FString& Name = Own.Params[i].Name;
			if (OutInputs.Params.ContainsByPredicate([&Name](const FBangoScriptParamInfo& Other) { return Other.Name.Equals(Name, ESearchCase::CaseSensitive); }))
			{
				OutError = FString::Printf(TEXT("This object's own input '%s' (SetInstanceInputs) has the name of an input of %s. Rename it."), *Name, Signature ? *Signature->Label : TEXT("its signature"));
				return false;
			}
			OutInputs.Params.Add(Own.Params[i]);
			OutInputs.Tooltips.Add(Own.Tooltips.IsValidIndex(i) ? Own.Tooltips[i] : FString());
		}
		return true;
	}

	namespace
	{
		bool IsSpaceChar(TCHAR Char)
		{
			return Char == TEXT(' ') || Char == TEXT('\t') || Char == TEXT('\r');
		}

		/**
		 * For CompileCondition: if Source's code is a single expression on one line (other lines being blank, comments or
		 * # lines, and no ; { or } outside strings and comments), where it is: the line (from 1), the start of the
		 * expression and the end of it (before any comment after it), as indices into Source.
		 */
		bool FindConditionExpression(const FString& Source, int32& OutLine, int32& OutBegin, int32& OutEnd)
		{
			bool bFound = false;
			int32 Line = 1;
			int32 LineStart = 0;
			const int32 Length = Source.Len();
			while (LineStart <= Length)
			{
				int32 LineEnd = LineStart;
				while (LineEnd < Length && Source[LineEnd] != TEXT('\n'))
				{
					++LineEnd;
				}
				int32 First = LineStart;
				while (First < LineEnd && IsSpaceChar(Source[First]))
				{
					++First;
				}
				const bool bComment = First + 1 < LineEnd && Source[First] == TEXT('/') && Source[First + 1] == TEXT('/');
				if (First < LineEnd && !bComment && Source[First] != TEXT('#'))
				{
					if (bFound)
					{
						return false; // more than one line of code
					}
					// Where the code ends (a // outside a string starts a comment), and whether it's a statement.
					int32 CodeEnd = LineEnd;
					bool bInString = false;
					for (int32 i = First; i < LineEnd; ++i)
					{
						const TCHAR Char = Source[i];
						if (bInString)
						{
							if (Char == TEXT('\\'))
							{
								++i;
							}
							else if (Char == TEXT('"'))
							{
								bInString = false;
							}
						}
						else if (Char == TEXT('"'))
						{
							bInString = true;
						}
						else if (Char == TEXT('/') && i + 1 < LineEnd && Source[i + 1] == TEXT('/'))
						{
							CodeEnd = i;
							break;
						}
						else if (Char == TEXT(';') || Char == TEXT('{') || Char == TEXT('}'))
						{
							return false;
						}
					}
					while (CodeEnd > First && IsSpaceChar(Source[CodeEnd - 1]))
					{
						--CodeEnd;
					}
					bFound = true;
					OutLine = Line;
					OutBegin = First;
					OutEnd = CodeEnd;
				}
				LineStart = LineEnd + 1;
				++Line;
			}
			return bFound;
		}
	}

	bool ConditionExpressionToScript(const FString& Source, FString& OutScript)
	{
		int32 ExprLine = 0;
		int32 ExprBegin = 0;
		int32 ExprEnd = 0;
		if (!FindConditionExpression(Source, ExprLine, ExprBegin, ExprEnd))
		{
			return false;
		}
		OutScript = Source.Left(ExprBegin) + TEXT("return ") + Source.Mid(ExprBegin, ExprEnd - ExprBegin) + TEXT(";") + Source.Mid(ExprEnd, Source.Len() - ExprEnd);
		return true;
	}

	bool CompileCondition(const FString& Source, const FString& FileName, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError,
		const FBangoInputsSignature* Signature)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Bango_CompileCondition);
		// A single expression is compiled as `return <expression>;`.
		static const FString ReturnPrefix = TEXT("return ");
		int32 ExprLine = 0;
		int32 ExprBegin = 0;
		int32 ExprEnd = 0;
		const bool bExpression = FindConditionExpression(Source, ExprLine, ExprBegin, ExprEnd);
		const FString Text = bExpression
			? Source.Left(ExprBegin) + ReturnPrefix + Source.Mid(ExprBegin, ExprEnd - ExprBegin) + TEXT(";") + Source.Mid(ExprEnd, Source.Len() - ExprEnd)
			: Source;

		FBangoScript Script;
		bool bOk = ParseScript(Text, FileName, Script, OutError);
		if (bOk)
		{
			OutError = FBangoCompileError();
			OutError.FileName = Script.FileName;
			BangoCompilerPrivate::FCompiler Compiler(Script, Commands, OutProgram, OutError);
			Compiler.bCondition = true;
			Compiler.InputsSignature = Signature;
			bOk = Compiler.Run();
		}
		if (!bExpression)
		{
			return bOk;
		}
		// Errors (now, and while it runs) point into the expression as it was written, without the added `return `.
		int32 ExprColumn = 1;
		for (int32 i = ExprBegin - 1; i >= 0 && Source[i] != TEXT('\n'); --i)
		{
			++ExprColumn;
		}
		auto FixColumn = [ExprLine, ExprColumn](int32 Line, int32& Column)
		{
			if (Line == ExprLine && Column > ExprColumn)
			{
				Column = FMath::Max(ExprColumn, Column - ReturnPrefix.Len());
			}
		};
		if (!bOk)
		{
			FixColumn(OutError.Line, OutError.Column);
			return false;
		}
		for (FBangoFunctionCode& Function : OutProgram.Functions)
		{
			for (FBangoDebugLoc& Loc : Function.Debug)
			{
				FixColumn(Loc.Line, Loc.Column);
			}
		}
		return true;
	}
}
