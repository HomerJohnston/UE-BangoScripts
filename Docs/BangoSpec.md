# Bango Language Spec (draft v0.41)

Bango is a text scripting language for Unreal Engine 5. It is meant for level and dialogue scripting, not heavy computation. Every script can also be shown and edited as a Blueprint-style node graph, so the text format is designed around that.

**Guiding rule:** any mistake, big or small, stops the script immediately and shows an error on screen. Nothing is quietly handled. Level designers have to fix every error.

**Changes in v0.41:** a **`switch`** runs the first case whose value equals the switch's value, or `default`: `switch (_x) { case 1: { ... } case 2, 3: { ... } default: { ... } }` (section 8). The value is an `int`, a `string` or an `entity`; case values are fixed values written out, and a value can only be in one case. There's no fallthrough, and `break`/`continue` act on the loop around the switch. In the graph it's a **Switch** node: a box with each case's values beside that case's output, then **Default** and **Then** (section 12). `switch`, `case` and `default` are keywords, and `:` is a new token.

**Changes in v0.40:** a **`sequence`** runs one block after another: `sequence { ... } { ... }`, each block to its end (waits and all) before the next, then what follows (section 8). In the graph it's a **Sequence** node with an output per block, named `Scope 0`, `Scope 1`, `Scope 2`, ..., and **Then**; **Add pin** adds an output and right-clicking one removes it (section 12). `sequence` is a keyword. A `{` block on its own is still an error; the error now suggests a sequence.

**Changes in v0.39:** a block no longer needs a `;` after its `}`: `if`, `while`, `for`, `forEach` and `function` blocks end with `}` (`};` still reads the same). The graph editor writes `}` (section 2).

**Changes in v0.38:** **`Exec(Path, ...)`** starts another script file from a script, with the extra values as its parameters, and gives back its id (section 7). Script handles (`FBangoScriptHandle`) are now 32-bit, like a Bango `int`; save data from earlier versions can't be loaded (section 11).

**Changes in v0.37:** `Log` is renamed **`WriteLog`**, and `Log` is now the **logarithm**. A script that still calls `Log("...")` gets an error that says to use `WriteLog`. **More math commands**: `Ceil`, `Sign`, `Sqrt`, `Pow`, `Log`, `Ln`, `Lerp`, `MapRange`, `InterpTo`, `DeltaTime`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2` (each also as `...Degrees`), `Pi`, `RandomBool`, `Chance` and `IsNearlyEqual`, and **vector commands**: `Length`, `Distance`, `Normalize`, `Dot`, `Cross` and `LerpVector` (section 7).

**Changes in v0.36:** **disabled nodes**. A node can be turned off without unwiring it: in the text it's written between `/*` and `*/`, and it does nothing when the script runs (execution goes straight on to the next node, and a disabled `if` or loop skips its block). What's between the markers is still code, not a comment, and the markers can be nested (section 1). In the graph, **Disable Node** / **Enable Node** in a node's right-click menu (or Ctrl+/) turns nodes off and on, and a disabled node is drawn faded with a **Disabled** banner (section 12).

**Changes in v0.35:** scripts can be **spawned from text** in C++ with `UBangoScriptSubsystem::SpawnFromSource`, as well as from a file (section 10). Their text is saved with them. Save data from earlier versions can't be loaded (section 11). **Conditions**: `UBangoScriptSubsystem::EvaluateBoolCondition` runs a script, or a single expression, that answers true or false, for other plugins and C++ to use (section 10).

**Changes in v0.34:** **debugging**, first part. While playing in the editor, the script editor's **Debug** dropdown lists the running copies of the script, and the graph outlines where the one being followed is: a pulsing outline on the node it's paused on, a dim one on the calls on the way there, and a red one where a copy stopped with an error. Nodes it has just run flash and fade out. The script text pane's gutter shows the same, line by line (section 12).

**Changes in v0.33:** **`INVTEXT("...")`**, a text that is never translated, like Unreal's `INVTEXT`: it needs no namespace or key, and isn't gathered for translators (section 3). In the graph, a text field's arrow has a **Translated** checkbox that switches between the two (section 12). Words Bango uses that aren't keywords (`LOCTEXT`, `INVTEXT`, `IsNil` and the like) can no longer be used as names (section 4).

**Changes in v0.32:** **hot reload**. While playing in the editor, saving a script switches its running copies to the new version where they are, without starting over: a loop that moves a platform picks up its new math at its next `Sleep`, keeping its variables. A copy that can't switch carries on with the old version, and the output log says why (section 11).

**Changes in v0.31:** **number commands**. A command can take a **number**, an `int` or a `float`, and give back a number whose type follows what it was given, like `+`: an `int` if every number passed in is an `int`, a `float` if any is a `float`. New math commands work this way: `Min`, `Max`, `Abs` and `Clamp`. Also new: `Sin`, `Cos`, `SinDegrees` and `CosDegrees`, and `GameTime()` is renamed `Time()` (section 7).

**Changes in v0.30:** `GetPos` takes its entity as an argument, `GetPos(_who)`, not as a target (section 7).

**Changes in v0.30, continued:** strings use the same placeholders as texts, Unreal's `{0}`, `{1}`, ... with backtick escapes, instead of `%0` and `%%` (section 7).

**Changes in v0.30, continued:** a local's name no longer has to start with `_`, and a global's may (section 4). A name means the graph's local, parameter or loop variable if there is one, and otherwise a global.

**Changes in v0.30, continued:** a script can have a **description**: the `//` lines it starts with, edited in the new **Script Description** box at the top of the script editor's left pane, and not shown in the graph (section 12). When saving, each part of the file gets a section heading: `// ============ SCRIPT INPUT ====================`, `GLOBAL VARS`, `SCRIPT VARS`, `SCRIPT BODY` and `FUNCTIONS`, and a `// ----------------------------------------------` line goes between functions. Reading skips any comment that starts with `// ====` (section 12). The script editor has a **script text pane** beside the graph, opened with the **Source Editor** tab on the graph area's left edge. It shows the script as it would be saved, and follows the graph; text typed there is applied to the graph with **Apply** (Ctrl+Enter), and a script whose text doesn't read can be fixed there (section 12).

**Changes in v0.29:** `IsDefined(x)` is replaced by **`IsNil(x)`**, which gives the opposite answer: `if (!IsDefined(_x))` is now `if (IsNil(_x))` (section 3). New **`IsNull`** command: `IsNull(_who)` is true if the entity is `nil`, or no actor in the level has its Bango ID because it was never there or has been destroyed (section 7).

**Changes in v0.28:** **declarations go at the top.** Every `global` declaration is in one block at the top of the file, after the `#define` lines, and has no value: `global int foundThings;` only says the script uses that global and what type it is, and it stays `nil` until code or a Blueprint sets it. Every local is declared at the top of its graph (the main graph after the globals, a function at the top of its body), is visible in the whole graph rather than one block, and its value, if it has one, must be a fixed value like a parameter default. `for` and `forEach` still declare their loop variables. A declaration anywhere else is a compile error (sections 4, 8 and 9). In the graph editor, declarations aren't nodes any more: the Declare nodes are gone, and the script editor's **Globals** and **Locals** lists (replacing **Variables**) add, rename, retype and remove them (section 12).

**Changes in v0.27:** the script editor has a toolbar across the top, and on its left three lists: **Functions** (add, rename and delete functions; drag one onto a graph to call it), **Inputs** (add, remove, reorder, rename and retype the shown graph's parameters, and edit the script's parameter defaults, which the Script Start node no longer shows) and **Variables** (the shown graph's locals and globals; click one to jump to its declaration). Dragging a variable or input onto the graph offers the nodes that work on it (Set, Add To, Increment, Push, ...). A function's graph has a **Back to Script Graph** button in its top-left corner (section 12).

**Changes in v0.26:** a script changed on disk by something other than the graph editor is **reloaded automatically**, or, if it has unsaved changes in the editor, a bar offers to reload it (section 12).

**Changes in v0.25:** **Refresh** only looks for scripts again; each script editor has its own **Reload** button (section 12).

**Changes in v0.24:** several scripts can be open at once in the Bango Scripts tab, one **script tab** each (section 12).

**Changes in v0.23:** the Bango Scripts tab is split into the scripts list and a **script editor** for one script (its graphs, toolbar and graph view), which can later be shown in a window of its own (section 12).

**Changes in v0.22:** the graph editor can **copy, cut, paste and duplicate** nodes (section 12).

**Changes in v0.21:** when saving, statements of the same kind (declarations, assignments, calls) are no longer separated by blank lines; flow control still always gets blank lines around it (section 12).

**Changes in v0.20:** **empty values**. A value can be left out right before `)` or `;` (`if ()`, `_x = ;`, `= 5;`, `++;`, `forEach (_e in )`, `label ;`): the line still reads, but doesn't compile until it's filled in (section 5). The graph editor's **add-node menu** (right-click, or drag a wire off a pin) uses this: new nodes start with their fields empty (section 12).

**Changes in v0.19:** metadata is written without spaces, `//@12(4, 7)`, and a reroute line can hold several reroutes in a row: `//@7(4, 9) @8(8, 13)`. When saving, reroutes get no blank lines around them (sections 1 and 12).

**Changes in v0.18:** **reroute nodes**. A line with nothing on it but node metadata, where a statement could go, is a reroute node: a bend in a wire that does nothing when the script runs (sections 1 and 12). Double-clicking a wire in the graph editor adds one, and deleting one keeps its wire joined.

**Graph pin names (v0.17):** If's outputs are **Do**, **Else** and **Then**; While's, For's and For Each's are **Do** and **Then** (section 12).

**Changes in v0.17:** new `vector2` and `vector3` types: two or three floats, the same as Unreal's FVector2D and FVector. A vector is written as an array literal of the right length (`vector3 _p = [1, 2, 3];`), its parts are `.x`, `.y` and `.z`, and vectors work with `+`, `-`, `*` and `/` by a number, unary `-`, `==` and `!=` (sections 3 and 5). `SetPos` now takes one `vector3`, and `GetPos` returns one (section 7). Vector pins on the Spawn Bango Script node, and **Set/Get Bango Global Vector2/Vector3** (sections 10 and 11).

**Changes in v0.16:** the `double` type is renamed `float`. Only the name changed: it's still a 64-bit (double-precision) number, so values, saves and Blueprint pins are the same. Writing `double` is a compile error that says to write `float` (section 3). The Blueprint nodes are now **Set/Get Bango Global Float** (section 11); the graph's declaration nodes are **Declare Float** and so on.

**Changes in v0.15:** the script's settings lines are written like C++ preprocessor lines, with no `;`: the parameters line is `#define params (...)`, and the namespace line is `#define LOCTEXT_NAMESPACE "Name"`, which needs `#undef LOCTEXT_NAMESPACE` as the script's last line (sections 3 and 10). Unreal's own localization gatherer now reads Bango scripts, set up from the Localization Dashboard; the separate gather step is gone (section 11). Text parameters can't have text defaults, since gatherers skip `#define` lines (section 10).

**Changes in v0.14:** localized text. A new `text` type holds text the player reads, written `LOCTEXT("Key", "Hello")` (or `NSLOCTEXT`, `NOTELOCTEXT`, `NSNOTELOCTEXT`), with a `locNamespace("...");` line at the top of the script (section 3). `Hint` now takes text with `{0}` placeholders; new `FormatText`, `ToText` and `ToString` commands (section 7). Text parameters and Text pins on the Spawn Bango Script node (section 10); **Set/Get Bango Global Text** and **OnHint** now passes a Text (section 11). Texts are gathered for translation by a localization gather step (section 11). In the graph, Script Start has a **Loc Namespace** row and text fields have an arrow that shows their Key, Namespace and Notes (section 12).

**Changes in v0.13:** actors are named with an `@`: `@john Speak("Hi");`. Bango IDs no longer create globals; they are a separate set of names, written as entity literals (sections 3 and 4). Entity parameters can now have defaults like `= @john` (section 10). New `ToEntity` command (section 7). In the graph editor, each type has its own declaration node (**Declare Int**, **Declare Global Int**, ...), replacing **Declare** and **Declare Global**, and `Hint`, `Log` and `Format` nodes show a row for every placeholder in their text; Compile warns about placeholders with no value, and they're saved as `nil` (sections 3 and 12). Text fields are typed without quotes or escapes. Command nodes no longer have a Result row: a line that stores a call's result is a **Set** or **Declare** node. Commands that return a value must have it used: `Format("x");` on its own is a compile error, and such commands never get a node of their own (sections 7 and 12). A `%n` in a literal text with no value for it is now a compile error (section 7).

**Changes in v0.12:** the graph editor shows each node's text as typed fields, gives every command and script function its own node, and can now edit, compile and save scripts (section 12). Variadic commands can choose how their extra values are labelled (section 7).

**Changes in v0.11:** functions are saved in depth-first order of first use (section 12). The graph editor's first version, a read-only viewer in the **Bango Scripts** tab, is described in section 12.

**Changes in v0.10:** the `params` line never has node metadata; it is always node 0 at (0,0), and no other node may use ID 0 (section 1). New section 12 describes the graph form that the graph editor reads and writes: pins, disconnected nodes (`//~` lines), comment bubbles and the layout it saves in.

**Changes in v0.9:** `int` is now 32-bit (section 3). The `hint` command is renamed `Hint` (section 7). A wrongly capitalised name gets a "Did you mean" suggestion. The parameter-aware **Spawn Bango Script** Blueprint node is done (section 10), and the old node is renamed **Spawn Bango Script (Dynamic Path)** (section 11).

---

## 1. Lines and nodes

- **One statement line = one graph node.** So is a reroute line (below).
- **A statement must fit on one line**, from its first token to its `;` (or, for `if`, `while`, `for` and `forEach`, to the `)` that ends the header). A statement that continues onto the next line is a compile error.
- **Only one statement per line.** `a(); b();` is a compile error, and so is `if (x) { a(); }` written on one line.
- Lines that contain only `{`, `}`, `};` or `else` are structure. In the graph they become wires and pins, not nodes. Structure tokens may share a line with a statement (`if (a) {`, `} else if (b) {`).
- Node metadata goes in a trailing comment that the compiler ignores:

  ```
  WriteLog("Hello");   //@12(4, 7)
  ```
  The format is `//@<NodeID>(<X>, <Y>)`. NodeID is a unique positive integer within the file (0 is reserved, see below). X and Y are integer grid coordinates and may be negative. One grid unit is 16 pixels in the graph editor (the Blueprint editor's snap grid).
- **All whitespace inside the metadata comment is optional and ignored.** These are all equivalent:
  ```
  //@12(4, 7)         <- default style the editor writes
  //@12(4,7)
  //@ 12 ( 4 , 7 )
  ```
- Metadata is optional, and the editor assigns it to lines that don't have it.
- **A reroute node is a line with only metadata on it**, where a statement could go: between two statements, or at the start or end of a block. It's a point the graph editor bends a wire through; it does nothing when the script runs. Indentation before the `//@` doesn't matter:
  ```
  WriteLog("Hello");   //@12(4, 7)
                  //@13(10, 7)

  WriteLog("World");   //@14(16, 7)
  ```
  Here node 13 is a reroute between the two `WriteLog` nodes.
- **Several reroutes in a row can share a line**: after the first entry, each further one starts with `@`, and they follow each other in that order:
  ```
                  //@7(4, 9) @8(8, 13) @9(23, 12)
  ```
  Only a reroute line can have more than one entry; a statement line is one node. Comments directly above a reroute line are its comment bubble, as for other nodes. A reroute can't go anywhere else, such as between an `if (...)` line and its `{`, before the `#define` lines, or after a function's `function` line and before its `{`.
- **The entry node is always node 0 at (0,0).** In the main graph that is the `#define params` line (or an implicit entry if there is none), and in a function's graph it is the function's implicit entry. Scripters move the rest of the graph around it.
- These are compile errors:
  - Badly formed metadata
  - Text after the `)`
  - The same NodeID used twice in a file
  - NodeID 0 on any line
  - Metadata on a `#define` or `#undef` line, or on a declaration
  - Metadata on a line with no statement, including a `function` line or a line with only `{`, `}` or `};`
  - A reroute line where a statement couldn't go
- Errors about the `params` line, and argument errors for a script with no `params` line, show `[node 0]`.
- Ordinary comments: `// ...`. A comment starting with `//@` is always metadata. There are no block comments: `/*` and `*/` mark **disabled nodes** (below), and what's between them must be code.

### Disabled nodes
- A statement between `/*` and `*/` is **disabled**: it's still a node in the graph, wired as before, but it does nothing when the script runs. Execution goes straight on to the statement after it, as if it were a reroute. A disabled `if`, `while`, `for` or `forEach` skips its whole block (and its `else`).
- A disabled simple statement is written on its line, with its metadata after the `*/`:
  ```
  WriteLog("a");                   //@1(0, 0)
  /* WriteLog("b"); */             //@2(4, 0)
  WriteLog("c");                   //@3(8, 0)
  ```
- A disabled block statement has `/*` and `*/` on lines of their own, at its indentation, around the whole statement, from its `if (...)` line to its last `}`:
  ```
  /*
  if (_ready)                 //@4(12, 0)
  {
  	/* WriteLog("not yet"); */     //@5(16, 4)
  	WriteLog("go");                //@6(16, 8)
  }
  */
  ```
- Unlike C++ comments, the markers **nest**: a statement inside a disabled block keeps its own setting (above, `WriteLog("not yet")` stays disabled when the `if` is enabled again). Each `/*` must be closed by a `*/` in the same block. `/*` and `*/` inside strings and `//` comments don't count.
- What's between the markers is **read as code**: it must be statements, each on its own line as usual, and can have `//` comments and reroutes among them. Prose there is an error that says to write notes as `//` comments. Disabled statements are parsed but not compiled: their names, types and commands aren't checked, and they can use names that don't exist.
- Only statements can be disabled. A declaration, a `#define` or `#undef` line or a `function` can't be: each is an error.
- A disabled `label` isn't in the compiled script, so a `goto` to it (or to a label inside a disabled block) is a compile error that says the label is disabled. A disabled `goto`, `break`, `continue` or `return` does nothing.
- `//~` lines are still only for disconnected nodes (section 12). A disconnected node can be disabled too: `//~ /* WriteLog("x"); */`.
- **Lines starting with `#` are the script's settings**, written like C++ preprocessor lines: `#define params (...)` (section 10), `#define LOCTEXT_NAMESPACE "Name"` and `#undef LOCTEXT_NAMESPACE` (section 3). They start their line, have a space after `#define` / `#undef`, don't end with `;`, aren't graph nodes and can't have metadata. Any other `#` line is a compile error. Future settings will use the same form.

### Statements that become nodes

| Statement | Node pins |
|---|---|
| `#define params (...)` | the script's entry node (node 0): exec out; shows the parameter list |
| Assignment / `+=` etc. / `++` / `--` | exec in → exec out |
| Command or function call | exec in → exec out |
| `if (cond)` | exec in → Do, Else, Then |
| `while (cond)` | exec in → Do, Then |
| `for (init; cond; step)` | exec in → Do, Then |
| `forEach (_x in arr)` | exec in → Do, Then |
| `sequence { ... } { ... }` | exec in → Scope 0, Scope 1, Scope 2, ... (one per block), Then |
| `switch (value) { case ...: { } default: { } }` | exec in → one output per case (beside a box with its values), Default, Then |
| `break;` / `continue;` | exec in → exec out |
| `return [expr];` | exec in → exec out |
| `label Name;` | exec in → exec out |
| `goto Name;` | exec in → exec out |

The exec out of `break`, `continue`, `return` and `goto` leads to whatever comes after them in their block. That code only runs if it's reached through a label (see section 12).

Declarations (section 4) aren't nodes: they're listed in the graph editor's side panel instead (section 12), so they can't have metadata. Expressions are typed as text inside the node, not drawn as separate data nodes. The `init` and `step` parts of a `for` header are part of the for node, not separate nodes.

### goto in the graph
A `goto` node **does not draw a wire** to its label. It shows the target label's name on the node. A `label` node is shown as a named entry point.

### Functions in the graph
- A `function` definition line is **not a node** and has no metadata.
- Each function has **its own graph**, picked in the script editor's Functions list (section 12). That graph starts at an implicit entry node, which has no metadata and is always node 0 at (0,0) of that graph. It shows the function's signature.
- Nodes inside a function body have metadata. Their coordinates belong to that function's graph, not the main script graph. NodeIDs are still unique across the whole file.

---

## 2. Blocks and semicolons

- A block that ends a statement (`if`, `while`, `for`, `forEach`, `sequence`, `switch` and `function`) closes with `}`. A `;` after it is allowed (`};`, as earlier versions required) but not needed; the graph editor writes `}`. The next statement starts on a new line.
- A `}` that is followed by `else` has no semicolon.
- `else if` is allowed. A whole `if / else if / else` chain ends with a single `}`. In the graph, the inner `if` is the node on the outer if's Else pin.
- A `{` block on its own, not belonging to one of the statements above, is a compile error. (To group statements, use a `sequence`, section 8.)

```
if (globalBoolVariable)
{
	WriteLog("true");
}
else if (otherBool)
{
	WriteLog("other");
}
else
{
	WriteLog("false");
}
```

---

## 3. Types and nil

| Type | Notes |
|---|---|
| `bool` | `true` / `false` |
| `int` | **32-bit signed**: -2147483648 to 2147483647. The same as a Blueprint Integer. |
| `float` | A 64-bit (double-precision) number, the same as a Blueprint Float. There's no 32-bit float type. Writing `double` is a compile error that says to write `float`. |
| `string` | `"..."` with escapes `\" \\ \n \t` |
| `entity` | Names an actor that has a Bango ID component. Written `@john`. Stored as the name and looked up each time it's used. |
| `text` | Localized text the player reads, the same as an Unreal `FText`. Written `LOCTEXT("Key", "Hello")` (see Localized text below). |
| `vector2` | Two floats, `x` and `y`. The same as an Unreal `FVector2D`. |
| `vector3` | Three floats, `x`, `y` and `z`. The same as an Unreal `FVector`, so it holds positions, directions and sizes. |
| `T[]` | Array of any type, including other arrays (`int[][]`), up to **8 levels** deep (see section 6). |

Use `float` for numbers that may not fit in an int.

### Vectors
A vector is written as an array literal with exactly the right number of numbers, wherever a vector is expected (a declaration, an assignment, a typed argument, a return value, a parameter default, an element of a `vector3[]`):
```
vector3 _pos = [100, 50, 75];
vector2 _uv = [0.5, 1];
vector3 _up;
vector3[] _path;

_up = [0, 0, _height];              // parts can be any number expression
_who SetPos([0, 0, 100]);
_path = [[0, 0, 0], _pos];
```
- `_v = [1, 2];` and `_v = [1, 2, 3, 4];` for a `vector3 _v` are compile errors: *"A vector3 needs exactly 3 numbers, like [1, 2, 3], but this has 2."*
- Each part must be a number (an int is turned into a float). A `nil` part is a compile error.
- Only a literal becomes a vector. A `float[]` variable can't be assigned to a vector, even one with 3 elements.
- An array literal next to a vector in an operator is a vector of the same kind: `_pos + [0, 0, 100]`, `_v -= [1, 0, 0];`.
- A vector variable declared without a value is `nil`, like any other type.

**Parts.** `_v.x`, `_v.y` and `_v.z` read and write the parts, which are floats. They work after array elements and call results too: `_path[2].y`, `GetPos(_who).z`.
```
_pos.z = 0;
_pos.x += 10;
_height = GetPos(_who).z;
```
- A `vector2` has only `x` and `y`. Parts are lowercase; `.X` and any other name after a `.` is a compile error.
- Setting a part of a `nil` vector stops the script; give the vector a whole value first. A part can't be set to `nil`.
- A part is never `nil` on its own, so `IsNil(_v.x)` is a compile error; use `IsNil(_v)`.
- `++` and `--` don't work on parts, since they are floats.
- Vectors are values, like arrays: `_b = _a;` copies the vector.
- `Format`, `WriteLog` and `Hint` show a vector as `(1.0, 2.0, 3.0)`.

### Literals
- Integers: `0`, `42`. A value larger than 2147483647 is a compile error; the message suggests writing it as a float (`2147483648.0`). Negative numbers are written with unary minus (`-5`). Because the minus is applied to `2147483648`, which is too large, the smallest int can't be written as a literal; use `-2147483647 - 1`.
- Doubles: `2.5`, `1.0`, `1e3`, `2.5e-3`. There must be a digit on both sides of the `.` (`0.5`, not `.5`; `1.0`, not `1.`).
- A number followed directly by a letter (`12abc`) is a compile error.
- Strings must end on the line they start. Any other escape than `\" \\ \n \t` is a compile error.
- Entities: `@` followed directly by a Bango ID name, like `@john` or `@door17` (section 4). No space is allowed after the `@`. An entity literal is a fixed value with the type `entity`, so it can be a parameter default and the compiler knows its type.
- Entity literals are never checked when the script compiles, since which actors exist depends on the level. Using one whose actor isn't in the level stops the script (see Conversions below).
- Texts: `LOCTEXT("Key", "Source")`, `NSLOCTEXT("Namespace", "Key", "Source")`, `NOTELOCTEXT("Key", "Source", "Notes")`, `NSNOTELOCTEXT("Namespace", "Key", "Source", "Notes")`, and `INVTEXT("Source")` for a text that isn't translated. Every part must be a string literal.

### Localized text
`string` is for names, ids and debug output; `text` is for anything the player reads, so it can be translated. The two are separate types: a string can't be passed where a text is expected, or the other way round, without `ToText` / `ToString` (section 7).

A text literal copies Unreal's macros. Each text has a **namespace**, a **key** and its **source** (the native-language text), and optionally **notes** for translators:

```
#define params (text _line = nil)
#define LOCTEXT_NAMESPACE "Quest.Intro"

if (IsNil(_line))
{
	_line = LOCTEXT("GuardHello", "Halt! Who goes there?");
}
Hint(_line);
Hint(NOTELOCTEXT("GuardBye", "Move along.", "Said by the gate guard, annoyed"));
Hint(NSLOCTEXT("Shared.UI", "Continue", "Continue"));

#undef LOCTEXT_NAMESPACE
```

- **`#define LOCTEXT_NAMESPACE "..."`** gives the namespace for every `LOCTEXT` / `NOTELOCTEXT` in the script, exactly as in Unreal C++ source, so Unreal's localization tools read it (section 11). It must come right after the `#define params` line (or first, if there is none), before any other statement or function, and only once. The namespace is a string and can't be empty. `NSLOCTEXT` / `NSNOTELOCTEXT` give their own namespace instead.
- A script with `#define LOCTEXT_NAMESPACE` **must end with `#undef LOCTEXT_NAMESPACE`**: the last line of the file, after the functions; only comments can follow it. An `#undef` anywhere else, or without a `#define`, is a compile error.
- A `LOCTEXT` in a script with no `#define LOCTEXT_NAMESPACE` is a compile error that says to add one.
- Keys and namespaces can't be empty. **The same key can't be used for two different texts** in one namespace: *"The key 'GuardHello' is already used in namespace 'Quest.Intro' for a different text, on line 2."* Writing the same text with the same key twice is fine.
- Keys made in the graph editor are random: 32 hex digits, like Unreal's own (`9F3A...`). They can be changed to something readable, in the graph or the text.
- **`INVTEXT("Source")`** is a text that is **never translated**: it's shown as it is in every language, like Unreal's `INVTEXT`. It has no namespace, key or notes, so it needs no `#define LOCTEXT_NAMESPACE`, and Unreal's gatherer doesn't pick it up. Use it for text the player sees that isn't words to translate, like a name, a number pattern (`INVTEXT("{0} / {1}")`) or a debug message. It works anywhere a text does, with the same `{0}` placeholders. `INVTEXT("x")` is the same as `ToText("x")`, but written as a literal, so its placeholders are checked when compiling.
- Texts can't be joined with `+`; use `FormatText` (section 7). Texts can only be compared with `==` and `!=`, which compare what they show in the current language.
- Text parameters can't have a text default (section 10); default them to `nil` and set the text in the script, as above.
- `Format("{0}", _text)` shows the text in the current language.

### nil
- `nil` is the **undefined value**. It is not a separate type: any variable, global or array element of any type can hold `nil`.
- A variable declared without a value holds `nil`.
- A global that has never been created anywhere behaves as `nil`.
- Array gaps created by resizing hold `nil` (see section 6).
- **Reading `nil` stops the script with an error.** This applies to variables, globals and array elements alike.
- `nil` can be **assigned, passed or returned** wherever its type is known: assigning to a variable, passing it as a typed argument, returning it from a typed function, or putting it in a typed array.
  ```
  _name = nil;
  _arr[2] = nil;
  ```
- `nil` with no known type is a compile error, like `[nil]` passed where any array would do. The exception is an extra value of a variadic command: `WriteLog("{0}", nil)` is allowed, and shows `nil`.
- The only way to test for `nil` is `IsNil(...)`. `x == nil` is a compile error, because comparing reads `x`.

### IsNil
- True if the value is `nil`. Built into the compiler, and never reads the value, so it never stops the script.
- Accepts a variable name, a global name or an array element. Anything else (a call, a literal, an expression) is a compile error.
  ```
  IsNil(_name)
  IsNil(playerHasGun)     // true if the global was never created
  IsNil(_arr[5])          // true if index 5 is nil or past the end
  IsNil(_grid[3][2])      // true if any level is nil or past the end
  ```
- The index expressions inside `IsNil(...)` are still evaluated normally, so `IsNil(_a[_i])` stops the script if `_i` is `nil`.
- `IsNil` is about values, not actors: an entity that names an actor that's gone isn't `nil`. `IsNull` checks both (section 7).
- It has no node of its own in the graph (it gives a value, like `Format`), so it's written in a field, such as an If's condition.

### Conversions
- Strongly typed. The only automatic conversion is **int → float**. It also applies to arrays at the same depth (`int[][]` → `float[][]`), to array literals (`float[] _d = [1, 2.5];`), and to script arguments and parameter defaults. Everything else needs an explicit conversion command, such as `Floor`, `Round` or `Format` (section 7).
- **Entities are checked before a command runs.** If a command's target or entity-typed argument names an actor that isn't registered (no Bango ID component with that name in the level), the script stops with an error naming the missing Bango ID. The command's code never runs. Storing, comparing and formatting an entity never checks it.
- A name that comes from data (a string) becomes an entity with `ToEntity(name)` (section 7). `Format("{0}", @john)` gives the name back as a string.
- A string becomes a text with `ToText(string)`; it isn't translated (it's shown as it is in every language), so use it for names and numbers, not for sentences. `ToString(text)` gives the text in the current language.

---

## 4. Variables

### Names
- **Names are case-sensitive**: `john` and `John` are different names. This covers variables, globals, functions, labels, commands, keywords, type names and Bango ID names (`@john` and `@John` are different actors).
- Calling a command or function with the wrong capitalisation is a compile error that suggests the right name: *"Unknown command or function 'hint'. Did you mean 'Hint'? Names are case-sensitive."*
- A name is letters, digits and `_`, and doesn't start with a digit.
- Keywords and type names can't be used as names: `if else while for forEach in sequence switch case default break continue return label goto function global params true false nil bool int float double string entity text vector2 vector3 void IsNil LOCTEXT NSLOCTEXT NOTELOCTEXT NSNOTELOCTEXT INVTEXT`. This doesn't apply to Bango ID names, since the `@` sets them apart.

### Locals
- Any name will do; a leading `_` is allowed but not needed (`count` and `_count` are both fine). The same goes for function parameters, script parameters and loop variables. Locals only exist inside the running script instance.
- **Name lookup:** a name means the graph's local, parameter or loop variable of that name if there is one, and otherwise a global. So a local can have a global's name, and then hides that global in its graph. A misspelled local that's only read compiles as a read of a global, which stops the script when it runs if there's no such global (section 4, Undeclared globals); a misspelled local that's written is a compile error, since only declared globals can be written.
- **Declared at the top of their graph**, with a type: the main graph's after the `#define` lines and the global declarations, and a function's at the top of its body, before its first statement. A declaration anywhere else (after a statement, or inside a block) is a compile error that says to move it to the top.
- **A local is visible in its whole graph**, not just one block, and holds its value until something changes it. A loop that needs a fresh value each time round sets it at the top of its body (`_tries = 0;`).
- **A declaration's value must be a fixed value**, like a parameter default (section 10): a number (including negatives like `-2.5`), a string, `true`, `false`, `nil`, a Bango ID (`@john`), a text (`LOCTEXT(...)` and the like, section 3), or an array of those (`[1, -2]`, `[[0, 0, 1]]`, a vector literal like `[0, 0, 100]`). It must fit the local's type (int → float is allowed). A variable, call, operator or any other expression is a compile error that says to assign it in the code instead: `int _n = Count(_a);` becomes `int _n;` at the top and `_n = Count(_a);` where it's needed. A local without a value holds `nil`.
- The values are given when the graph starts running: when the instance starts for the main graph, and on each call for a function.
- Writing to a name that isn't declared (as a local, parameter, loop variable or global) is a **compile error**. Reading one is a read of an undeclared global (see Name lookup above).
- Two locals of one graph can't share a name, or share one with a parameter of that graph. `for` and `forEach` loop variables are the exception to declaring at the top: they're declared by their loop's header and only exist inside the loop (section 8), so two loops one after the other can use the same name, but a loop variable can't have the name of a local, a parameter or the variable of a loop it's inside.
- Functions can't see the top level's locals (including script parameters), and the top level can't see a function's.

```
#define params (int _rounds = 3)

global int foundThings;

int _count = 0;
string _name;          // nil

_count = _count + 1;
_count += 2;
_count++;
```

### Globals
- Any name will do, including one starting with `_`. A local, parameter or loop variable of the same name hides the global in its graph (see Locals).
- They are shared across all scripts and live in a single global registry on the game instance, so they survive level changes.
- Declared with `global`, **at the top of the file**: after the `#define` lines and before everything else, including the main graph's local declarations. A `global` line anywhere else (later in the file, in a block, in a function) is a compile error that says to move it to the top. Functions use the file's globals like the main graph does.

```
global bool playerHasGun;
global int foundThings;
global string[] questLog;        // arrays can be global
```

- **A declaration has no value.** It only says the script uses that global, and what its type is. `global int foundThings = 0;` is a compile error: *"A global declaration can't have a value, since it would reset the global every time the script starts. Declare it here, and set it in the code (foundThings = 0;)."* A global holds `nil` until a script or a Blueprint sets it, and a script that needs a starting value sets one:
  ```
  if (IsNil(foundThings))
  {
  	foundThings = 0;
  }
  ```
- **All of a script's global declarations take effect when an instance starts, before any of its code runs.** A global that already exists keeps its value.
- Each global is declared once per file. Declaring the same global twice is a compile error.
- A declaration without `global` must be a local, so `int ArrayCount;` is a compile error. Write `int _arrayCount;` or `global int ArrayCount;` instead.
- A `for` header can't declare a global.
- Any number of scripts may declare the same global with the **same type**.
- A **different type** is a conflict. Whichever script hits the conflict second stops with an error. Because declarations take effect at instance start, this happens as soon as the conflicting script is spawned. The first script's global is kept.
- Blueprints can also set and read globals (section 11). Setting a global from Blueprint that doesn't exist yet declares it with that value's type.

### Bango ID names
- Adding a Bango ID component to an actor and giving it a name (e.g. `john`) lets scripts use that actor as `@john`. The name is registered when play begins.
- Bango ID names are **their own set of names**, separate from variables and globals. They don't create globals, so `@john` and a global called `john` can both exist without clashing.
- A name is letters, digits and `_`, and doesn't start with a digit. Keywords and a leading `_` are allowed (`@if`, `@_hidden`).
- An empty or invalid name, or a name already used by another actor in the level, shows an error on screen. The name is then not registered.
- When the actor is removed, its name is unregistered. Entity values naming it keep the name, and using them afterwards stops the script because the actor can't be found.
- Loading a save or resetting state doesn't change which names are registered.
- Writing a Bango ID without its `@` (`john Speak("Hi");`) reads a global called `john`. If there is no such global but there is an actor with that Bango ID, the error says so: *"Global 'john' doesn't exist. There is an actor with the Bango ID 'john': write @john to use it."*

### Undeclared globals (resolved at runtime)
A script may use a global without declaring it, for example one that another script declares or a Blueprint sets. The compiler can't know its type, so it is checked **when the script runs**:

- **Reading:** the global is looked up when it's used. If it doesn't exist, is `nil`, or has the wrong type for how it's being used (e.g. an `int` where a `bool` condition is expected, or a string as a command's target), the script stops with an error.
- **Writing:** assigning to a global the script hasn't declared is a **compile error**, and so is changing it with an array command (`questLog Push("x");`). A script must declare `global <type> name;` to write to it. That way a typo can't quietly create a new global.
- **Where the compiler needs a type**, an undeclared global is a compile error. Examples: `forEach (_x in questLog)` (the loop variable needs a type), and an array literal whose type depends on it.
- If a script declares the global, the compiler checks its type at compile time, and the global registry checks the declaration for conflicts when the script starts.

---

## 5. Expressions and assignment

Precedence, highest first:

1. `( )`, call, `[ ]` index, `.x` vector part, targeted call (`target Name(...)`)
2. `!` `-` (unary)
3. `*` `/` `%`
4. `+` `-`
5. `<` `<=` `>` `>=`
6. `==` `!=`
7. `&&`
8. `||`

All binary operators are left-associative: `a - b - c` is `(a - b) - c`.

| Operator | Operands | Result |
|---|---|---|
| `+` | two numbers, or two strings | int if both are ints, float if either is a float; string for two strings |
| `-` `*` `/` `%` | two numbers | int if both are ints, otherwise float |
| `+` `-` | two vectors of the same kind | that vector type |
| `*` | a vector and a number (either order) | the vector type, each part multiplied |
| `/` | a vector, then a number | the vector type, each part divided |
| `<` `<=` `>` `>=` | two numbers (**not strings**) | bool |
| `==` `!=` | two values of the same type, or two numbers | bool (vectors compare every part) |
| `&&` `\|\|` | two bools | bool |
| `!` | bool | bool |
| unary `-` | number or vector | same type |

- `&&` and `||` **short-circuit**, so `if (!IsNil(x) && x)` is safe.
- `int / int` gives `int`, rounded toward zero. `%` on floats gives the floating-point remainder.
- **Dividing by zero stops the script**, for both ints and floats, with `/` or `%`.
- **Integer overflow stops the script.** An int result outside -2147483648 to 2147483647 is an error that points at the operator, e.g. `2147483647 + 1`, `65536 * 65536`, `(-2147483647 - 1) / -1`, or negating -2147483648. It never wraps around. Mixing in a float (`65536 * 65536.0`) gives a float instead.
- `==` on strings and entities is case-sensitive.
- Joining a string with any other type (`"a" + 1`) is a compile error.
- Arrays can't be used with any operator, including `==`.
- Vectors can't be added to numbers (`_v + 1`), multiplied together (`_a * _b`), used with `%`, or compared with `<` `<=` `>` `>=`. A number can't be divided by a vector. Dividing a vector by zero stops the script.
- `nil` can't be used with any operator.
- A call that returns nothing can't be used as a value.
- Conditions in `if`, `while` and `for` must be `bool`. There is no "truthy".
- When one side is an undeclared global, the compiler checks what it can (e.g. the other side must be a number for `-`), and the rest is checked when the script runs.
- A single `&` or `|` is a compile error.
- Expressions can be nested up to 64 levels deep (parentheses, unary operators and blocks combined).

### Empty values
A value can be **left empty** where it comes right before a `)` or `;`: `if ()`, `while ()`, `_x = ;`, `_a > ;`, `forEach (_e in )`. So can the variable of an assignment or `++`/`--` (`= 5;`, `+= 2;`, `++;`) and the name after `label` or `goto` (`label ;`). Such a line reads, so it can be saved, but **it doesn't compile**: the error says what's missing ("This value is missing. Fill it in."). This is how the graph editor saves a new node whose fields haven't been filled in yet (section 12). A value can't be left out before a `,` (`Hint("{0}", , 2);` is an error), and a missing argument at the end is simply an argument that wasn't given.

### Assignment statements
| Form | Meaning | Allowed types |
|---|---|---|
| `x = e;` | assign | any |
| `x += e;` | `x = x + e;` | `int`, `float`, `string`, vectors |
| `x -= e;` | `x = x - e;` | `int`, `float`, vectors |
| `x *= e;` | `x = x * e;` | `int`, `float`, vectors (by a number) |
| `x /= e;` | `x = x / e;` | `int`, `float` (an int variable does integer division), vectors (by a number) |
| `x++;` / `++x;` | `x += 1;` | `int` |
| `x--;` / `--x;` | `x -= 1;` | `int` |

- These are all **statements, not expressions**. `y = x++;`, `y = ++x;` and `y = (x += 1);` are compile errors.
- Assignments can't be chained: `_a = _b = 1;` is a compile error.
- All of them except `=` read `x` first, so they stop the script if `x` is `nil`. For an array element (`_g[1][2] += 5;`), that includes an inner array that doesn't exist yet.
- The result of a compound assignment must fit the variable: `_i += 1.5;` for an `int _i` is a compile error because the result is a float.
- The left side may be a variable, a declared global, an array element (`_a[i] += 1;`, `_grid[i][j] = 0;`) or a vector part (`_pos.x = 0;`, `_path[i].y += 1;`). Anything else, such as a call, is a compile error. Index expressions on the left side are evaluated once.
- Assigning a `float` value to an `int` is a compile error. `int → float` is allowed.
- A line that only computes a value (`_x + 1;`) is a compile error. Only declarations, assignments, `++`/`--` and calls can be statements.

---

## 6. Arrays

```
global string[] questLog;

int[] _intArray = [1, 2, 3];
string[] _names = [];
int[][] _grid = [[1, 2], [3]];
```

- Elements can be any type, including other arrays, up to 8 levels deep (`int[][][][][][][][]`).
- Array types are always written with empty brackets. `int[3]` is a compile error.
- Inner arrays can be different lengths.
- **Arrays are values.** `_b = _a;` copies the whole array, including any inner arrays, so changing `_b` afterwards doesn't change `_a`. This also keeps saving and loading simple.

### Array literals
- Where the expected type is known (a declaration, an assignment, a typed argument, a return value), each element must fit the element type: `_a = [1, "x"];` for an `int[] _a` is a compile error.
- Where it isn't known (e.g. an extra argument to `Hint`), the type is worked out from the elements: `[1, 2]` is `int[]`, `[1, 2.5]` is `float[]`, `[[], [1]]` is `int[][]`. Mixing types that don't fit together (`[1, "x"]`) is a compile error.
- `[]` (or an array of only `nil`s) whose type can't be worked out is a compile error, e.g. `Count([])`. Declare a typed variable first.
- An array literal where a non-array is expected is a compile error, except where a vector is expected (section 3, Vectors).

### Indexing
```
_x = _intArray[0];
_intArray[1] = 17;
_y = _grid[0][1];
_grid[1][0] = 5;
```
- Indices start at 0 and must be ints. A negative index stops the script with an error.
- **Writing past the end grows the array.** Every new slot before the written index holds `nil`.
  ```
  int[] _a = [1, 2];
  _a[5] = 9;          // _a is now [1, 2, nil, nil, nil, 9]
  ```
- **Reading past the end stops the script with an error**, like any other `nil` read. A read never grows the array. Use `IsNil(_a[i])` to check first.
- Using more indices than the array has levels (`_intArray[0][0]`), or indexing a non-array, is a compile error.

### Nested writes
Writing through several indices grows every level as needed. **Any missing or `nil` inner array along the way is created automatically.**
```
_grid = [[1, 2], [3]];
_grid[3][2] = 7;    // _grid is now [[1, 2], [3], nil, [nil, nil, 7]]
```
- Only the inner arrays on the path being written are created. Other gap slots, like `_grid[2]` above, stay `nil`.
- The outermost array itself must not be `nil`. With `int[][] _g;` declared, `_g[0][0] = 1;` stops the script. Declare it as `int[][] _g = [];`, or set `_g = [];` first.
- Everything is checked before anything changes, so a write that fails (wrong type, negative index, too large) leaves the array untouched.
- Reading through a `nil` or missing inner array (`_v = _grid[2][0];`) stops the script.

### Array commands
These are built into the compiler. `Count`, `Push`, `InsertAt`, `Pop` and `RemoveAt` can't be used as names for C++ commands or script functions.

A command that changes an array needs a variable, a declared global or an array element as its target. An array element target lets you change an inner array in place:
```
_grid[1] Push(4);
```
The target is read, not written, so it must already exist and not be `nil`.

| Command | Effect | Errors |
|---|---|---|
| `Count(_a)` → `int` | Number of slots, including `nil` slots. Its value must be used. | — |
| `_a Push(v);` | Adds `v` at the end | — |
| `_a InsertAt(v, i);` | Inserts `v` at index `i` and moves later elements up. If `i` is past the end, the gap is filled with `nil`, as with writing. | negative `i` |
| `_a Pop()` → `T` | Removes and returns the last element | empty array; last element is `nil` |
| `_a RemoveAt(i)` → `T` | Removes and returns element `i` and moves later elements down | `i` out of range; element is `nil` |

```
_arrayCount = Count(_intArray);
_intArray Push(8);           // adds 8 at the end
_intArray InsertAt(8, 0);    // adds 8 at index 0
_last = _intArray Pop();
_first = _intArray RemoveAt(0);
_row = _grid Pop();          // removes and returns the last row
```

`Pop` and `RemoveAt` can also be used as statements to throw the value away (`_intArray Pop();`). In that case removing a `nil` element is allowed.

Every array can hold up to 1,000,000 elements. Growing past that stops the script.

---

## 7. Commands

Commands are defined in C++ at the project or engine level. Scripts can only use commands that have been registered.

```
[Target] CommandName(Args);
```

```
@player SetPos(200, 500, 900);
@john Speak("Hello.");
AwaitSpeak();
AwaitSpeak(0.5);
@door17 SetLocked(true);
WriteLog("There are {0} things left.", _thingCount);
_t = Random(3.0, 5.0);
```

- Command names are written in PascalCase (`Hint`, `SetPos`, `RandomInt`), and so are their parameter names (`Msg`, `Time`, `Min`), since they label the fields of the command's graph node. A parameter can also have a short description, shown when hovering its value on the node: `.Param(TEXT("Time"), Double, TEXT("Seconds"))`. Registering a command with a parameter name that starts with a lowercase letter logs a warning.
- For the graph editor, a signature can also have a category and a one-line description (`.InCategory(TEXT("Actors")).Describe(TEXT("Moves the actor."))`), and a variadic command can choose how its extra values are labelled (`.Variadic(TEXT("{"), 0, TEXT("}"))` gives `{0}`, `{1}`, ...; the default gives `Value 1`, `Value 2`, ...). `.WithPlaceholders()` after a parameter marks it as a text whose `{0}`, `{1}`, ... the extra values fill, as `Hint`, `WriteLog` and `Format` do; the graph editor then shows a row for each placeholder (section 12).
- A signature also says whether the command is one of the plugin's own (`bBuiltIn`). `Bango::RegisterBuiltinCommands` sets it on every command it registers; a game's commands leave it false (the default), so there's nothing extra to set when defining one. The plugin's categories are Actors, Flow Control (`Sleep`, `Exec`), Math, Text, Time and Vectors.
- A command's signature declares:
  - **Target:** either none, or a required target of one type. A command with a target can't be called without one, and vice versa. *Guideline (not yet a fixed rule):* give a command a target only if it also takes at least one argument (`@john Speak("Hi")`, `_who SetPos(_p)`). A command that only needs the thing it works on takes it as its first and only argument instead (`GetPos(_who)`, `IsNull(_who)`).
  - **Parameters:** typed. Trailing parameters may be **optional**. Normally a `nil` variable passed to a command, or an entity naming no actor, stops the script before the command runs. `.MayBeNil()` after a `.Param(...)` hands the command that argument as it is instead, `nil` included, and leaves the actor check to the command (as `IsNull` does).
  - **Number parameters:** `.NumberParam(TEXT("A"))` adds a parameter that takes an `int` or a `float` and hands it on as it is (a `float` parameter would turn an `int` into a `float`). Signatures, errors and the graph editor show its type as `number`. An undeclared global passed to one is checked to be a number when the command is called.
  - **Variadic:** optionally, any number of extra arguments of any type after the parameters (like `Hint`'s `{0}` values).
  - **Return type:** optional. `.ReturnsNumber()` gives back a **number** whose type follows the number parameters, like `+` (section 5): an `int` if every number argument is an `int`, a `float` if any is a `float`. The compiler works the type out at each call, so `_i = Max(_a, 2.5);` for an `int _i` is a compile error, the same as `_i = _a + 2.5;`; say how to make it an int (`_i = Round(Max(_a, 2.5));`). If one of them is an undeclared global (and none is a `float`), the type is known only when the script runs, and storing a `float` in an `int` stops the script then. The command must give back that type (an `int` where a `float` is expected is turned into a `float`; a `float` where an `int` is expected stops the script), and such a command can't be latent.
  - **Discardable:** for a command with a return type, whether a script may call it on a line of its own and throw the value away (`.Discardable()`). Without it, the value **must be used** (see below).
- Calling a command with the wrong number of arguments, or an argument or target of the wrong type, is a compile error. The error shows the command's signature.
- Arguments and targets that are undeclared globals are type-checked when the script runs.
- The target can be any value: a Bango ID (`@john Speak(...)`), a variable, an array element (`_grid[1] Push(4)`), or a call result. The command name must be on the same line as the target.
- A call always needs parentheses, even with no arguments: `QuitGame();`. `@john Speak;` is a compile error.
- Calling an unknown command is a compile error. If only the capitalisation is wrong, the error suggests the right name (section 4).
- Calling a command that returns nothing where a value is needed (`_x = QuitGame();`) is a compile error.
- **A command's value must be used** unless its signature says `.Discardable()`, like C++'s `[[nodiscard]]` the other way round. Calling such a command on a line of its own is a compile error: *"'Format' gives a value that must be used, like '_result = Format(...);'. On a line of its own its value would be thrown away."* Its value can be used anywhere a value goes: an assignment, a condition, another command's argument, a `return`. Mark a command `.Discardable()` when calling it is useful by itself, like a latent dialogue choice whose pausing is the point (`.Returns(Int).Discardable()`). Every built-in command that returns a value must have it used. Script functions can always be called on a line of their own (section 9).
- **Any command may pause the script** ("latent"). The script resumes when the command finishes. The graph doesn't draw latent commands differently.
- Latent commands can appear inside expressions, e.g. `if (AskChoice("A", "B") == 0)`. The interpreter can pause partway through evaluating an expression.
- A latent command keeps its pending state (e.g. Sleep's remaining seconds) as Bango values, so a script paused on it can be saved and loaded.
- A command that fails stops the script with the command's name and reason, e.g. `RandomInt: Min (5) is greater than Max (1).`
- If a script calls a command that isn't registered or has no implementation in the running game, the script fails when it's spawned, not when it reaches the call.

### Text formatting
Strings and texts use the same placeholders, Unreal's: `{0}`, `{1}`, ..., with a backtick escaping the next character (`` `{ `` is a literal `{`, ``` `` ``` a backtick). `%` is just a character.

- **Strings** (`WriteLog`, `Format`): described below.
- **Texts** (`Hint`, `FormatText`): filled by `FText::Format`, so translators can move them around: `Hint(LOCTEXT("Left", "{0} things left"), _count)`. Ints and floats are formatted as numbers for the current language, texts are inserted as they are, and anything else (strings, entities, arrays, bools) is inserted as it would be by `Format`, untranslated. A backtick escapes the next character (`` `{ `` is a literal `{`). Named placeholders like `{Name}` aren't supported: they're a compile error when the text is a literal, and stop the script otherwise. Unreal's plural and gender forms (`{0}|plural(one=thing,other=things)`) work, since they're part of the translated text.

The rules for which placeholders need values below apply to both kinds.

`WriteLog` and `Format` replace `{0}`, `{1}`, ... in their text with the extra arguments, in one pass, with `FText::Format`'s rules: a backtick makes the next character literal, and anything else that isn't a numbered placeholder (`{Name}`, a `{` with no `}`) is shown as it is. Strings appear without quotes, entities as their name, arrays as `[1, 2]`. A `nil` value shows as `nil`.

Every placeholder needs a value. When the text is written as a literal (`WriteLog("{0} and {1}", _a);`, `Hint(LOCTEXT("K", "{0} and {1}"), _a);`), a placeholder with no value after it is a **compile error**: *"This text uses {1}, but only 1 value comes after it."* This applies to any command marked `.WithPlaceholders()`, wherever the call is (`_s = Format("{0}", _a) + Format("{1}", _b);` fails on the second). When the text is worked out while the script runs (`WriteLog(_text, 1);`), a placeholder with no value stops the script. A `nil` value for a text placeholder stops the script too.

### Built-in commands

| Command | Effect |
|---|---|
| `Hint(text Msg, ...)` | Shows text to the player, with `{0}`, `{1}`, ... filled in. Games show it in their own UI by binding the script subsystem's **OnHint** event (which gets a Text); otherwise it appears as on-screen debug text. |
| `WriteLog(string Msg, ...)` | Writes text to the output log. |
| `Format(string Text, ...)` → `string` | Returns the formatted text. Also the way to turn numbers into strings. |
| `FormatText(text Text, ...)` → `text` | Returns the text with `{0}`, `{1}`, ... filled in, still translatable. |
| `ToText(string Value)` → `text` | The string as a text that isn't translated. |
| `ToString(text Value)` → `string` | The text in the current language. |
| `Sleep(float Time = optional)` | Pauses the script for `Time` seconds. `Sleep()` (no time) and `Sleep(0)` wait until the next frame. Negative times are an error. |
| `Random(float Min, float Max)` → `float` | A random number from Min to Max. |
| `RandomInt(int Min, int Max)` → `int` | A random int from Min to Max inclusive. |
| `Floor(float Value)` → `int` | Rounds down. A result outside the int range is an error. |
| `Round(float Value)` → `int` | Rounds to the nearest int (halves away from zero). A result outside the int range is an error. |
| `Min(number A, number B)` → `number` | The smaller of the two: an `int` if both are ints, otherwise a `float`. |
| `Max(number A, number B)` → `number` | The larger of the two: an `int` if both are ints, otherwise a `float`. |
| `Abs(number Value)` → `number` | The number without its sign: an `int` for an int, a `float` for a float. `Abs(-2147483647 - 1)` is an error, since 2147483648 doesn't fit in an int. |
| `Clamp(number Value, number Min, number Max)` → `number` | The value, kept from Min to Max: an `int` if all three are ints, otherwise a `float`. Min greater than Max is an error. |
| `Sin(float Rads)` → `float` | The sine of an angle in radians. |
| `Cos(float Rads)` → `float` | The cosine of an angle in radians. |
| `SinDegrees(float Degrees)` → `float` | The sine of an angle in degrees. |
| `CosDegrees(float Degrees)` → `float` | The cosine of an angle in degrees. |
| `Ceil(float Value)` → `int` | Rounds up. A result outside the int range is an error. |
| `Sign(number Value)` → `number` | -1 if the number is negative, 1 if it's positive, 0 if it's 0: an `int` for an int, a `float` for a float. |
| `Sqrt(float Value)` → `float` | The square root. A negative number is an error. |
| `Pow(float Base, float Exponent)` → `float` | The base raised to the power of the exponent: `Pow(2, 3)` is `8.0`. Always a float, since `Pow(2, -1)` is `0.5` and big powers don't fit an int. A negative base with an exponent that isn't a whole number, 0 to a negative power, and a result too large for a float are errors. |
| `Log(float Value, float Base = optional)` → `float` | The logarithm: the power the base must be raised to to give the value. The base is 10 if none is given: `Log(1000)` is `3.0`, `Log(8, 2)` is `3.0`. A value of 0 or less, and a base of 0 or less or of 1, are errors. (To write to the output log, use `WriteLog`.) |
| `Ln(float Value)` → `float` | The natural logarithm (base *e*). A value of 0 or less is an error. |
| `Lerp(float A, float B, float Alpha)` → `float` | Blends from A to B: Alpha 0 gives A, 1 gives B, 0.5 halfway between. Alpha isn't clamped, so 2 goes as far past B again, like Unreal's Lerp. |
| `MapRange(float Value, float InMin, float InMax, float OutMin, float OutMax)` → `float` | Where the value is from InMin to InMax, as the same place from OutMin to OutMax, kept in that range (Unreal's MapRangeClamped): `MapRange(_dist, 0, 1000, 1, 0)` is 1 up close and 0 from 1000 away. Either range can go down as well as up. InMin equal to InMax is an error. |
| `InterpTo(float Current, float Target, float Speed)` → `float` | One frame's step from Current towards Target, fast when far and slowing as it gets close (Unreal's FInterpTo, with this frame's `DeltaTime()`). Call it once a frame, after `Sleep()`: `_x = InterpTo(_x, 100, 2);`. A speed of 0 jumps straight to the target; a negative speed is an error. |
| `DeltaTime()` → `float` | How long the last frame took, in seconds (the world's delta time: it follows time dilation and is 0 while paused). Multiply a speed by it to move the same distance per second at any frame rate. |
| `Tan(float Rads)` → `float`, `TanDegrees(float Degrees)` → `float` | The tangent of an angle in radians, or in degrees. |
| `Asin(float Value)`, `Acos(float Value)` → `float` | The angle in radians whose sine, or cosine, is the value. The value must be from -1 to 1. `AsinDegrees` and `AcosDegrees` give the angle in degrees. |
| `Atan(float Value)` → `float` | The angle in radians whose tangent is the value. `AtanDegrees` gives degrees. |
| `Atan2(float Y, float X)` → `float` | The angle in radians of the direction (X, Y) from the X axis, from -pi to pi. Note that Y comes first, as in C++. `Atan2Degrees` gives degrees, from -180 to 180: the way to turn a direction into a facing angle. |
| `Pi()` → `float` | 3.14159..., half a turn in radians. |
| `RandomBool()` → `bool` | `true` or `false` at random, each half the time. |
| `Chance(float Probability)` → `bool` | `true` at random, as often as the probability says: `if (Chance(0.3)) { ... }` runs 30% of the time. 0 is never and 1 is always; a probability outside 0 to 1 is an error. |
| `IsNearlyEqual(float A, float B, float Tolerance = optional)` → `bool` | True if the two numbers are at most the tolerance apart (0.0001 if none is given). Use it rather than `==` for floats that come out of math, since `0.1 + 0.2 == 0.3` is false. A negative tolerance is an error. |
| `Length(vector3 Vector)` → `float` | The vector's length. |
| `Distance(vector3 A, vector3 B)` → `float` | The distance between two points. For two actors, use `DistanceTo`. |
| `Normalize(vector3 Vector)` → `vector3` | The vector made 1 long, keeping its direction. `[0, 0, 0]` (or a vector too small to have a direction) is an error. |
| `Dot(vector3 A, vector3 B)` → `float` | The dot product. For two directions 1 long, it's 1 if they're the same, 0 at right angles and -1 if they're opposite. |
| `Cross(vector3 A, vector3 B)` → `vector3` | The cross product: a vector at right angles to both. |
| `LerpVector(vector3 A, vector3 B, float Alpha)` → `vector3` | Blends from A to B, like `Lerp`. |
| `Exec(string Path, ...)` → `int` | Starts another script: the path is relative to this script's folder (`"Intro"`, `"../Shared/Greet"`) or a full asset path (`"/Game/Quests/Intro"`) (section 15). The extra values are its parameters, in order, checked as `Spawn` checks them. The new script runs on its own, alongside this one: it runs straight away until it first pauses (or ends), and then this script carries on from the `Exec`. Gives back the new script's id, which can be ignored (`Exec("Doors/OpenAll");` on its own line is fine). A script that can't be found, doesn't compile, or whose parameters don't fit stops this script with that error; an error the new script hits once it's running stops only it. A chain of more than 32 scripts each started by the one before, before it pauses (a script that runs `Exec` on itself straight away), is an error. Needs a game world. |
| `Time()` → `float` | The world's time (`GetWorld()->GetTimeSeconds()`): seconds since the level started; stops while the game is paused. |
| `ToEntity(string Name)` → `entity` | The entity with this Bango ID name, for names that come from data (`ToEntity("john")` is the same as `@john`). A name that isn't letters, digits and `_` (or starts with a digit) is an error. |
| `entity SetPos(vector3 Pos)` | Moves the actor. |
| `GetPos(entity Entity)` → `vector3` | The actor's location. |
| `entity SetHidden(bool Hidden)` | Hides or shows the actor in game. |
| `entity DistanceTo(entity Other)` → `float` | Distance between two actors. |
| `IsNull(entity Entity)` → `bool` | True if the entity is `nil`, or no actor in the level has its Bango ID: it was never there (or never registered, like a duplicate name), or it's been destroyed. It never stops the script for those: a `nil` variable, a global nobody made or an element past the end counts as `nil` here, and a missing actor isn't an error. Use it before a command that would stop the script: `if (!IsNull(_who)) { _who SetHidden(true); }`. |

Game-specific commands (`Speak`, `AwaitSpeak`, `SetLocked`, dialogue choices, ...) are written in C++ by each project and registered with `FBangoModule::Get().RegisterCommand(...)`.

---

## 8. Control flow

### if / while
```
if (cond) { ... }
if (cond) { ... } else { ... }
if (cond) { ... } else if (cond2) { ... } else { ... }
while (cond) { ... }
break;
continue;
```
(Written on separate lines in practice, since each statement inside a block needs its own line.)

- `break` and `continue` outside a loop are compile errors.

### for
```
for (int _i = 0; _i <= 8; _i++)
{
	// _i is a local, scoped to the loop
}
```
- `init` is one local declaration or assignment (or empty). `cond` must be `bool` (or empty, meaning `true`). `step` is one assignment statement (`=`, `+=`, `++`, etc.) or empty. Calls and global declarations are not allowed in `init` or `step`.
- A variable declared in `init` is local to the loop: it only exists inside it, and its value can be any expression (`for (int _i = Count(_a) - 1; _i >= 0; _i--)`). This and `forEach` are the only declarations allowed below the top of a graph (section 4). `init` can also assign a local declared at the top (`for (_i = 0; ...)`), which keeps its value after the loop.
- `continue` runs `step` and then checks `cond` again.
- The whole `for (...)` header is **one node** and must be on one line.

### forEach
```
forEach (_x in _myArray)
{
	// _x is a local holding the current element
}
```
- Declares the local `_x`, whose type is the array's element type and which only exists inside the loop. `_x` can't have the name of a local, a parameter or another loop's variable it's inside. For an `int[][]`, `_x` is an `int[]` (a copy of that row).
- The array must have a type known to the compiler, so looping over a global the script hasn't declared is a compile error.
- The array is copied once, when the loop starts. Changing the array inside the loop doesn't affect which elements the loop visits.
- A `nil` element is visited too. Reading `_x` then stops the script, so check `IsNil(_x)` if the array might have gaps.
- `break` and `continue` work as usual.

### sequence
`sequence` is followed by one or more blocks, each `{` on the line after the previous block's `}`. It runs the blocks in order, each **to its end** before the next starts, then carries on with what follows the last block:

```
sequence                              //@5(20, 0)
{
	@gate OpenDoor();
	Sleep(2);
}
{
	Hint(LOCTEXT("Open", "The gate is open."));
}
WriteLog("both done");                // after both blocks
```

- A block that waits (`Sleep`, or any command that takes time) holds up the next one: the second block above starts two seconds later. Unlike Blueprint's Sequence node, the blocks never run at the same time. To run two things at once, start another script with `Exec` (section 7).
- It only groups statements, to keep a graph tidy: it makes no scope, and `break`, `continue`, `return`, labels and `goto` work in its blocks just as they do around it (a `break` in a sequence in a loop leaves the loop).
- A block can be empty (`{` then `}`): an output with nothing on it.
- The `//@` metadata goes on the `sequence` line. Disabling a sequence disables all its blocks.

### switch
`switch (value)` is followed by `{`, then its cases, each `case` and its values, a `:` and a block, then an optional `default:` and a block, then `}`:

```
switch (_answer)                      //@6(20, 0)
{
	case 1:
	{
		Hint(LOCTEXT("Yes", "Agreed."));
	}
	case 2, 3:
	{
		Hint(LOCTEXT("Maybe", "Let me think."));
		Sleep(1);
	}
	default:
	{
		Hint(LOCTEXT("No", "No deal."));
	}
}
WriteLog("answered");                 // after whichever case ran
```

- The value is evaluated once. It must be an `int`, a `string` or an `entity` (not an array); anything else is a compile error, as is an undeclared global (declare it first, so its type is known). Reading a `nil` value stops the script, as reading `nil` always does.
- Each case lists one or more values, separated by commas. They must be **fixed values written out** of the switch value's type: an int (`3`, `-1`), a string (`"north"`) or a Bango ID (`@guard`); not names, calculations or constants made of several parts. A value can only be in one case: a repeat is a compile error. Strings compare with case (`"a"` doesn't match `"A"`).
- The first case with an equal value runs, to its end (waits and all), then the script carries on after the switch. **There's no fallthrough**: a case never runs into the next one, and needs no `break`.
- If no case matches, `default` runs; with no `default`, nothing does. `default` comes last, at most once. A switch can have no cases at all.
- A switch isn't a loop: `break` and `continue` inside it act on the loop around it (a `break` in a case leaves that loop), as do `return`, labels and `goto` as usual.
- A case's block can be empty: a case with nothing on it.
- Only `case` and `default` parts can be inside the switch's braces; statements go in their blocks. The `case` and `default` lines aren't nodes (they're rows of the switch node), so they can't have `//@` metadata, and nothing else can sit between the cases. The `//@` metadata goes on the `switch` line. Disabling a switch disables all its cases.

### Labels and goto
```
label Hub;
...
goto Hub;
```
- Label names are unique within their function, or within the top level of the script. A duplicate is a compile error.
- `goto` can only jump within the same function or the same top level. Jumping to a label that doesn't exist there is a compile error.
- `goto` can jump **out of** blocks, including loops, and to a label later in the same block, but not **into** a block that doesn't contain it (including a sibling block). Breaking this rule is a compile error.

---

## 9. Functions

```
function int AddScore(int _amount)
{
	return _amount * 2;
}

function void Greet(entity _who)
{
	int _times = 2;

	for (int _i = 0; _i < _times; _i++)
	{
		_who Speak("Hello.");
		AwaitSpeak();
	}
}
```

- A function's locals are declared at the top of its body, before its first statement (section 4), and get their values each time it's called. A function can't declare globals; it uses the ones declared at the top of the file.
- Defined at the top level of a file. **Only usable within that file.** A `function` inside a block is a compile error.
- Can be called before their definition in the file.
- Function names must be unique in the file, and can't be the same as a registered command or a built-in array command.
- Parameters are locals, and two parameters can't share a name. Array arguments are copied, as with assignment.
- A call must pass exactly one argument per parameter (function parameters have no defaults). Functions can't be called with a target.
- **Calls wait for the function to finish.** A function may contain latent commands, and the caller pauses until the function returns.
- A function that returns a value can be called on a line of its own, throwing the value away (`AddScore(5);`), since functions are often called for what they do.
- A `void` function's `return;` has no value, and returning a value from it is a compile error. A non-void function's `return` must have a value of its type. A non-void function that reaches its end without returning stops the script with an error.
- Recursion is allowed up to the call-depth limit (default 200 nested calls, see section 11). Going past it stops the script with an error.
- Reading a function parameter that holds `nil` stops the script with a hint that it's a parameter: *"'_s' is nil. It's a parameter of function 'F'; check what the caller passed."*
- The `function` line itself has no node metadata (see section 1, "Functions in the graph"). The header may span several lines.

---

## 10. Script parameters

A script can take arguments when it's spawned, declared with a `#define params` line:

```
#define params (entity _speaker, string _line, int _repeats = 1, entity _listener = nil)

for (int _i = 0; _i < _repeats; _i++)
{
	_speaker Speak(_line);
}
```

### Declaring
- **`#define params (...)` is optional.** If present, it must be the **first line in the file** (comments aside), before any other statement or function, and can appear only once. A `#define params` line anywhere else (later in the file, in a block, in a function) is a compile error, and so is `params` written without `#define`. Like every `#` line, it has no `;` and must fit on one line.
- Parameters are comma-separated `type _name` or `type _name = default`. `#define params ()` is allowed and means the same as no params line.
- **Parameters without a default are required. Parameters with a default are optional**, as with C++ default arguments.
- **All required parameters must come first.** A required parameter after one with a default is a compile error.
- **`= nil` makes a parameter optional with no particular default.** It is `nil` when left out.
- **Defaults are fixed values only:** numbers (including negatives like `-2.5`), strings, `true`, `false`, `nil`, Bango IDs (`@john`), or arrays of those (`[1, -2]`, `[["a"], []]`, `[@bob, @alice]`). A global, variable, call, operator or any other expression is a compile error.
- **Texts can't be defaults**, even inside an array: Unreal's localization gatherer skips `#define` lines, so the text would never be translated. Give a text parameter the default `nil` and set it in the script (section 3).
- A default must fit its parameter's type (int → float is allowed). `int _a = "x"` and `int[] _a = [1, 2.5]` are compile errors.
- Parameters are ordinary locals of the top level. Two parameters can't share a name, and a local can't have a parameter's name. Functions in the file can't see them.
- They are saved with the instance like any other local.
- In the graph, the `#define params` line is the script's **entry node** (node 0 at (0,0)). It shows the parameter list; it has no data pins, since Bango expressions are text.
- The `#define params` line can't have node metadata (section 1).

### Passing arguments
Arguments are **positional**: the first argument goes to the first parameter, and so on.

```
Spawn("Dialogue/Greet", @john, "Hello!");               // _repeats = 1, _listener = nil
Spawn("Dialogue/Greet", @john, "Hello!", 3, @player);
```

These checks happen when the script is spawned. If any fails, the script doesn't start, no globals are declared, and an error pointing at the `params` line is shown:
1. **Too few:** fewer arguments than required parameters. The error names the first missing parameter, its position and type: *"Missing argument 2 ('_line', a string). This script needs at least 2 argument(s), but 1 were passed."*
2. **Too many:** more arguments than parameters, or any arguments to a script with no parameters.
3. **Types:** each argument is checked in order (1, 2, 3, ...) against its parameter's type. The first mismatch is an error: *"Argument 2 ('_line') must be a string, but it is an int."* int → float is allowed.

- **Leaving off trailing optional arguments uses their defaults.** In script and C++ calls, an argument in the middle can't be skipped. (The Blueprint node can skip one; see below.)
- **Passing `nil` passes `nil`**, even to a parameter with a default, and even to a required one (passing `nil` counts as passing the argument).
- Each spawn gets its own copy of a parameter's default, so a script changing an array default never affects later spawns.
- **Reading a parameter that holds `nil`** stops the script as usual, with a hint: *"'_line' is nil. It's a parameter of this script; check what the caller passed."*

### From C++
- `UBangoScriptSubsystem::Spawn(ScriptPath, Args)`, where `Args` is a `TArray<FBangoValue>` in parameter order. It returns an `FBangoScriptHandle` for `Terminate` and `IsRunning`. Its `Id` is an `int32`, given out in order from 1 by each world's subsystem (and saved with it), so two running scripts never have the same one; a script started with `Exec` gets the same id back as an `int`. `Spawn(ScriptPath, Args, Error)` is the same, except that an error starting the script is put in `Error` instead of being shown.
- `UBangoScriptSubsystem::SpawnFromSource(Source, Name, Args)` starts a script from its **text** instead of a file, for scripts made at runtime or kept somewhere else (a data asset, a string table, a server). It's compiled on every call, then started exactly like `Spawn`: the same arguments, checks and errors, and the same handle. `Name` stands in for the file name in its errors (`Dialogue.Intro(3:5) [node 2]: ...`) and in the editor's debugger; it defaults to `Script`.
  ```cpp
  const FString Source = TEXT("#define params (entity _who)\n_who Speak(\"Hello.\");");
  TArray<FBangoValue> Args;
  Args.Add(FBangoValue::MakeEntity(TEXT("john")));
  UBangoScriptSubsystem::Get(this)->SpawnFromSource(Source, TEXT("Greeting"), Args);
  ```
  A script spawned from text keeps its text: saving the state saves it, and loading compiles it again (section 11). It isn't hot reloaded, since there's no file to watch, and no script editor tab follows it.
- `Bango::RegisterCondition(Source, Name, Error)` and `Bango::UnregisterCondition(Handle)` compile a **condition** once and keep it, and `UBangoScriptSubsystem::EvaluateBoolCondition(Handle, bResult, Error, Args)` runs it and gives its answer (see Conditions below).
- `Bango::ActorToEntity(Actor, Value, Error)` turns an actor into an entity argument: its Bango ID name, or `nil` for a null pointer. An actor with no Bango ID component, or whose Bango ID name isn't registered, is an error.
- `FBangoModule::Get().GetScriptParams(ScriptPath, Params, Error)` reads a script's parameters (names, types, whether each has a default, and the default value) without running it.

### Conditions
A **condition** is a script that answers **true or false**, for C++ and other plugins (like a triggers system) to ask. It's either:
- **one expression**, on a line of its own (comments, blank lines and `#define` lines can be around it):
  ```
  foundThings >= 10 && !IsNull(@door)
  ```
- or **a script whose main graph returns** `true` or `false`, with everything a script can have (`#define params`, globals, locals, loops, functions):
  ```
  #define params (entity _who)
  global int foundThings;

  if (IsNull(_who))
  {
  	return false;
  }

  return foundThings >= 10 && GetPos(_who).z > 100;
  ```

Text with no `;`, `{` or `}` (outside strings and comments) and only one line of code is an expression; it's compiled as `return <expression>;`, and errors still point at the expression as it was written. In a condition, the main graph's `return` must have a `bool` value (`return;` is an error), and reaching the end without returning one is an error when it runs. Functions are as usual.

A condition is **registered** once, which compiles it, then checked as often as needed, and **unregistered** when it's no longer needed:
```cpp
// When the trigger is set up (BangoConditions.h):
FString Error;
FBangoConditionHandle Condition = Bango::RegisterCondition(TEXT("foundThings >= 10"), TEXT("Trigger.Gate"), Error);
if (!Condition.IsValid())
{
	Bango::ReportError(Error); // it doesn't compile
}

// Whenever it's checked:
bool bOpen = false;
if (!UBangoScriptSubsystem::Get(this)->EvaluateBoolCondition(Condition, bOpen, Error))
{
	Bango::ReportError(Error);
}

// When the trigger goes away:
Bango::UnregisterCondition(Condition); // and Condition is made invalid
```
- `RegisterCondition` gives an invalid handle, and the compile error, if the condition doesn't compile. Commands it calls must be registered before it is (built-in commands always are). `Name` stands in for the file name in its errors.
- There's **no limit** on how many are registered. Registering the same text with the same name again gives the **same handle**; it's kept until it has been unregistered as many times as it was registered. Registrations are shared by every world, and last until they're unregistered (or the game ends).
- `UBangoScriptSubsystem::EvaluateBoolCondition(Source, bResult, Error, Args, Name)` checks a condition given as text, without registering it. It's compiled on every call, so it's for conditions checked once in a while.
- It runs straight away, to the end, in that world: with its globals and its actors' Bango IDs. `Args` fill its `#define params` line, as for spawning (section 10).
- **A condition must answer at once:** calling a command that waits (`Sleep`, a dialogue choice) is an error.
- `EvaluateBoolCondition` returns `true` with the answer in `bResult` if it ran and answered, and `false` with the error in `Error` (and `bResult` false) if the handle isn't registered (or the text doesn't compile), its arguments don't fit, it stopped with an error, it didn't return a `bool`, or it waited. Errors aren't shown or logged by it, so a caller asking often can show each once (`Bango::ReportError`).
- A condition isn't a running script: it isn't listed in the debugger, saved or reloaded, and it has no handle. Globals it sets stay set.
- For C++ that compiles it itself: `Bango::CompileCondition`, and `FBangoInstance::GetResult` after it finishes. `Bango::GetNumRegisteredConditions()` says how many are registered, for checking that a plugin unregisters its own.

### From Blueprint: the Spawn Bango Script node
The **Spawn Bango Script** node reads the script's `params` line and creates one input pin per parameter:

- The script is **picked on the node** with an asset picker (a Bango Script asset, section 15) and is fixed, not computed at runtime: the Blueprint references the asset, so it's loaded and packaged with the Blueprint, and renaming or moving the script keeps the node pointing at it. The script can't be connected. To choose a script at runtime, use **Spawn Bango Script (Dynamic Path)** (section 11), which passes no arguments. (A node made when scripts were typed paths picks up its script asset when it's next refreshed, if there's one at that path under `/Game` or `/Game/Bango`.)
- Pins are named after the parameters, in order. Pin types:

  | Bango type | Blueprint pin |
  |---|---|
  | `bool` | Boolean |
  | `int` | Integer |
  | `float` | Float (double precision) |
  | `string` | String |
  | `text` | Text |
  | `entity` | Actor object reference |
  | `vector2` | Vector 2D |
  | `vector3` | Vector |
  | `T[]` (one level) | Array of the pin type above |

  Nested arrays (`int[][]`) can't come from Blueprint; a script with one shows a compile error on the node.
- A pin's default value is the script's default.
- **Required pins always pass their value.**
- **An optional pin that is unconnected and still at the script's default counts as "not passed"**, so the script uses its own default. Unlike script and C++ calls, this works for an optional parameter in the middle too.
- Text pins start empty. An empty, unconnected Text pin for an optional parameter counts as "not passed" (the parameter is `nil`); typing a text in the pin passes it.
- Actor pins can't show a default, so an unconnected Actor pin for an optional parameter always counts as "not passed": the script uses its default (`@john`, or `nil`). An unconnected Actor pin for a required parameter, or a connected one whose actor is null, passes `nil`. An Actor without a registered Bango ID stops the spawn with an error. The pin's tooltip shows the script's default.
- Refreshing the node (or recompiling the Blueprint) rebuilds its pins from the script's current `params` line, and wires on pins whose names haven't changed are kept.
- Missing scripts, scripts that don't compile, and pin type mismatches are shown when the Blueprint compiles.
- The node returns a script handle, like the dynamic-path node.

---

## 11. Execution model

### Script files
- Scripts are **Bango Script assets** (section 15), anywhere in Content: embedded (the text is in the asset) or linked (the text is a `.bango` file next to the asset, which always wins).
- A script is referred to by its **asset path**: `/Game/Quests/Intro`. From another script (`Exec`), a path is relative to the calling script's folder (`Intro`, `../Shared/Greet`) or full (`/Game/...`); from Blueprint and C++, one without a leading `/` is relative to `/Game`. Backslashes and a trailing `.bango` are accepted. This name, with `.bango` (`/Game/Quests/Intro.bango`), is what errors show as the file name.
- **Compiled on first use:** a script is compiled when it's first spawned (its asset is loaded if it isn't). The compiled script is cached and reused until the asset's text changes, then it is recompiled on its next spawn. This works while playing in the editor, so scripts can be edited and re-spawned without restarting.
- A script can't start unless it compiles without errors.
- Script assets are packaged like any asset; a Spawn Bango Script node's script is packaged with its Blueprint. (`.bango` files aren't packaged: the asset keeps a copy of a linked script's text.)
- Example scripts: `/Game/Bango/Examples/Hello` and `/Game/Bango/Examples/Greet` (a script with parameters), linked to their `.bango` files in `Content/Bango/Examples/`.

### Instances
- Each spawn creates a separate running instance. When it starts:
  1. Every command the script calls is checked to exist. A missing command stops the spawn.
  2. The arguments are checked against the script's parameters (section 10). A problem stops the spawn.
  3. The script's globals are declared (section 4). A type conflict stops the spawn.
  4. The top level starts running **immediately**, in the same frame, until it first pauses, finishes or fails.
- `return;` at the top level ends the instance. Returning a value from the top level is a compile error.
- **Instances belong to a game world.** They stop when their level is unloaded. Globals belong to the game instance and carry over between levels.
- **Scheduling:** every frame, each running instance resumes in the order it was spawned and runs until it pauses, finishes or fails. Instances pause while the game is paused.
- **Infinite-loop guard:** an instance that runs more than **1,000,000** instructions in one frame without pausing stops with an error (e.g. a `while` loop with no `Sleep`). The limit is a project setting.
- There is **no** `call`. Only `spawn`.

### Blueprint API
| Node | Effect |
|---|---|
| **Spawn Bango Script** (fixed path, one pin per parameter) → Handle | Starts a script with arguments (section 10). Returns an invalid handle (and shows an error) if it can't start. |
| **Spawn Bango Script (Dynamic Path)** (path) → Handle | Starts a script from a path chosen at runtime, with no arguments, so the script can't have required parameters. Returns an invalid handle (and shows an error) if it can't start. |
| **Terminate Bango Script** (Handle) | Stops a script. A script stopped while one of its commands is running stays stopped. |
| **Is Bango Script Running** (Handle) → bool | True while running or paused; false once it has finished, failed or been terminated. |
| **Is Valid Bango Script Handle** (Handle) → bool | True if the handle came from a successful spawn. |
| **Set Bango Global** Bool / Int / Float / String / Text / Vector2 / Vector3 | Sets a global, declaring it with that type if it doesn't exist. A type conflict is an error. Int uses a Blueprint Integer. |
| **Get Bango Global** Bool / Int / Float / String / Text / Vector2 / Vector3 | Reads a global. `Is Defined` is false if it doesn't exist or is nil. A different type is an error. |
| **Clear Bango Global** | Makes a global nil. |
| **Save Bango State** → bytes | Saves all running scripts and all globals, for storing in a SaveGame. |
| **Load Bango State** (bytes) | Replaces all running scripts and globals with saved ones. |
| **Reset Bango State** | Stops all scripts and clears all globals, e.g. for a new game. |

The script subsystem (`UBangoScriptSubsystem`) also has two Blueprint events:
- **OnHint** (Text) for showing `Hint` text in game UI. It gets an `FText`, already formatted, in the current language.
- **OnScriptFailed** (handle, error), fired when a script stops with an error.

### Translation
Bango scripts are translated with Unreal's localization tools. Each Bango Script asset (section 15) reports its texts to Unreal's asset gathering itself: every `LOCTEXT`, `NSLOCTEXT`, `NOTELOCTEXT` and `NSNOTELOCTEXT`, with its namespace (from `#define LOCTEXT_NAMESPACE` for `LOCTEXT` and `NOTELOCTEXT`), its key, and for `NOTELOCTEXT` its notes for translators. This happens when the asset is saved (the texts are kept in the package), so a script saved before this existed needs saving once more (or a gather with the gather cache skipped).

To set it up, in the **Localization Dashboard**, make sure the game target's **Gather from Packages** is on and includes the folders the scripts are in (Content, by default). Then gather as usual. The scripts aren't gathered as text files any more: remove any **Gather from Text Files** entries for `Content/Bango` or `*.bango`, which would gather linked scripts' files a second time, and wrongly.

- Only texts that run are gathered: not those on lines disabled with `//~`, or in disabled nodes (between `/*` and `*/`), however they're nested. (Gathering `.bango` files as C++ text used to pick up some texts in nested disabled nodes.)
- Each namespace and key is gathered once per script. A key used for two different texts in one script is a compile error; across scripts, the gatherer reports it.
- Texts made with `INVTEXT` or `ToText` aren't gathered; they're never translated.
- Parameter defaults can't be texts (section 10).
- A script that doesn't parse has nothing gathered, and the output log says so when it's saved.
- At runtime nothing changes: a text is looked up by its namespace and key, as before.

### Errors
- Compile errors and runtime errors are shown on screen and logged, with the file, line, column and NodeID, and for runtime errors the function: `Quest/Intro.bango(12:5) [node 7] in Nag(): '_x' is nil.`
- Compiling stops at the first error.
- A runtime error stops only that instance. Other instances keep running.

### Hot reload
While playing in the editor, when a running script's file is saved (from the graph editor, the script text pane or anything else), each running copy of it **switches to the new version where it is**, instead of only scripts spawned afterwards getting it. The editor checks for saved scripts a few times a second. Packaged games don't do this.

```
float _x = 0;
float _speed = 1;

while (true)                    //@1(0, 0)
{
	_x += _speed * 2;           //@2(2, 0)   <- change the math and save:
	@platform SetPos([_x, 0, 0]);   //@3(4, 0)
	Sleep(0);                   //@4(6, 0)   <- the next frame, it carries on here, in the new version
}
```

How a running copy switches:
- **Where:** the next time it finishes a **simple statement**: an assignment, `++`/`--`, or a call on a line of its own. That's usually as soon as the command it's waiting on (a `Sleep`) is done. It carries on right after the **same node** (by its NodeID) in the new version, so a node keeps its place however much code around it changes. Moving nodes around in the graph doesn't change their IDs.
- **Function calls under way:** each is matched the same way. The function is found by name, and the call goes back to the node it's in, in the new version. That node must be a call on its own line or the value of an assignment (`Step();`, `_r = Step();`); a call in the middle of a calculation (`_r = 1 + Step();`) can't be switched until it's finished.
- **Variables:** locals, parameters and loop variables keep their values, matched by name (a `for` or `forEach` loop's variable by its loop node too). A local the new version adds starts with its declared value; one it removes is dropped. Globals are shared and keep their values; the new version's global declarations take effect when it switches, as when a script starts.
- **What can't switch:** if, at that point, the node isn't in the new version (or isn't a simple statement any more), a local, parameter or loop variable has changed type, a function in progress has been removed or has a new parameter, or a loop under way has a new variable, it tries again at each following simple statement. If it pauses again without being able to switch, it gives up and **carries on with the version it had**; the output log (and the screen) says why. Scripts spawned from then on use the new version.
- A new version that doesn't compile isn't switched to: the running copies keep their version, and the compile error is shown once.
- A new version that calls a command that isn't registered isn't switched to either.
- Switching doesn't run anything again: code above the node isn't run, so a value a new line above it would have set isn't set until the code gets there.
- Turn it off with **Reload Running Scripts** in the project settings.

### Save and load
A save stores:
- Every running instance: script path (or, for a script spawned from text, its name and its whole text), call stack (function, resume point, locals including script parameters), and value stack, so a script paused in the middle of a function or an expression resumes exactly where it was.
- The pending state of whatever latent command each instance is waiting on.
- All global values.

On load, each saved script is loaded and compiled from its path (or from its saved text), and its saved state is checked against it. If a script was changed so that the saved state no longer fits (or the file is missing), that script isn't restored and an error is shown. Other scripts still load. Beyond that check, loading state into a changed script is **undefined behaviour** (not supported).

The save data has a format version. Data from an older version (such as saves made before ints became 32-bit in v0.9, or before scripts spawned from text were saved with their text in v0.35, or before script ids became 32-bit in v0.38) fails to load with an error rather than being misread.

### Limits (project settings)
| Setting | Default |
|---|---|
| Script folder under Content | `Bango` |
| Reload running scripts when their file is saved, in the editor (hot reload) | on |
| Instructions per frame before the infinite-loop guard stops a script | 1,000,000 |
| Nested function calls | 200 |
| How long Hint text stays on screen when OnHint isn't bound | 5 seconds |

---

## 12. The graph form

The graph editor shows a script as nodes and wires and saves it back as text. The `.bango` text stays the only thing stored on disk. This section says exactly how the two map, so a script can be edited either way.

### Graphs and nodes
- A file has a **main graph** (the top-level code) and **one graph per function**.
- Each graph starts at its **entry node**, node 0 at (0,0) (section 1). The main graph's entry, **Script Start**, shows the `#define params` list under an **Input Params** heading (left out when there are none), and a **Namespace** row under a **Localization** heading for the `#define LOCTEXT_NAMESPACE` line (section 3): typing a name there adds or changes the line (and the `#undef LOCTEXT_NAMESPACE` at the end), and clearing it removes both. A function's entry shows its signature, like `int AddScore(int _amount)`.
- **Declarations aren't nodes.** A graph's locals, and the file's globals, are listed in the script editor's **Locals** and **Globals** lists and edited there (below); in the text they're the declarations at the top (section 4). The Script Start node and a function's entry node don't show them.
- Every other statement line is one node, and so is every reroute line (a line with only `//@` metadata, section 1). A reroute is drawn like Blueprint's: one dot that is both its input and its output, with a larger area around it to grab and move it. Dragging from the dot to the left wires its input, and to the right its output; Ctrl-dragging it moves it. Its grab area is two grid squares each way with the dot in the middle, so the dot is on a grid line. Every node's pins are a whole number of grid squares apart (currently two) and on grid lines too, so a wire between a pin and a reroute at the same height on the grid runs straight. **Double-clicking a wire adds a reroute** on it there, like in Blueprint; it gets the next free NodeID. **Deleting a reroute keeps its wire**: what led into it is wired to where it led.
- A node shows its kind as the title and its text as one editable line, without keywords, brackets or `;`: the condition of `if`/`while`, the header of `for`/`forEach`, the name of `label`/`goto`, the value of `return`, and the whole statement for assignments and calls.

### Pins and wires
- Every node except the entry has one **exec input**, and each input can have **only one wire** into it.
- Every node has one main **exec output**. On `if`, loops, sequences and switches it is called **Then**; it leads to the statement after the block (a sequence's last block).
- `if` also has **Do** and **Else** outputs; `while`, `for` and `forEach` have **Do**. A **Sequence** has an output per block, named **Scope 0**, **Scope 1**, **Scope 2**, ..., above **Then** (for what follows its blocks). Its **Add pin** button (under its outputs, as on Blueprint's Sequence node) or right-click > **Add Output** adds one at the end; right-clicking one > **Remove Scope N** removes it and its wire (the nodes on it stay, unwired), and the later outputs move up. A Sequence always keeps at least one output. In the add-node menu it's under Flow Control, and starts with two.
- A **Switch** has its **Var** field (the value it switches on), then an output per case, each with a box just inside the node showing that case's values (click it to edit them; Enter or Tab goes on to the next case's box, Shift goes back). Then come **Default** and **Then**. Its **Add pin** button or right-click > **Add Case** adds a case, with no value yet, before Default; right-clicking a case's output > **Remove Case** removes it and its wire, and the later cases move up. **Default** is only written to the text when something is wired to it. A compile error in a case's values outlines that case's box in red. In the add-node menu it's under Flow Control, and starts with one case.
- A block is the chain of nodes along those wires: the Do chain is the `if` block, the Else chain the `else` block, and so on.
- **`else if`:** an Else output wired to an `if` node that has nothing on its Then output is written as `else if`. An `else` block containing only an `if` means the same thing, so it's shown the same way.
- Wires can't form a loop. Loops are made with `while`, `for`, `forEach` or `goto`.
- `goto` draws no wire to its label (section 1).

### Disconnected nodes
- A chain of nodes that nothing wires into (other than the entry's chain) is **disconnected**. It's kept in the file as `//~` comment lines, so the compiler ignores it but the graph keeps it:
  ```
  WriteLog("connected");           //@1(0, 0)

  //~ WriteLog("disconnected");    //@2(0, 20)
  //~
  //~ if (true)               //@3(20, 20)
  //~ {
  //~ 	WriteLog("inside");        //@4(40, 30)
  //~ }
  ```
- `//~` lines are written at the end of their graph: after the main graph's code, or at the end of a function's body. A blank line separates one disconnected chain from the next.
- `//~` lines must read as valid statements (as if the `//~ ` were removed). Declarations can't be in them: a disconnected chain uses the graph's locals like any other node. A broken `//~` line, a `#` line or a `function` in them, or a NodeID already used elsewhere in the file stops the graph editor from opening the file.
- A disconnected `label` isn't part of the compiled script, so a `goto` to it is a compile error until it's wired in.

### Disabled nodes in the graph
- Any node but an entry or a reroute can be disabled (section 1). It keeps its wires; when the script runs, execution passes through it to its Next pin, and a disabled If or loop never runs its branches.
- Saving writes a disabled simple node as `/* <statement> */` followed by its `//@` comment, and a disabled If or loop with `/*` and `*/` lines around it (its comment goes above the `/*`). A disabled If that would be written as `else if` is written in an `else { ... }` block instead, since the markers can't go around an `else if`.

### Comments
- **The script's description:** the `//` lines the file starts with, from its very first line, up to the first line that isn't one (a blank line, code, a `//~` line or a `// ====` heading). It describes the script for designers. It isn't a node's comment and isn't shown in the graph; the script editor's **Script Description** box shows and edits it. A file that doesn't start with a `//` line has none. A bare `//` line in it is an empty line of the description. A description line can't start with `====` (it would be read as a section heading).
- `//` comment lines directly above a statement become that node's **comment bubble**. A comment after the code on a statement line joins that node's bubble too.
- Other comments go to the next node below them in the same graph, or to the graph's last node if none follows. Comments just above a `function` line go to that function's entry node, and comments above the `#define` lines go to the main entry node.
- When saved, each bubble is written as `//` lines directly above its node. So a comment at the end of a block, or after a `}`, moves down to the node that follows it.
- A main-graph entry bubble with no parameters is saved above the `#define LOCTEXT_NAMESPACE` line, or above an empty `#define params ()` line if there is none, which means the same as no params line.
- Carriage returns in comments are dropped, so comments read the same whatever the file's line endings.

### Saved layout
When the graph editor saves, it writes the whole file in a fixed layout:
0. The script's description, if it has one, as `//` lines from the first line of the file (see Comments above).
1. The entry node's comment, then the `#define params` line (if there are parameters, or a comment and no namespace line), then the `#define LOCTEXT_NAMESPACE` line.
2. The global declarations, in the order the Globals list shows them; then the main graph's local declarations, in the order of the Locals list. Each declaration's comment goes directly above it.
3. The main graph's code, then its disconnected chains.

   Each of these parts that has anything in it starts with a **section heading** and a blank line, after a blank line (unless it's the first line of the file): `// ============ SCRIPT INPUT ====================` above the `#define` lines, `// ============ GLOBAL VARS =====================` above the globals, `// ============ LOCAL VARS ======================` above the main graph's locals, and `// ============ SCRIPT BODY =====================` above the code.
4. Each function, after a blank line and a separator line and another blank line: its comment, `function <signature>`, `{`, its local declarations and a blank line (if it has any), its code and disconnected chains, `}`. The separator before the first function is `// ============ FUNCTIONS =======================`; before each of the others it's `// ----------------------------------------------`. When a file is read, **any comment line starting with `// ====`** is skipped wherever it is, as a section heading (they're all written again on save), so headings never become comment bubbles; don't start a comment of your own that way, as it would be lost. The `// ---...` line between functions is skipped outside function bodies only; inside a function's body it's an ordinary comment.
5. After a blank line, `#undef LOCTEXT_NAMESPACE`, if the script has a namespace. Comments below the last node, including one next to the `#undef`, move up to it, like other comments with no node after them (see Comments above).

**Function order** is depth-first by first use. Reading the main graph in the order its lines are written (connected nodes, then disconnected ones), the first call to a function places it, and that function's own calls are placed right after it, before the main graph goes on. Functions nothing calls follow, in their existing order, each followed by the functions it calls. A call counts wherever it appears in a node's text (conditions, arguments, values), but not inside a string. Recursive or circular calls don't change the order: a function is only placed the first time it's used. So if the main graph calls `A` then `B`, and `A` calls `C`, the order is `A`, `C`, `B`. Opening a file also lists its functions in this order.

Other details:
- Blocks use the brace-on-its-own-line style of the examples in this spec, with one tab per level.
- Every node line gets its `//@` comment, aligned in one column 4 spaces past the longest node line (lines over 100 characters don't move the column; they get 4 spaces). A reroute line is its block's indentation, then spaces to that column, then the `//@` comment (with an ` @<NodeID>(<X>, <Y>)` for each further reroute in the run); in a disconnected chain it's `//~ ` and the same.
- Nodes without metadata get the next free NodeID and a position next to the node that leads into them.
- Statements of the same kind sit together with no blank line between them: **assignments** (`=`, `+=` and the like, `++`, `--`), and **calls** (commands and functions). A blank line goes between statements of different kinds, before and after **flow control** (`if`, `while`, `for`, `forEach`, `break`, `continue`, `return`, `label`, `goto`), above a statement with a comment (its comment stays directly above it, after the blank line), and after the `#define` lines. Reroutes get no blank lines around them: they're written right after the statement before them, and reroutes in a row share one line (a reroute with a comment starts a new line, under its comment). Inside a disconnected chain the line between nodes is a bare `//~` instead, since a blank line would end the chain. There's no blank line just inside a `{` or before a `}`.
- Other blank lines, `#define params ()` with no comment, and an empty `else { }` block are not kept; none of them change what the script does.
- Lines end with `\n`.

Opening a file and saving it without changes gives the same script: the same statements with the same meaning (functions may move, per the order above). Saving again changes nothing.

### The graph editor
The graph editor is the **Bango Scripts** tab (Tools menu). It doesn't need the details panel. It has three parts:
- **Scripts** (left): every Bango Script asset (section 15), by asset path without `/Game/`; open ones are in bold. Clicking one opens it in a new **script tab**, or shows its tab if it's already open. The **Folders** toggle beside Refresh switches between this list and a **folder tree** like the Content Browser's, with a root per content root (Content first): folders first, then scripts, each by name (only folders that hold a script). Clicking a script opens it; double-clicking a folder (or its arrow) opens or closes it. The shown script is selected in either view, with its folders opened. Which view is shown is remembered between sessions, and Refresh keeps the open folders open. The list follows script assets as they're added, renamed and deleted; **Refresh** lists them again; it doesn't touch open scripts.
- **Script tabs** (along the top of the rest): one per open script, named by its path, with a `*` while it has unsaved changes. They're the editor's own document tabs, as an open Blueprint's are, and look and behave the same: clicking a tab shows it; its × button, a middle-click or its right-click menu closes it, asking first whether to save unsaved changes; tabs can be reordered, and dragged out into a window of their own. Each tab keeps its own script editor while it's open, so switching tabs keeps each script's view, selection and unsaved changes. With no tab open, a hint says to pick a script.
- **The script editor** (under the tabs): everything for one open script. It's a self-contained pane, so it can also be shown in a window of its own (planned).
  - **Toolbar** (across the top, styled like an asset editor's, such as Blueprint's: each button an icon with its label): **Save** (also Ctrl+S) and **Reload**, then **Compile Test**, then the **Debug** dropdown, with a separator between each group. Compile Test is only a check: scripts are always compiled when they're loaded at runtime. Its icon shows its status as Blueprint's does. The script is test-compiled quietly when it's opened (only the icon changes: no notification, nothing marked on its nodes), and the icon shows a yellow `?` once it's changed (or its text is typed in) after, a green tick when it compiled without errors, a warning when it compiled with warnings, a red cross when it didn't compile (its tooltip says which). The tab shows the script's name, with a `*` while it has unsaved changes. **Reload** reads the script again from its file, so edits made to the text elsewhere show up; it asks first whether to save unsaved changes (Cancel keeps them and doesn't reload). It works on a script whose text didn't read too, so the text can be fixed elsewhere and reloaded. Reloading keeps the shown graph (if the script still has it) and its view.
  - **Script Description** (left, first): a box holding the script's description (section 12, Comments), as several lines of text. Enter starts a new line; clicking elsewhere applies it, as one undoable edit (trailing spaces and empty lines at the end are dropped). Saving while typing in it saves what's typed.
  - **Functions** (left, last, after Local Vars): the script's functions, in the order they are in the file. Clicking one shows its graph. **+ Add** adds `function void NewFunction()` (numbered if that name is taken), shows it, and starts renaming it. Double-clicking a function, or clicking it again once it's selected, renames it: the name must be a valid identifier that no other function and no command has, and every call to it in the script is renamed too, as one undoable edit. Dragging a function onto a graph adds a call to it where it's dropped (dropped on a pin, the call is wired to it, as with a node from the add-node menu). Its × deletes it, after asking; calls to it are left alone and won't compile until they're removed. Deleting a function can't be undone with Undo (Reload brings it back while it's unsaved). An option to sort functions alphabetically is planned.
  - **Script Description**, **Script Input**, **Global Vars**, **Local Vars** and **Functions** are one after another in a single scrolling pane on the left, in the order their parts come in the script's text, and with the same names as its section headings. Each list is as tall as its rows, so only the pane scrolls; the splitter between the pane and the graph area sets its width.
  - **Side panes keep their width.** The Scripts list, the left pane, the script text pane and the reference pane each have a width in pixels, set by dragging the splitter beside it (no narrower than 120 pixels); resizing the window only changes the graph's width. The Scripts list's width is remembered between sessions.
  - Each of the five sections is a **foldout**: clicking its header (the arrow or the title) opens or closes it, and a closed one is just its header. The **+ Add** buttons are on the headers' right. All five start open.
  - **Reference pane** (right): two tabs at its top, **COMMANDS** and **NODES** (the tab shown is remembered between sessions). Each tab has a **search box** that shows only the names that contain what's typed, and a dropdown beside it that picks the order: **Alphabetical** (two groups, **Native** for what's built into Bango and **Game** for the project's own, each by name) or **Category** (a group per category, by name within it). Each group is a foldout like the left pane's sections, with its number of entries in its title, and remembers whether it's open while the editor's open. Each tab's order is remembered between sessions.
    - **COMMANDS** lists every command a script can call (the registered commands, the plugin's and the project's own, and the built-in array commands). By category, commands with no category are under OTHER, last, and the array commands under ARRAYS. Hovering a name shows how it's called, its description, and whether it's built into Bango or the project's own.
    - **NODES** lists every node that can be placed in the graph: the same nodes as the graph's right-click menu, by their titles (flow control, variables, arrays, every command that can be a line of its own, and the script's own functions). Commands whose value must be used aren't nodes, so they aren't listed. Native is flow control, variables, arrays and the plugin's commands; Game is the project's own commands and the script's own functions. By category, the groups are the right-click menu's, with COMMANDS (commands with no category) and THIS SCRIPT last. Each node is a small card with a grab handle that lights up under the mouse: **dragging it onto the graph places the node** where it's dropped, and dropping it on a pin wires it there, just like picking it from the right-click menu (names the node makes up, like a For's `_i`, are picked when it's dragged, so they don't clash with the script's). Hovering one shows the node's description, and whether it's built into Bango, the project's own command or the script's own function. (COMMANDS' names can't be dragged: a command is placed as a node from NODES, or used in an expression.)
    - It's hidden to start with: the **Command Palette** tab, a folder tab floating on the graph's right edge with its name written down it (reading top to bottom), slides it out from the right, like the Source Editor tab does the text pane on the left. Clicking the tab again puts it away; when it's open, the splitter beside it sets its width. Both lists are read again each time it's opened (and NODES when the script's functions change), so commands added by other modules show up.
  - **Script Input** (left, after Script Description): the shown graph's parameters: the script's `#define params` in the main graph, or the function's in a function graph. Each row has a grab handle, its type's icon, its name and its type, and in the main graph a box for its default. **+ Add** adds a `bool` parameter at the end (`_newParam`, numbered if taken) and starts renaming it; after an optional script parameter it gets `= nil`, since required parameters come first. Dragging a row by its handle moves the parameter up or down (a script's required parameters must stay before its optional ones). Clicking the type icon opens a menu of the types and arrays of them (deeper arrays, like `int[][]`, aren't offered). Changing an optional parameter's type resets its default to `nil`. Double-clicking a name, or clicking it again once it's selected, renames it, and its uses in that graph are renamed too; the name must be a valid name (letters, digits and `_`, not starting with a digit, not a keyword) and not clash with another parameter or a local of the graph. The default box is typed like a node's field (a string without quotes); leaving it empty makes the parameter required, and `nil` makes it optional with no particular value. × removes a parameter; its uses are left alone, so they won't compile until changed. Each of these is one undoable edit. Calls to a function aren't changed when its parameters are; they show errors until their arguments match. The type and default columns have fixed widths, so rows line up. A required parameter's default box is locked unless every parameter after it is optional (required parameters come before optional ones). The Script Start node lists the parameters' names and types, but not their defaults, which are shown and edited only here.
  - **Global Vars** (left, below Script Input): the file's globals, the same in every graph. Each row has a grab handle, its type's icon (a button with the type menu, as in Inputs), its name and its type; a global has no value (section 4). **+ Add** adds `global bool newGlobal;` (numbered if taken) and starts renaming it. Dragging a row by its handle reorders the declarations. Renaming one (double-click, or click it again once it's selected) renames its uses everywhere in the file, as one undoable edit; the name must be a valid name and can't clash with another global; in a graph whose local or parameter has the old name, that graph's uses aren't renamed, since they're the local's. Its × removes the declaration, after asking if the script uses it; its uses are left alone. Under the list, **Used here, declared elsewhere** lists, greyed out and without a type, the globals the shown graph uses but the file doesn't declare (declared by another script or a Blueprint, or a typo); the + on one opens the type menu, and picking a type declares it here.
  - **Local Vars** (left, below Global Vars): the shown graph's locals. Each row is like an Inputs row: grab handle, type icon and menu, name, type, and a box for its starting value, typed like a node's field (a string without quotes, a text as its source text); empty means it starts as `nil`. Only fixed values are allowed there (section 4). **+ Add** adds `bool _newLocal` (numbered if taken) and starts renaming it. Renaming renames its uses in the graph; the name must be a valid name and not clash with another local, a parameter or a loop variable of the graph. × removes it; its uses are left alone. Changing its type clears its starting value (unless it's `nil`, which fits any type). Under the list, greyed out, are the graph's `for` and `forEach` loop variables, with their type; clicking one shows and selects its loop.
  - Each of the changes in Globals and Locals is one undoable edit.
  - **Dragging a variable onto the graph:** a row of **Inputs**, **Globals** or **Locals** (by anything but its grab handle), or a loop variable, can be dragged onto the graph. Dropping it opens a menu of the nodes that work on that variable, with it filled in: **Set** for every type; **Add To** for `int`, `float`, `string` and vectors; **Subtract From**, **Multiply** and **Divide** for `int`, `float` and vectors; **Increment** and **Decrement** for `int`; and for arrays **Push**, **Insert At**, **Pop**, **Remove At** and **For Each** (with a new loop variable). A global whose type isn't known (declared elsewhere) offers only Set. The node picked goes where the variable was dropped; dropped on a pin, it's wired to it, as with a node from the add-node menu.
  - **Changes on disk:** when something other than the graph editor changes the open script's file (a text editor, source control), the editor notices within a moment. With no unsaved changes, it **reloads** the script, keeping the shown graph and its view. With unsaved changes, it never throws them away by itself: a bar above the graph says the file changed, with **Reload** (read the file, losing the changes) and **Keep Mine** (keep them; the bar goes away until the file changes again, and Save then overwrites the file without asking). A script tab that isn't shown checks when it's shown again. A file that's been deleted is left alone.
  - **Script text pane** (between the lists and the graph): the script's text. It's hidden to start with. The **Source Editor** tab, a folder tab floating on the graph's left edge with its name written up it (reading bottom to top), pulls it out: the pane slides open from the left, and the tab rides out on its right side. Clicking the tab again slides it back. When open, the splitter between it and the graph resizes it, and it opens to that width next time.
    - It shows the script as it would be saved, and follows the graph as it's edited. It's the Bango text editor (**Bango Text Editor Spec**): Bango syntax highlighting, tabs typed and shown as tabs. Line numbers are shown down its left side, and scroll with the text.
    - **Locals can be declared anywhere while typing.** Applying moves a local declared further down a graph (in its code, or inside a block) up to the graph's other locals. A starting value that's a fixed value moves with it (`int _n = 3;`); any other is left where the declaration was, as a Set node (`float _d = Distance(_a, _b);` leaves `_d = Distance(_a, _b);`, a new node). A name declared twice in one graph is an error. Moving changes when a fixed starting value is given: once, when the graph starts, rather than each time that line is reached (in a loop, say). This is only for the text pane: a `.bango` file with a local declared below its code still doesn't compile.
    - **While there's typed text that isn't applied, the left pane and the graph are locked**: the pane is greyed out and the graph is covered by a note, with **Apply** and **Revert** buttons. The text is what counts until it's applied or reverted.
    - It can be typed in. Typed text isn't used until it's applied: **Apply** (or Ctrl+Enter) makes the graphs again from it, keeping the shown graph and its view, like Reload. Applying can't be undone with Undo. If the text doesn't read, the error is shown under the text, at the bottom of the pane (so the text doesn't move), below a line and headed **ERROR:** in bold red; it flashes red once each time it's set, the cursor goes to where it is, and the graphs are left as they are. **Revert** throws the typing away. While there's typed text that isn't applied, the graph doesn't change the pane's text, a note says so, and the script counts as having unsaved changes; **Save** and **Compile** apply it first (and don't go ahead if it doesn't read).
    - A script whose text doesn't read opens with the pane shown, holding the file's text and its error, so it can be fixed there and applied.
  - **Debug** (toolbar, after Save): while playing in the editor, which running copy of this script to follow, like Blueprint's debug object. The dropdown lists every copy running in the Play in Editor worlds, as `#3: Sleep at node 12 in Step()` (its number, the command it's waiting on, the node, and the function if it's in one), and copies that **stopped with an error** in this play session, as `#4: failed at node 7: '_x' is nil.`. Picking one follows it and shows the node it's at; **No debugging** follows none; **Show Where It Is** shows the node again; **Forget Failed Copies** takes the failed ones off the list. When nothing is followed, the only running copy is followed by itself, or, if none is running, the copy that failed last (so a script that fails as soon as it's spawned is shown too); No debugging stops this until playing stops. A copy that's followed stays followed when it fails. The followed copy is shown in the graph, updated ten times a second:
    - the node it's running or paused on has a **pulsing yellow outline**, like Blueprint's;
    - each call under way on the way there (the node calling the function it's in, and so on outward) has a **dim yellow outline**;
    - a copy that failed has a **red outline** on the node where it failed;
    - each node it **just ran** gets a pale blue outline that fades out over a second (the **trail**), so a loop's nodes light up in turn and an `if` shows which way it went. A node that runs every frame stays lit. The trail starts from when the copy is picked, and shows up to the last 256 nodes run between checks. A copy that fails keeps its last 256 nodes, so the way it came to the error flashes too, even if it failed as soon as it was spawned.

    Nodes are found by their NodeID, so this works in every graph of the script, and still after the graph has been edited (or the script reloaded while running, section 11).

    The **script text pane** shows the same in its gutter, on the line of each node (found by the line's `//@` metadata, in the text as it is, even while it's being typed in): a dot in a column left of the line numbers and a tint behind the number, pulsing yellow where the copy is, dim yellow along its calls and red where it failed, and a fading blue tint on the lines it just ran. A reroute line shows the strongest of its reroutes. **Show Where It Is**, and picking a copy, also moves the text's cursor to its line when the pane is open.
  - **Graph view**: a Blueprint-style graph of the selected graph.
    - A function's graph has a **Back to Script Graph** button in its top-left corner, which shows the main graph again.
    - Pins are Blueprint exec pins; wires have no arrowheads. There are no data pins: values are text in fields.
    - Title colours follow Blueprint: red entry, blue commands, a calmer blue for this script's functions, green assignments, grey flow control, purple labels and `goto`, orange `return`/`break`/`continue`.
    - Comments are pinned comment bubbles.
    - **Disable Node** / **Enable Node**, in a node's right-click menu, or **Ctrl+/**, turns the selected nodes off, or back on if they're all off already, as one undoable edit. A disabled node is drawn faded, with a striped **Disabled** banner under its fields, like a disabled Blueprint node. Entries and reroutes can't be disabled. A node's own text can't contain `/*` or `*/`.
    - In the script text pane, disabled code is faded (more than `//~` lines, and not in italics), and the `/*` and `*/` markers are shown in bold green.
    - Node coordinates are multiplied by 16 pixels.
- A script whose text doesn't parse (or whose `//~` lines don't) shows the error instead of a graph, and its text in the script text pane.

### Fields
Each node shows its text as **fields** in columns, like the details panel: each row starts with its type's icon (bool (a ?), int, float, number (float's icon, tinted between int's and float's colours), string (an S), text (a T), entity, array, or any; hovering it shows the type), then the name, then the value, which fills the rest. Values stay on one line, in a box 90 to 240 pixels wide (wider only if the node is): a longer value ends in "…". Hovering a value shows its parameter's description if it has one (Sleep's Time says *Seconds*), or else the whole value. Rows that name a variable (a Var, For Each's Variable, an array command's Target) show a variable icon, an italic `(x)`, tinted with the variable's type when it's known. Rows without a type leave the icon space empty so the names line up, and a node where no row has a type has no icon column. Empty fields show *not set* (optional) or *missing* (required).

**Autocomplete.** While a field is being edited, typing a name opens a list under it of what fits there, as in the script text pane: the variables the node can see (its graph's parameters and locals, the file's globals), this script's functions, the registered commands and the built-ins, sorted by name, with each one's kind (P, L, G, F, C or B), type or signature, and a command's description. It filters as more is typed and closes when nothing matches or the name is typed in full. Up and Down (and Page Up / Page Down) pick, **Enter** or **Tab** puts the name in (a command or function also gets its `(`), clicking an entry does the same, and **Escape** closes the list (a second Escape cancels the edit). **Ctrl+Space** opens it for the word at the cursor. Nothing is offered in a string or text field's plain text (one that starts with `=` is an expression, and gets the list), after `@` or `.`, or inside a string.

**Moving between fields.** While a field is being edited (and the autocomplete list isn't open; when it is, Enter and Tab pick from it), **Enter** applies what's typed and opens the node's next field, with its text selected; after the last field, editing ends. **Tab** does the same but wraps around to the first field, and **Shift+Enter** / **Shift+Tab** go to the previous field instead. Fields that can't be edited are skipped, and the next field is found after the edit, so an edit that changes the node's fields (like a call's command) moves on through its new ones.

A statement that is exactly one call (`[target] Name(args);`) gets a **dedicated node** titled with the command or function's name:

| Field | When |
|---|---|
| **Target** | the command takes a target (`@john Speak(...)`) |
| one field per parameter | labelled with the parameter's name; optional ones may be left empty |
| extra values | variadic commands; labelled from the signature (`{0}`, `{1}`, ... for `WriteLog`, `Format`, `Hint` and `FormatText`; `Value 1`, `Value 2`, ... by default). For these commands (and others marked `.WithPlaceholders()`), when the text is a literal there is a row for every placeholder it uses, up to `{31}`, even if it has no value yet: changing `"Test {0} {1}"` to `"Test {0} {1} {2}"` adds a `{2}` row, shown as *missing* until it's filled in. A row left *missing* is **saved as `nil`** (`WriteLog("Test {0} {1} {2}", 1, 2, nil);`), so the text shows `nil` there; after reopening, the row shows `nil`. Values beyond the placeholders still get rows. |

This covers registered commands, this script's functions, and the array commands. A command whose value can be thrown away (`_a Pop();`, `AskChoice("A", "B");`, a script function) can be a node on its own; the node shows no row for the value.

**Commands whose value must be used never get a node of their own** (section 7): `Format`, `Random`, `Count` and the like only appear inside a field's text, such as a Set's To (`_s = Format("{0}", _n)`) or a Hint's value. A file that has one on a line of its own shows it as that command's node with an **ERROR!** banner saying so, and it doesn't compile. The add-node menu leaves these commands out.

**A line that stores a value is about storing it.** Any assignment is a **Set** node (or Add To and the like), even when its value is a call: `_t = Random(1.0, 2.0);` is a **Set** node with Var `_t` and To `Random(1.0, 2.0)`. So a line never changes kind depending on what its value is.

**For nodes** have two looks. A simple counting loop looks like Blueprint's For Loop, with **Var**, **First** and **Last** rows: its header is `int _i = First; _i <= Last; _i++` (or `_i = First` for a variable declared before it, `++_i`, or counting down with `>=` and `--`). Like Blueprint's Last Index, Last is included. Renaming Var renames it in all three parts. Any other header (`_i < 10`, `_i += 2`, two variables, an empty part) shows as one row holding the whole header, with no label. A For node has a toggle right after its title, in italics: *(Simple)* while it shows Var, First and Last, and *(Complex)*, lit, while it shows the whole header. On a simple loop it switches between the two, so the header can be rewritten into something else; on any other header it stays *(Complex)* and greyed out. The toggle isn't saved. The look follows the text: editing the whole header into a simple loop shows Var, First and Last (unless the toggle is on), and the other way round.

**Set nodes know their variable's type** when they can see its declaration: the graph's parameters and locals, the file's globals, or the variable of a `for` or `forEach` loop the node is inside (For Each's typed from the array). The Var's `(x)` icon is then tinted for that type, an array element like `_grid[1]` takes off one `[]` per index, and the To (Add To and Subtract From's Value, Multiply and Divide's By) takes the same type: it gets that type's icon, and for a `string` or `text` variable it's typed as text. A variable it can't see (a global declared by another script, a loop variable outside its loop, a name being typed) stays grey. The same goes for For Each's Variable. Changing a local's or global's type, or renaming a Var, updates the types on every node within a moment.

Anything that isn't a single call is a generic node: **Set** (Var, To), **Add To** / **Subtract From** (Var, Value), **Multiply** / **Divide** (Var, By), **Increment** / **Decrement** (Var), **If** / **While** (the condition, in one row with no label), **For** (see below), **For Each** (Variable, Array), **Return** (see below), **Label** and **Go To** (a name). The **Script Start** node has a row per parameter (its name and type; defaults are in the Inputs list); a function's start node shows its return type and parameters.

**Return** nodes know where they are. In the main graph, Return ends the script and has no Value row. In a `void` function it ends the function, with no Value row. In a function with a return type, it has a **Value** row of that type (with its icon; for a `string` or `text` function it's typed as text). Changing the function's return type updates its Return nodes within a moment. A value where none belongs (`return 5;` in the main graph) still shows a Value row, so it can be cleared; compiling says why it's wrong.

Problems visible from the line alone show Blueprint's **ERROR!** banner on the node: an unknown command or function, too many arguments, a target on a command that doesn't take one, or a command whose value must be used on a line of its own.

### Editing
- **Fields:** click a value to edit it. Enter or clicking elsewhere applies it; Escape cancels. Every edit is checked before it's applied: the node's text must still read as code for its kind (one line, valid syntax). A refused edit leaves the node alone and a notification says why. Types and names aren't checked until Compile.
  - Clearing an optional field removes it with its separator (`, ` or ` = `). Only the last argument can be removed. An optional argument after a missing one is locked ("fill in the values above first").
  - **Text fields** (any field whose type is `string`, like WriteLog's Msg or a string default) are typed as plain text, without quotes: typing `Say "hi"` saves `"Say \"hi\""`. The string escapes of section 3 are added when saving and removed for editing, so quotes never need escaping. Typing `\n` or `\t` gives a line break or tab, and `\\` a backslash; a backslash before anything else is just a backslash. The field shows the text itself in a normal font (line breaks and tabs as `\n` and `\t`), or *empty text* for `""`.
  - **Localized text fields** (type `text`, like Hint's Msg) are typed the same way, and show the source text. Typing in an empty one makes a `LOCTEXT` with a new random key; retyping one keeps its key, namespace and notes (or keeps an `INVTEXT` an `INVTEXT`). The arrow at the end of the field opens rows under it, like an FText property's advanced options:
    - **Translated**: a checkbox. Unchecking it makes the text an `INVTEXT` (its key, namespace and notes are dropped, and the rows below are hidden); checking it makes it a `LOCTEXT` again, with a new key.
    - **Key**: can't be empty.
    - **Namespace**: empty means the script's namespace, from Script Start (`LOCTEXT`); filling it in gives `NSLOCTEXT`.
    - **Notes**: for translators; filling it in gives `NOTELOCTEXT` / `NSNOTELOCTEXT`.

    Which rows are open isn't saved.
  - A text field can hold a variable or expression instead: type it after a `=`, like `=_name` or `=Format("{0}", _n)`. Such a field shows `=` and the expression. To start the text itself with `=`, type `\=`. Clearing an optional text field removes it; type `=""` for an empty text.
- **Adding nodes:** right-click the graph, or drag a wire off a pin and let go, for the **add-node menu**: Blueprint's searchable menu, with the nodes grouped by category and sorted by name in each.
  - **Flow Control:** If, While, For, For Each, Break, Continue, Return, Label, Go To, Reroute.
  - **Variables:** Set, Add To, Subtract From, Multiply, Divide, Increment, Decrement. (Variables themselves are added in the Globals and Locals lists.)
  - **Arrays:** Push, InsertAt, Pop, RemoveAt. Then a group per command category (commands without one are under **Commands**), and **This Script** for the script's own functions. Commands whose value must be used aren't offered.
  - A new node's **fields start empty**, and it doesn't compile until they're filled in (section 5): nothing runs with a made-up value. The only things filled in are names a node needs to read at all, chosen not to clash with any name in the graph: a For's Var (`_i`) and For Each's Variable (`_item`). Compile puts the error on the first empty field.
  - Dragged off a pin, the new node is wired to it. If that pin already had a wire, the new node goes **in the middle** of it.
- **Nodes:** drag to move (saved positions are rounded to whole grid units). Delete removes the selected nodes. The entry node can't be moved or deleted: it stays at (0,0), even when it's dragged along with other nodes.
- **Copy, Cut, Paste, Duplicate** (Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+W), as in Blueprint. Pasted nodes are centred on the mouse, keep their places relative to each other, and are selected. Wires between copied nodes are kept; wires to nodes that weren't copied are dropped. Each pasted node gets the next free NodeID. Nodes can be pasted into another graph of the script or another script; variables they use that the new graph doesn't declare fail to compile until they're declared there. The entry node isn't copied, and Bango nodes can't be pasted into a Blueprint (or Blueprint nodes into a Bango graph). The clipboard holds Unreal's text form of the nodes, not Bango code.
- **Wires:** drag between exec pins. Each pin has at most one wire, so a new wire replaces the old ones. Wires that would form a loop are refused.
- **Comments:** edit a node's comment bubble as in Blueprint.
- **Undo / Redo** (Ctrl+Z / Ctrl+Y) undo every kind of change above.
- **Compile** checks the script as it would be saved. The first error is shown on its node (with the error's field outlined in red) and the view jumps to it; a notification repeats the message. A clean compile says so.
- Compile also shows **warnings**, which don't stop the script from compiling or running: each placeholder row that is *missing* or `nil` gets a yellow outline (its tooltip says why) and its node a **WARNING!** banner. If there are warnings but no error, the view jumps to the first one and the notification says how many there are. An error on a node replaces its warning banner. Any edit to the node clears its warnings until the next Compile.
- **Save** writes the file in the saved layout above. If the file was changed outside the editor since it was opened (and Keep Mine wasn't chosen for that change), Save asks before overwriting it.
- If the Bango Scripts tab closes with unsaved changes, each changed script is kept in `Saved/Bango/Unsaved/<script>.bango` and the output log says so. (Closing a script tab asks instead; answering No throws the changes away.)
- Functions and parameters are added, renamed, removed and edited in the script editor's **Functions** and **Inputs** lists (above). Not yet: adding extra values to variadic commands other than through placeholders, and updating calls to a function when its parameters change.

---

## 13. Deferred / later
- Commands that use the id `Exec` gives back: stop a script, or check whether it's still running (and maybe a handle type in the language instead of an `int`)
- A level-placed "Bango Script" component that shows a script's parameters as editable fields
- Maps
- Rotators, and commands for them
- A way to check whether a Bango ID's actor is in the level before using it
- A shorthand for giving a global a starting value only if it's `nil` (like a "Set if not defined" node)
- A deterministic random source, seedable and saved with the game, used by every random command (`Random`, `RandomInt`, `RandomBool`, `Chance`), so a run can be replayed exactly
- The vector commands for `vector2` too (they take `vector3` for now)
- Debugger: while following a copy that failed, keep a faint steady outline on the nodes that led to the error (its trail), instead of only flashing them
- Graph editor: opening a script's editor in a window of its own, a pop-up for editing long text, updating a function's calls when its parameters change, an alphabetical-sort toggle for the function list, adding extra values to variadic commands, and asking before the tab closes with unsaved changes

---

## 14. Example

```
#define LOCTEXT_NAMESPACE "Examples.Things"

global int foundThings;
global bool playerHasGun;
global string[] questLog;

function void Nag()
{
	@john Speak("Find a gun.");    //@20(0, 0)
	AwaitSpeak();                  //@21(0, 1)
}

if (!IsNil(foundThings) && foundThings >= 10)   //@4(0, 3)
{
	Hint(LOCTEXT("FoundTen", "You found at least 10 things!"));    //@5(1, 4)
	questLog Push("Found 10 things");              //@6(1, 5)
}

for (int _i = 0; _i < 3; _i++)     //@7(0, 6)
{
	Hint(LOCTEXT("Countdown", "Countdown: {0}"), 3 - _i);          //@8(1, 7)
	Sleep(1.0);                    //@9(1, 8)
}

label Check;                       //@10(0, 9)
if (IsNil(playerHasGun))      //@11(0, 10)
{
	@john Speak("Congrats! You found a bug.");     //@12(1, 11)
	AwaitSpeak();                  //@13(1, 12)
	return;                        //@14(1, 13)
}
else
{
	if (!playerHasGun)             //@15(2, 11)
	{
		Nag();                     //@16(3, 12)
		Sleep(Random(3.0, 5.0));   //@17(3, 13)
		goto Check;                //@18(3, 14)
	}
}

forEach (_entry in questLog)       //@19(0, 15)
{
	if (!IsNil(_entry))         //@22(1, 16)
	{
		WriteLog(_entry);               //@23(2, 17)
	}
}

@john Speak("Great, you found a gun.");  //@24(0, 18)
AwaitSpeak();                            //@25(0, 19)

#undef LOCTEXT_NAMESPACE
```

`@john` is the actor whose Bango ID component is named `john`; the script fails at that line if no such actor is in the level. `Speak` and `AwaitSpeak` are example game-specific commands, not built-ins.

---

## 15. Script assets

*Built in stages (below); it replaced the script files in a `Content/Bango` folder.*

Scripts become assets: a **Bango Script** asset (`UBangoScriptAsset`, a `.uasset`) can live anywhere in Content, is found by the Asset Registry, is packaged like any asset, and can be referenced like one (renaming or moving it in the Content Browser keeps the references). A script asset is one of two kinds:

- **Embedded:** the script's text is stored in the asset. Made with the Content Browser's **Add > Bango Script**. Edited only in the editor.
- **Linked:** the script's text is a `.bango` file next to the asset, with the same name (`Quest/Intro.uasset` and `Quest/Intro.bango`). **The file always wins:** it is the script; the asset keeps a copy of its text (for packaged games and the runtime), refreshed whenever the file changes. The file can be edited in the graph editor or anything else (VS Code).

### Linking
- **Importing:** a `.bango` file that appears in Content, while the editor runs (saved from another editor, copied in) or since it last ran, is **imported automatically**: a linked asset is made next to it, and a notification says so. Dragging a `.bango` into the Content Browser does the same.
- **Keeping the copy current:** the editor watches Content for changes to `.bango` files. When a linked file changes, its asset's copy is refreshed from it (and the asset saved). When the editor starts, every linked asset whose file changed while it was closed is refreshed the same way. A hash of the file's text, kept in the asset, tells a real change from the editor's own save.
- **Saving in the graph editor:** for a linked script, Save writes the `.bango` file, then refreshes the asset's copy and saves the asset. For an embedded script, Save writes the text into the asset and saves it. With source control, Save checks out what it writes (both files for a linked script).
- **Moving and renaming:** moving or renaming a linked asset in the Content Browser moves or renames its `.bango` file too, so they stay side by side.
- **Deleting the file:** when a linked asset's `.bango` file is deleted, the asset isn't deleted: it becomes embedded (keeping its last copy of the text), and a notification says so. Deleting the asset leaves the file, which would then be imported again: so deleting the asset asks whether to delete its file as well.
- **Commands** (the asset's right-click menu): **Export to .bango** (embedded → linked: writes the file next to it), **Unlink** (linked → embedded: keeps the text, leaves the file, which is then ignored for this asset), and, for a linked asset, **Show .bango File** (in the file browser) and **Edit in External Editor** (opens the file with the system's editor for `.bango` files).
- A `.bango` file that isn't next to an asset of the same name is always a new script to import; a file next to an embedded asset (after Unlink) is left alone.

### Naming scripts
- A script's name is its **asset path**, without the asset name repeated: `/Game/Quests/Intro`. Errors show it with `.bango` (`/Game/Quests/Intro.bango(12:5)`).
- **From a script** (`Exec`): a path is **relative to the calling script's folder**: in `/Game/Quests/Start`, `Exec("Intro")` starts `/Game/Quests/Intro` and `Exec("../Shared/Greet")` starts `/Game/Shared/Greet`. A path starting with `/` is a full asset path (`Exec("/Game/Shared/Greet")`). `..` can't go above the content root (`/Game`, or a plugin's).
- **From Blueprint and C++** (the Dynamic Path spawn node, `UBangoScriptSubsystem::Spawn` with a string): a path starting with `/` is a full asset path; one without is relative to `/Game` (`"Quests/Intro"` is `/Game/Quests/Intro`).
- **The Spawn Bango Script node** *(built)* picks its script with an asset picker (a Bango Script asset reference), instead of a typed path (section 10). C++ can also spawn an asset directly (`Spawn(UBangoScriptAsset*, Args)`).
- Backslashes and a trailing `.bango` are still accepted in paths.

### Runtime
- Spawning loads the asset if it isn't loaded (a synchronous load), then compiles its text on first use, as now. The compiled script is cached per asset and reused until the asset's text changes.
- **Hot reload** (section 11) works from the asset: when a script's asset changes in the editor (saved in the graph editor, or its linked file changed and was refreshed), running copies switch to the new version as now.
- **Save Bango State** stores each running script's asset path (saves from before this change can't be loaded).
- The **Script Directory** project setting, and the packaging setting for `Content/Bango`, are gone *(built)*. The graph editor only opens script assets.

### Translation
*(built)* A Bango Script asset reports its script's texts to Unreal's asset gathering itself, with their namespaces, keys and translator notes (a gathering callback for the class, run when the asset is saved): see section 11, Translation. Embedded and linked scripts alike; nothing extra is stored in the asset. Gathering `.bango` files as text files is no longer needed, and would gather linked scripts twice.

### The graph editor
- **Opening a script asset** (double-clicking it in the Content Browser) opens the **Bango Script editor** *(built)*: a **Script** tab with the graph editor, in front, and a **Details** tab behind it in the same tab stack. Its script's name is its asset path. A linked script is read from its `.bango` file (if the file can't be read, the asset's copy is shown); Save writes the file (checking it out with source control), then the asset's copy, and saves the asset; for an embedded script, Save writes the asset and saves it. The window's own Save (File menu) does the same, and closing it asks about unsaved changes. Running copies of the script pick up the saved text (hot reload).
- **The Bango Scripts tab** *(built)* lists every Bango Script asset from the Asset Registry, by asset path without `/Game/` (`Quests/Intro`), or in their folders (a root per content root: Content first, then plugins'; only folders that hold a script). It follows script assets as they're added, renamed and deleted. Opening one gives it a tab there, titled with its name, with the same editor as the asset's own window (above).
- A linked script shows its file's path under the toolbar, and the editor's existing changed-on-disk handling (section 12) watches the `.bango` file. For an embedded script it watches the asset instead (changed by source control, say).

### Building it, in stages
1. **Runtime** *(built)*: the asset class (text, link data, localized texts), the script cache by asset, path resolution (relative paths for `Exec`), the Blueprint/C++ spawn functions, saved state, hot reload from assets. Spawning by asset path works; the K2 node still takes a typed path.
2. **Editor import and linking** *(built)*: the factories (import a `.bango`, Add > Bango Script), the Content watcher, the startup scan (in the background, skipping World Partition's `__External…` folders), moving/renaming, copying (Duplicate writes the copy's own file), deleting, and the right-click commands. Assets are saved as soon as they're imported or refreshed (while playing in the editor, once play ends). The engine's own Auto Reimport, if it's turned on, may import a new `.bango` first, through the same importer; the watcher then finds the asset already there.
3. **The graph editor and nodes** *(built)*: opening a script asset in the graph editor, loading and saving through the asset (linked: the file), the script browser from the Asset Registry, the Spawn Bango Script node's asset picker, the localized texts (gathered by the asset), tests, and removing the Script Directory setting. (The example scripts became assets through the import.)
