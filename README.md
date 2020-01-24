# sudoku-master

Ok so if you want a good boost to your ego, yet lack certain qualities in the
nether regions of your body the first thing you need to do is write a sudoku
solver. Sooner or later you're going to run into another one of your kind and
you're going to have to throw down my dude. This bad boy here does you the
service of measuring your bad boys side by side, stroke for stroke.

## ABI Interface

For this to work you're going to have to follow a relatively strict but simple
interface:

```
const char* name;
const char* author;
int solve(int*);
```

and then you compile it as a shared object with ``-shared -fPIC``. Sudoku Master
will load your shared object, search for the appropriate symbols, and solve some
puzzles. The name and author will be conveniently printed in the output and will
see your name!

The pointer given to the solve function is both an input and output parameter,
and the buffer length is always 81 ints. It is initially seeded with constant
values and you need to fill the zeroed cells with your solution.
