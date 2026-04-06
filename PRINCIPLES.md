# The 5 programming principles

These are Rob Pike's programming principles, and I agree with them.

**Rule 1.** You can't tell where a program is going to spend its time.  Bottlenecks occur in surprising places, 
so don't try to second guess and put in a speed hack until you've proven that's where the bottleneck is.

**Rule 2.** Measure.  Don't tune for speed until you've measured, and even then don't unless one part of the 
code overwhelms the rest.

**Rule 3.** Fancy algorithms are slow when n is small, and n is usually small. Fancy algorithms have big constants. 
Until you know that n is frequently going to be big, don't get fancy.  (Even if n does get big, use Rule 2 first.) 
For example, binary trees are always faster than splay trees for workaday problems.

**Rule 4.** Fancy algorithms are buggier than simple ones, and they're much harder to implement.  
Use simple algorithms as well as simple data structures. The following data structures are a complete list 
for almost all practical programs:
- array
- linked list
- hash table
- binary tree

Of course, you must also be prepared to collect these into compound data structures.
For instance, a symbol table might be implemented as a hash table containing linked lists of arrays of characters.

**Rule 5.** Data dominates. If you've chosen the right data structures and organized things well, the algorithms 
will almost always be self-evident. Data structures, not algorithms, are central to programming.

# Behaviour is data interpreted by a simple engine

Push complexity out of control flow and into data. Instead of hardcoding behavior with if/switch, 
describe it as tables (data) and write a small, stable engine that interprets those tables. 
The program becomes simpler, easier to change, and often generatable by other programs.

# Function Pointers make behavior a value

Function pointers let you treat behavior like data—storing and passing functions just like values.
Function pointers let you move behavior out of control flow into interchangeable functions 
that follow a common protocol. A small core dispatches calls while each function handles its own logic,
distributing complexity and avoiding central switches. This makes programs easier to extend, test, and evolve.
