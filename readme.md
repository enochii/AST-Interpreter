## Compiler Homework

```shell
cd ast-interpreter
mkdir build
cd build
cmake –DLLVM_DIR=your-llvm-dir -DCMAKE_BUILD_TYPE=Debug ../.
make
//ast-interpreter will be built in dir your-llvmdir/build/bin

clang -Xclang -ast-dump -fsyntax-only

"`cat ../test/test01.c`"
```

### About `call handling`

The mechanism of function call is mainly related to 2 functions:

- push a frame
- enter callee body, visit the stmt list
- when encounter a return stmt, we throw a exception with a return value
- `VisitCallExpr` will catch exception and extract the return value, which will be binded into env.

```c++
    virtual void VisitCallExpr(CallExpr * call) {
	   VisitStmt(call);
	   mEnv->call(call);
      try {
         VisitStmt(mEnv->stackTop().getPC());
      } catch (ReturnException& e) {
         int retVal = e.getRetVal();
         mEnv->stackPop();
         llvm::errs() << "catch val: " << retVal << "\n";
         mEnv->stackTop().bindStmt(call, retVal);
      }
   }

   void call(CallExpr * callexpr) {
	   mStack.back().setPC(callexpr);
	   int val = 0;
	   FunctionDecl * callee = callexpr->getDirectCallee();
	   // ...
	   } else {
		   /// You could add your code here for Function call Return
		    mStack.push_back(StackFrame());
		    int retVal = 0;
			mStack.back().setPC(callee->getBody());
	   }
   }
```