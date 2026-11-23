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

	Table.Add(FBangoCommandSignature::Make(TEXT("Talk")).Param(TEXT("text"), String).WithPlaceholders().Variadic(TEXT("{"), 0, TEXT("}")), Error);
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
	// Type parameters: a number command like the built-in Max, and Lerp over floats and vectors.
	const FBangoTypeVar T(TEXT("T"));
	Table.Add(FBangoCommandSignature::Make(TEXT("Max")).TypeParam(T, { Int, Double }, TEXT("number")).Param(TEXT("A"), T).Param(TEXT("B"), T).Returns(T).Pure(), Error);
	Table.Add(FBangoCommandSignature::Make(TEXT("Lerp")).TypeParam(T, { Double, FBangoType::Vector2(), FBangoType::Vector3() })
		.Param(TEXT("A"), T).Param(TEXT("B"), T).Param(TEXT("Alpha"), Double).Returns(T).Pure(), Error);
	Bango::RegisterCoreCommands(Table, Error); // Count, and the array commands
	// Changes its target in place, like the array commands: adds one to an int.
	Table.Add(FBangoCommandSignature::Make(TEXT("Bump")).Target(Int).MutableTarget(), Error);
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
		TEXT("float _f = 2;\nvector3 _v;\n_v = [1, _f, 3];\n_v.y += _v.x;\nTalk(\"{0} {1}\", _v.z, Up().x);\nfunction vector2 Up()\n{\n\treturn [0, 1];\n};"),
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
		TEXT("  17 CallCommand Talk 3\n")
		TEXT("  18 Halt\n")
		TEXT("function Up:\n")
		TEXT("  0 PushConst 0\n")
		TEXT("  1 PushConst 1\n")
		TEXT("  2 MakeVector 2\n")
		TEXT("  3 ReturnValue\n")
		TEXT("  4 ErrorMissingReturn\n"), nullptr, nullptr },
	{ TEXT("listing: if"),
		TEXT("int _x;\n_x = 1 + 2;\nif (_x > 2)\n{\n\tTalk(\"big\");\n};"),
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
		TEXT("  11 CallCommand Talk 1\n")
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
		TEXT("global int gold;\nbool _b;\ngold = 0;\nfor (int _i = 0; _i < 3; _i++)\n{\n\tgold += _i;\n};\n_b = gold > 1 && isNil(gold);"),
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
		TEXT("  20 TryUnknown -> 26\n")
		TEXT("  21 LoadPlace gold\n")
		TEXT("  22 PushConst 1\n")
		TEXT("  23 Greater\n")
		TEXT("  24 EndTryUnknown\n")
		TEXT("  25 JumpIfFalseOrPop -> 28\n")
		TEXT("  26 IsNilPlace gold\n")
		TEXT("  27 ResolveUnknown 26 &&\n")
		TEXT("  28 StorePlace _b\n")
		TEXT("  29 Halt\n"),
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
		TEXT("  13 CallCommandInPlace _g[] Push 1\n")
		TEXT("  14 PushConst 0\n")
		TEXT("  15 CallCommandInPlace _g[] Pop 0 +result\n")
		TEXT("  16 StorePlace _p\n")
		TEXT("  17 CallCommandInPlace _g Pop 0\n")
		TEXT("  18 Pop\n")
		TEXT("  19 Halt\n"),
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
		TEXT("global int foundThings;\nglobal bool playerHasGun;\nglobal string[] questLog;\nfunction void Nag()\n{\n\t@john Speak(\"Find a gun.\");    //@20(0, 0)\n\tAwaitSpeak();                  //@21(0, 1)\n};\nif (!isNil(foundThings) && foundThings >= 10)   //@4(0, 3)\n{\n\tTalk(\"You found at least 10 things!\");         //@5(1, 4)\n\tquestLog Push(\"Found 10 things\");              //@6(1, 5)\n};\nfor (int _i = 0; _i < 3; _i++)     //@7(0, 6)\n{\n\tTalk(\"Countdown: {0}\", 3 - _i); //@8(1, 7)\n\tSleep(1.0);                    //@9(1, 8)\n};\nlabel Check;                       //@10(0, 9)\nif (isNil(playerHasGun))      //@11(0, 10)\n{\n\t@john Speak(\"Congrats! You found a bug.\");     //@12(1, 11)\n\tAwaitSpeak();                  //@13(1, 12)\n\treturn;                        //@14(1, 13)\n}\nelse\n{\n\tif (!playerHasGun)             //@15(2, 11)\n\t{\n\t\tNag();                     //@16(3, 12)\n\t\tSleep(Random(3.0, 5.0));   //@17(3, 13)\n\t\tgoto Check;                //@18(3, 14)\n\t};\n};\nforEach (_entry in questLog)       //@19(0, 15)\n{\n\tif (!isNil(_entry))         //@22(1, 16)\n\t{\n\t\tTalk(_entry);              //@23(2, 17)\n\t};\n};\n@john Speak(\"Great, you found a gun.\");  //@24(0, 18)\nAwaitSpeak();                            //@25(0, 19)\n"),
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
		TEXT("Talk(\"{0} {1}\", 1, nil);\nCollect(nil);"),
		TEXT("function <main>:\n")
		TEXT("  0 PushConst \"{0} {1}\"\n")
		TEXT("  1 PushConst 1\n")
		TEXT("  2 PushNil string\n")
		TEXT("  3 CallCommand Talk 3\n")
		TEXT("  4 PushNil string\n")
		TEXT("  5 CallCommand Collect 1\n")
		TEXT("  6 Halt\n"),
		nullptr, nullptr },
	{ TEXT("Bango IDs in many places"),
		TEXT("#inputs (entity _who = @john, entity[] _crowd = [@bob, @alice], entity _other = nil)\nentity _e;\nbool _same;\nstring _n;\n@door17 SetLocked(true);\n_e = @john;\n_same = _e == @john && _who != @if;\nforEach (_x in [@a, @b])\n{\n\t_x Speak(\"hi\");\n};\n_crowd Push(@carol);\n_n = @john GetName();\nTalk(\"{0} {1}\", @john, [@a]);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("undeclared globals in many places"),
		TEXT("string _n;\nint _x;\nbool _b;\nfloat _d;\nint _e;\n_n = boss GetName();\n_x = gold + 1;\n_b = gold == 3 || flag;\nTalk(\"{0}\", boss);\n_d = -speed;\n_e = list[2];\nif (!isNil(list[1][2]))\n{\n};"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("nested arrays"),
		TEXT("int[][] _g = [[1, 2], []];\nint[] _row;\nint _c;\nfloat[] _d;\nint _r;\n_g[3][2] = 7;\n_g[0] Push(4);\n_row = _g Pop();\n_c = Count(_g) + Count(_g[0]);\n_d = [1, 2.5];\n_g Pop();\n_g[1] InsertAt(9, 0);\n_r = _g[1] RemoveAt(0);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("inferred array literals"),
		TEXT("int _c;\nTalk(\"{0}\", [1, 2.5]);\nTalk(\"{0}\", [[], [1]]);\nTalk(\"{0}\", [nil, \"a\"]);\n_c = Count([1, 2, 3]);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("recursion"),
		TEXT("int _f;\nfunction int Fact(int _n)\n{\n\tif (_n <= 1)\n\t{\n\t\treturn 1;\n\t};\n\treturn _n * Fact(_n - 1);\n};\n_f = Fact(5);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("goto a disabled label"), TEXT("/* label Top; */\ngoto Top;"), nullptr, TEXT("(2:1)"), TEXT("Label 'Top' (line 1) is disabled") },
	{ TEXT("goto a label in a disabled block"), TEXT("/*\nwhile (true)\n{\n\tlabel In;\n};\n*/\ngoto In;"), nullptr, TEXT("(7:1)"), TEXT("is disabled") },
	{ TEXT("a disabled goto"), TEXT("/* goto Nowhere; */\nTalk(\"x\");"), TEXT("*"), nullptr, nullptr },
	{ TEXT("goto out of loop and forward"),
		TEXT("label Top;\nwhile (true)\n{\n\tif (true)\n\t{\n\t\tgoto Top;\n\t};\n\tgoto End;\n};\nTalk(\"skipped\");\nlabel End;"),
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

	{ TEXT("inputs used in the script"),
		TEXT("#inputs (int _a, string _b = \"x\", float _c = 1, int[] _d = [1, 2])\nTalk(\"{0} {1} {2} {3}\", _a, _b, _c, _d);\n_a = 5;"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("listing: inputs"),
		TEXT("#inputs (int _a, float _b = 2, entity _e = nil)\nTalk(\"{0}\", _a + _b);"),
		TEXT("inputs: int _a, float _b = 2.0, entity _e = nil;\n")
		TEXT("function <main>:\n")
		TEXT("  0 Nop\n")
		TEXT("  1 PushConst \"{0}\"\n")
		TEXT("  2 LoadPlace _a\n")
		TEXT("  3 LoadPlace _b\n")
		TEXT("  4 Add\n")
		TEXT("  5 CallCommand Talk 2\n")
		TEXT("  6 Halt\n"), nullptr, nullptr },

	// ---- Errors ----
	{ TEXT("undeclared local"), TEXT("_x = 1;"), nullptr, TEXT("(1:1)"), TEXT("'_x' isn't declared in this script") },
	{ TEXT("float into int"), TEXT("int _x = 2.5;"), nullptr, TEXT("(1:10)"), TEXT("needs an int, but this is a float") },
	{ TEXT("string plus int"), TEXT("string _s;\n_s = \"a\" + 1;"), nullptr, TEXT("(2:10)"), TEXT("join two strings") },
	{ TEXT("condition not bool"), TEXT("if (1)\n{\n};"), nullptr, TEXT("(1:5)"), TEXT("condition must be a bool") },
	{ TEXT("compare with nil"), TEXT("int _x = 1;\nif (_x == nil)\n{\n};"), nullptr, TEXT("(2:8)"), TEXT("Use isNil") },
	{ TEXT("write undeclared global"), TEXT("gold = 5;"), nullptr, TEXT("(1:1)"), TEXT("isn't declared in this script") },
	{ TEXT("global declared twice"), TEXT("global int a;\nglobal float a;"), nullptr, TEXT("(2:1)"), TEXT("Global 'a' is already declared on line 1") },
	{ TEXT("unknown command"), TEXT("Foo();"), nullptr, TEXT("(1:1)"), TEXT("Unknown command or function 'Foo'") },
	{ TEXT("command with wrong case"), TEXT("talk(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("Did you mean 'Talk'?") },
	{ TEXT("array command with wrong case"), TEXT("int[] _a = [];\n_a push(1);"), nullptr, TEXT("(2:4)"), TEXT("Did you mean 'Push'?") },
	{ TEXT("function with wrong case"), TEXT("function void Greet()\n{\n};\ngreet();"), nullptr, TEXT("(4:1)"), TEXT("Did you mean 'Greet'?") },
	{ TEXT("wrong argument count"), TEXT("Sleep(1.0, 2.0);"), nullptr, TEXT("(1:1)"), TEXT("Wrong number of arguments") },
	{ TEXT("missing target"), TEXT("Speak(\"hi\");"), nullptr, TEXT("(1:1)"), TEXT("needs a target") },
	{ TEXT("unexpected target"), TEXT("@john Talk(\"a\");"), nullptr, TEXT("(1:7)"), TEXT("doesn't take a target") },
	{ TEXT("target type"), TEXT("5 Speak(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("The target of 'Speak' needs an entity, but this is an int") },
	{ TEXT("argument type"), TEXT("@john Speak(5);"), nullptr, TEXT("(1:13)"), TEXT("Argument 1 of 'Speak' needs a string") },
	{ TEXT("Bango ID where a string is needed"), TEXT("string _s = @john;"), nullptr, TEXT("(1:13)"), TEXT("'_s' needs a string, but this is an entity") },
	{ TEXT("Bango ID as a string argument"), TEXT("@john Speak(@bob);"), nullptr, TEXT("(1:13)"), TEXT("Argument 1 of 'Speak' needs a string") },
	{ TEXT("Bango ID arithmetic"), TEXT("Talk(\"{0}\", @john + 1);"), nullptr, TEXT("(1:19)"), TEXT("'+' needs numbers, but this is an entity + an int") },
	{ TEXT("Bango ID with the wrong default type"), TEXT("#inputs (string _s = @john)"), nullptr, TEXT("(1:22)"), TEXT("The default for '_s' must be a string") },
	{ TEXT("Bango ID compared with a string"), TEXT("bool _b;\n_b = @john == \"john\";"), nullptr, TEXT("(2:12)"), TEXT("an entity == a string") },
	{ TEXT("indexing a Bango ID"), TEXT("Talk(\"{0}\", @john[0]);"), nullptr, TEXT("(1:18)"), TEXT("Only arrays, vectors and strings can be indexed, but this is an entity") },
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
	{ TEXT("function named like a command"), TEXT("function void Talk()\n{\n};"), nullptr, TEXT("(1:1)"), TEXT("already a command") },
	{ TEXT("function named like a built-in"), TEXT("function void Push()\n{\n};"), nullptr, TEXT("(1:1)"), TEXT("'Push' is already a command") },
	{ TEXT("duplicate parameter"), TEXT("function void F(int _a, int _a)\n{\n};"), nullptr, TEXT("(1:29)"), TEXT("Two parameters") },
	{ TEXT("top-level local inside function"), TEXT("int _x = 1;\nfunction void F()\n{\n\t_x = 2;\n};"), nullptr, TEXT("(4:2)"), TEXT("'_x' isn't declared in this script") },
	{ TEXT("loop variable with a local's name"), TEXT("int _x = 1;\nfor (int _x = 0; _x < 2; _x++)\n{\n\tTalk(\"x\");\n};"), nullptr, TEXT("(2:6)"), TEXT("already declared") },
	{ TEXT("computed starting value"), TEXT("scriptVar int _a = 1;\nscriptVar int _x = _a + 1;"), nullptr, TEXT("(2:23)"), TEXT("must be a fixed value") },
	{ TEXT("a local's computed value"), TEXT("int _a = 1;\nint _x = _a + 1;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("forEach variable shadows"), TEXT("int _x = 1;\nint[] _a = [];\nforEach (_x in _a)\n{\n};"), nullptr, TEXT("(3:1)"), TEXT("already declared") },
	{ TEXT("loop variable after its loop"), TEXT("for (int _i = 0; _i < 2; _i++)\n{\n\tTalk(\"x\");\n};\n_i = 2;"), nullptr, TEXT("(5:1)"), TEXT("'_i' is the variable of the loop on line 1") },
	// `target call Name(args)`: the Blueprint function is found when it runs; its result's type is known then.
	{ TEXT("call: a statement and a result"), TEXT("@light call TurnOn(2, \"x\", nil);\nint _n = @light call GetCount();"),
		TEXT("function <main>:\n")
		TEXT("  0 PushConst @light\n")
		TEXT("  1 PushConst 2\n")
		TEXT("  2 PushConst \"x\"\n")
		TEXT("  3 PushNil entity\n")
		TEXT("  4 CallReflected TurnOn 3\n")
		TEXT("  5 PushConst @light\n")
		TEXT("  6 CallReflected GetCount 0 +result\n")
		TEXT("  7 StorePlace _n\n")
		TEXT("  8 Halt\n"),
		nullptr, nullptr },
	{ TEXT("call: the target must be an entity"), TEXT("5 call TurnOn();"), nullptr, TEXT("(1:1)"), TEXT("The target of 'call TurnOn'") },
	{ TEXT("global with a default"), TEXT("global int a ?= 1;\nglobal string[] b ?= [\"x\"];\nglobal entity c ?= @john;\nglobal bool d ?= nil;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("global with a computed default"), TEXT("global int a ?= 1 + 2;"), nullptr, TEXT("(1:19)"), TEXT("A global's default must be a fixed value") },
	{ TEXT("global default of the wrong type"), TEXT("global int a ?= \"x\";"), nullptr, TEXT("(1:17)"), TEXT("must be an int") },
	// `global T name = value;` in the code: declares it for the whole file, and sets it each time the line runs.
	{ TEXT("global set in the code"), TEXT("Talk(\"x\");\nglobal bool open = true;"),
		TEXT("globals: bool open;\n")
		TEXT("function <main>:\n")
		TEXT("  0 PushConst \"x\"\n")
		TEXT("  1 CallCommand Talk 1\n")
		TEXT("  2 PushConst true\n")
		TEXT("  3 StorePlace open\n")
		TEXT("  4 Halt\n"),
		nullptr, nullptr },
	{ TEXT("global set in the code, first thing"), TEXT("global int stage = 2;\nTalk(\"x\");"), TEXT("*"), nullptr, nullptr },
	{ TEXT("global set in the code to something worked out"), TEXT("int _a = 2;\nglobal int stage = _a * 3;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("global set in a block, a function and twice"), TEXT("if (true)\n{\n\tglobal bool open = true;\n};\nglobal bool open = false;\nfunction void F()\n{\n\tglobal int stage = 1;\n};"), TEXT("*"), nullptr, nullptr },
	{ TEXT("global set in the code and declared at the top"), TEXT("global int stage ?= 0;\nTalk(\"x\");\nglobal int stage = 1;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("global set in the code: its type is used before the line"), TEXT("Talk(\"x\", stage + 1);\nglobal int stage = 1;\nforEach (_s in names)\n{\n};\nglobal string[] names = [];"), TEXT("*"), nullptr, nullptr },
	{ TEXT("global set in the code: the wrong type"), TEXT("global int stage = \"two\";"), nullptr, TEXT("(1:20)"), TEXT("Global 'stage' needs an int") },
	{ TEXT("global set in the code: another type than at the top"), TEXT("global int stage;\nTalk(\"x\");\nglobal bool stage = true;"), nullptr, TEXT("(3:1)"), TEXT("declared as bool here, but as int elsewhere") },
	{ TEXT("global set in the code: a local of its name is an error"), TEXT("if (true)\n{\n\tint stage = 5;\n\tglobal int stage = 1;\n};"), nullptr, TEXT("(3:2)"), TEXT("'stage' is a global this file declares") },
	{ TEXT("global set in the code with metadata"), TEXT("Talk(\"x\"); //@1(0, 0)\nglobal bool open = true; //@2(0, 4)"), TEXT("*"), nullptr, nullptr },
	{ TEXT("global without a value after code"), TEXT("Talk(\"x\");\nglobal bool open;"), nullptr, TEXT("(2:1)"), TEXT("A global declared in the code must be given a value") },
	{ TEXT("global default after code"), TEXT("Talk(\"x\");\nglobal bool open ?= true;"), nullptr, TEXT("(2:1)"), TEXT("A global's default ('?=') goes in its declaration at the top") },
	{ TEXT("global default in a block"), TEXT("if (true)\n{\n\tglobal bool open ?= true;\n};"), nullptr, TEXT("(3:2)"), TEXT("A global's default ('?=') goes in its declaration at the top") },
	{ TEXT("global default after one set in the code"), TEXT("global bool open = true;\nglobal int stage ?= 1;"), nullptr, TEXT("(2:1)"), TEXT("A global's default ('?=') goes in its declaration at the top") },
	{ TEXT("?= on a local"), TEXT("int _a ?= 1;"), nullptr, TEXT("(1:8)"), TEXT("'?=' gives a global its default") },
	{ TEXT("?= as a statement"), TEXT("global int stage;\nstage ?= 1;"), nullptr, TEXT("(2:7)"), TEXT("'?=' only gives a global its default") },
	{ TEXT("a local named like a declared global"), TEXT("global int same;\nint same = 7;"), nullptr, TEXT("(2:1)"), TEXT("'same' is a global this file declares") },
	{ TEXT("a loop variable named like a declared global"), TEXT("global int i;\nfor (int i = 0; i < 2; i++)\n{\n};"), nullptr, TEXT("(2:6)"), TEXT("'i' is a global this file declares") },
	{ TEXT("a function parameter named like a declared global"), TEXT("global int n;\nfunction void F(int n)\n{\n};"), nullptr, TEXT("(2:21)"), TEXT("'n' is a global this file declares") },
	{ TEXT("a script variable named like a declared global"), TEXT("global int same;\nscriptVar int same = 1;"), nullptr, TEXT("(2:1)"), TEXT("'same' is a global this file declares") },
	{ TEXT("a function's script variable named like a declared global"), TEXT("global int same;\nfunction void F()\n{\n\tscriptVar int same;\n};"), nullptr, TEXT("(4:2)"), TEXT("'same' is a global this file declares") },
	{ TEXT("a spawn's id named like a declared global"), TEXT("global int same;\nint same = spawn\n{\n};"), nullptr, TEXT("(2:5)"), TEXT("'same' is a global this file declares") },
	{ TEXT("a script parameter named like a declared global"), TEXT("#inputs (int n)\nglobal int n;"), nullptr, TEXT("(1:14)"), TEXT("'n' is a global this file declares") },
	{ TEXT("global in a function"), TEXT("function void F()\n{\n\tglobal int g;\n};"), nullptr, TEXT("(3:2)"), TEXT("not in a function") },
	{ TEXT("global after a statement"), TEXT("Talk(\"x\");\nglobal int g;"), nullptr, TEXT("(2:1)"), TEXT("To only declare it, put 'global int g;' at the top of the file") },
	{ TEXT("global after a function"), TEXT("function void F()\n{\n\tTalk(\"x\");\n};\nglobal int g;"), nullptr, TEXT("(5:1)"), TEXT("To only declare it, put 'global int g;' at the top of the file") },
	{ TEXT("global after a scriptVar"), TEXT("scriptVar int _x;\nglobal int g;"), nullptr, TEXT("(2:1)"), TEXT("come before the script's scriptVars") },
	{ TEXT("global after a local"), TEXT("int _x;\nglobal int g;"), nullptr, TEXT("(2:1)"), TEXT("To only declare it, put 'global int g;' at the top of the file") },
	{ TEXT("scriptVar after a statement"), TEXT("Talk(\"x\");\nscriptVar int _x;"), nullptr, TEXT("(2:1)"), TEXT("scriptVar declarations go at the top of their script") },
	{ TEXT("scriptVar after a local"), TEXT("int _a = 1;\nscriptVar int _b = 2;"), nullptr, TEXT("(2:1)"), TEXT("scriptVar declarations go at the top of their script") },
	{ TEXT("scriptVar in a block"), TEXT("if (true)\n{\n\tscriptVar int _x;\n};"), nullptr, TEXT("(3:2)"), TEXT("scriptVar declarations go at the top of their script") },
	{ TEXT("function scriptVar after a statement"), TEXT("function void F()\n{\n\tTalk(\"x\");\n\tscriptVar int _x;\n};"), nullptr, TEXT("(4:2)"), TEXT("scriptVar declarations go at the top of their function") },
	{ TEXT("scriptVar with metadata"), TEXT("scriptVar int _x; //@3(0, 0)"), nullptr, TEXT("(1:1)"), TEXT("can't have node metadata") },
	{ TEXT("call as a scriptVar's starting value"), TEXT("scriptVar float _t = Random(1.0, 2.0);"), nullptr, TEXT("(1:22)"), TEXT("Drop 'scriptVar' to declare a local") },
	{ TEXT("local after a statement"), TEXT("Talk(\"x\");\nint _x;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("local in a block"), TEXT("if (true)\n{\n\tint _x;\n};"), TEXT("*"), nullptr, nullptr },
	{ TEXT("function local after a statement"), TEXT("function void F()\n{\n\tTalk(\"x\");\n\tint _x;\n};"), TEXT("*"), nullptr, nullptr },
	{ TEXT("local with metadata"), TEXT("int _x; //@3(0, 0)"), TEXT("*"), nullptr, nullptr },
	{ TEXT("call as a local's starting value"), TEXT("float _t = Random(1.0, 2.0);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("local used after its block"), TEXT("if (true)\n{\n\tint _x = 1;\n};\n_x = 2;"), nullptr, TEXT("(5:1)"), TEXT("'_x' is a local declared on line 3, and isn't visible here") },
	{ TEXT("local used before its declaration"), TEXT("_x = 2;\nint _x = 1;"), nullptr, TEXT("(1:1)"), TEXT("'_x' is a local declared on line 2") },
	{ TEXT("locals in sibling blocks"), TEXT("if (true)\n{\n\tint _x = 1;\n}\nelse\n{\n\tint _x = 2;\n};"), TEXT("*"), nullptr, nullptr },
	{ TEXT("local redeclared in an inner block"), TEXT("int _x = 1;\nif (true)\n{\n\tint _x = 2;\n};"), nullptr, TEXT("(4:2)"), TEXT("already declared") },
	{ TEXT("a disabled local"), TEXT("/* int _x = 1; */\n_x = 2;"), nullptr, TEXT("(2:1)"), TEXT("that declaration is disabled") },
	{ TEXT("goto past a local's declaration"), TEXT("goto Skip;\nint _x = 1;\nlabel Skip;\nTalk(\"{0}\", _x);"), nullptr, TEXT("(1:1)"), TEXT("jumps past the declaration of local '_x' (line 2)") },
	{ TEXT("goto back over a local's declaration"), TEXT("label Top;\nint _x = 1;\ngoto Top;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("goto out of a local's block"), TEXT("while (true)\n{\n\tint _x = 1;\n\tgoto Out;\n};\nlabel Out;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("too many indexes"), TEXT("int[] _a = [1];\nint _b;\n_b = _a[0][0];"), nullptr, TEXT("(3:6)"), TEXT("indexed at most 1") },
	{ TEXT("index a non-array"), TEXT("int _a = 1;\nint _b;\n_b = _a[0];"), nullptr, TEXT("(3:6)"), TEXT("'_a' is an int, so it can't be indexed: only arrays, vectors and strings can be") },
	// Vectors' parts and strings' characters by index.
	{ TEXT("index a vector and a string"), TEXT("vector3 _v = [1, 2, 3];\nstring _s = \"abc\";\nfloat _f;\nstring _c;\n_f = _v[2];\n_c = _s[0];\n_v[1] = 5;\n_s[2] = \"z\";\n_v[0] += 1;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("index a vector in an array, and a string in one"), TEXT("vector2[] _path = [[1, 2]];\nstring[] _names = [\"ann\"];\nfloat _f;\nstring _c;\n_f = _path[0][1];\n_c = _names[0][2];\n_path[0][0] = 3;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("a vector2 has no [2]"), TEXT("vector2 _v = [1, 2];\nfloat _f;\n_f = _v[2];"), nullptr, TEXT("(3:9)"), TEXT("A vector2 has 2 parts, [0] to [1] (x to y), so it has no [2]") },
	{ TEXT("a negative index written as a number"), TEXT("string _s = \"abc\";\nstring _c;\n_c = _s[-1];"), nullptr, TEXT("(3:9)"), TEXT("Index -1 is negative") },
	{ TEXT("a vector part is a float"), TEXT("vector3 _v = [1, 2, 3];\nint _i;\n_i = _v[0];"), nullptr, TEXT("(3:8)"), TEXT("'_i' needs an int, but this is a float") },
	{ TEXT("a string's character is a string"), TEXT("string _s = \"abc\";\nint _i;\n_i = _s[0];"), nullptr, TEXT("(3:8)"), TEXT("'_i' needs an int, but this is a string") },
	{ TEXT("index a vector twice"), TEXT("vector3 _v = [1, 2, 3];\nfloat _f;\n_f = _v[0][0];"), nullptr, TEXT("(3:6)"), TEXT("can be indexed at most 1 time(s)") },
	{ TEXT("isNil of a vector's part by index"), TEXT("vector3 _v = [1, 2, 3];\nbool _b;\n_b = isNil(_v[0]);"), nullptr, TEXT("(3:6)"), TEXT("is never nil") },
	{ TEXT("a text can't be indexed"), TEXT("text _t = INVTEXT(\"hi\");\nstring _c;\n_c = _t[0];"), nullptr, TEXT("(3:6)"), TEXT("it can't be indexed") },
	{ TEXT("index a call's vector"), TEXT("float _f;\n_f = P()[2];\nfunction vector3 P()\n{\n\treturn [1, 2, 3];\n};"), TEXT("*"), nullptr, nullptr },
	{ TEXT("Count of a string and a vector"), TEXT("int _n;\n_n = Count(\"abc\") + Count([1, 2, 3]);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("Count of an int"), TEXT("int _n;\n_n = Count(5);"), nullptr, TEXT("(2:12)"), TEXT("Argument 1 of 'Count' needs an array, a vector2, a vector3 or a string, but this is an int.") },
	{ TEXT("index not an int"), TEXT("int[] _a = [1];\nint _b;\n_b = _a[\"x\"];"), nullptr, TEXT("(3:9)"), TEXT("index must be an int") },
	{ TEXT("array element type"), TEXT("int[] _a = [1, \"x\"];"), nullptr, TEXT("(1:16)"), TEXT("needs an int, but this is a string") },
	{ TEXT("array mixed types inferred"), TEXT("Talk(\"{0}\", [1, \"x\"]);"), nullptr, TEXT("(1:13)"), TEXT("mixes an int and a string") },
	{ TEXT("empty array without a type"), TEXT("int _c;\n_c = Count([]);"), nullptr, TEXT("(2:12)"), TEXT("can't be worked out") },
	{ TEXT("array literal where scalar expected"), TEXT("int _x = [1];"), nullptr, TEXT("(1:10)"), TEXT("Expected an int here, but this is an array") },
	{ TEXT("++ on float"), TEXT("float _d = 1.0;\n_d++;"), nullptr, TEXT("(2:1)"), TEXT("only works on an int") },
	{ TEXT("compound result type"), TEXT("int _i = 0;\n_i += 1.5;"), nullptr, TEXT("(2:1)"), TEXT("stored in '_i' needs an int, but this is a float") },
	{ TEXT("Push wrong type"), TEXT("int[] _a = [];\n_a Push(\"x\");"), nullptr, TEXT("(2:9)"), TEXT("Argument 1 of 'Push' needs an int, but this is a string.") },
	{ TEXT("Push on non-array"), TEXT("int _a = 0;\n_a Push(1);"), nullptr, TEXT("(2:1)"), TEXT("needs an array as its target") },
	{ TEXT("Push on undeclared global"), TEXT("questLog Push(\"x\");"), nullptr, TEXT("(1:1)"), TEXT("isn't declared in this script") },
	{ TEXT("Push on a value"), TEXT("[1] Push(2);"), nullptr, TEXT("(1:1)"), TEXT("must be a variable") },
	{ TEXT("Pop with arguments"), TEXT("int[] _a = [];\n_a Pop(1);"), nullptr, TEXT("(2:4)"), TEXT("Wrong number of arguments for 'Pop' (1 given): array Pop() -> element") },
	{ TEXT("Count wrong"), TEXT("int _c;\n_c = Count(5);"), nullptr, TEXT("(2:12)"), TEXT("of 'Count' needs an array") },
	{ TEXT("forEach over undeclared global"), TEXT("forEach (_x in questLog)\n{\n};"), nullptr, TEXT("(1:16)"), TEXT("declare it first") },
	{ TEXT("spawn: the id goes in an int"), TEXT("string _s;\n_s = spawn\n{\n}"), nullptr, TEXT("(2:1)"), TEXT("stored in '_s'") },
	{ TEXT("spawn: declaring its id variable on the spot"), TEXT("Talk(\"x\");\nint _t = spawn\n{\n}\nTalk(\"{0}\", _t);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("spawn: its declared id isn't visible inside"), TEXT("int _t = spawn\n{\n\tTalk(\"{0}\", _t);\n}"), nullptr, TEXT("(3:14)"), TEXT("'_t' is a local declared on line 1") },
	{ TEXT("spawn: its declared id in a string"), TEXT("string _t = spawn\n{\n}"), nullptr, TEXT("(1:8)"), TEXT("stored in '_t'") },
	{ TEXT("spawn: break can't leave it"), TEXT("while (true)\n{\n\tspawn\n\t{\n\t\tbreak;\n\t}\n}"), nullptr, TEXT("(5:3)"), TEXT("can't leave a spawn block") },
	{ TEXT("spawn: break in its own loop"), TEXT("spawn\n{\n\twhile (true)\n\t{\n\t\tbreak;\n\t}\n}"), TEXT("*"), nullptr, nullptr },
	{ TEXT("spawn: return with a value"), TEXT("function int F()\n{\n\tspawn\n\t{\n\t\treturn 1;\n\t}\n\treturn 2;\n}"), nullptr, TEXT("(5:10)"), TEXT("can't give back a value") },
	{ TEXT("spawn: goto out of it"), TEXT("label Top;\nspawn\n{\n\tgoto Top;\n}"), nullptr, TEXT("(4:2)"), TEXT("jump out of a spawn block") },
	{ TEXT("spawn: goto inside it"), TEXT("spawn\n{\n\tlabel Again;\n\tgoto Again;\n}"), TEXT("*"), nullptr, nullptr },
	{ TEXT("spawn: a local in it"), TEXT("spawn\n{\n\tint _x;\n}"), TEXT("*"), nullptr, nullptr },
	{ TEXT("switch on a bool"), TEXT("switch (true)\n{\n}"), nullptr, TEXT("(1:9)"), TEXT("int, a string or an entity, but this is a bool") },
	{ TEXT("switch on a float"), TEXT("float _d = 1.5;\nswitch (_d)\n{\n}"), nullptr, TEXT("(2:9)"), TEXT("but this is a float") },
	{ TEXT("switch on an undeclared global"), TEXT("switch (stage)\n{\n}"), nullptr, TEXT("(1:9)"), TEXT("declare it first") },
	{ TEXT("switch: a case value of the wrong type"), TEXT("int _x = 1;\nswitch (_x)\n{\n\tcase \"a\":\n\t{\n\t}\n}"), nullptr, TEXT("(4:7)"), TEXT("each case value must be an int") },
	{ TEXT("switch: a case value that's a name"), TEXT("int _x = 1;\nint _y = 2;\nswitch (_x)\n{\n\tcase _y:\n\t{\n\t}\n}"), nullptr, TEXT("(5:7)"), TEXT("must be a fixed value") },
	{ TEXT("switch: a repeated case value"), TEXT("string _s = \"a\";\nswitch (_s)\n{\n\tcase \"a\", \"b\":\n\t{\n\t}\n\tcase \"b\":\n\t{\n\t}\n}"), nullptr, TEXT("(7:7)"), TEXT("already in an earlier case") },
	{ TEXT("switch: a declaration in a case"), TEXT("int _x = 1;\nswitch (_x)\n{\n\tcase 1:\n\t{\n\t\tint _y;\n\t}\n}"), TEXT("*"), nullptr, nullptr },
	{ TEXT("forEach over non-array"), TEXT("forEach (_x in 5)\n{\n};"), nullptr, TEXT("(1:16)"), TEXT("loops over an array") },
	{ TEXT("nil without a type"), TEXT("bool _b;\n_b = isNil(nil);"), nullptr, TEXT("(2:"), TEXT("isNil needs") },
	{ TEXT("array of only nils"), TEXT("Talk(\"{0}\", [nil]);"), nullptr, TEXT("(1:13)"), TEXT("can't be worked out") },
	{ TEXT("comparing different types"), TEXT("bool _b;\n_b = \"a\" == 1;"), nullptr, TEXT("(2:10)"), TEXT("same type") },
	{ TEXT("arrays with operators"), TEXT("int[] _a = [];\nbool _b;\n_b = _a == _a;"), nullptr, TEXT("(3:9)"), TEXT("Arrays can't be used") },
	{ TEXT("negate a string"), TEXT("string _s;\n_s = -\"a\";"), nullptr, TEXT("(2:6)"), TEXT("needs a number") },
	{ TEXT("&& on a non-bool"), TEXT("bool _b;\n_b = 1 && true;"), nullptr, TEXT("(2:6)"), TEXT("needs bools") },
	{ TEXT("! on a non-bool"), TEXT("bool _b;\n_b = !1;"), nullptr, TEXT("(2:6)"), TEXT("needs a bool") },
	{ TEXT("less-than on strings"), TEXT("bool _b;\n_b = \"a\" < \"b\";"), nullptr, TEXT("(2:10)"), TEXT("compares numbers") },
	{ TEXT("inputs default of the wrong type"), TEXT("#inputs (int _a = \"x\")"), nullptr, TEXT("(1:19)"), TEXT("The default for '_a' must be an int, but it is a string") },
	{ TEXT("inputs array default of the wrong type"), TEXT("#inputs (int[] _a = [1, 2.5])"), nullptr, TEXT("(1:25)"), TEXT("The default for '_a' must be an int, but it is a float") },
	{ TEXT("inputs array default for a scalar"), TEXT("#inputs (int _a = [1])"), nullptr, TEXT("(1:19)"), TEXT("but it is an array") },
	{ TEXT("inputs duplicate name"), TEXT("#inputs (int _a, string _a)"), nullptr, TEXT("(1:25)"), TEXT("Two parameters are named '_a'") },
	{ TEXT("inputs shadowed by a local"), TEXT("#inputs (int _a)\nint _a = 1;"), nullptr, TEXT("(2:1)"), TEXT("already declared") },
	{ TEXT("inputs not visible in functions"), TEXT("#inputs (int _a)\nfunction void F()\n{\n\t_a = 1;\n};"), nullptr, TEXT("(4:2)"), TEXT("'_a' isn't declared in this script") },   // (a read there is of a global _a),
	{ TEXT("error carries node id"), TEXT("int _x;\n_x = \"no\"; //@42(0, 0)"), nullptr, TEXT("[node 42]"), TEXT("needs an int") },
	{ TEXT("fixed starting values"), TEXT("int[] _a = [1, -2];\nvector3 _v = [0, 0, 1];\nentity _e = @john;\nbool _b = true;\nstring _s = nil;\nfloat _f = -2.5;\nTalk(\"{0} {1} {2} {3} {4}\", _a, _v, _e, _b, _f);"), TEXT("*"), nullptr, nullptr },

	// ---- Results that must be used ----
	{ TEXT("no-discard command on its own line"), TEXT("Random(1.0, 2.0);"), nullptr, TEXT("(1:1)"), TEXT("'Random' gives a value that must be used") },
	{ TEXT("no-discard targeted command on its own line"), TEXT("@john GetName();"), nullptr, TEXT("(1:7)"), TEXT("'GetName' gives a value that must be used") },
	{ TEXT("Count on its own line"), TEXT("int[] _a = [];\nCount(_a);"), nullptr, TEXT("(2:1)"), TEXT("'Count' gives a value that must be used") },
	{ TEXT("no-discard results used anywhere a value goes"),
		TEXT("float _t;\nint[] _a;\nint _n;\n_t = Random(1.0, 2.0);\n_t = Random(0.0, 1.0) * 2;\nif (Random(0.0, 1.0) < 0.5)\n{\n};\nTalk(\"{0}\", Random(1.0, 2.0));\n_a = [];\n_n = Count(_a) + 1;"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("discardable command on its own line"), TEXT("AskChoice(\"A\", \"B\");"), TEXT("*"), nullptr, nullptr },
	{ TEXT("Pop and RemoveAt can be thrown away"), TEXT("int[] _a = [1, 2];\n_a Pop();\n_a RemoveAt(0);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("script functions can be thrown away"), TEXT("function int F()\n{\n\treturn 1;\n};\nF();"), TEXT("*"), nullptr, nullptr },

	// ---- Localized text ----
	{ TEXT("text literals and LOCTEXT_NAMESPACE"),
		TEXT("#inputs (text _line = nil)\n#LOCTEXT_NAMESPACE \"Bango.Intro\"\nglobal text gt;\ntext _a = LOCTEXT(\"Greet\", \"Hello\");\ntext[] _b;\nbool _same;\ntext _f;\n_b = [_a, NSLOCTEXT(\"Other\", \"Greet\", \"x\"), ToText(\"Bob\")];\nSay(_a);\nSay(LOCTEXT(\"Greet\", \"Hello\"));\n_same = _a == _line;\n_f = FormatText(LOCTEXT(\"Count\", \"{1} has {0}\"), 3, _a);\ngt = nil;\nfunction text F(text _t)\n{\n\treturn _t;\n};"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("LOCTEXT needs LOCTEXT_NAMESPACE"), TEXT("Say(LOCTEXT(\"Key\", \"Hi\"));"), nullptr, TEXT("(1:5)"), TEXT("needs the script's namespace: add the line #LOCTEXT_NAMESPACE") },
	{ TEXT("NSLOCTEXT doesn't need LOCTEXT_NAMESPACE"), TEXT("Say(NSLOCTEXT(\"Ns\", \"Key\", \"Hi\"));\nSay(NSNOTELOCTEXT(\"Ns\", \"Other\", \"Hi\", \"Notes\"));"), TEXT("*"), nullptr, nullptr },
	{ TEXT("a key used for different texts"), TEXT("#LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"Key\", \"Hi\"));\nSay(NSLOCTEXT(\"N\", \"Key\", \"Bye\"));"), nullptr, TEXT("(3:20)"), TEXT("The key 'Key' is already used in namespace 'N' for a different text, on line 2") },
	{ TEXT("a key used with different notes"), TEXT("#LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"Key\", \"Hi\"));\nSay(NOTELOCTEXT(\"Key\", \"Hi\", \"Whispered\"));"), nullptr, TEXT("(3:17)"), TEXT("already used") },
	{ TEXT("the same key in two namespaces"), TEXT("#LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"Key\", \"Hi\"));\nSay(NSLOCTEXT(\"M\", \"Key\", \"Bye\"));"), TEXT("*"), nullptr, nullptr },
	{ TEXT("a string isn't text"), TEXT("Say(\"Hi\");"), nullptr, TEXT("(1:5)"), TEXT("text") },
	{ TEXT("text isn't a string"), TEXT("#LOCTEXT_NAMESPACE \"N\"\nstring _s = LOCTEXT(\"Key\", \"Hi\");"), nullptr, TEXT("(2:13)"), TEXT("string") },
	{ TEXT("texts can't be joined"), TEXT("#LOCTEXT_NAMESPACE \"N\"\ntext _a;\n_a = LOCTEXT(\"A\", \"a\") + LOCTEXT(\"B\", \"b\");"), nullptr, TEXT("(3:24)"), TEXT("Texts can't be joined with '+'") },
	{ TEXT("texts can't be ordered"), TEXT("#LOCTEXT_NAMESPACE \"N\"\nbool _b;\n_b = LOCTEXT(\"A\", \"a\") < LOCTEXT(\"B\", \"b\");"), nullptr, TEXT("(3:24)"), TEXT("compares numbers") },
	{ TEXT("text and string can't be compared"), TEXT("#LOCTEXT_NAMESPACE \"N\"\nbool _b;\n_b = LOCTEXT(\"A\", \"a\") == \"a\";"), nullptr, TEXT("(3:24)"), TEXT("same type") },
	{ TEXT("a text placeholder without a value"), TEXT("#LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"Key\", \"{0} and {1}\"), 1);"), nullptr, TEXT("(2:20)"), TEXT("This text uses {1}, but only 1 value comes after it") },
	{ TEXT("a named text placeholder"), TEXT("#LOCTEXT_NAMESPACE \"N\"\ntext _t;\n_t = FormatText(LOCTEXT(\"Key\", \"{Name}\"), 1);"), nullptr, TEXT("(3:32)"), TEXT("uses {Name}. Text placeholders are numbered") },
	{ TEXT("text placeholders: escapes, %, and plural forms"), TEXT("#LOCTEXT_NAMESPACE \"N\"\nSay(LOCTEXT(\"A\", \"`{0`} 100% {0}|plural(one=item,other=items)\"), 1);\nSay(LOCTEXT(\"B\", \"%0 is fine here\"));"), TEXT("*"), nullptr, nullptr },

	// ---- Placeholders in a literal text need values ----
	{ TEXT("placeholder without a value"), TEXT("Talk(\"{0} and {1}\", 1);"), nullptr, TEXT("(1:6)"), TEXT("This text uses {1}, but only 1 value comes after it") },
	{ TEXT("placeholder with no values at all"), TEXT("Talk(\"only {2}\");"), nullptr, TEXT("(1:6)"), TEXT("uses {2}, but no values come after it") },
	{ TEXT("placeholder inside an expression"), TEXT("string _s = \"a\";\nTalk(\"{0}\", Format(\"{0} {1}\", 1, 2) + Format(\"{3}\", 1, 2));"), nullptr, TEXT("(2:46)"), TEXT("uses {3}, but only 2 values come after it") },
	{ TEXT("placeholders filled, nil included"), TEXT("string _t;\nTalk(\"{0} {1} `{\", 1, nil);\nTalk(\"100% sure\");\n_t = \"{5}\";\nTalk(_t);"), TEXT("*"), nullptr, nullptr },
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
	{ TEXT("adding vectors of different kinds"), TEXT("vector3 _v = [1, 2, 3];\nvector2 _w = [1, 2];\nTalk(\"{0}\", _v + _w);"), nullptr, TEXT("(3:16)"), TEXT("'+' works on two vectors of the same kind, but this is a vector3 + a vector2.") },
	{ TEXT("adding a number to a vector"), TEXT("vector3 _v = [1, 2, 3];\nTalk(\"{0}\", _v + 1);"), nullptr, TEXT("(2:16)"), TEXT("use a vector, like _v + [1, 1, 1]") },
	{ TEXT("multiplying two vectors"), TEXT("vector3 _v = [1, 2, 3];\nTalk(\"{0}\", _v * _v);"), nullptr, TEXT("(2:16)"), TEXT("Two vectors can't be multiplied with '*'.") },
	{ TEXT("dividing by a vector"), TEXT("vector3 _v = [1, 2, 3];\nTalk(\"{0}\", 1 / _v);"), nullptr, TEXT("(2:15)"), TEXT("nothing can be divided by a vector") },
	{ TEXT("comparing vectors with <"), TEXT("vector3 _v = [1, 2, 3];\nTalk(\"{0}\", _v < _v);"), nullptr, TEXT("(2:16)"), TEXT("Vectors can't be compared with '<'.") },
	{ TEXT("comparing a vector with a number"), TEXT("vector3 _v = [1, 2, 3];\nTalk(\"{0}\", _v == 1);"), nullptr, TEXT("(2:16)"), TEXT("'==' compares two values of the same type, but this is a vector3 == an int.") },
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
	{ TEXT("vector default of the wrong size"), TEXT("#inputs (vector3 _p = [1, 2])"), nullptr, TEXT("(1:23)"), TEXT("A vector3 needs exactly 3 numbers") },

	// ---- INVTEXT ----
	{ TEXT("INVTEXT is reserved"), TEXT("int INVTEXT = 1;"), nullptr, TEXT("(1:"), TEXT("INVTEXT") },
	{ TEXT("LOCTEXT is reserved"), TEXT("int LOCTEXT = 1;"), nullptr, TEXT("(1:"), TEXT("LOCTEXT") },
	{ TEXT("invariant text needs no namespace"), TEXT("text _t = INVTEXT(\"x\");\nSay(INVTEXT(\"{0} {1}\"), 1, 2);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("invariant text placeholders are checked"), TEXT("Say(INVTEXT(\"{0} {1}\"), 1);"), nullptr, TEXT("(1:13)"), TEXT("This text uses {1}, but only 1 value comes after it.") },
	{ TEXT("invariant text with a named placeholder"), TEXT("Say(INVTEXT(\"{Name}\"));"), nullptr, TEXT("(1:13)"), TEXT("This text uses {Name}") },

	// ---- Number commands (a type parameter that's an int or a float): an int for ints, otherwise a float ----
	{ TEXT("number: ints give an int"), TEXT("int _a = 1;\nint _b;\n_b = Max(_a, 2);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("number: a float gives a float"), TEXT("int _a = 1;\nfloat _f;\n_f = Max(_a, 2.5);\n_f = Max(1, 2);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("number: a float result into an int"), TEXT("int _a = 1;\nint _b;\n_b = Max(_a, 2.5);"), nullptr, TEXT("(3:6)"), TEXT("float") },
	{ TEXT("number: nested"), TEXT("int _b;\n_b = Max(Max(1, 2), 3);\n_b = Max(1, 2) + 3;"), TEXT("*"), nullptr, nullptr },
	{ TEXT("number: not a number"), TEXT("int _b;\n_b = Max(1, \"two\");"), nullptr, TEXT("(2:13)"), TEXT("Argument 2 of 'Max' needs a number (an int or a float), but this is a string.") },
	{ TEXT("number: nil"), TEXT("int _b;\n_b = Max(1, nil);"), nullptr, TEXT("(2:13)"), TEXT("nil can only be assigned, passed or returned where its type is known.") },
	{ TEXT("number: an array"), TEXT("int _b;\n_b = Max([1], 2);"), nullptr, TEXT("(2:10)"), TEXT("needs a number (an int or a float), but this is an array.") },
	{ TEXT("number: an undeclared global is allowed"), TEXT("int _b;\n_b = Max(someGlobal, 2);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("number: must be used"), TEXT("Max(1, 2);"), nullptr, TEXT("(1:1)"), TEXT("'Max' gives a value that must be used") },

	// ---- Type parameters (TypeParam): Lerp takes floats or vectors, both the same; Count an array, a vector or a string ----
	{ TEXT("type param: ints count as floats where only floats are allowed"), TEXT("float _f;\n_f = Lerp(0, 10, 0.5);\n_f = Lerp(_f, 10, 1);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("type param: so the result is a float"), TEXT("int _i;\n_i = Lerp(0, 10, 0.5);"), nullptr, TEXT("(2:6)"), TEXT("float") },
	{ TEXT("type param: vectors, written out too"),
		TEXT("vector3 _a = [0, 0, 0];\nvector3 _v;\nvector2 _w;\n_v = Lerp(_a, [1, 2, 3], 0.5);\n_v = Lerp([1, 2, 3], _a, 0.5);\n_w = Lerp([0, 0], [1, 1], 0.5);\n_v = Lerp([0, 0, 0], [1, 1, 1], 1);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("type param: a vector3 result into a vector2"), TEXT("vector2 _w;\n_w = Lerp([0, 0, 0], [1, 1, 1], 0.5);"), nullptr, TEXT("(2:6)"), TEXT("vector3") },
	{ TEXT("type param: two kinds of vector"), TEXT("vector3 _a = [0, 0, 0];\nvector2 _b = [0, 0];\n_a = Lerp(_a, _b, 0.5);"), nullptr, TEXT("(3:15)"),
		TEXT("Argument 2 of 'Lerp' is a vector2, but argument 1 ('A') is a vector3. They must be the same type: Lerp(float|vector2|vector3 A, float|vector2|vector3 B, float Alpha) -> float|vector2|vector3") },
	{ TEXT("type param: a float and a written-out vector"), TEXT("float _f;\n_f = Lerp(1.0, [0, 0], 0.5);"), nullptr, TEXT("(2:16)"),
		TEXT("Argument 2 of 'Lerp' is an array of 2 value(s), but argument 1 ('A') is a float. They must be the same type") },
	{ TEXT("type param: a type that isn't allowed"), TEXT("string _s;\n_s = Lerp(\"a\", \"b\", 0.5);"), nullptr, TEXT("(2:11)"),
		TEXT("Argument 1 of 'Lerp' needs a float, a vector2 or a vector3, but this is a string.") },
	{ TEXT("type param: a written-out array that's no vector"), TEXT("vector3 _v;\n_v = Lerp([1], [2], 0.5);"), nullptr, TEXT("(2:11)"),
		TEXT("Argument 1 of 'Lerp' needs a float, a vector2 or a vector3, but this is an array of 1 value(s).") },
	{ TEXT("type param: written-out vectors of two sizes"), TEXT("vector3 _v;\n_v = Lerp([1, 2, 3], [1, 2], 0.5);"), nullptr, TEXT("(2:22)"), TEXT("A vector3 needs exactly 3 numbers") },
	{ TEXT("type param: an undeclared global beside a written-out vector"), TEXT("vector3 _v;\n_v = Lerp(someGlobal, [1, 2, 3], 0.5);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("type param: only undeclared globals, checked when it runs"), TEXT("float _f;\nvector3 _v;\n_f = Lerp(globalA, globalB, 0.5);\n_v = Lerp(globalA, globalB, 0.5);"), TEXT("*"), nullptr, nullptr },
	{ TEXT("type param: a call inside a call"), TEXT("vector3 _v;\n_v = Lerp(Lerp([0, 0, 0], [1, 1, 1], 0.5), Lerp([2, 2, 2], _v, Max(0, 1)), Lerp(0, 1, 0.5));"), TEXT("*"), nullptr, nullptr },
	{ TEXT("type param: nil"), TEXT("float _f;\n_f = Lerp(nil, 1.0, 0.5);"), nullptr, TEXT("(2:11)"), TEXT("nil can only be assigned, passed or returned where its type is known.") },
	// ---- Commands that change their target in place (MutableTarget): the array commands, and the test table's Bump ----
	{ TEXT("in place: the array says what an element is"),
		TEXT("float[] _f = [];\nint[][] _g = [];\nvector3[] _p = [];\n_f Push(1);\n_f Push(nil);\n_f InsertAt(2, 0);\n_g Push([]);\n_g Push([1, 2]);\n_g[0] Push(3);\n_p Push([1, 2, 3]);\nfloat _x = _f Pop();\nint[] _row = _g RemoveAt(0);"),
		TEXT("*"), nullptr, nullptr },
	{ TEXT("in place: an array isn't changed to fit a value"), TEXT("int[] _a = [];\n_a Push(2.5);"), nullptr, TEXT("(2:9)"), TEXT("Argument 1 of 'Push' needs an int, but this is a float.") },
	{ TEXT("in place: the result is an element"), TEXT("int[] _a = [1];\nstring _s;\n_s = _a Pop();"), nullptr, TEXT("(3:6)"), TEXT("'_s' needs a string, but this is an int.") },
	{ TEXT("in place: not an array"), TEXT("int _x = 1;\n_x Push(1);"), nullptr, TEXT("(2:1)"), TEXT("Push needs an array as its target, but '_x' is an int.") },
	{ TEXT("in place: no target"), TEXT("Push(1);"), nullptr, TEXT("(1:1)"), TEXT("'Push' needs a target: array Push(element Value)") },
	{ TEXT("in place: the target must be a place"), TEXT("[1, 2] Push(3);"), nullptr, TEXT("(1:1)"), TEXT("Push changes its target, so the target must be a variable, a global or an array element.") },
	{ TEXT("in place: a call's result isn't a place"), TEXT("function int[] Make()\n{\n\treturn [1];\n}\nMake() Push(3);"), nullptr, TEXT("(5:1)"), TEXT("Push changes its target") },
	{ TEXT("in place: a target of one type"), TEXT("int _n = 1;\nint[] _a = [5, 6];\n_n Bump();\n_a[1] Bump();"),
		TEXT("function <main>:\n  0 PushConst 1\n  1 StorePlace _n\n  2 PushConst 5\n  3 PushConst 6\n  4 MakeArray 2 int\n  5 StorePlace _a\n  6 CallCommandInPlace _n Bump 0\n  7 PushConst 1\n  8 CallCommandInPlace _a[] Bump 0\n  9 Halt\n"), nullptr, nullptr },
	{ TEXT("in place: a target of another type"), TEXT("float _f = 1.0;\n_f Bump();"), nullptr, TEXT("(2:1)"), TEXT("The target of 'Bump' needs an int to change, but '_f' is a float.") },
	{ TEXT("in place: not a character of a string"), TEXT("string _s = \"ab\";\n_s[0] Bump();"), nullptr, TEXT("(2:3)"), TEXT("not a part of a vector or a character of a string ('_s[...]')") },
	{ TEXT("in place: an undeclared global can't be changed"), TEXT("someGlobal Bump();"), nullptr, TEXT("(1:1)"), TEXT("someGlobal") },
	{ TEXT("Count is a command: no target"), TEXT("int[] _a = [];\nint _n;\n_n = _a Count();"), nullptr, TEXT("(3:9)"), TEXT("'Count' doesn't take a target: Count(array|vector2|vector3|string Value) -> int") },
	{ TEXT("Count is a command: one argument"), TEXT("int[] _a = [];\nint _n;\n_n = Count(_a, _a);"), nullptr, TEXT("(3:6)"), TEXT("Wrong number of arguments for 'Count' (2 given)") },
	{ TEXT("Count can't be a function's name"), TEXT("function int Count(int _n)\n{\n\treturn _n;\n}"), nullptr, TEXT("(1:"), TEXT("'Count' is already a command") },
	{ TEXT("pure commands don't make a condition polled"), TEXT("global int[] found;\nglobal int stage;\nwaitUntil (Count(found) > Max(stage, 2));"),
		TEXT("globals: int[] found; int stage;\nwait #0: found, stage\nfunction <main>:\n  0 Nop\n  1 Nop\n  2 WaitCheck #0\n  3 LoadPlace found\n  4 CallCommand Count 1\n  5 LoadPlace stage\n  6 PushConst 2\n  7 CallCommand Max 2\n  8 Greater\n  9 WaitResult #0 -> 2\n  10 Halt\n"), nullptr, nullptr },
	{ TEXT("waitUntil: globals, in functions too"),
		TEXT("global int stage;\nfunction bool Dead()\n{\n\treturn soldierIsDead;\n};\nwaitUntil (stage == 3 && Dead());"),
		TEXT("globals: int stage;\nwait #0: soldierIsDead, stage\nfunction <main>:\n  0 Nop\n  1 WaitCheck #0\n  2 TryUnknown -> 8\n  3 LoadPlace stage\n  4 PushConst 3\n  5 Equal\n  6 EndTryUnknown\n  7 JumpIfFalseOrPop -> 10\n  8 CallFunction Dead 0\n  9 ResolveUnknown 8 &&\n  10 WaitResult #0 -> 1\n  11 Halt\nfunction Dead:\n  0 LoadPlace soldierIsDead\n  1 Convert bool\n  2 ReturnValue\n  3 ErrorMissingReturn\n"),
		nullptr, nullptr },
	{ TEXT("waitUntil: a command makes it polled"), TEXT("waitUntil (Random(0, 1) > 0.5 && isNil(boss));"),
		TEXT("wait #0: boss; polled: it calls Random\nfunction <main>:\n  0 WaitCheck #0\n  1 TryUnknown -> 11\n  2 PushConst 0\n  3 Convert float\n  4 PushConst 1\n  5 Convert float\n  6 CallCommand Random 2\n  7 PushConst 0.5\n  8 Greater\n  9 EndTryUnknown\n  10 JumpIfFalseOrPop -> 13\n  11 IsNilPlace boss\n  12 ResolveUnknown 11 &&\n  13 WaitResult #0 -> 0\n  14 Halt\n"),
		nullptr, nullptr },
	{ TEXT("waitUntil: its condition must be a bool"), TEXT("waitUntil (3);"), nullptr, TEXT("(1:12)"), TEXT("A condition must be a bool, but this is an int.") },
	{ TEXT("waitUntil: its condition can't be left out"), TEXT("waitUntil ();"), nullptr, TEXT("(1:12)"), TEXT("This value is missing.") },
	{ TEXT("waitUntil: a condition that can't change (a constant)"), TEXT("Sleep(1);\nwaitUntil (true);"), nullptr, TEXT("(2:1)"), TEXT("can't change while the script waits") },
	{ TEXT("waitUntil: a condition that can't change (locals and a pure command)"), TEXT("int[] _a = [1];\nint _n = 3;\nwaitUntil (Count(_a) > Max(_n, 2));    //@4(0, 0)"), nullptr, TEXT("(3:1) [node 4]"), TEXT("or wait forever. Read a global in it, or use an if.") },
	{ TEXT("waitUntil: a condition that can't change (a function that reads none)"), TEXT("function bool F(int _n)\n{\n\treturn _n > 2;\n}\nwaitUntil (F(3));"), nullptr, TEXT("(5:1)"), TEXT("can't change while the script waits") },
	{ TEXT("waitUntil: a condition that can't change, with a timeout"), TEXT("bool _b = false;\nwaitUntil (_b) timeout (5)\n{\n}"), nullptr, TEXT("(2:1)"), TEXT("always wait for its whole timeout. Read a global in it, or use Sleep.") },
	{ TEXT("waitUntil: a global nobody declares can change"), TEXT("waitUntil (ready);"), TEXT("wait #0: ready\nfunction <main>:\n  0 WaitCheck #0\n  1 LoadPlace ready\n  2 WaitResult #0 -> 0\n  3 Halt\n"), nullptr, nullptr },
	{ TEXT("waitUntil with a timeout"), TEXT("global int stage;\nwaitUntil (stage == 3) timeout (20)\n{\n\tTalk(\"late\");\n}\nTalk(\"on\");"),
		TEXT("globals: int stage;\nwait #0: stage\nfunction <main>:\n  0 Nop\n  1 PushConst 20\n  2 Convert float\n  3 WaitBegin #0 +timeout\n  4 WaitCheck #0\n  5 LoadPlace stage\n  6 PushConst 3\n  7 Equal\n  8 WaitResult #0 -> 4\n  9 Jump -> 12\n  10 PushConst \"late\"\n  11 CallCommand Talk 1\n  12 PushConst \"on\"\n  13 CallCommand Talk 1\n  14 Halt\n"), nullptr, nullptr },
	{ TEXT("a timeout's seconds are a number"), TEXT("global bool b;\nwaitUntil (b) timeout (\"x\")\n{\n}"), nullptr, TEXT("(2:24)"), TEXT("A timeout's seconds needs a float, but this is a string.") },
	{ TEXT("a timeout's seconds can't be left out"), TEXT("global bool b;\nwaitUntil (b) timeout ()\n{\n}"), nullptr, TEXT("(2:24)"), TEXT("This value is missing.") },
	{ TEXT("a timeout's block is a block: its locals end with it"), TEXT("global bool b;\nwaitUntil (b) timeout (1)\n{\n\tint _n = 1;\n}\n_n = 2;"), nullptr, TEXT("(6:1)"), TEXT("'_n'") },
	{ TEXT("goto can't jump into a timeout's block"), TEXT("global bool b;\ngoto In;\nwaitUntil (b) timeout (1)\n{\n\tlabel In;\n}"), nullptr, TEXT("(2:1)"), TEXT("'goto In' jumps into a block") },
	{ TEXT("forEach over a name that's nothing here says so"), TEXT("forEach (_lamp in lamps)\n{\n}"), nullptr, TEXT("(1:19)"), TEXT("forEach needs to know the type of 'lamps'. It isn't a local, and this script is given no inputs. So it's read as a global") },
	{ TEXT("forEach over a name that's nothing in a function"), TEXT("function void F()\n{\n\tforEach (_l in lamps)\n\t{\n\t}\n}\nF();"), nullptr, TEXT("(3:17)"), TEXT("It isn't a local or a parameter of this function.") },
	{ TEXT("forEach over an undeclared global's element"), TEXT("forEach (_l in lamps[0])\n{\n}"), nullptr, TEXT("(1:"), TEXT("comes from a global that isn't declared in this script") },
	{ TEXT("waitUntil with heldFor"), TEXT("global bool b;\nwaitUntil (b) heldFor (2);\nTalk(\"on\");"),
		TEXT("globals: bool b;\nwait #0: b\nfunction <main>:\n  0 Nop\n  1 PushConst 2\n  2 Convert float\n  3 WaitBegin #0 +hold\n  4 WaitCheck #0\n  5 LoadPlace b\n  6 WaitResult #0 -> 4\n  7 PushConst \"on\"\n  8 CallCommand Talk 1\n  9 Halt\n"), nullptr, nullptr },
	{ TEXT("waitUntil with heldFor, a timeout and IgnoreHold"), TEXT("global bool b;\nwaitUntil (b) heldFor (2.5) timeout (9.5, true)\n{\n}"),
		TEXT("globals: bool b;\nwait #0: b\nfunction <main>:\n  0 Nop\n  1 PushConst 2.5\n  2 PushConst 9.5\n  3 PushConst true\n  4 WaitBegin #0 +hold +timeout +ignoreHold\n  5 WaitCheck #0\n  6 LoadPlace b\n  7 WaitResult #0 -> 5\n  8 Jump -> 9\n  9 Halt\n"), nullptr, nullptr },
	{ TEXT("heldFor's seconds are a number"), TEXT("global bool b;\nwaitUntil (b) heldFor (\"x\");"), nullptr, TEXT("(2:24)"), TEXT("heldFor's seconds needs a float, but this is a string.") },
	{ TEXT("heldFor's seconds can't be left out"), TEXT("global bool b;\nwaitUntil (b) heldFor ();"), nullptr, TEXT("(2:24)"), TEXT("This value is missing.") },
	{ TEXT("IgnoreHold is a bool"), TEXT("global bool b;\nwaitUntil (b) heldFor (2) timeout (5, 1)\n{\n}"), nullptr, TEXT("(2:39)"), TEXT("A timeout's second value (IgnoreHold) needs a bool, but this is an int.") },
	{ TEXT("waitUntil: a condition that can't change, with heldFor"), TEXT("bool _b = false;\nwaitUntil (_b) heldFor (5);"), nullptr, TEXT("(2:1)"), TEXT("can't change while the script waits") },
	{ TEXT("number: signature in errors"), TEXT("int _b;\n_b = Max(1);"), nullptr, TEXT("(2:6)"), TEXT("Max(number A, number B) -> number") },
};
