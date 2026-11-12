#include "BangoAST.h"

namespace BangoDumpPrivate
{
	static FString QuoteString(const FString& Value)
	{
		FString Result = TEXT("\"");
		for (int32 i = 0; i < Value.Len(); ++i)
		{
			const TCHAR C = Value[i];
			if (C == TEXT('"')) Result += TEXT("\\\"");
			else if (C == TEXT('\\')) Result += TEXT("\\\\");
			else if (C == TEXT('\n')) Result += TEXT("\\n");
			else if (C == TEXT('\t')) Result += TEXT("\\t");
			else Result.AppendChar(C);
		}
		Result += TEXT("\"");
		return Result;
	}

	static FString FormatDouble(double Value)
	{
		// The shortest text that reads back as the same value.
		FString Text;
		for (int32 Precision = 1; Precision <= 17; ++Precision)
		{
			switch (Precision)
			{
			case 1:  Text = FString::Printf(TEXT("%.1g"), Value); break;
			case 2:  Text = FString::Printf(TEXT("%.2g"), Value); break;
			case 3:  Text = FString::Printf(TEXT("%.3g"), Value); break;
			case 4:  Text = FString::Printf(TEXT("%.4g"), Value); break;
			case 5:  Text = FString::Printf(TEXT("%.5g"), Value); break;
			case 6:  Text = FString::Printf(TEXT("%.6g"), Value); break;
			case 7:  Text = FString::Printf(TEXT("%.7g"), Value); break;
			case 8:  Text = FString::Printf(TEXT("%.8g"), Value); break;
			case 9:  Text = FString::Printf(TEXT("%.9g"), Value); break;
			case 10: Text = FString::Printf(TEXT("%.10g"), Value); break;
			case 11: Text = FString::Printf(TEXT("%.11g"), Value); break;
			case 12: Text = FString::Printf(TEXT("%.12g"), Value); break;
			case 13: Text = FString::Printf(TEXT("%.13g"), Value); break;
			case 14: Text = FString::Printf(TEXT("%.14g"), Value); break;
			case 15: Text = FString::Printf(TEXT("%.15g"), Value); break;
			case 16: Text = FString::Printf(TEXT("%.16g"), Value); break;
			default: Text = FString::Printf(TEXT("%.17g"), Value); break;
			}
			// %g switches to an exponent when the precision is too small (1000 at %.1g is "1e+03"),
			// so for ordinary magnitudes keep going until the plain form appears.
			const double Magnitude = Value < 0.0 ? -Value : Value;
			const bool bWantPlain = Magnitude >= 1e-4 && Magnitude < 1e16;
			bool bHasExponent = false;
			for (int32 i = 0; i < Text.Len(); ++i)
			{
				if (Text[i] == TEXT('e'))
				{
					bHasExponent = true;
				}
			}
			if (FCString::Atod(*Text) == Value && !(bWantPlain && bHasExponent))
			{
				break;
			}
		}
		bool bLooksLikeInt = true;
		for (int32 i = 0; i < Text.Len(); ++i)
		{
			const TCHAR C = Text[i];
			if (C == TEXT('.') || C == TEXT('e') || C == TEXT('n') || C == TEXT('i'))
			{
				bLooksLikeInt = false;
			}
		}
		if (bLooksLikeInt)
		{
			Text += TEXT(".0");
		}
		return Text;
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

	static const TCHAR* AssignOpText(EBangoAssignOp Op)
	{
		switch (Op)
		{
		case EBangoAssignOp::Assign:   return TEXT("=");
		case EBangoAssignOp::Add:      return TEXT("+=");
		case EBangoAssignOp::Subtract: return TEXT("-=");
		case EBangoAssignOp::Multiply: return TEXT("*=");
		case EBangoAssignOp::Divide:   return TEXT("/=");
		default:                       return TEXT("?");
		}
	}

	static FString DumpStmt(const FBangoStmt& Stmt);

	static FString DumpBlock(const FBangoBlock& Block)
	{
		FString Result = TEXT("(");
		for (int32 i = 0; i < Block.Num(); ++i)
		{
			if (i > 0)
			{
				Result += TEXT(" ");
			}
			Result += DumpStmt(*Block[i]);
		}
		Result += TEXT(")");
		return Result;
	}

	static FString DumpOptionalStmt(const FBangoStmtPtr& Stmt)
	{
		return Stmt ? DumpStmt(*Stmt) : FString(TEXT("_"));
	}

	static FString DumpOptionalExpr(const FBangoExprPtr& Expr)
	{
		return Expr ? Bango::DumpExpr(*Expr) : FString(TEXT("_"));
	}

	static FString DumpStmt(const FBangoStmt& Stmt)
	{
		switch (Stmt.Kind)
		{
		case EBangoStmtKind::VarDecl:
		{
			const FBangoVarDeclStmt& S = *Stmt.As<FBangoVarDeclStmt>();
			FString Result = FString::Printf(TEXT("(%s %s %s"), S.bGlobal ? TEXT("global") : TEXT("decl"), *S.Type.ToString(), *S.Name);
			if (S.Init)
			{
				Result += TEXT(" ");
				Result += Bango::DumpExpr(*S.Init);
			}
			Result += TEXT(")");
			return Result;
		}
		case EBangoStmtKind::Assign:
		{
			const FBangoAssignStmt& S = *Stmt.As<FBangoAssignStmt>();
			return FString::Printf(TEXT("(%s %s %s)"), AssignOpText(S.Op), *Bango::DumpExpr(*S.Target), *Bango::DumpExpr(*S.Value));
		}
		case EBangoStmtKind::IncDec:
		{
			const FBangoIncDecStmt& S = *Stmt.As<FBangoIncDecStmt>();
			return FString::Printf(TEXT("(%s %s)"), S.bIncrement ? TEXT("++") : TEXT("--"), *Bango::DumpExpr(*S.Target));
		}
		case EBangoStmtKind::Call:
			return Bango::DumpExpr(*Stmt.As<FBangoCallStmt>()->Call);
		case EBangoStmtKind::If:
		{
			const FBangoIfStmt& S = *Stmt.As<FBangoIfStmt>();
			FString Result = FString::Printf(TEXT("(if %s %s"), *Bango::DumpExpr(*S.Condition), *DumpBlock(S.Then));
			if (S.bHasElse)
			{
				Result += TEXT(" else ");
				Result += DumpBlock(S.Else);
			}
			Result += TEXT(")");
			return Result;
		}
		case EBangoStmtKind::While:
		{
			const FBangoWhileStmt& S = *Stmt.As<FBangoWhileStmt>();
			return FString::Printf(TEXT("(while %s %s)"), *Bango::DumpExpr(*S.Condition), *DumpBlock(S.Body));
		}
		case EBangoStmtKind::For:
		{
			const FBangoForStmt& S = *Stmt.As<FBangoForStmt>();
			return FString::Printf(TEXT("(for %s %s %s %s)"), *DumpOptionalStmt(S.Init), *DumpOptionalExpr(S.Condition), *DumpOptionalStmt(S.Step), *DumpBlock(S.Body));
		}
		case EBangoStmtKind::ForEach:
		{
			const FBangoForEachStmt& S = *Stmt.As<FBangoForEachStmt>();
			return FString::Printf(TEXT("(forEach %s %s %s)"), *S.VarName, *Bango::DumpExpr(*S.Array), *DumpBlock(S.Body));
		}
		case EBangoStmtKind::Break:
			return TEXT("(break)");
		case EBangoStmtKind::Continue:
			return TEXT("(continue)");
		case EBangoStmtKind::Return:
		{
			const FBangoReturnStmt& S = *Stmt.As<FBangoReturnStmt>();
			return S.Value ? FString::Printf(TEXT("(return %s)"), *Bango::DumpExpr(*S.Value)) : FString(TEXT("(return)"));
		}
		case EBangoStmtKind::Label:
			return FString::Printf(TEXT("(label %s)"), *Stmt.As<FBangoLabelStmt>()->Name);
		case EBangoStmtKind::Goto:
			return FString::Printf(TEXT("(goto %s)"), *Stmt.As<FBangoGotoStmt>()->Name);
		case EBangoStmtKind::Reroute:
			return TEXT("(reroute)");
		case EBangoStmtKind::Params:
		{
			FString Result = TEXT("(params");
			for (const FBangoScriptParamDecl& Param : Stmt.As<FBangoParamsStmt>()->Params)
			{
				Result += FString::Printf(TEXT(" (%s %s"), *Param.Type.ToString(), *Param.Name);
				if (Param.Default)
				{
					Result += TEXT(" = ");
					Result += Bango::DumpExpr(*Param.Default);
				}
				Result += TEXT(")");
			}
			Result += TEXT(")");
			return Result;
		}
		default:
			return TEXT("(?)");
		}
	}
}

namespace Bango
{
	FString DumpExpr(const FBangoExpr& Expr)
	{
		using namespace BangoDumpPrivate;

		switch (Expr.Kind)
		{
		case EBangoExprKind::Literal:
		{
			const FBangoLiteralExpr& E = *Expr.As<FBangoLiteralExpr>();
			switch (E.LiteralKind)
			{
			case EBangoLiteralKind::Bool:   return E.BoolValue ? TEXT("true") : TEXT("false");
			case EBangoLiteralKind::Int:    return FString::Printf(TEXT("%lld"), E.IntValue);
			case EBangoLiteralKind::Double: return FormatDouble(E.DoubleValue);
			case EBangoLiteralKind::String: return QuoteString(E.StringValue);
			case EBangoLiteralKind::Nil:    return TEXT("nil");
			case EBangoLiteralKind::Entity: return TEXT("@") + E.StringValue;
			case EBangoLiteralKind::Text:
			{
				// (text "Key" "Source"), with the namespace first and the notes last when they're given; (invtext "Source")
				if (E.bTextInvariant)
				{
					return TEXT("(invtext ") + QuoteString(E.StringValue) + TEXT(")");
				}
				FString Result = TEXT("(text");
				if (E.bTextHasNamespace)
				{
					Result += TEXT(" ") + QuoteString(E.TextNamespace);
				}
				Result += TEXT(" ") + QuoteString(E.TextKey) + TEXT(" ") + QuoteString(E.StringValue);
				if (!E.TextNotes.IsEmpty())
				{
					Result += TEXT(" notes ") + QuoteString(E.TextNotes);
				}
				return Result + TEXT(")");
			}
			default:                        return TEXT("?");
			}
		}
		case EBangoExprKind::ArrayLiteral:
		{
			const FBangoArrayLiteralExpr& E = *Expr.As<FBangoArrayLiteralExpr>();
			FString Result = TEXT("(array");
			for (const FBangoExprPtr& Element : E.Elements)
			{
				Result += TEXT(" ");
				Result += DumpExpr(*Element);
			}
			Result += TEXT(")");
			return Result;
		}
		case EBangoExprKind::Name:
			return Expr.As<FBangoNameExpr>()->Name;
		case EBangoExprKind::Index:
		{
			const FBangoIndexExpr& E = *Expr.As<FBangoIndexExpr>();
			return FString::Printf(TEXT("(index %s %s)"), *DumpExpr(*E.Array), *DumpExpr(*E.Index));
		}
		case EBangoExprKind::Unary:
		{
			const FBangoUnaryExpr& E = *Expr.As<FBangoUnaryExpr>();
			return FString::Printf(TEXT("(%s %s)"), E.Op == EBangoUnaryOp::Not ? TEXT("!") : TEXT("neg"), *DumpExpr(*E.Operand));
		}
		case EBangoExprKind::Binary:
		{
			const FBangoBinaryExpr& E = *Expr.As<FBangoBinaryExpr>();
			return FString::Printf(TEXT("(%s %s %s)"), BinaryOpText(E.Op), *DumpExpr(*E.Left), *DumpExpr(*E.Right));
		}
		case EBangoExprKind::Call:
		{
			const FBangoCallExpr& E = *Expr.As<FBangoCallExpr>();
			FString Result = TEXT("(call ");
			if (E.Target)
			{
				Result += DumpExpr(*E.Target);
				Result += TEXT(":");
			}
			Result += E.Name;
			for (const FBangoExprPtr& Arg : E.Args)
			{
				Result += TEXT(" ");
				Result += DumpExpr(*Arg);
			}
			Result += TEXT(")");
			return Result;
		}
		case EBangoExprKind::IsNil:
			return FString::Printf(TEXT("(isnil %s)"), *DumpExpr(*Expr.As<FBangoIsNilExpr>()->Operand));
		case EBangoExprKind::Member:
		{
			const FBangoMemberExpr& E = *Expr.As<FBangoMemberExpr>();
			return FString::Printf(TEXT("(. %s %s)"), *DumpExpr(*E.Object), *E.Member);
		}
		case EBangoExprKind::Empty:
			return TEXT("<empty>");
		default:
			return TEXT("?");
		}
	}

	FString DumpScript(const FBangoScript& Script)
	{
		using namespace BangoDumpPrivate;

		TArray<FString> Parts;
		if (Script.bHasLocNamespace)
		{
			Parts.Add(TEXT("(locNamespace ") + QuoteString(Script.LocNamespace) + TEXT(")"));
		}
		for (const TUniquePtr<FBangoFunction>& Function : Script.Functions)
		{
			FString Params;
			for (int32 i = 0; i < Function->Params.Num(); ++i)
			{
				if (i > 0)
				{
					Params += TEXT(" ");
				}
				Params += Function->Params[i].Type.ToString();
				Params += TEXT(" ");
				Params += Function->Params[i].Name;
			}
			Parts.Add(FString::Printf(TEXT("(function %s %s (%s) %s)"),
				Function->bReturnsVoid ? TEXT("void") : *Function->ReturnType.ToString(),
				*Function->Name, *Params, *DumpBlock(Function->Body)));
		}
		for (const FBangoStmtPtr& Stmt : Script.TopLevel)
		{
			Parts.Add(DumpStmt(*Stmt));
		}
		return FString::Join(Parts, TEXT(" "));
	}
}
