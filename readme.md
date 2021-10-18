## Compiler Homework

### How to Use

Build the project:

```shell
mkdir build
cd build
cmake –DLLVM_DIR=your-llvm-dir -DCMAKE_BUILD_TYPE=Debug ../.
# as an example: cmake –DLLVM_DIR=/usr/local/llvm10d -DClang_DIR=/usr/local/llvm10d/lib/cmake/clang  -DCMAKE_BUILD_TYPE=Debug ../.
make
//ast-interpreter will be built in dir your-llvmdir/build/bin
```

Visualize the AST via clang:

```shell
clang -Xclang -ast-dump -fsyntax-only
```

Run our tiny interpreter:

```shell
"`cat ../test/test01.c`"
```

### Test & grading

I write [a simple script](./grade.sh) to grade the interpreter implementation. It compares the output of ast-interpreter with gcc. The official grading script(`grade-official.sh`) is also provided, which is modified from `grade.sh`. Run by:

```shell
source grade.sh # or grade-official.sh
```