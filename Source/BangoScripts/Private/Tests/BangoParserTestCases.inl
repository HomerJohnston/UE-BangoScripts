#pragma once

// Parser test cases, shared by the Unreal automation test (BangoParserTests.cpp)
// and the stand-alone test harness used during development.

struct FBangoParserTestCase
{
	const TCHAR* Name;
	const TCHAR* Source;
	/** Expected Bango::DumpScript output. "*" means "must parse, don't check the dump". Null for error cases. */
	const TCHAR* ExpectedDump;
	/** For error cases: "(line:column)" the error must point at. */
	const TCHAR* ErrorLocation;
	/** For error cases: text the error message must contain. */
	const TCHAR* ErrorContains;
};

static const FBangoParserTestCase GBangoParserTestCases[] =
{
	// ---- Valid scripts ----
	{ TEXT("empty"), TEXT(""), TEXT(""), nullptr, nullptr },
	{ TEXT("globals"),
		TEXT("global int foundThings;\nglobal bool playerHasGun = true;"),
		TEXT("(global int foundThings) (global bool playerHasGun true)"), nullptr, nullptr },
	{ TEXT("locals and arrays"),
		TEXT("int _x = 5;\nstring _s;\nfloat[][] _g = [[1, 2.5], []];"),
		TEXT("(decl int _x 5) (decl string _s) (decl float[][] _g (array (array 1 2.5) (array)))"), nullptr, nullptr },
	{ TEXT("precedence"),
		TEXT("bool _b = !_a || _c && _d == 1 + 2 * -3 < 4;"),
		TEXT("(decl bool _b (|| (! _a) (&& _c (== _d (< (+ 1 (* 2 (neg 3))) 4)))))"), nullptr, nullptr },
	{ TEXT("parentheses and modulo"),
		TEXT("int _x = (1 + 2) * 3 % 4 - -(5);"),
		TEXT("(decl int _x (- (% (* (+ 1 2) 3) 4) (neg 5)))"), nullptr, nullptr },
	{ TEXT("calls"),
		TEXT("Hint(\"Hi\");\n@player SetDamage(0.2);\nHint(\"{0} left\", _n);\nQuitGame();\n_grid[1] Push(4);\nint _e = _arr Pop();"),
		TEXT("(call Hint \"Hi\") (call @player:SetDamage 0.2) (call Hint \"{0} left\" _n) (call QuitGame) (call (index _grid 1):Push 4) (decl int _e (call _arr:Pop))"), nullptr, nullptr },
	{ TEXT("entity literals"),
		TEXT("@john Speak(\"Hi\");\nentity _e = @door17;\nentity[] _c = [@bob, @_alice, @if];\nbool _b = _e == @john;\n@john GetPos()[0];"),
		nullptr, TEXT("(5:1)"), TEXT("doesn't do anything") },
	{ TEXT("entity literals parse"),
		TEXT("@john Speak(\"Hi\");\nentity _e = @door17;\nentity[] _c = [@bob, @_alice, @if];\nbool _b = _e == @john;\nHint(\"{0}\", @john GetPos()[0]);"),
		TEXT("(call @john:Speak \"Hi\") (decl entity _e @door17) (decl entity[] _c (array @bob @_alice @if)) (decl bool _b (== _e @john)) (call Hint \"{0}\" (index (call @john:GetPos) 0))"), nullptr, nullptr },
	{ TEXT("call in expression"),
		TEXT("if (AskChoice(\"A\", \"B\") == 0)\n{\n};\nint _c = Count(_a) + 1;"),
		TEXT("(if (== (call AskChoice \"A\" \"B\") 0) ()) (decl int _c (+ (call Count _a) 1))"), nullptr, nullptr },
	{ TEXT("assignment forms"),
		TEXT("_x = 1;\n_x += 2;\n_x -= 3;\n_x *= 4;\n_x /= 5;\n_x++;\n--_x;\n_g[1][2] = 7;\ngold = nil;"),
		TEXT("(= _x 1) (+= _x 2) (-= _x 3) (*= _x 4) (/= _x 5) (++ _x) (-- _x) (= (index (index _g 1) 2) 7) (= gold nil)"), nullptr, nullptr },
	{ TEXT("if else"),
		TEXT("if (a)\n{\n\tHint(\"1\");\n}\nelse\n{\n\tHint(\"2\");\n};"),
		TEXT("(if a ((call Hint \"1\")) else ((call Hint \"2\")))"), nullptr, nullptr },
	{ TEXT("else if chain"),
		TEXT("if (a) {\n\tx();\n} else if (b) {\n\ty();\n} else {\n\tz();\n};"),
		TEXT("(if a ((call x)) else ((if b ((call y)) else ((call z)))))"), nullptr, nullptr },
	{ TEXT("nested if"),
		TEXT("if (a)\n{\n\tif (b)\n\t{\n\t\tx();\n\t};\n};"),
		TEXT("(if a ((if b ((call x)))))"), nullptr, nullptr },
	{ TEXT("while"),
		TEXT("while (true)\n{\n\tbreak;\n\tcontinue;\n};"),
		TEXT("(while true ((break) (continue)))"), nullptr, nullptr },
	{ TEXT("for"),
		TEXT("for (int _i = 0; _i <= 8; _i++)\n{\n\tHint(\"x\");\n};"),
		TEXT("(for (decl int _i 0) (<= _i 8) (++ _i) ((call Hint \"x\")))"), nullptr, nullptr },
	{ TEXT("for with empty parts"),
		TEXT("for (;;)\n{\n\tbreak;\n};"),
		TEXT("(for _ _ _ ((break)))"), nullptr, nullptr },
	{ TEXT("for with assignment parts"),
		TEXT("for (_i = 10; _i > 0; _i -= 2)\n{\n};"),
		TEXT("(for (= _i 10) (> _i 0) (-= _i 2) ())"), nullptr, nullptr },
	{ TEXT("forEach"),
		TEXT("forEach (_x in questLog)\n{\n\tHint(_x);\n};"),
		TEXT("(forEach _x questLog ((call Hint _x)))"), nullptr, nullptr },
	{ TEXT("labels, goto, return"),
		TEXT("label Hub;\ngoto Hub;\nreturn;\nreturn 5;"),
		TEXT("(label Hub) (goto Hub) (return) (return 5)"), nullptr, nullptr },
	{ TEXT("functions"),
		TEXT("function int AddScore(int _amount, float[] _w)\n{\n\treturn _amount * 2;\n};\nfunction void Nag()\n{\n};\nNag();"),
		TEXT("(function int AddScore (int _amount float[] _w) ((return (* _amount 2)))) (function void Nag () ()) (call Nag)"), nullptr, nullptr },
	{ TEXT("IsNil"),
		TEXT("if (!IsNil(x) && IsNil(_g[3][2]))\n{\n};"),
		TEXT("(if (&& (! (isnil x)) (isnil (index (index _g 3) 2))) ())"), nullptr, nullptr },
	{ TEXT("largest int literal"), TEXT("int _x = 2147483647;\nint _y = -2147483647;"), TEXT("(decl int _x 2147483647) (decl int _y (neg 2147483647))"), nullptr, nullptr },
	{ TEXT("literals"),
		TEXT("string _s = \"a\\\"b\\\\c\\nd\\te\";\nfloat _d = 1e3;\nfloat _e = 2.5e-1;\nbool _f = false;\nint _n = nil;\nfloat _z = 0.1;"),
		TEXT("(decl string _s \"a\\\"b\\\\c\\nd\\te\") (decl float _d 1000.0) (decl float _e 0.25) (decl bool _f false) (decl int _n nil) (decl float _z 0.1)"), nullptr, nullptr },
	{ TEXT("comments and metadata"),
		TEXT("Hint(\"x\"); // just a comment\nHint(\"y\"); //@12(4,7)\nHint(\"z\");   //@ 13 ( -1 , 7 )   \n// a comment line\n"),
		TEXT("(call Hint \"x\") (call Hint \"y\") (call Hint \"z\")"), nullptr, nullptr },
	{ TEXT("disabled: inline"),
		TEXT("Hint(\"a\");\n/* Hint(\"b\"); */               //@2(4, 0)\nHint(\"c\");"),
		TEXT("(call Hint \"a\") (disabled (call Hint \"b\")) (call Hint \"c\")"), nullptr, nullptr },
	{ TEXT("disabled: block, nested, strings"),
		TEXT("/*\nif (a)\n{\n\t/* Hint(\"*/\"); */\n\tHint(\"/*\");\n};\n*/\nx();"),
		TEXT("(disabled (if a ((disabled (call Hint \"*/\")) (call Hint \"/*\")))) (call x)"), nullptr, nullptr },
	{ TEXT("disabled: several, with comments and reroutes"),
		TEXT("/*\n// note\na();\n//@5(1, 1)\nb();\n*/"),
		TEXT("(disabled (call a)) (reroute) (disabled (call b))"), nullptr, nullptr },
	{ TEXT("disabled: in a loop and a function"),
		TEXT("while (a)\n{\n\t/* break; */\n};\nfunction void F()\n{\n\t/* return; */\n};"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("disabled: double"), TEXT("/* /* a(); */ */"), TEXT("(disabled (call a))"), nullptr, nullptr },
	{ TEXT("structure tokens share lines"),
		TEXT("while (a) {\n\tx();\n} ;"),
		TEXT("(while a ((call x)))"), nullptr, nullptr },
	{ TEXT("spec example"),
		TEXT("global int foundThings;            //@1(0, 0)\n")
		TEXT("global bool playerHasGun;          //@2(0, 1)\n")
		TEXT("global string[] questLog;          //@3(0, 2)\n")
		TEXT("\n")
		TEXT("function void Nag()\n")
		TEXT("{\n")
		TEXT("\tjohn Speak(\"Find a gun.\");     //@20(0, 0)\n")
		TEXT("\tAwaitSpeak();                  //@21(0, 1)\n")
		TEXT("};\n")
		TEXT("\n")
		TEXT("if (!IsNil(foundThings) && foundThings >= 10)   //@4(0, 3)\n")
		TEXT("{\n")
		TEXT("\tHint(\"You found at least 10 things!\");         //@5(1, 4)\n")
		TEXT("\tquestLog Push(\"Found 10 things\");              //@6(1, 5)\n")
		TEXT("};\n")
		TEXT("\n")
		TEXT("for (int _i = 0; _i < 3; _i++)     //@7(0, 6)\n")
		TEXT("{\n")
		TEXT("\tHint(\"Countdown: {0}\", 3 - _i); //@8(1, 7)\n")
		TEXT("\tSleep(1.0);                    //@9(1, 8)\n")
		TEXT("};\n")
		TEXT("\n")
		TEXT("label Check;                       //@10(0, 9)\n")
		TEXT("if (IsNil(playerHasGun))      //@11(0, 10)\n")
		TEXT("{\n")
		TEXT("\tjohn Speak(\"Congrats! You found a bug.\");      //@12(1, 11)\n")
		TEXT("\tAwaitSpeak();                  //@13(1, 12)\n")
		TEXT("\treturn;                        //@14(1, 13)\n")
		TEXT("}\n")
		TEXT("else\n")
		TEXT("{\n")
		TEXT("\tif (!playerHasGun)             //@15(2, 11)\n")
		TEXT("\t{\n")
		TEXT("\t\tNag();                     //@16(3, 12)\n")
		TEXT("\t\tSleep(Random(3.0, 5.0));   //@17(3, 13)\n")
		TEXT("\t\tgoto Check;                //@18(3, 14)\n")
		TEXT("\t};\n")
		TEXT("};\n")
		TEXT("\n")
		TEXT("forEach (_entry in questLog)       //@19(0, 15)\n")
		TEXT("{\n")
		TEXT("\tif (!IsNil(_entry))         //@22(1, 16)\n")
		TEXT("\t{\n")
		TEXT("\t\tHint(_entry);              //@23(2, 17)\n")
		TEXT("\t};\n")
		TEXT("};\n")
		TEXT("\n")
		TEXT("@john Speak(\"Great, you found a gun.\");  //@24(0, 18)\n")
		TEXT("AwaitSpeak();                            //@25(0, 19)\n"),
		TEXT("*"), nullptr, nullptr },

	{ TEXT("params"),
		TEXT("#define params (entity _speaker, string _line, int _repeats = 1, entity _listener = nil, float _d = -2.5, int[] _list = [1, -2], string[][] _g = [[\"a\"], []])\nHint(_line);"),
		TEXT("(params (entity _speaker) (string _line) (int _repeats = 1) (entity _listener = nil) (float _d = (neg 2.5)) (int[] _list = (array 1 (neg 2))) (string[][] _g = (array (array \"a\") (array)))) (call Hint _line)"), nullptr, nullptr },
	{ TEXT("params with entity defaults"),
		TEXT("#define params (entity _who = @john, entity[] _crowd = [@bob, @alice])"),
		TEXT("(params (entity _who = @john) (entity[] _crowd = (array @bob @alice)))"), nullptr, nullptr },
	{ TEXT("empty params"), TEXT("#define params ()"), TEXT("(params)"), nullptr, nullptr },
	{ TEXT("params, then a function"), TEXT("#define params (int _a)\nfunction void F()\n{\n};"), TEXT("(function void F () ()) (params (int _a))"), nullptr, nullptr },
	{ TEXT("params line with metadata"), TEXT("#define params (int _a) //@1(0, 0)\nHint(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("can't have node metadata") },
	{ TEXT("node ID 0 is reserved"), TEXT("Hint(\"x\"); //@0(1, 1)"), nullptr, TEXT("(1:12)"), TEXT("Node ID 0 is reserved") },

	// ---- Errors ----
	{ TEXT("missing semicolon"), TEXT("Hint(\"x\")\nHint(\"y\");"), nullptr, TEXT("(2:1)"), TEXT("Expected ';'") },
	{ TEXT("local without underscore"), TEXT("int ArrayCount = 5;"), TEXT("(decl int ArrayCount 5)"), nullptr, nullptr },
	{ TEXT("global with underscore"), TEXT("global int _x;"), TEXT("(global int _x)"), nullptr, nullptr },
	{ TEXT("block missing ;"), TEXT("if (a)\n{\n};\nwhile (b)\n{\n}\nHint();"), nullptr, TEXT("(6:1)"), TEXT("Expected ';' after '}'") },
	{ TEXT("; before else"), TEXT("if (a)\n{\n};\nelse\n{\n};"), nullptr, TEXT("(4:1)"), TEXT("'else' must come straight after") },
	{ TEXT("two statements on a line"), TEXT("Hint(\"a\"); Hint(\"b\");"), nullptr, TEXT("(1:12)"), TEXT("Only one statement") },
	{ TEXT("statement across lines"), TEXT("Hint(\"a\",\n\"b\");"), nullptr, TEXT("(2:5)"), TEXT("continues onto line 2") },
	{ TEXT("if body on its header line"), TEXT("if (a) { Hint(); };"), nullptr, TEXT("(1:10)"), TEXT("Only one statement") },
	{ TEXT("if header across lines"), TEXT("if (a &&\nb)\n{\n};"), nullptr, TEXT("(2:2)"), TEXT("continues onto line 2") },
	{ TEXT("metadata without a node"), TEXT("if (a)\n{ //@1(0, 0)\n};"), nullptr, TEXT("(2:1)"), TEXT("no statement") },
	{ TEXT("duplicate node id"), TEXT("a(); //@1(0, 0)\nb(); //@1(0, 1)"), nullptr, TEXT("(2:6)"), TEXT("used more than once") },
	{ TEXT("bad metadata"), TEXT("a(); //@ x"), nullptr, TEXT("(1:10)"), TEXT("expected a node ID") },
	{ TEXT("text after metadata"), TEXT("a(); //@1(0, 0) extra"), nullptr, TEXT("(1:17)"), TEXT("Unexpected text after node metadata") },
	{ TEXT("function inside block"), TEXT("if (a)\n{\n\tfunction void F()\n\t{\n\t};\n};"), nullptr, TEXT("(3:2)"), TEXT("top level") },
	{ TEXT("function with metadata"), TEXT("function void F() //@3(0, 0)\n{\n};"), nullptr, TEXT("(1:1)"), TEXT("not a graph node") },
	{ TEXT("function missing ;"), TEXT("function void F()\n{\n}\nF();"), nullptr, TEXT("(3:1)"), TEXT("Expected ';' after '}'") },
	{ TEXT("parameter without underscore"), TEXT("function void F(int x)\n{\n};"), TEXT("(function void F (int x) ())"), nullptr, nullptr },
	{ TEXT("postfix ++ in expression"), TEXT("_y = _x++;"), nullptr, TEXT("(1:8)"), TEXT("statements, not expressions") },
	{ TEXT("prefix ++ in expression"), TEXT("_y = ++_x;"), nullptr, TEXT("(1:6)"), TEXT("statements, not expressions") },
	{ TEXT("unused value"), TEXT("_x + 1;"), nullptr, TEXT("(1:1)"), TEXT("doesn't do anything") },
	{ TEXT("command without parentheses"), TEXT("@john Speak;"), nullptr, TEXT("(1:7)"), TEXT("needs parentheses") },
	{ TEXT("global target without parentheses"), TEXT("boss Speak;"), nullptr, TEXT("(1:6)"), TEXT("needs parentheses") },
	{ TEXT("@ without a name"), TEXT("Hint(@);"), nullptr, TEXT("(1:6)"), TEXT("Expected a Bango ID name right after '@'") },
	{ TEXT("space after @"), TEXT("@ john Speak(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("no space in between") },
	{ TEXT("@ before a number"), TEXT("Hint(@5);"), nullptr, TEXT("(1:6)"), TEXT("Expected a Bango ID name") },
	{ TEXT("assign to a Bango ID"), TEXT("@john = @bob;"), nullptr, TEXT("(1:1)"), TEXT("only assign to a variable") },
	{ TEXT("Bango ID as a declared name"), TEXT("entity @john;"), nullptr, TEXT("(1:8)"), TEXT("'@john'") },
	{ TEXT("unterminated string"), TEXT("Hint(\"abc);"), nullptr, TEXT("(1:6)"), TEXT("no closing quote") },
	{ TEXT("bad escape"), TEXT("Hint(\"a\\qb\");"), nullptr, TEXT("(1:8)"), TEXT("Unknown escape") },
	{ TEXT("disabled: prose is an error"), TEXT("/* hi there */"), nullptr, TEXT("(1:7)"), TEXT("must still be Bango statements") },
	{ TEXT("disabled: never closed"), TEXT("/*\nHint(\"a\");"), nullptr, TEXT("(1:1)"), TEXT("never closed") },
	{ TEXT("disabled: stray end"), TEXT("Hint(\"a\"); */"), nullptr, TEXT("(1:12)"), TEXT("has no '/*'") },
	{ TEXT("disabled: ends in another block"), TEXT("/*\nif (a)\n{\n\tx(); */\n};"), nullptr, TEXT("(4:7)"), TEXT("same block") },
	{ TEXT("disabled: closes an outer block"), TEXT("if (a)\n{\n\t/* x();\n};\n*/"), nullptr, TEXT("(4:1)"), TEXT("opened before the '/*'") },
	{ TEXT("disabled: no functions"), TEXT("/*\nfunction void F()\n{\n};\n*/"), nullptr, TEXT("(2:1)"), TEXT("A function can't be disabled") },
	{ TEXT("disabled: no defines"), TEXT("/*\n#define params (int _a)\n*/"), nullptr, TEXT("(2:1)"), TEXT("can't be disabled") },
	{ TEXT("disabled: no declarations"), TEXT("/* int _a = 1; */"), nullptr, TEXT("(1:4)"), TEXT("A declaration can't be disabled") },
	{ TEXT("disabled: two on a line"), TEXT("/* a(); */ /* b(); */"), nullptr, TEXT("(1:15)"), TEXT("one statement is allowed per line") },
	{ TEXT("disabled: bad code inside gets the note"), TEXT("/*\nHint(\"a\"\n*/"), nullptr, TEXT("(3:1)"), TEXT("must still be Bango statements") },
	{ TEXT("unclosed block"), TEXT("while (a)\n{\n\tHint();"), nullptr, TEXT("(3:9)"), TEXT("never closed") },
	{ TEXT("array type too deep"), TEXT("int[][][][][][][][][] _x;"), nullptr, TEXT("(1:1)"), TEXT("at most 8") },
	{ TEXT("assign to call"), TEXT("Foo() = 5;"), nullptr, TEXT("(1:1)"), TEXT("only assign to") },
	{ TEXT("int too large"), TEXT("int _x = 9223372036854775808;"), nullptr, TEXT("(1:10)"), TEXT("too large") },
	{ TEXT("int just too large"), TEXT("int _x = 2147483648;"), nullptr, TEXT("(1:10)"), TEXT("largest int is 2147483647") },
	{ TEXT("for init is a call"), TEXT("for (Hint(); _i < 3; _i++)\n{\n};"), nullptr, TEXT("(1:6)"), TEXT("declaration or an assignment") },
	{ TEXT("for declares global"), TEXT("for (global int g = 0; g < 3; g++)\n{\n};"), nullptr, TEXT("(1:6)"), TEXT("can't declare a global") },
	{ TEXT("keyword as name"), TEXT("int if = 5;"), nullptr, TEXT("(1:5)"), TEXT("keyword") },
	{ TEXT("single &"), TEXT("bool _b = a & b;"), nullptr, TEXT("(1:13)"), TEXT("Did you mean '&&'") },
	{ TEXT("forEach variable without underscore"), TEXT("forEach (x in a)\n{\n};"), TEXT("(forEach x a ())"), nullptr, nullptr },
	{ TEXT("forEach missing in"), TEXT("forEach (_x of a)\n{\n};"), nullptr, TEXT("(1:13)"), TEXT("Expected 'in'") },
	{ TEXT("else at top"), TEXT("else\n{\n};"), nullptr, TEXT("(1:1)"), TEXT("'else'") },
	{ TEXT("chained assignment"), TEXT("_a = _b = 1;"), nullptr, TEXT("(1:9)"), TEXT("can't be chained") },
	{ TEXT("decimal point without digits"), TEXT("float _d = 1.;"), nullptr, TEXT("(1:13)"), TEXT("decimal point") },
	{ TEXT("bare block"), TEXT("{\n};"), nullptr, TEXT("(1:1)"), TEXT("must belong to") },
	{ TEXT("declaration in expression"), TEXT("Hint(int _x);"), nullptr, TEXT("(1:6)"), TEXT("own line") },
	{ TEXT("IsNil of a call"), TEXT("if (IsNil(Foo()))\n{\n};"), nullptr, TEXT("(1:11)"), TEXT("IsNil needs") },
	{ TEXT("array type with size"), TEXT("int[3] _a;"), nullptr, TEXT("(1:4)"), TEXT("empty brackets") },
	{ TEXT("stray semicolon"), TEXT("Hint();;"), nullptr, TEXT("(1:8)"), TEXT("Unexpected ';'") },
	{ TEXT("missing condition paren"), TEXT("if a\n{\n};"), nullptr, TEXT("(1:4)"), TEXT("Expected '('") },
	{ TEXT("params not first"), TEXT("Hint(\"x\");\n#define params (int _a)"), nullptr, TEXT("(2:1)"), TEXT("must be the first line") },
	{ TEXT("params after a function"), TEXT("function void F()\n{\n};\n#define params (int _a)"), nullptr, TEXT("(4:1)"), TEXT("must be the first line") },
	{ TEXT("params twice"), TEXT("#define params (int _a)\n#define params (int _b)"), nullptr, TEXT("(2:1)"), TEXT("must be the first line") },
	{ TEXT("params in a block"), TEXT("if (true)\n{\n\t#define params (int _a)\n};"), nullptr, TEXT("(3:2)"), TEXT("outside any block or function") },
	{ TEXT("params in a function"), TEXT("function void F()\n{\n\t#define params (int _a)\n};"), nullptr, TEXT("(3:2)"), TEXT("outside any block or function") },
	{ TEXT("params without #define"), TEXT("params (int _a);"), nullptr, TEXT("(1:1)"), TEXT("written '#define params (...)'") },
	{ TEXT("params with a ';'"), TEXT("#define params (int _a);"), nullptr, TEXT("(1:24)"), TEXT("don't end with ';'") },
	{ TEXT("params with something after it"), TEXT("#define params (int _a) Hint(\"x\");"), nullptr, TEXT("(1:25)"), TEXT("Expected the end of the #define params line") },
	{ TEXT("required after default"), TEXT("#define params (int _a = 1, int _b)"), nullptr, TEXT("(1:33)"), TEXT("Required parameters must come first") },
	{ TEXT("non-literal default"), TEXT("#define params (int _a = 1 + 2)"), nullptr, TEXT("(1:28)"), TEXT("must be a fixed value") },
	{ TEXT("default from a variable"), TEXT("#define params (int _a = gold)"), nullptr, TEXT("(1:26)"), TEXT("must be a fixed value") },
	{ TEXT("default from a call"), TEXT("#define params (float _a = Random(1.0, 2.0))"), nullptr, TEXT("(1:28)"), TEXT("must be a fixed value") },
	{ TEXT("text default"), TEXT("#define params (text _t = LOCTEXT(\"K\", \"x\"))"), nullptr, TEXT("(1:27)"), TEXT("can't be translated") },
	{ TEXT("text default in an array"), TEXT("#define params (text[] _t = [nil, NSLOCTEXT(\"N\", \"K\", \"x\")])"), nullptr, TEXT("(1:35)"), TEXT("can't be translated") },
	{ TEXT("params name without underscore"), TEXT("#define params (int count)"), TEXT("(params (int count))"), nullptr, nullptr },
	{ TEXT("params across lines"), TEXT("#define params (int _a,\n\tint _b)"), nullptr, TEXT("(2:8)"), TEXT("continues onto line 2") },
	{ TEXT("unknown directive"), TEXT("#include \"x\""), nullptr, TEXT("(1:1)"), TEXT("Unknown '#include'") },
	{ TEXT("directive without a space"), TEXT("#define(x)"), nullptr, TEXT("(1:1)"), TEXT("Put a space after '#define'") },
	{ TEXT("directive mid-line"), TEXT("Hint(\"x\"); #define params ()"), nullptr, TEXT("(1:12)"), TEXT("must start its line") },
	{ TEXT("unknown define"), TEXT("#define DEBUG 1"), nullptr, TEXT("(1:9)"), TEXT("Bango only has") },
	{ TEXT("text literals"),
		TEXT("#define params (text _line = nil)\n#define LOCTEXT_NAMESPACE \"Bango.Intro\"\ntext _a = LOCTEXT(\"Greet\", \"Hello \\\"you\\\"\");\ntext[] _b = [NSLOCTEXT(\"Other\", \"K\", \"x\"), NOTELOCTEXT(\"N\", \"y\", \"Said slowly\"), NSNOTELOCTEXT(\"O\", \"M\", \"z\", \"Shouted\")];\n#undef LOCTEXT_NAMESPACE"),
		TEXT("(locNamespace \"Bango.Intro\") (params (text _line = nil)) (decl text _a (text \"Greet\" \"Hello \\\"you\\\"\")) (decl text[] _b (array (text \"Other\" \"K\" \"x\") (text \"N\" \"y\" notes \"Said slowly\") (text \"O\" \"M\" \"z\" notes \"Shouted\")))"), nullptr, nullptr },
	{ TEXT("invariant text"), TEXT("text _a = INVTEXT(\"Never \\\"translated\\\"\");\nHint(INVTEXT(\"{0}\"), 1);"),
		TEXT("(decl text _a (invtext \"Never \\\"translated\\\"\")) (call Hint (invtext \"{0}\") 1)"), nullptr, nullptr },
	{ TEXT("invariant text takes one string"), TEXT("text _a = INVTEXT(\"K\", \"S\");"), nullptr, TEXT("(1:22)"), TEXT("INVTEXT takes 1 string: INVTEXT(\"Text\")") },
	{ TEXT("invariant text needs a string"), TEXT("text _a = INVTEXT(_s);"), nullptr, TEXT("(1:19)"), TEXT("INVTEXT's values must be written as strings") },
	{ TEXT("namespace first"), TEXT("#define LOCTEXT_NAMESPACE \"A\"\nHint(\"x\");\n#undef LOCTEXT_NAMESPACE"), TEXT("(locNamespace \"A\") (call Hint \"x\")"), nullptr, nullptr },
	{ TEXT("namespace with functions"), TEXT("#define LOCTEXT_NAMESPACE \"A\"\nfunction void F()\n{\n};\n#undef LOCTEXT_NAMESPACE\n// the end"), TEXT("(locNamespace \"A\") (function void F () ())"), nullptr, nullptr },
	{ TEXT("namespace after a statement"), TEXT("Hint(\"x\");\n#define LOCTEXT_NAMESPACE \"A\""), nullptr, TEXT("(2:1)"), TEXT("must come at the top of the script") },
	{ TEXT("namespace before params"), TEXT("#define LOCTEXT_NAMESPACE \"A\"\n#define params (int _a)"), nullptr, TEXT("(2:1)"), TEXT("must be the first line") },
	{ TEXT("namespace twice"), TEXT("#define LOCTEXT_NAMESPACE \"A\"\n#define LOCTEXT_NAMESPACE \"B\""), nullptr, TEXT("(2:1)"), TEXT("only have one '#define LOCTEXT_NAMESPACE'") },
	{ TEXT("namespace in a block"), TEXT("if (true)\n{\n\t#define LOCTEXT_NAMESPACE \"A\"\n};"), nullptr, TEXT("(3:2)"), TEXT("outside any block or function") },
	{ TEXT("namespace needs a string"), TEXT("#define LOCTEXT_NAMESPACE A"), nullptr, TEXT("(1:27)"), TEXT("as a string") },
	{ TEXT("namespace empty"), TEXT("#define LOCTEXT_NAMESPACE \"\""), nullptr, TEXT("(1:27)"), TEXT("can't be empty") },
	{ TEXT("namespace with metadata"), TEXT("#define LOCTEXT_NAMESPACE \"A\" //@1(0, 0)\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(1:1)"), TEXT("can't have node metadata") },
	{ TEXT("namespace with a ';'"), TEXT("#define LOCTEXT_NAMESPACE \"A\";"), nullptr, TEXT("(1:30)"), TEXT("don't end with ';'") },
	{ TEXT("namespace across lines"), TEXT("#define LOCTEXT_NAMESPACE\n\"A\""), nullptr, TEXT("(1:9)"), TEXT("as a string") },
	{ TEXT("namespace without undef"), TEXT("#define LOCTEXT_NAMESPACE \"A\"\nHint(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("must end with the line '#undef LOCTEXT_NAMESPACE'") },
	{ TEXT("undef not last"), TEXT("#define LOCTEXT_NAMESPACE \"A\"\n#undef LOCTEXT_NAMESPACE\nHint(\"x\");"), nullptr, TEXT("(3:1)"), TEXT("must be the last line") },
	{ TEXT("undef without define"), TEXT("Hint(\"x\");\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(2:1)"), TEXT("no '#define LOCTEXT_NAMESPACE' for this '#undef'") },
	{ TEXT("undef in a function"), TEXT("#define LOCTEXT_NAMESPACE \"A\"\nfunction void F()\n{\n\t#undef LOCTEXT_NAMESPACE\n};"), nullptr, TEXT("(4:2)"), TEXT("must be the last line of the script, outside") },
	{ TEXT("undef of something else"), TEXT("#undef params"), nullptr, TEXT("(1:1)"), TEXT("The only '#undef'") },
	{ TEXT("old double type"), TEXT("float _a = 1.5;\ndouble _b = 2.0;"), nullptr, TEXT("(2:1)"), TEXT("'double' type is now called 'float'") },
	{ TEXT("old locNamespace line"), TEXT("locNamespace(\"A\");"), nullptr, TEXT("(1:1)"), TEXT("now written '#define LOCTEXT_NAMESPACE") },
	{ TEXT("text literal values must be strings"), TEXT("Say(LOCTEXT(\"Key\", _s));"), nullptr, TEXT("(1:20)"), TEXT("must be written as strings") },
	{ TEXT("text literal with too few values"), TEXT("Say(NSLOCTEXT(\"Ns\", \"Key\"));"), nullptr, TEXT("(1:26)"), TEXT("NSLOCTEXT takes 3 strings") },
	{ TEXT("text literal with too many values"), TEXT("Say(LOCTEXT(\"Key\", \"a\", \"b\"));"), nullptr, TEXT("(1:23)"), TEXT("LOCTEXT takes 2 strings") },
	{ TEXT("text literal with an empty key"), TEXT("Say(LOCTEXT(\"\", \"a\"));"), nullptr, TEXT("(1:13)"), TEXT("key can't be empty") },
	{ TEXT("text literal with an empty namespace"), TEXT("Say(NSLOCTEXT(\"\", \"K\", \"a\"));"), nullptr, TEXT("(1:15)"), TEXT("namespace can't be empty") },
	{ TEXT("text is a keyword"), TEXT("int text = 1;"), nullptr, TEXT("(1:5)"), TEXT("keyword") },
	{ TEXT("vectors"), TEXT("vector3 _v = [1, 2, 3];\nvector2[] _w;\n_v.x = _w[0].y + _who GetPos().z;"),
		TEXT("(decl vector3 _v (array 1 2 3)) (decl vector2[] _w) (= (. _v x) (+ (. (index _w 0) y) (. (call _who:GetPos) z)))"), nullptr, nullptr },
	{ TEXT("unknown vector part"), TEXT("float _f = _v.w;"), nullptr, TEXT("(1:15)"), TEXT("'.w' isn't a vector part") },
	{ TEXT("uppercase vector part"), TEXT("float _f = _v.X;"), nullptr, TEXT("(1:15)"), TEXT("write .x, not .X") },
	{ TEXT("nothing after a dot"), TEXT("float _f = _v.;"), nullptr, TEXT("(1:15)"), TEXT("Expected a vector part (x, y or z) after '.'") },
	{ TEXT("IsNil on a vector part"), TEXT("bool _b = IsNil(_v.x);"), nullptr, TEXT("(1:20)"), TEXT("Check the whole vector instead: IsNil(_v)") },
	{ TEXT("vector3 is a keyword"), TEXT("int vector3 = 1;"), nullptr, TEXT("(1:5)"), TEXT("keyword") },
	{ TEXT("reroutes"), TEXT("Hint(\"a\");\n   //@3(1, 2)\nif (true)\n{\n//@4(0, 0)\n};\n\t//@5(2, 2)"), TEXT("(call Hint \"a\") (reroute) (if true ((reroute))) (reroute)"), nullptr, nullptr },
	{ TEXT("reroute between if and {"), TEXT("if (true)\n//@4(0, 0)\n{\n};"), nullptr, TEXT("(2:1)"), TEXT("so it's a reroute node, but it isn't where a statement could go") },
	// Values left empty parse (new graph nodes start this way); compiling them is the error.
	{ TEXT("empty if condition"), TEXT("if ()\n{\n};"), TEXT("(if <empty> ())"), nullptr, nullptr },
	{ TEXT("empty assignment"), TEXT("= ;\n_x = ;\n+= 2;"), TEXT("(= <empty> <empty>) (= _x <empty>) (+= <empty> 2)"), nullptr, nullptr },
	{ TEXT("empty ++ target"), TEXT("++;\n--;"), TEXT("(++ <empty>) (-- <empty>)"), nullptr, nullptr },
	{ TEXT("empty forEach array"), TEXT("forEach (_e in )\n{\n};"), TEXT("(forEach _e <empty> ())"), nullptr, nullptr },
	{ TEXT("empty label and goto"), TEXT("label ;\ngoto ;"), TEXT("(label ) (goto )"), nullptr, nullptr },
	{ TEXT("empty right side"), TEXT("Hint(\"{0}\", 1 + );"), TEXT("(call Hint \"{0}\" (+ 1 <empty>))"), nullptr, nullptr },
	{ TEXT("a value can't be left out before a comma"), TEXT("Hint(\"{0}\", , 2);"), nullptr, TEXT("(1:13)"), TEXT("Expected a value, but found ','.") },
	{ TEXT("an assignment target that can't be stored to"), TEXT("1 = ;"), nullptr, TEXT("(1:1)"), TEXT("You can only assign to a variable") },
	{ TEXT("a run of reroutes on one line"), TEXT("Hint(\"a\");\n//@3(1, 2) @4(2,2)   @5 ( 3 , 2 )\nHint(\"b\");"), TEXT("(call Hint \"a\") (reroute) (reroute) (reroute) (call Hint \"b\")"), nullptr, nullptr },
	{ TEXT("a statement line with two nodes"), TEXT("Hint(\"a\"); //@3(1, 2) @4(2, 2)"), nullptr, TEXT("(1:1)"), TEXT("only have one //@ entry") },
	{ TEXT("a repeated ID in a reroute run"), TEXT("//@3(1, 2) @3(2, 2)"), nullptr, TEXT("(1:12)"), TEXT("Node ID 3 is used more than once") },
	{ TEXT("junk after a reroute run"), TEXT("//@3(1, 2) @4(2, 2) x"), nullptr, TEXT("(1:21)"), TEXT("Unexpected text after node metadata") },
};
