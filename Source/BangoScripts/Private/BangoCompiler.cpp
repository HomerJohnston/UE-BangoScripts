#include "BangoCompiler.h"
#include "BangoText.h"

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
			case ETypeKind::Unknown:    return TEXT("an undeclared global");
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
	};

	struct FGotoFixup
	{
		int32 Instruction = 0;
		FString Name;
		int32 BlockId = 0;
		FBangoSourceLocation Loc;
		const FBangoStmt* Stmt = nullptr;
	};

	struct FPlaceInfo
	{
		int32 PlaceIndex = 0;
		int32 NumIndexes = 0;
		FExprType Type;
		FString Text;  // "_x", "_grid[...]", "_pos.x"
	};

	static const TCHAR* PartName(int32 Part)
	{
		return Part == 0 ? TEXT("x") : Part == 1 ? TEXT("y") : TEXT("z");
	}

	enum class EPlaceUse : uint8
	{
		Read,
		Write,
		Check,  // IsNil
	};

	class FCompiler
	{
	public:
		FCompiler(const FBangoScript& InScript, const IBangoCommandLookup& InCommands, FBangoProgram& InProgram, FBangoCompileError& InError)
			: Script(InScript), Commands(InCommands), Program(InProgram), Error(InError)
		{
		}

		bool Run()
		{
			Program = FBangoProgram();
			Program.FileName = Script.FileName;

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
			return !bFailed;
		}

	private:
		const FBangoScript& Script;

		/** Every text literal's "namespace\nkey", and the first literal that used it (spec section 3, keys must be unique). */
		TMap<FString, const FBangoLiteralExpr*> TextKeys;

		/** The value of a text literal: localized text with its namespace (the script's LOCTEXT_NAMESPACE for LOCTEXT) and key. */
		bool MakeTextValue(const FBangoLiteralExpr& Lit, FBangoValue& OutValue)
		{
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
				return Fail(Lit.Loc, TEXT("This text needs the script's namespace: add the line #define LOCTEXT_NAMESPACE \"Bango.Intro\" at the top of the script (after #define params, if there is one) and #undef LOCTEXT_NAMESPACE as its last line. (NSLOCTEXT and NSNOTELOCTEXT give their own namespace instead.)"));
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

		TBangoNameMap<FBangoType> DeclaredGlobals;
		TBangoNameMap<int32> FunctionIndexes;

		// Per function
		FBangoFunctionCode* Fn = nullptr;
		const FBangoFunction* FnAst = nullptr;
		TArray<FScope> Scopes;
		TArray<FLoop> Loops;
		TArray<int32> BlockParents;
		TBangoNameMap<FLabel> Labels;
		TArray<FGotoFixup> Gotos;
		const FBangoStmt* CurrentStmt = nullptr;
		FBangoSourceLocation CurrentLoc;

		/** True while compiling array literal elements only to learn their types. */
		bool bInferring = false;

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
			return Fn->SlotTypes.Add(Type);
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
				return Fail(Loc, FString::Printf(TEXT("nil can't be used %s. To check whether something is nil, use IsNil(...)."), Where));
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
					return Fail(Loc, TEXT("Comparing with nil isn't allowed, because comparing reads the value. Use IsNil(...) instead."));
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
			if (Decl.bGlobal)
			{
				return Fail(Decl.Loc, bInFunction
					? FString::Printf(TEXT("Globals are declared at the top of the file, not in a function: move 'global %s %s;' up there, after the #define lines."), *Decl.Type.ToString(), *Decl.Name)
					: FString::Printf(TEXT("Globals are declared at the top of the file, after the #define lines and before everything else: move 'global %s %s;' up there."), *Decl.Type.ToString(), *Decl.Name));
			}
			return Fail(Decl.Loc, FString::Printf(TEXT("Locals are declared at the top of their %s, before its first statement: move '%s %s;' up there, and set it here with '%s = ...;' if it needs a value at this point."),
				bInFunction ? TEXT("function") : TEXT("script"), *Decl.Type.ToString(), *Decl.Name, *Decl.Name));
		}

		/** A declaration nested in a block (not a for header, which isn't in a block). */
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
				return FailMisplacedDecl(*Stmt.As<FBangoVarDeclStmt>(), bInFunction);
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
			default:
				return true;
			}
		}

		/** Declarations aren't graph nodes (they're listed in the editor's side panel), so they have no metadata. */
		bool CheckDeclHasNoMetadata(const FBangoVarDeclStmt& Decl)
		{
			if (Decl.Node.bHasMetadata)
			{
				CurrentStmt = nullptr; // the error isn't on a node
				return Fail(Decl.Loc, TEXT("A declaration isn't a graph node, so it can't have node metadata: remove the //@ comment."));
			}
			return true;
		}

		/** A local declared at the top of a graph: its starting value must be a fixed value. */
		bool CheckLocalDecl(const FBangoVarDeclStmt& Decl)
		{
			if (!CheckDeclHasNoMetadata(Decl))
			{
				return false;
			}
			if (Decl.Init.IsValid() && !IsFixedValue(*Decl.Init))
			{
				CurrentStmt = &Decl;
				return Fail(Decl.Init->Loc, FString::Printf(TEXT("A local's starting value must be a fixed value: a number, a string, true, false, nil, a Bango ID like @john, a text, or an array of those. Declare it as '%s %s;' and set it in the code: '%s = ...;'."),
					*Decl.Type.ToString(), *Decl.Name, *Decl.Name));
			}
			return true;
		}

		/**
		 * Globals are declared only at the top of the file (after the #define lines, before the main graph's locals and
		 * anything else, functions included), once each and without a value. Locals are declared only at the top of
		 * their graph, with fixed starting values. Loop variables (for and forEach headers) aren't in a block, so they
		 * aren't checked here.
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
			bool bSeenLocal = false;
			for (const FBangoStmtPtr& Stmt : Script.TopLevel)
			{
				if (Stmt->Kind == EBangoStmtKind::Params)
				{
					continue;
				}
				const FBangoVarDeclStmt* Decl = Stmt->As<FBangoVarDeclStmt>();
				if (!Decl)
				{
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
					if (bSeenLocal)
					{
						return Fail(Decl->Loc, FString::Printf(TEXT("Global declarations come before the script's locals: move 'global %s %s;' above them."), *Decl->Type.ToString(), *Decl->Name));
					}
					if (Decl->Init.IsValid())
					{
						return Fail(Decl->Init->Loc, FString::Printf(TEXT("A global declaration can't have a value, since it would reset the global every time the script starts. Declare it here, and set it in the code (%s = ...;)."), *Decl->Name));
					}
					if (const int32* Line = GlobalLines.Find(Decl->Name))
					{
						return Fail(Decl->Loc, FString::Printf(TEXT("Global '%s' is already declared on line %d."), *Decl->Name, *Line));
					}
					GlobalLines.Add(Decl->Name, Decl->Loc.Line);
				}
				else
				{
					bSeenLocal = true;
					if (!CheckLocalDecl(*Decl))
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
					if (!Decl)
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
					if (!CheckLocalDecl(*Decl))
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
			for (const TCHAR* Builtin : { TEXT("Count"), TEXT("Push"), TEXT("InsertAt"), TEXT("Pop"), TEXT("RemoveAt") })
			{
				if (Name.Equals(Builtin, ESearchCase::IgnoreCase))
				{
					return Builtin;
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
				if (Bango::IsBuiltinCommandName(Function.Name))
				{
					return Fail(Function.Loc, FString::Printf(TEXT("'%s' is a built-in array command, so it can't be used as a function name."), *Function.Name));
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
			FnAst = Ast;
			Scopes.Reset();
			Loops.Reset();
			BlockParents.Reset();
			Labels = TBangoNameMap<FLabel>();
			Gotos.Reset();
			CurrentStmt = nullptr;
			CurrentLoc = Ast ? Ast->Loc : FBangoSourceLocation();

			PushScope();
			if (!Ast && !DeclareScriptParams(Body))
			{
				return false;
			}
			if (Ast)
			{
				for (const FBangoParam& Param : Ast->Params)
				{
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
				Emit(EBangoOp::Halt);
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

		/** Evaluates a params default (a literal, negated number or array of those) as a value of Type. */
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

		/** Declares the params line's parameters as the first slots of the top level. */
		bool DeclareScriptParams(const FBangoBlock& Body)
		{
			const FBangoParamsStmt* ParamsStmt = Body.Num() > 0 ? Body[0]->As<FBangoParamsStmt>() : nullptr;
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
					return Fail(Goto.Loc, FString::Printf(TEXT("There is no label '%s' in this %s."), *Goto.Name, FnAst ? TEXT("function") : TEXT("script's top level")));
				}
				// The label's block must contain the goto: jumping out of blocks is fine, jumping into one isn't.
				bool bReachable = false;
				for (int32 Block = Goto.BlockId; Block >= 0; Block = BlockParents[Block])
				{
					if (Block == Label->BlockId)
					{
						bReachable = true;
						break;
					}
				}
				if (!bReachable)
				{
					return Fail(Goto.Loc, FString::Printf(TEXT("'goto %s' jumps into a block (label '%s' is on line %d, inside a block this goto is not in). goto can jump out of blocks, but not into them."),
						*Goto.Name, *Goto.Name, Label->Line));
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

		bool DeclareLocal(const FString& Name, const FBangoType& Type, const FBangoSourceLocation& Loc, int32& OutSlot)
		{
			if (FindLocal(Name))
			{
				return Fail(Loc, FString::Printf(TEXT("'%s' is already declared (as a local, a parameter or a loop's variable). Use a different name."), *Name));
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

		bool CompileStmt(const FBangoStmt& Stmt)
		{
			CurrentStmt = &Stmt;
			CurrentLoc = Stmt.Loc;
			return CompileStmtBody(Stmt);
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
			case EBangoStmtKind::Break:
			case EBangoStmtKind::Continue: return CompileBreakContinue(Stmt);
			case EBangoStmtKind::Return:   return CompileReturn(*Stmt.As<FBangoReturnStmt>());
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
			if (Decl.bGlobal)
			{
				// Globals are declared when the instance starts, and never have a value (CheckDeclarations).
				if (!Decl.Init)
				{
					Emit(EBangoOp::Nop);
					return true;
				}
				FExprType ValueType;
				if (!CompileExpr(*Decl.Init, &Decl.Type, ValueType)
					|| !CanAssign(Decl.Type, ValueType, Decl.Init->Loc, FString::Printf(TEXT("Global '%s'"), *Decl.Name)))
				{
					return false;
				}
				Emit(EBangoOp::StorePlace, AddPlace(EBangoPlaceKind::Global, AddName(Decl.Name), 0));
				return true;
			}

			// The new local isn't visible in its own initializer.
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

			int32 Slot = 0;
			if (!DeclareLocal(Decl.Name, Decl.Type, Decl.Loc, Slot))
			{
				return false;
			}
			Emit(EBangoOp::StorePlace, AddPlace(EBangoPlaceKind::Local, Slot, 0));
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

		bool CompileFor(const FBangoForStmt& For)
		{
			PushScope();  // holds a variable declared in the header

			if (For.Init && !CompileStmtBody(*For.Init))
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
				return Fail(ForEach.Array->Loc, TEXT("forEach needs to know the array's type. This global isn't declared in this script, so declare it first, like 'global string[] questLog;'."));
			}
			if (!ArrayType.IsKnownArray())
			{
				return Fail(ForEach.Array->Loc, FString::Printf(TEXT("forEach loops over an array, but this is %s."), *ArrayType.Describe()));
			}

			PushScope();
			const int32 ArraySlot = AllocSlot(ArrayType.Type, TEXT("(forEach array)"));
			const int32 PositionSlot = AllocSlot(FBangoType::Int(), TEXT("(forEach position)"));
			check(PositionSlot == ArraySlot + 1);

			Emit(EBangoOp::StorePlace, AddPlace(EBangoPlaceKind::Local, ArraySlot, 0));
			Emit(EBangoOp::PushConst, AddConstant(FBangoValue::MakeInt(0)));
			Emit(EBangoOp::StorePlace, AddPlace(EBangoPlaceKind::Local, PositionSlot, 0));

			int32 VarSlot = 0;
			if (!DeclareLocal(ForEach.VarName, ArrayType.Type.GetElementType(), ForEach.Loc, VarSlot))
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
			if (Loops.Num() == 0)
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

			if (RootType.IsKnown() && IndexExprs.Num() > RootType.Type.ArrayDepth)
			{
				if (!RootType.Type.IsArray())
				{
					return Fail(Root->Loc, FString::Printf(TEXT("'%s' is %s, not an array, so it can't be indexed."), *Root->Name, *RootType.Describe()));
				}
				return Fail(Root->Loc, FString::Printf(TEXT("'%s' is %s, so it can be indexed at most %d time(s)."),
					*Root->Name, *RootType.Describe(), (int32)RootType.Type.ArrayDepth));
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
			Out.Text = Root->Name;
			for (int32 i = 0; i < IndexExprs.Num(); ++i)
			{
				Out.Text += TEXT("[...]");
			}
			if (RootType.IsKnown())
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
			Out.PlaceIndex = AddPlace(Kind, Index, IndexExprs.Num(), Part);
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
			if (ArrayType.IsKnown() && !ArrayType.Type.IsArray())
			{
				return Fail(IndexExpr.Loc, FString::Printf(TEXT("Only arrays can be indexed, but this is %s."), *ArrayType.Describe()));
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
			OutType = ArrayType.IsKnown() ? FExprType::MakeKnown(ArrayType.Type.GetElementType()) : FExprType::MakeUnknown();
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
				const bool bAnd = Binary.Op == EBangoBinaryOp::And;
				FExprType LeftType;
				if (!CompileExpr(*Binary.Left, nullptr, LeftType))
				{
					return false;
				}
				if (!LeftType.Is(FBangoType::Bool()) && !LeftType.IsUnknown())
				{
					return Fail(Binary.Left->Loc, FString::Printf(TEXT("'%s' needs bools, but this is %s."), bAnd ? TEXT("&&") : TEXT("||"), *LeftType.Describe()));
				}
				const int32 Jump = Emit(bAnd ? EBangoOp::JumpIfFalseOrPop : EBangoOp::JumpIfTrueOrPop);
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

		/** NilOk (optional): the arguments a command takes even if nil (MayBeNil), so a nil variable there is passed on as nil. */
		bool CompileArgs(const FBangoCallExpr& Call, const TArray<FBangoType>& ParamTypes, const FString& What, const TArray<bool>* NilOk = nullptr)
		{
			for (int32 i = 0; i < Call.Args.Num() && i < ParamTypes.Num(); ++i)
			{
				FExprType ArgType;
				bool bArgOk = false;
				if (NilOk && NilOk->IsValidIndex(i) && (*NilOk)[i] && IsPlaceExpr(*Call.Args[i]))
				{
					// Like IsNil: a nil variable, a global nobody made or an element past the end is passed on as nil.
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
			return true;
		}

		bool CompileCall(const FBangoCallExpr& Call, bool bValueNeeded, FExprType& OutType)
		{
			if (Bango::IsBuiltinCommandName(Call.Name))
			{
				return CompileBuiltin(Call, bValueNeeded, OutType);
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

			if (Call.Target)
			{
				FExprType TargetType;
				if (!CompileExpr(*Call.Target, &Signature->TargetType, TargetType)
					|| !CanAssign(Signature->TargetType, TargetType, Call.Target->Loc, FString::Printf(TEXT("The target of '%s'"), *Call.Name)))
				{
					return false;
				}
				EmitConvertIfNeeded(Signature->TargetType, TargetType);
			}

			TArray<FBangoType> ParamTypes;
			TArray<bool> NilOk;
			for (const FBangoCommandParam& Param : Signature->Params)
			{
				ParamTypes.Add(Param.Type);
				NilOk.Add(Param.bMayBeNil);
			}
			if (!CompileArgs(Call, ParamTypes, FString::Printf(TEXT("'%s'"), *Call.Name), &NilOk))
			{
				return false;
			}
			for (int32 i = Signature->Params.Num(); i < Call.Args.Num(); ++i)
			{
				// An extra value may be a plain nil (Hint shows it as "nil"). The graph editor saves a placeholder
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
			Emit(EBangoOp::CallCommand, AddName(Call.Name), Call.Args.Num(), Call.Target ? 1 : 0);
			return FinishCall(Call, Signature->bReturnsValue, Signature->ReturnType, bValueNeeded, OutType);
		}

		/**
		 * For a command whose text has %0, %1, ... placeholders (Hint, Log, Format): when the text is written out as a
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
			// A text's placeholders are {0}, {1}, ... (FText::Format); a string's are %0, %1, ...
			const bool bText = Text->LiteralKind == EBangoLiteralKind::Text;
			FString Named;
			const int32 Needed = bText ? Bango::CountTextFormatValues(Text->StringValue, &Named) : Bango::CountFormatValues(Text->StringValue);
			const FBangoSourceLocation& SourceLoc = bText && Text->TextArgLocs.Num() > 0 ? Text->TextArgLocs[Text->bTextHasNamespace ? 2 : 1] : Text->Loc;
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
			const FString Placeholder = bText ? FString::Printf(TEXT("{%d}"), Needed - 1) : FString::Printf(TEXT("%%%d"), Needed - 1);
			return Fail(SourceLoc, FString::Printf(TEXT("This text uses %s, but %s after it. Add a value for every placeholder (nil shows as \"nil\")."), *Placeholder, *Values));
		}

		/** A call whose result must be used, on a line of its own. */
		bool FailUnusedResult(const FBangoCallExpr& Call)
		{
			return Fail(Call.NameLoc, FString::Printf(TEXT("'%s' gives a value that must be used, like '_result = %s(...);'. On a line of its own its value would be thrown away."), *Call.Name, *Call.Name));
		}

		bool FinishCall(const FBangoCallExpr& Call, bool bReturnsValue, const FBangoType& ReturnType, bool bValueNeeded, FExprType& OutType)
		{
			if (bReturnsValue)
			{
				OutType = FExprType::MakeKnown(ReturnType);
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

		bool CompileBuiltin(const FBangoCallExpr& Call, bool bValueNeeded, FExprType& OutType)
		{
			const FString& Name = Call.Name;

			if (Name.Equals(TEXT("Count"), ESearchCase::CaseSensitive))
			{
				if (Call.Target || Call.Args.Num() != 1)
				{
					return Fail(Call.NameLoc, TEXT("Count takes one array and no target: 'Count(_myArray)'."));
				}
				if (!bValueNeeded && !bInferring)
				{
					return FailUnusedResult(Call);
				}
				FExprType ArrayType;
				if (!CompileExpr(*Call.Args[0], nullptr, ArrayType))
				{
					return false;
				}
				if (!ArrayType.IsKnownArray() && !ArrayType.IsUnknown())
				{
					return Fail(Call.Args[0]->Loc, FString::Printf(TEXT("Count needs an array, but this is %s."), *ArrayType.Describe()));
				}
				CurrentLoc = Call.Loc;
				Emit(EBangoOp::ArrayCount);
				return FinishCall(Call, true, FBangoType::Int(), bValueNeeded, OutType);
			}

			EBangoArrayModify Kind = EBangoArrayModify::Push;
			int32 ExpectedArgs = 1;
			const TCHAR* Usage = TEXT("");
			if (Name.Equals(TEXT("Push"), ESearchCase::CaseSensitive))
			{
				Kind = EBangoArrayModify::Push; ExpectedArgs = 1; Usage = TEXT("'_myArray Push(value);'");
			}
			else if (Name.Equals(TEXT("InsertAt"), ESearchCase::CaseSensitive))
			{
				Kind = EBangoArrayModify::InsertAt; ExpectedArgs = 2; Usage = TEXT("'_myArray InsertAt(value, index);'");
			}
			else if (Name.Equals(TEXT("Pop"), ESearchCase::CaseSensitive))
			{
				Kind = EBangoArrayModify::Pop; ExpectedArgs = 0; Usage = TEXT("'_myArray Pop();'");
			}
			else
			{
				Kind = EBangoArrayModify::RemoveAt; ExpectedArgs = 1; Usage = TEXT("'_myArray RemoveAt(index);'");
			}

			if (!Call.Target || Call.Args.Num() != ExpectedArgs)
			{
				return Fail(Call.NameLoc, FString::Printf(TEXT("%s takes an array as its target and %d argument(s): %s"), *Name, ExpectedArgs, Usage));
			}
			if (!IsPlaceExpr(*Call.Target))
			{
				return Fail(Call.Target->Loc, FString::Printf(TEXT("%s changes its target, so the target must be a variable, a global or an array element."), *Name));
			}

			FPlaceInfo Place;
			if (!ResolvePlace(*Call.Target, EPlaceUse::Write, Place))
			{
				return false;
			}
			if (!Place.Type.IsKnownArray())
			{
				return Fail(Call.Target->Loc, FString::Printf(TEXT("%s needs an array as its target, but '%s' is %s."), *Name, *Place.Text, *Place.Type.Describe()));
			}
			const FBangoType ElementType = Place.Type.Type.GetElementType();

			if (Kind == EBangoArrayModify::Push || Kind == EBangoArrayModify::InsertAt)
			{
				FExprType ValueType;
				if (!CompileExpr(*Call.Args[0], &ElementType, ValueType)
					|| !CanAssign(ElementType, ValueType, Call.Args[0]->Loc, FString::Printf(TEXT("%s on '%s'"), *Name, *Place.Text)))
				{
					return false;
				}
			}
			if (Kind == EBangoArrayModify::InsertAt || Kind == EBangoArrayModify::RemoveAt)
			{
				const FBangoExpr& IndexArg = *Call.Args[Kind == EBangoArrayModify::InsertAt ? 1 : 0];
				FExprType IndexType;
				if (!CompileExpr(IndexArg, nullptr, IndexType))
				{
					return false;
				}
				if (!IndexType.Is(FBangoType::Int()) && !IndexType.IsUnknown())
				{
					return Fail(IndexArg.Loc, FString::Printf(TEXT("The index for %s must be an int, but this is %s."), *Name, *IndexType.Describe()));
				}
			}

			const bool bReturns = Kind == EBangoArrayModify::Pop || Kind == EBangoArrayModify::RemoveAt;
			CurrentLoc = Call.Loc;
			Emit(EBangoOp::ArrayModify, Place.PlaceIndex, (int32)Kind, (bReturns && bValueNeeded) ? 1 : 0);

			if (bReturns)
			{
				// The result is only pushed when it's used, so there's nothing to pop.
				OutType = FExprType::MakeKnown(ElementType);
				return true;
			}
			return FinishCall(Call, false, FBangoType(), bValueNeeded, OutType);
		}
	};
}

namespace Bango
{
	bool CompileScript(const FBangoScript& Script, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError)
	{
		OutError = FBangoCompileError();
		OutError.FileName = Script.FileName;
		BangoCompilerPrivate::FCompiler Compiler(Script, Commands, OutProgram, OutError);
		return Compiler.Run();
	}

	bool CompileSource(const FString& Source, const FString& FileName, const IBangoCommandLookup& Commands, FBangoProgram& OutProgram, FBangoCompileError& OutError)
	{
		FBangoScript Script;
		if (!ParseScript(Source, FileName, Script, OutError))
		{
			return false;
		}
		return CompileScript(Script, Commands, OutProgram, OutError);
	}
}
