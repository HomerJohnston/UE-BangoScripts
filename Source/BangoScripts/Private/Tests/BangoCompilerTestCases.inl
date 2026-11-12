#pragma once

// Compiler test cases, shared by the Unreal automation test (BangoCompilerTests.cpp)
// and the stand-alone test harness used during development.

/** The command signatures the compiler tests use. */
static void BangoMakeTestCommands(FBangoCommandTable& Table)
{
	FString Error;
	const FBangoType Bool = FBangoType::Bool();
	const FBangoType Int = FBangoType::Int();
	const FBangoType Double = FBangoType::Double();
	const FBangoType String = FBangoType::String();
	const FBangoType Entity = FBangoType::Entity();

	Table.Add(FBangoCommandSignature::Make(TEXT("Hint")).Param(TEXT("text"), String).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("Collect")).Variadic(), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("Wave")).Optional().Param(TEXT("times"), Int).Param(TEXT("speed"), Double), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("Speak")).Target(Entity).Param(TEXT("text"), String), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("AwaitSpeak")).Optional().Param(TEXT("pause"), Double), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("Sleep")).Optional().Param(TEXT("seconds"), Double), Error); // like the built-in Sleep
	Table.Add(FBangoCommandSignature::Make(TEXT("Random")).Param(TEXT("min"), Double).Param(TEXT("max"), Double).Returns(Double), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("SetDamage")).Target(Entity).Param(TEXT("amount"), Double), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("SetLocked")).Target(Entity).Param(TEXT("locked"), Bool), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("AskChoice")).Param(TEXT("a"), String).Param(TEXT("b"), String).Returns(Int).Discardable(), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("GetName")).Target(Entity).Returns(String), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("QuitGame")), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("Format")).Param(TEXT("text"), String).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")).Returns(String), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("Say")).Param(TEXT("Text"), FBangoType::Text()).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("FormatText")).Param(TEXT("Text"), FBangoType::Text()).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")).Returns(FBangoType::Text()), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("ToText")).Param(TEXT("Value"), String).Returns(FBangoType::Text()), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("Max")).NumberParam(TEXT("A")).NumberParam(TEXT("B")).ReturnsNumber(), Error);
}

struct FBangoCompilerTestCase
{
	const TCHAR* Name;
	const TCHAR* Source;
	/** Expected Bango::Disassemble output. "*" means "must compile, don't check". Null for error cases. */
	const TCHAR* ExpectedListing;
	const TCHAR* ErrorLocation;
	const TCHAR* ErrorContains;
};

static const FBangoCompilerTestCase GBangoCompilerTestCases[] =
{
	// ---- Exact listings ----
	{ TEXT("listing: vectors"),
		TEXT("float _f = 2;\nvector3 _v;\n_v = [1, _f, 3];\n_v.y += _v.x;\nHint(\"{0} {1}\", _v.z, Up().x);\nfunction vector2 Up()\n{\n\treturn [0, 1];\n};"),
		TEXT("function <main>:\n")
		TEXT("  0 PushConst 2\n")
		TEXT("  1 StorePlace _f\n")
		TEXT("  2 PushNil vector3\n")
		TEXT("  3 StorePlace _v\n")
		TEXT("  4 PushConst 1\n")
		TEXT("  5 LoadPlace _f\n")
		TEXT("  6 PushConst 3\n")
		TEXT("  7 MakeVector 3\n")
		TEXT("  8 StorePlace _v\n")
		TEXT("  9 LoadPlace _v.y\n")
		TEXT("  10 LoadPlace _v.x\n")
		TEXT("  11 Add\n")
		TEXT("  12 StorePlace _v.y\n")
		TEXT("  13 PushConst \"{0} {1}\"\n")
		TEXT("  14 LoadPlace _v.z\n")
		TEXT("  15 CallFunction Up 0\n")
		TEXT("  16 GetPart x\n")
		TEXT("  17 CallCommand Hint 3\n")
		TEXT("  18 Halt\n")
		TEXT("function Up:\n")
		TEXT("  0 PushConst 0\n")
		TEXT("  1 PushConst 1\n")
		TEXT("  2 MakeVector 2\n")
		TEXT("  3 ReturnValue\n")
		TEXT("  4 ErrorMissingReturn\n"), nullptr, nullptr },
	{ TEXT("listing: if"),
		TEXT("int _x;\n_x = 1 + 2;\nif (_x > 2)\n{\n\tHint(\"big\");\n};"),
		TEXT("function <main>:\n")
		TEXT("  0 PushNil int\n")
		TEXT("  1 StorePlace _x\n")
		TEXT("  2 PushConst 1\n")
		TEXT("  3 PushConst 2\n")
		TEXT("  4 Add\n")
		TEXT("  5 StorePlace _x\n")
		TEXT("  6 LoadPlace _x\n")
		TEXT("  7 PushConst 2\n")
		TEXT("  8 Greater\n")
		TEXT("  9 JumpIfFalse -> 12\n")
		TEXT("  10 PushConst \"big\"\n")
		TEXT("  11 CallCommand Hint 1\n")
		TEXT("  12 Halt\n"),
		nullptr, nullptr },
	{ TEXT("listing: forEach with continue and break"),
		TEXT("int[] _a = [1, 2];\nforEach (_v in _a)\n{\n\tif (_v == 1)\n\t{\n\t\tcontinue;\n\t};\n\tbreak;\n};"),
		TEXT("function <main>:\n")
		TEXT("  0 PushConst 1\n")
		TEXT("  1 PushConst 2\n")
		TEXT("  2 MakeArray 2 int\n")
		TEXT("  3 StorePlace _a\n")
		TEXT("  4 LoadPlace _a\n")
		TEXT("  5 StorePlace (forEach array)\n")
		TEXT("  6 PushConst 0\n")
		TEXT("  7 StorePlace (forEach position)\n")
		TEXT("  8 ForEachNext (forEach array) _v -> 16\n")
		TEXT("  9 LoadPlace _v\n")
		TEXT("  10 PushConst 1\n")
		TEXT("  11 Equal\n")
		TEXT("  12 JumpIfFalse -> 14\n")
		TEXT("  13 Jump -> 8\n")
		TEXT("  14 Jump -> 16\n")
		TEXT("  15 Jump -> 8\n")
		TEXT("  16 Halt\n"),
		nullptr, nullptr },
	{ TEXT("listing: global, for, compound, &&"),
		TEXT("global int gold;\nbool _b;\ngold = 0;\nfor (int _i = 0; _i < 3; _i++)\n{\n\tgold += _i;\n};\n_b = gold > 1 && IsNil(gold);"),
		TEXT("globals: int gold;\n")
		TEXT("function <main>:\n")
		TEXT("  0 Nop\n")
		TEXT("  1 PushNil bool\n")
		TEXT("  2 StorePlace _b\n")
		TEXT("  3 PushConst 0\n")
		TEXT("  4 StorePlace gold\n")
		TEXT("  5 PushConst 0\n")
		TEXT("  6 StorePlace _i\n")
		TEXT("  7 LoadPlace _i\n")
		TEXT("  8 PushConst 3\n")
		TEXT("  9 Less\n")
		TEXT("  10 JumpIfFalse -> 20\n")
		TEXT("  11 LoadPlace gold\n")
		TEXT("  12 LoadPlace _i\n")
		TEXT("  13 Add\n")
		TEXT("  14 StorePlace gold\n")
		TEXT("  15 LoadPlace _i\n")
		TEXT("  16 PushConst 1\n")
		TEXT("  17 Add\n")
		TEXT("  18 StorePlace _i\n")
		TEXT("  19 Jump -> 7\n")
		TEXT("  20 LoadPlace gold\n")
		TEXT("  21 PushConst 1\n")
		TEXT("  22 Greater\n")
		TEXT("  23 JumpIfFalseOrPop -> 25\n")
		TEXT("  24 IsNilPlace gold\n")
		TEXT("  25 StorePlace _b\n")
		TEXT("  26 Halt\n"),
		nullptr, nullptr },
	{ TEXT("listing: function with conversion"),
		TEXT("float _h;\nfunction float Half(int _n)\n{\n\treturn _n / 2;\n};\n_h = Half(3);"),
		TEXT("function <main>:\n")
		TEXT("  0 PushNil float\n")
		TEXT("  1 StorePlace _h\n")
		TEXT("  2 PushConst 3\n")
		TEXT("  3 CallFunction Half 1\n")
		TEXT("  4 StorePlace _h\n")
		TEXT("  5 Halt\n")
		TEXT("function Half:\n")
		TEXT("  0 LoadPlace _n\n")
		TEXT("  1 PushConst 2\n")
		TEXT("  2 Divide\n")
		TEXT("  3 Convert float\n")
		TEXT("  4 ReturnValue\n")
		TEXT("  5 ErrorMissingReturn\n"),
		nullptr, nullptr },
	{ TEXT("listing: nested element compound assignment"),
		TEXT("int[][] _g = [];\nint _p;\n_g[1][2] += 5;\n_g[0] Push(1);\n_p = _g[0] Pop();\n_g Pop();"),
		TEXT("function <main>:\n")
		TEXT("  0 MakeArray 0 int[]\n")
		TEXT("  1 StorePlace _g\n")
		TEXT("  2 PushNil int\n")
		TEXT("  3 StorePlace _p\n")
		TEXT("  4 PushConst 1\n")
		TEXT("  5 PushConst 2\n")
		TEXT("  6 DupN 2\n")
		TEXT("  7 LoadPlace _g[][]\n")
		TEXT("  8 PushConst 5\n")
		TEXT("  9 Add\n")
		TEXT("  10 StorePlace _g[][]\n")
		TEXT("  11 PushConst 0\n")
		TEXT("  12 PushConst 1\n")
		TEXT("  13 ArrayModify _g[] Push\n")
		TEXT("  14 PushConst 0\n")
		TEXT("  15 ArrayModify _g[] Pop +result\n")
		TEXT("  16 StorePlace _p\n")
		TEXT("  17 ArrayModify _g Pop\n")
		TEXT("  18 Halt\n"),
		nullptr, nullptr },
	{ TEXT("listing: undeclared globals and commands"),
		TEXT("int _g;\nif (gold > 10)\n{\n\tboss Speak(\"rich\");\n};\n_g = gold;"),
		TEXT("function <main>:\n")
		TEXT("  0 PushNil int\n")
		TEXT("  1 StorePlace _g\n")
		TEXT("  2 LoadPlace gold\n")
		TEXT("  3 PushConst 10\n")
		TEXT("  4 Greater\n")
		TEXT("  5 JumpIfFalse -> 10\n")
		TEXT("  6 LoadPlace boss\n")
		TEXT("  7 Convert entity\n")
		TEXT("  8 PushConst \"rich\"\n")
		TEXT("  9 CallCommand Speak 1 +target\n")
		TEXT("  10 LoadPlace gold\n")
		TEXT("  11 StorePlace _g\n")
		TEXT("  12 Halt\n"),
		nullptr, nullptr },

	// ---- Must compile ----
	{ TEXT("spec example"),
		TEXT("global int foundThings;\nglobal bool playerHasGun;\nglobal string[] questLog;\nfunction void Nag()\n{\n\t@john Speak(\"Find a gun.\");    //@20(0, 0)\n\tAwaitSpeak();                  //@21(0, 1)\n};\nif (!IsNil(foundThings) && foundThings >= 10)   //@4(0, 3)\n{\n\tHint(\"You found at least 10 things!\");         //@5(1, 4)\n\tquestLog Push(\"Found 10 things\");              //@6(1, 5)\n};\nfor (int _i = 0; _i < 3; _i++)     //@7(0, 6)\n{\n\tHint(\"Countdown: {0}\", 3 - _i); //@8(1, 7)\n\tSleep(1.0);                    //@9(1, 8)\n};\nlabel Check;                       //@10(0, 9)\nif (IsNil(playerHasGun))      //@11(0, 10)\n{\n\t@john Speak(\"Congrats! You found a bug.\");     //@12(1, 11)\n\tAwaitSpeak();                  //@13(1, 12)\n\treturn;                        //@14(1, 13)\n}\nelse\n{\n\tif (!playerHasGun)             //@15(2, 11)\n\t{\n\t\tNag();                     //@16(3, 12)\n\t\tSleep(Random(3.0, 5.0));   //@17(3, 13)\n\t\tgoto Check;                //@18(3, 14)\n\t};\n};\nforEach (_entry in questLog)       //@19(0, 15)\n{\n\tif (!IsNil(_entry))         //@22(1, 16)\n\t{\n\t\tHint(_entry);              //@23(2, 17)\n\t};\n};\n@john Speak(\"Great, you found a gun.\");  //@24(0, 18)\nAwaitSpeak();                            //@25(0, 19)\n"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("listing: Bango IDs"),
		TEXT("entity[] _c;\n@john Speak(\"hi\");\n_c = [@a, @b];"),
		TEXT("function <main>:\n")
		TEXT("  0 PushNil entity[]\n")
		TEXT("  1 StorePlace _c\n")
		TEXT("  2 PushConst @john\n")
		TEXT("  3 PushConst \"hi\"\n")
		TEXT("  4 CallCommand Speak 1 +target\n")
		TEXT("  5 PushConst @a\n")
		TEXT("  6 PushConst @b\n")
		TEXT("  7 MakeArray 2 entity\n")
		TEXT("  8 StorePlace _c\n")
		TEXT("  9 Halt\n"),
		nullptr, nullptr },
	{ TEXT("nil as an extra value"),
		TEXT("Hint(\"{0} {1}\", 1, nil);\nCollect(nil);"),
		TEXT("function <main>:\n")
		TEXT("  0 PushConst \"{0} {1}\"\n")
		TEXT("  1 PushConst 1\n")
		TEXT("  2 PushNil string\n")
		TEXT("  3 CallCommand Hint 3\n")
		TEXT("  4 PushNil string\n")
		TEXT("  5 CallCommand Collect 1\n")
		TEXT("  6 Halt\n"),
		nullptr, nullptr },
	{ TEXT("Bango IDs in many places"),
		TEXT("#define params (entity _who = @john, entity[] _crowd = [@bob, @alice], entity _other = nil)\nentity _e;\nbool _same;\nstring _n;\n@door17 SetLocked(true);\n_e = @john;\n_same = _e == @john && _who != @if;\nforEach (_x in [@a, @b])\n{\n\t_x Speak(\"hi\");\n};\n_crowd Push(@carol);\n_n = @john GetName();\nHint(\"{0} {1}\", @john, [@a]);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("undeclared globals in many places"),
		TEXT("string _n;\nint _x;\nbool _b;\nfloat _d;\nint _e;\n_n = boss GetName();\n_x = gold + 1;\n_b = gold == 3 || flag;\nHint(\"{0}\", boss);\n_d = -speed;\n_e = list[2];\nif (!IsNil(list[1][2]))\n{\n};"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("nested arrays"),
		TEXT("int[][] _g = [[1, 2], []];\nint[] _row;\nint _c;\nfloat[] _d;\nint _r;\n_g[3][2] = 7;\n_g[0] Push(4);\n_row = _g Pop();\n_c = Count(_g) + Count(_g[0]);\n_d = [1, 2.5];\n_g Pop();\n_g[1] InsertAt(9, 0);\n_r = _g[1] RemoveAt(0);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("inferred array literals"),
		TEXT("int _c;\nHint(\"{0}\", [1, 2.5]);\nHint(\"{0}\", [[], [1]]);\nHint(\"{0}\", [nil, \"a\"]);\n_c = Count([1, 2, 3]);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("recursion"),
		TEXT("int _f;\nfunction int Fact(int _n)\n{\n\tif (_n <= 1)\n\t{\n\t\treturn 1;\n\t};\n\treturn _n * Fact(_n - 1);\n};\n_f = Fact(5);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("goto out of loop and forward"),
		TEXT("label Top;\nwhile (true)\n{\n\tif (true)\n\t{\n\t\tgoto Top;\n\t};\n\tgoto End;\n};\nHint(\"skipped\");\nlabel End;"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("optional parameters"),
		TEXT("AwaitSpeak();\nAwaitSpeak(0.5);\nAwaitSpeak(1);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("else if, nil assignment, string joins"),
		TEXT("int _x = 1;\nstring _s;\n_s = \"a\" + \"b\";\n_s += \"c\";\nif (_x == 1)\n{\n} else if (_x == 2)\n{\n\t_x = nil;\n} else\n{\n};"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("same name in sibling blocks"),
		TEXT("int _t;\nif (true)\n{\n\t_t = 1;\n};\nif (true)\n{\n\t_t = 2;\n};\nfor (int _i = 0; _i < 1; _i++)\n{\n};\nfor (int _i = 0; _i < 1; _i++)\n{\n};"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("function using globals"),
		TEXT("global int gold;\nfunction void Pay(int _amount)\n{\n\tgold -= _amount;\n\tjohn Speak(\"Thanks\");\n};\ngold = 10;\nPay(3);"),
		TEXT("*"), nullptr, nullptr },

	{ TEXT("params used in the script"),
		TEXT("#define params (int _a, string _b = \"x\", float _c = 1, int[] _d = [1, 2])\nHint(\"{0} {1} {2} {3}\", _a, _b, _c, _d);\n_a = 5;"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("listing: params"),
		TEXT("#define params (int _a, float _b = 2, entity _e = nil)\nHint(\"{0}\", _a + _b);"),
		TEXT("params: int _a, float _b = 2.0, entity _e = nil;\n")
		TEXT("function <main>:\n")
		TEXT("  0 Nop\n")
		TEXT("  1 PushConst \"{0}\"\n")
		TEXT("  2 LoadPlace _a\n")
		TEXT("  3 LoadPlace _b\n")
		TEXT("  4 Add\n")
		TEXT("  5 CallCommand Hint 2\n")
		TEXT("  6 Halt\n"), nullptr, nullptr },

	// ---- Errors ----
	{ TEXT("undeclared local"), TEXT("_x = 1;"), nullptr, TEXT("(1:1)"), TEXT("'_x' isn't declared in this script") },
	{ TEXT("float into int"), TEXT("int _x = 2.5;"), nullptr, TEXT("(1:10)"), TEXT("needs an int, but this is a float") },
	{ TEXT("string plus int"), TEXT("string _s;\n_s = \"a\" + 1;"), nullptr, TEXT("(2:10)"), TEXT("join two strings") },
	{ TEXT("condition not bool"), TEXT("if (1)\n{\n};"), nullptr, TEXT("(1:5)"), TEXT("condition must be a bool") },
	{ TEXT("compare with nil"), TEXT("int _x = 1;\nif (_x == nil)\n{\n};"), nullptr, TEXT("(2:8)"), TEXT("Use IsNil") },
	{ TEXT("write undeclared global"), TEXT("gold = 5;"), nullptr, TEXT("(1:1)"), TEXT("isn't declared in this script") },
	{ TEXT("global declared twice"), TEXT("global int a;\nglobal float a;"), nullptr, TEXT("(2:1)"), TEXT("Global 'a' is already declared on line 1") },
	{ TEXT("unknown command"), TEXT("Foo();"), nullptr, TEXT("(1:1)"), TEXT("Unknown command or function 'Foo'") },
	{ TEXT("command with wrong case"), TEXT("hint(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("Did you mean 'Hint'?") },
	{ TEXT("array command with wrong case"), TEXT("int[] _a = [];\n_a push(1);"), nullptr, TEXT("(2:4)"), TEXT("Did you mean 'Push'?") },
	{ TEXT("function with wrong case"), TEXT("function void Greet()\n{\n};\ngreet();"), nullptr, TEXT("(4:1)"), TEXT("Did you mean 'Greet'?") },
	{ TEXT("wrong argument count"), TEXT("Sleep(1.0, 2.0);"), nullptr, TEXT("(1:1)"), TEXT("Wrong number of arguments") },
	{ TEXT("missing target"), TEXT("Speak(\"hi\");"), nullptr, TEXT("(1:1)"), TEXT("needs a target") },
	{ TEXT("unexpected target"), TEXT("@john Hint(\"a\");"), nullptr, TEXT("(1:7)"), TEXT("doesn't take a target") },
	{ TEXT("target type"), TEXT("5 Speak(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("The target of 'Speak' needs an entity, but this is an int") },
	{ TEXT("argument type"), TEXT("@john Speak(5);"), nullptr, TEXT("(1:13)"), TEXT("Argument 1 of 'Speak' needs a string") },
	{ TEXT("Bango ID where a string is needed"), TEXT("string _s = @john;"), nullptr, TEXT("(1:13)"), TEXT("'_s' needs a string, but this is an entity") },
	{ TEXT("Bango ID as a string argument"), TEXT("@john Speak(@bob);"), nullptr, TEXT("(1:13)"), TEXT("Argument 1 of 'Speak' needs a string") },
	{ TEXT("Bango ID arithmetic"), TEXT("Hint(\"{0}\", @john + 1);"), nullptr, TEXT("(1:19)"), TEXT("'+' needs numbers, but this is an entity + an int") },
	{ TEXT("Bango ID with the wrong default type"), TEXT("#define params (string _s = @john)"), nullptr, TEXT("(1:29)"), TEXT("The default for '_s' must be a string") },
	{ TEXT("Bango ID compared with a string"), TEXT("bool _b;\n_b = @john == \"john\";"), nullptr, TEXT("(2:12)"), TEXT("an entity == a string") },
	{ TEXT("indexing a Bango ID"), TEXT("Hint(\"{0}\", @john[0]);"), nullptr, TEXT("(1:18)"), TEXT("Only arrays can be indexed, but this is an entity") },
	{ TEXT("void used as value"), TEXT("int _x;\n_x = QuitGame();"), nullptr, TEXT("(2:6)"), TEXT("doesn't return a value") },
	{ TEXT("goto into block"), TEXT("goto Inside;\nif (true)\n{\n\tlabel Inside;\n};"), nullptr, TEXT("(1:1)"), TEXT("jumps into a block") },
	{ TEXT("goto into sibling block"), TEXT("if (true)\n{\n\tgoto B;\n};\nif (true)\n{\n\tlabel B;\n};"), nullptr, TEXT("(3:2)"), TEXT("jumps into a block") },
	{ TEXT("missing label"), TEXT("goto Nowhere;"), nullptr, TEXT("(1:1)"), TEXT("no label 'Nowhere'") },
	{ TEXT("label in another function"), TEXT("label A;\nfunction void F()\n{\n\tgoto A;\n};"), nullptr, TEXT("(4:2)"), TEXT("no label 'A' in this function") },
	{ TEXT("duplicate label"), TEXT("label A;\nlabel A;"), nullptr, TEXT("(2:1)"), TEXT("already defined on line 1") },
	{ TEXT("break outside loop"), TEXT("break;"), nullptr, TEXT("(1:1)"), TEXT("only be used inside") },
	{ TEXT("continue outside loop"), TEXT("if (true)\n{\n\tcontinue;\n};"), nullptr, TEXT("(3:2)"), TEXT("only be used inside") },
	{ TEXT("top level returns value"), TEXT("return 5;"), nullptr, TEXT("(1:8)"), TEXT("can't return a value") },
	{ TEXT("void function returns value"), TEXT("function void F()\n{\n\treturn 1;\n};"), nullptr, TEXT("(3:9)"), TEXT("is void") },
	{ TEXT("return without value"), TEXT("function int F()\n{\n\treturn;\n};"), nullptr, TEXT("(3:2)"), TEXT("must return an int") },
	{ TEXT("return wrong type"), TEXT("function int F()\n{\n\treturn \"x\";\n};"), nullptr, TEXT("(3:9)"), TEXT("needs an int, but this is a string") },
	{ TEXT("function argument count"), TEXT("function void F(int _a)\n{\n};\nF();"), nullptr, TEXT("(4:1)"), TEXT("takes 1 argument(s), but 0") },
	{ TEXT("function argument type"), TEXT("function void F(int _a)\n{\n};\nF(\"x\");"), nullptr, TEXT("(4:3)"), TEXT("Argument 1 of 'F' needs an int") },
	{ TEXT("function with target"), TEXT("function void F()\n{\n};\njohn F();"), nullptr, TEXT("(4:6)"), TEXT("can't be called with a target") },
	{ TEXT("duplicate function"), TEXT("function void F()\n{\n};\nfunction void F()\n{\n};"), nullptr, TEXT("(4:1)"), TEXT("already a function named 'F'") },
	{ TEXT("function named like a command"), TEXT("function void Hint()\n{\n};"), nullptr, TEXT("(1:1)"), TEXT("already a command") },
	{ TEXT("function named like a built-in"), TEXT("function void Push()\n{\n};"), nullptr, TEXT("(1:1)"), TEXT("built-in") },
	{ TEXT("duplicate parameter"), TEXT("function void F(int _a, int _a)\n{\n};"), nullptr, TEXT("(1:29)"), TEXT("Two parameters") },
	{ TEXT("top-level local inside function"), TEXT("int _x = 1;\nfunction void F()\n{\n\t_x = 2;\n};"), nullptr, TEXT("(4:2)"), TEXT("'_x' isn't declared in this script") },
	{ TEXT("loop variable with a local's name"), TEXT("int _x = 1;\nfor (int _x = 0; _x < 2; _x++)\n{\n\tHint(\"x\");\n};"), nullptr, TEXT("(2:6)"), TEXT("already declared") },
	{ TEXT("computed starting value"), TEXT("int _a = 1;\nint _x = _a + 1;"), nullptr, TEXT("(2:13)"), TEXT("must be a fixed value") },
	{ TEXT("forEach variable shadows"), TEXT("int _x = 1;\nint[] _a = [];\nforEach (_x in _a)\n{\n};"), nullptr, TEXT("(3:1)"), TEXT("already declared") },
	{ TEXT("loop variable after its loop"), TEXT("for (int _i = 0; _i < 2; _i++)\n{\n\tHint(\"x\");\n};\n_i = 2;"), nullptr, TEXT("(5:1)"), TEXT("'_i' isn't declared in this script") },
	{ TEXT("global with a value"), TEXT("global int a = 1;"), nullptr, TEXT("(1:16)"), TEXT("A global declaration can't have a value") },
	{ TEXT("global in a function"), TEXT("function void F()\n{\n\tglobal int g;\n};"), nullptr, TEXT("(3:2)"), TEXT("not in a function") },
	{ TEXT("global after a statement"), TEXT("Hint(\"x\");\nglobal int g;"), nullptr, TEXT("(2:1)"), TEXT("Globals are declared at the top of the file") },
	{ TEXT("global after a function"), TEXT("function void F()\n{\n\tHint(\"x\");\n};\nglobal int g;"), nullptr, TEXT("(5:1)"), TEXT("Globals are declared at the top of the file") },
	{ TEXT("global after a local"), TEXT("int _x;\nglobal int g;"), nullptr, TEXT("(2:1)"), TEXT("come before the script's locals") },
	{ TEXT("local after a statement"), TEXT("Hint(\"x\");\nint _x;"), nullptr, TEXT("(2:1)"), TEXT("Locals are declared at the top of their script") },
	{ TEXT("local in a block"), TEXT("if (true)\n{\n\tint _x;\n};"), nullptr, TEXT("(3:2)"), TEXT("Locals are declared at the top of their script") },
	{ TEXT("function local after a statement"), TEXT("function void F()\n{\n\tHint(\"x\");\n\tint _x;\n};"), nullptr, TEXT("(4:2)"), TEXT("Locals are declared at the top of their function") },
	{ TEXT("declaration with metadata"), TEXT("int _x; //@3(0, 0)"), nullptr, TEXT("(1:1)"), TEXT("can't have node metadata") },
	{ TEXT("call as a starting value"), TEXT("float _t = Random(1.0, 2.0);"), nullptr, TEXT("(1:12)"), TEXT("Declare it as 'float _t;' and set it in the code") },
	{ TEXT("too many indexes"), TEXT("int[] _a = [1];\nint _b;\n_b = _a[0][0];"), nullptr, TEXT("(3:6)"), TEXT("indexed at most 1") },
	{ TEXT("index a non-array"), TEXT("int _a = 1;\nint _b;\n_b = _a[0];"), nullptr, TEXT("(3:6)"), TEXT("not an array") },
	{ TEXT("index not an int"), TEXT("int[] _a = [1];\nint _b;\n_b = _a[\"x\"];"), nullptr, TEXT("(3:9)"), TEXT("index must be an int") },
	{ TEXT("array element type"), TEXT("int[] _a = [1, \"x\"];"), nullptr, TEXT("(1:16)"), TEXT("needs an int, but this is a string") },
	{ TEXT("array mixed types inferred"), TEXT("Hint(\"{0}\", [1, \"x\"]);"), nullptr, TEXT("(1:13)"), TEXT("mixes an int and a string") },
	{ TEXT("empty array without a type"), TEXT("int _c;\n_c = Count([]);"), nullptr, TEXT("(2:12)"), TEXT("can't be worked out") },
	{ TEXT("array literal where scalar expected"), TEXT("int _x = [1];"), nullptr, TEXT("(1:10)"), TEXT("Expected an int here, but this is an array") },
	{ TEXT("++ on float"), TEXT("float _d = 1.0;\n_d++;"), nullptr, TEXT("(2:1)"), TEXT("only works on an int") },
	{ TEXT("compound result type"), TEXT("int _i = 0;\n_i += 1.5;"), nullptr, TEXT("(2:1)"), TEXT("stored in '_i' needs an int, but this is a float") },
	{ TEXT("Push wrong type"), TEXT("int[] _a = [];\n_a Push(\"x\");"), nullptr, TEXT("(2:9)"), TEXT("Push on '_a' needs an int") },
	{ TEXT("Push on non-array"), TEXT("int _a = 0;\n_a Push(1);"), nullptr, TEXT("(2:1)"), TEXT("needs an array as its target") },
	{ TEXT("Push on undeclared global"), TEXT("questLog Push(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("isn't declared in this script") },
	{ TEXT("Push on a value"), TEXT("[1] Push(2);"), nullptr, TEXT("(1:1)"), TEXT("must be a variable") },
	{ TEXT("Pop with arguments"), TEXT("int[] _a = [];\n_a Pop(1);"), nullptr, TEXT("(2:4)"), TEXT("0 argument(s)") },
	{ TEXT("Count wrong"), TEXT("int _c;\n_c = Count(5);"), nullptr, TEXT("(2:12)"), TEXT("Count needs an array") },
	{ TEXT("forEach over undeclared global"), TEXT("forEach (_x in questLog)\n{\n};"), nullptr, TEXT("(1:16)"), TEXT("declare it first") },
	{ TEXT("forEach over non-array"), TEXT("forEach (_x in 5)\n{\n};"), nullptr, TEXT("(1:16)"), TEXT("loops over an array") },
	{ TEXT("nil without a type"), TEXT("bool _b;\n_b = IsNil(nil);"), nullptr, TEXT("(2:"), TEXT("IsNil needs") },
	{ TEXT("array of only nils"), TEXT("Hint(\"{0}\", [nil]);"), nullptr, TEXT("(1:13)"), TEXT("can't be worked out") },
	{ TEXT("comparing different types"), TEXT("bool _b;\n_b = \"a\" == 1;"), nullptr, TEXT("(2:10)"), TEXT("same type") },
	{ TEXT("arrays with operators"), TEXT("int[] _a = [];\nbool _b;\n_b = _a == _a;"), nullptr, TEXT("(3:9)"), TEXT("Arrays can't be used") },
	{ TEXT("negate a string"), TEXT("string _s;\n_s = -\"a\";"), nullptr, TEXT("(2:6)"), TEXT("needs a number") },
	{ TEXT("&& on a non-bool"), TEXT("bool _b;\n_b = 1 && true;"), nullptr, TEXT("(2:6)"), TEXT("needs bools") },
	{ TEXT("! on a non-bool"), TEXT("bool _b;\n_b = !1;"), nullptr, TEXT("(2:6)"), TEXT("needs a bool") },
	{ TEXT("less-than on strings"), TEXT("bool _b;\n_b = \"a\" < \"b\";"), nullptr, TEXT("(2:10)"), TEXT("compares numbers") },
	{ TEXT("params default of the wrong type"), TEXT("#define params (int _a = \"x\")"), nullptr, TEXT("(1:26)"), TEXT("The default for '_a' must be an int, but it is a string") },
	{ TEXT("params array default of the wrong type"), TEXT("#define params (int[] _a = [1, 2.5])"), nullptr, TEXT("(1:32)"), TEXT("The default for '_a' must be an int, but it is a float") },
	{ TEXT("params array default for a scalar"), TEXT("#define params (int _a = [1])"), nullptr, TEXT("(1:26)"), TEXT("but it is an array") },
	{ TEXT("params duplicate name"), TEXT("#define params (int _a, string _a)"), nullptr, TEXT("(1:32)"), TEXT("Two parameters are named '_a'") },
	{ TEXT("params shadowed by a local"), TEXT("#define params (int _a)\nint _a = 1;"), nullptr, TEXT("(2:1)"), TEXT("already declared") },
	{ TEXT("params not visible in functions"), TEXT("#define params (int _a)\nfunction void F()\n{\n\t_a = 1;\n};"), nullptr, TEXT("(4:2)"), TEXT("'_a' isn't declared in this script") },   // (a read there is of a global _a),
	{ TEXT("error carries node id"), TEXT("int _x;\n_x = \"no\"; //@42(0, 0)"), nullptr, TEXT("[node 42]"), TEXT("needs an int") },
	{ TEXT("fixed starting values"), TEXT("int[] _a = [1, -2];\nvector3 _v = [0, 0, 1];\nentity _e = @john;\nbool _b = true;\nstring _s = nil;\nfloat _f = -2.5;\nHint(\"{0} {1} {2} {3} {4}\", _a, _v, _e, _b, _f);"), TEXT("*"), nullptr, nullptr },

	// ---- Results that must be used ----
	{ TEXT("no-discard command on its own line"), TEXT("Random(1.0, 2.0);"), nullptr, TEXT("(1:1)"), TEXT("'Random' gives a value that must be used") },
	{ TEXT("no-discard targeted command on its own line"), TEXT("@john GetName();"), nullptr, TEXT("(1:7)"), TEXT("'GetName' gives a value that must be used") },
	{ TEXT("Count on its own line"), TEXT("int[] _a = [];\nCount(_a);"), nullptr, TEXT("(2:1)"), TEXT("'Count' gives a value that must be used") },
	{ TEXT("no-discard results used anywhere a value goes"),
		TEXT("float _t;\nint[] _a;\nint _n;\n_t = Random(1.0, 2.0);\n_t = Random(0.0, 1.0) * 2;\nif (Random(0.0, 1.0) < 0.5)\n{\n};\nHint(\"{0}\", Random(1.0, 2.0));\n_a = [];\n_n = Count(_a) + 1;"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("discardable command on its own line"), TEXT("AskChoice(\"A\", \"B\");"), TEXT("*"), nullptr, nullptr },
	{ TEXT("Pop and RemoveAt can be thrown away"), TEXT("int[] _a = [1, 2];\n_a Pop();\n_a RemoveAt(0);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("script functions can be thrown away"), TEXT("function int F()\n{\n\treturn 1;\n};\nF();"), TEXT("*"), nullptr, nullptr },

	// ---- Localized text ----
	{ TEXT("text literals and LOCTEXT_NAMESPACE"),
		TEXT("#define params (text _line = nil)\n#define LOCTEXT_NAMESPACE \"Bango.Intro\"\nglobal text gt;\ntext _a = LOCTEXT(\"Greet\", \"Hello\");\ntext[] _b;\nbool _same;\ntext _f;\n_b = [_a, NSLOCTEXT(\"Other\", \"Greet\", \"x\"), ToText(\"Bob\")];\nSay(_a);\nSay(LOCTEXT(\"Greet\", \"Hello\"));\n_same = _a == _line;\n_f = FormatText(LOCTEXT(\"Count\", \"{1} has {0}\"), 3, _a);\ngt = nil;\nfunction text F(text _t)\n{\n\treturn _t;\n};\n#undef LOCTEXT_NAMESPACE"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("LOCTEXT needs LOCTEXT_NAMESPACE"), TEXT("Say(LOCTEXT(\"Key\", \"Hi\"));"), nullptr, TEXT("(1:5)"), TEXT("needs the script's namespace: add the line #define LOCTEXT_NAMESPACE") },
	{ TEXT("NSLOCTEXT doesn't need LOCTEXT_NAMESPACE"), TEXT("Say(NSLOCTEXT(\"Ns\", \"Key\", \"Hi\"));\nSay(NSNOTELOCTEXT(\"Ns\", \"Other\", \"Hi\", \"Notes\"));"), TEXT("*"), nullptr, nullptr },
	{ TEXT("a key used for different texts"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"Key\", \"Hi\"));\nSay(NSLOCTEXT(\"N\", \"Key\", \"Bye\"));\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(3:20)"), TEXT("The key 'Key' is already used in namespace 'N' for a different text, on line 2") },
	{ TEXT("a key used with different notes"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"Key\", \"Hi\"));\nSay(NOTELOCTEXT(\"Key\", \"Hi\", \"Whispered\"));\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(3:17)"), TEXT("already used") },
	{ TEXT("the same key in two namespaces"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"Key\", \"Hi\"));\nSay(NSLOCTEXT(\"M\", \"Key\", \"Bye\"));\n#undef LOCTEXT_NAMESPACE"), TEXT("*"), nullptr, nullptr },
	{ TEXT("a string isn't text"), TEXT("Say(\"Hi\");"), nullptr, TEXT("(1:5)"), TEXT("text") },
	{ TEXT("text isn't a string"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\nstring _s = LOCTEXT(\"Key\", \"Hi\");\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(2:13)"), TEXT("string") },
	{ TEXT("texts can't be joined"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\ntext _a;\n_a = LOCTEXT(\"A\", \"a\") + LOCTEXT(\"B\", \"b\");\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(3:24)"), TEXT("Texts can't be joined with '+'") },
	{ TEXT("texts can't be ordered"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\nbool _b;\n_b = LOCTEXT(\"A\", \"a\") < LOCTEXT(\"B\", \"b\");\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(3:24)"), TEXT("compares numbers") },
	{ TEXT("text and string can't be compared"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\nbool _b;\n_b = LOCTEXT(\"A\", \"a\") == \"a\";\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(3:24)"), TEXT("same type") },
	{ TEXT("a text placeholder without a value"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"Key\", \"{0} and {1}\"), 1);\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(2:20)"), TEXT("This text uses {1}, but only 1 value comes after it") },
	{ TEXT("a named text placeholder"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\ntext _t;\n_t = FormatText(LOCTEXT(\"Key\", \"{Name}\"), 1);\n#undef LOCTEXT_NAMESPACE"), nullptr, TEXT("(3:32)"), TEXT("uses {Name}. Text placeholders are numbered") },
	{ TEXT("text placeholders: escapes, %, and plural forms"), TEXT("#define LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"A\", \"`{0`} 100% {0}|plural(one=item,other=items)\"), 1);\nSay(LOCTEXT(\"B\", \"%0 is fine here\"));\n#undef LOCTEXT_NAMESPACE"), TEXT("*"), nullptr, nullptr },

	// ---- Placeholders in a literal text need values ----
	{ TEXT("placeholder without a value"), TEXT("Hint(\"{0} and {1}\", 1);"), nullptr, TEXT("(1:6)"), TEXT("This text uses {1}, but only 1 value comes after it") },
	{ TEXT("placeholder with no values at all"), TEXT("Hint(\"only {2}\");"), nullptr, TEXT("(1:6)"), TEXT("uses {2}, but no values come after it") },
	{ TEXT("placeholder inside an expression"), TEXT("string _s = \"a\";\nHint(\"{0}\", Format(\"{0} {1}\", 1, 2) + Format(\"{3}\", 1, 2));"), nullptr, TEXT("(2:46)"), TEXT("uses {3}, but only 2 values come after it") },
	{ TEXT("placeholders filled, nil included"), TEXT("string _t;\nHint(\"{0} {1} `{\", 1, nil);\nHint(\"100% sure\");\n_t = \"{5}\";\nHint(_t);"), TEXT("*"), nullptr, nullptr },
	// ---- Vectors ----
	{ TEXT("vector3 from 3 numbers"), TEXT("vector3 _vec = [1, 2, 3];\nvector3 _v1;\nvector3 _v2 = [0.5, 0, -1];\nvector3[] _path;\n_v1 = _vec;\n_vec = _v1 + _v2;\n_path = [[0, 0, 0], _vec];"), TEXT("*"), nullptr, nullptr },
	{ TEXT("vector3 from 2 numbers"), TEXT("vector3 _vec = [1, 2];"), nullptr, TEXT("(1:16)"), TEXT("A vector3 needs exactly 3 numbers, like [1, 2, 3], but this has 2.") },
	{ TEXT("vector3 from 4 numbers"), TEXT("vector3 _vec = [1, 2, 3, 4];"), nullptr, TEXT("(1:16)"), TEXT("A vector3 needs exactly 3 numbers, like [1, 2, 3], but this has 4.") },
	{ TEXT("vector2 from 3 numbers"), TEXT("vector2 _vec = [1, 2, 3];"), nullptr, TEXT("(1:16)"), TEXT("A vector2 needs exactly 2 numbers, like [1, 2]") },
	{ TEXT("vector from an array variable"), TEXT("float[] _a = [1, 2, 3];\nvector3 _v;\n_v = _a;"), nullptr, TEXT("(3:6)"), TEXT("'_v' needs a vector3, but this is a float[].") },
	{ TEXT("vector part that isn't a number"), TEXT("vector3 _v = [1, \"a\", 3];"), nullptr, TEXT("(1:18)"), TEXT("The y of a vector3 needs a float, but this is a string.") },
	{ TEXT("nil vector part"), TEXT("vector3 _v = [1, nil, 3];"), nullptr, TEXT("(1:18)"), TEXT("A vector's parts can't be nil") },
	{ TEXT("assigning nil to a vector part"), TEXT("vector3 _v = [1, 2, 3];\n_v.x = nil;"), nullptr, TEXT("(2:8)"), TEXT("A vector's parts can't be nil") },
	{ TEXT("vector2 has no z"), TEXT("vector2 _w = [1, 2];\nfloat _z;\n_z = _w.z;"), nullptr, TEXT("(3:9)"), TEXT("A vector2 only has x and y.") },
	{ TEXT("parts of a float"), TEXT("float _f = 1;\nfloat _g;\n_g = _f.x;"), nullptr, TEXT("(3:9)"), TEXT("'_f' is a float, which has no parts.") },
	{ TEXT("adding vectors of different kinds"), TEXT("vector3 _v = [1, 2, 3];\nvector2 _w = [1, 2];\nHint(\"{0}\", _v + _w);"), nullptr, TEXT("(3:16)"), TEXT("'+' works on two vectors of the same kind, but this is a vector3 + a vector2.") },
	{ TEXT("adding a number to a vector"), TEXT("vector3 _v = [1, 2, 3];\nHint(\"{0}\", _v + 1);"), nullptr, TEXT("(2:16)"), TEXT("use a vector, like _v + [1, 1, 1]") },
	{ TEXT("multiplying two vectors"), TEXT("vector3 _v = [1, 2, 3];\nHint(\"{0}\", _v * _v);"), nullptr, TEXT("(2:16)"), TEXT("Two vectors can't be multiplied with '*'.") },
	{ TEXT("dividing by a vector"), TEXT("vector3 _v = [1, 2, 3];\nHint(\"{0}\", 1 / _v);"), nullptr, TEXT("(2:15)"), TEXT("nothing can be divided by a vector") },
	{ TEXT("comparing vectors with <"), TEXT("vector3 _v = [1, 2, 3];\nHint(\"{0}\", _v < _v);"), nullptr, TEXT("(2:16)"), TEXT("Vectors can't be compared with '<'.") },
	{ TEXT("comparing a vector with a number"), TEXT("vector3 _v = [1, 2, 3];\nHint(\"{0}\", _v == 1);"), nullptr, TEXT("(2:16)"), TEXT("'==' compares two values of the same type, but this is a vector3 == an int.") },
	{ TEXT("++ on a vector part"), TEXT("vector3 _v = [1, 2, 3];\n_v.x++;"), nullptr, TEXT("(2:1)"), TEXT("'++' only works on an int, but '_v.x' is a float.") },
	// Empty values (from new graph nodes) must be filled in.
	{ TEXT("empty if condition"), TEXT("if ()\n{\n};"), nullptr, TEXT("(1:5)"), TEXT("This value is missing. Fill it in.") },
	{ TEXT("empty assignment value"), TEXT("int _x;\n_x = ;"), nullptr, TEXT("(2:6)"), TEXT("This value is missing. Fill it in.") },
	{ TEXT("empty assignment variable"), TEXT("= 5;"), nullptr, TEXT("(1:1)"), TEXT("The variable is missing. Fill it in.") },
	{ TEXT("empty ++ variable"), TEXT("++;"), nullptr, TEXT("(1:1)"), TEXT("The variable is missing. Fill it in.") },
	{ TEXT("empty forEach array"), TEXT("forEach (_e in )\n{\n};"), nullptr, TEXT("(1:16)"), TEXT("This value is missing. Fill it in.") },
	{ TEXT("empty for bounds"), TEXT("for (int _i = ; _i <= ; _i++)\n{\n};"), nullptr, TEXT("(1:15)"), TEXT("This value is missing. Fill it in.") },
	{ TEXT("empty label"), TEXT("label ;"), nullptr, TEXT("(1:7)"), TEXT("This label has no name. Fill it in.") },
	{ TEXT("empty goto"), TEXT("goto ;"), nullptr, TEXT("(1:6)"), TEXT("This goto doesn't say which label to jump to. Fill it in.") },
	{ TEXT("vector default of the wrong size"), TEXT("#define params (vector3 _p = [1, 2])"), nullptr, TEXT("(1:30)"), TEXT("A vector3 needs exactly 3 numbers") },

	// ---- Number commands (NumberParam, ReturnsNumber): an int for ints, otherwise a float ----
	{ TEXT("number: ints give an int"), TEXT("int _a = 1;\nint _b;\n_b = Max(_a, 2);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("number: a float gives a float"), TEXT("int _a = 1;\nfloat _f;\n_f = Max(_a, 2.5);\n_f = Max(1, 2);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("number: a float result into an int"), TEXT("int _a = 1;\nint _b;\n_b = Max(_a, 2.5);"), nullptr, TEXT("(3:6)"), TEXT("float") },
	{ TEXT("number: nested"), TEXT("int _b;\n_b = Max(Max(1, 2), 3);\n_b = Max(1, 2) + 3;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("number: not a number"), TEXT("int _b;\n_b = Max(1, \"two\");"), nullptr, TEXT("(2:13)"), TEXT("Argument 2 of 'Max' needs a number (an int or a float), but this is a string.") },
	{ TEXT("number: nil"), TEXT("int _b;\n_b = Max(1, nil);"), nullptr, TEXT("(2:13)"), TEXT("nil can only be assigned, passed or returned where its type is known.") },
	{ TEXT("number: an array"), TEXT("int _b;\n_b = Max([1], 2);"), nullptr, TEXT("(2:10)"), TEXT("needs a number (an int or a float), but this is an int[].") },
	{ TEXT("number: an undeclared global is allowed"), TEXT("int _b;\n_b = Max(someGlobal, 2);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("number: must be used"), TEXT("Max(1, 2);"), nullptr, TEXT("(1:1)"), TEXT("'Max' gives a value that must be used") },
	{ TEXT("number: signature in errors"), TEXT("int _b;\n_b = Max(1);"), nullptr, TEXT("(2:6)"), TEXT("Max(number A, number B) -> number") },
};
