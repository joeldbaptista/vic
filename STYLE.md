# C style

The style we have adopted in this project is the Suckless style. Check the details bellow.

## Guide lines

1. File layout

Consider the following order:

- Comment with LICENSE and possibly short explanation of file/tool.
- Headers
- Macros
- Types
- Function declarations:
    - Include variable names.
    - Group/order in logical manner.
- Global variables.
- Function definitions in same order as declarations.
- main

2. Key words

Use a space after `if`, `for`, `while`, `switch` (they are not function calls).
Do not use a space after the opening `(` and before the closing `)`.
Preferably use `()` with `sizeof`.
Do not use a space with `sizeof()`.

3. Blocks

Consider the following guide-lines:

All variable declarations at top of block.
`{` on same line preceded by single space (except functions).
`}` on own line unless continuing statement (`if`, `else`, `while`, etc).

Use block for single statement if inner statement needs a block:
```c
for (;;) {
	if (foo) {
		bar;
		baz;
	}
}
```

Use block if another branch of the same statement needs a block:
```c
if (foo) {
	bar;
} else {
	baz;
	qux;
}
```

4. White spaces

Use tabs instead of spaces to index code.

5. Variables

Keep the name of the variables short and expressive. For local variables, 
a simple letter is likely enough; for example, `i`, `j`, `k` for for-loop iterators;
`v` for a vector of things; `s` for a sum; `p` for a pointer; `num` for a fixed 
number of things; `max`, `min`, `avg` for maximum, minimum, and average; `tmp` for
a temporary thing; etc. In general, if the local variable is longer than 5 
letters, probably there is a shorter name equally expressive. 

Local variables should be defined at the top of its block. This is true for
function blocks, if blocks, for blocks, while blocks, switch blocks, etc. For
example:

```c
if (foo(a, b) > 0) {
    int i, j, k;
    int sum;
    
    sum = 0;
    for (k = 0; k < N; ++k) {
        i = k * k;
        j = k * M * N;
        if (bar(i, j) < 0) 
            sum += i * j;
    }
}
```

Use single-line declaration when variables are logically related. Otherwise,
declare each variable in one line. 

Declaration is assignment must be done separately. In declaration of pointers
the * is adjacent to variable name, not type.

Global variables not used outside translation unit should be declared static.

6. Functions

Return type and modifiers on own line.
Function name and argument list on next line. This allows to grep for function names simply using grep ^functionname(.
Opening { on own line (function definitions are a special case of blocks as they cannot be nested).
Functions not used outside translation unit should be declared and defined static.

```c
static void
usage(void)
{
	eprintf("usage: %s [file ...]\n", argv0);
}
```

Procedure names should reflect what they do; function names should reflect what they return.
Functions are used in expressions, often in things like if's, so they need to read appropriately.
`if(checksize(x))` is unhelpful because we can't deduce whether checksize returns true on error or non-error; instead
`if(validsize(x))` makes the point clear and makes a future mistake in using the routine less likely.

7. Switch

Do not indent cases another level.
Comment cases that FALLTHROUGH.
For example:

```c
switch (value) {
case 0: /* FALLTHROUGH */
case 1:
case 2:
	break;
default:
	break;
}
```

8. User defined types

User defined types must be written in CamelCase. Do not use `_t`. Use `struct` 
follow by the name in snake_case, and the use `typedef`. 

For example:

```c
typedef struct user_buffer UserBuffer;
struct user_buffer {
    char text[MAX_TEXT_LEN];
    size_t used;
};
```

9. Line length and tabs

Keep line length capped at 80 characters long.

10. Tests

Do not use C99 bool types (stick to integer types).
Otherwise use compound assignment and tests unless the line grows too long:

```c
if (!(p = malloc(sizeof(*p))))
	hcf();
```

11. Error handling

When functions return -1 for error test against 0 not -1:

```c
if (func() < 0)
	hcf();
```

Use goto to unwind and cleanup when necessary instead of multiple nested levels.
return or exit early on failures instead of multiple nested levels.
Unreachable code should have a NOTREACHED comment.
Think long and hard on whether or not you should cleanup on fatal errors. 
For simple "one-shot" programs (not daemons) it can be OK to not free memory. 
It is advised to cleanup temporary files however.

12. Enums and defines

Use enums for values that are grouped semantically and #define otherwise:

```c
#define MAXSZ  4096
#define MAGIC1 0xdeadbeef
enum {
	DIRECTION_X,
	DIRECTION_Y,
	DIRECTION_Z
};
```

13. About the use of `goto`

`goto` is used specifically for clean error handling, avoiding nested if statements,
and improving readability during cleanup, rather than chaotic branching. The primary 
use case is jumping to a single error label at the end of a function to free memory 
or close files, a technique often preferred over deeply nested conditional structures. 

For example:

```c
if (error_happened) {
    goto cleanup;
}
/* ... */
cleanup:
    free(resource);
    return -1;
```

## References

- [Suckless coding style](https://suckless.org/coding_style/)
- [OpenBSD style](https://man.openbsd.org/style)
- [Pike style](https://doc.cat-v.org/bell_labs/pikestyle)
