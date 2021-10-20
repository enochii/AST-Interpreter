## Compiler Homework

Assignment 1, a tiny ast interpreter for a subset of C.

### Build the project

```shell
mkdir build
cd build
cmake –DLLVM_DIR=your-llvm-dir -DCMAKE_BUILD_TYPE=Debug ../.
# as an example: cmake –DLLVM_DIR=/usr/local/llvm10d -DClang_DIR=/usr/local/llvm10d/lib/cmake/clang  -DCMAKE_BUILD_TYPE=Debug ../.
make
//ast-interpreter will be built in dir your-project-dir/build/bin
```

Visualize the AST via clang:

```shell
clang -Xclang -ast-dump -fsyntax-only
```

Run our tiny interpreter:

```shell
./ast-interpreter "`cat ../test/test01.c`"
```

### Test & grading

I write [a simple script](./grade.sh) to grade the interpreter implementation. It compares the output of ast-interpreter with gcc. The official grading script(`grade-official.sh`) is also provided, which is modified from `grade.sh`. Run by:

```shell
source grade.sh # or grade-official.sh
```

### More information

You can take a look at the [note.md](./note.md) if you are interested in implementation details.