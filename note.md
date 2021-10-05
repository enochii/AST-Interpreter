## Some Note

Passing null pointer to `dyn_cast` will crash!

Binop is `Stmt`

No Base Class for AST

### Why we need `VisitImplictCastExpr`

```shell
ArraySubscriptExpr 0x62adf08 'int' lvalue
|-ImplicitCastExpr 0x62aded8 'int *' <ArrayToPointerDecay>
| `-DeclRefExpr 0x62ade70 'int [3]' lvalue Var 0x62adb10 'a' 'int [3]'
`-ImplicitCastExpr 0x62adef0 'int' <LValueToRValue>
  `-DeclRefExpr 0x62ade90 'int' lvalue Var 0x62adba8 'i' 'int'
children size=child 0 0x62aded8 child 1 0x62adef0 
2
DeclRefExpr 0x62ade70 'int [3]' lvalue Var 0x62adb10 'a' 'int [3]'
DeclRefExpr 0x62ade90 'int' lvalue Var 0x62adba8 'i' 'int'
getBase() 0x62aded8
asti: /home/shichenghang/compiler/ast-interpreter/Environment.h:43: int StackFrame::getStmtVal(clang::Stmt*): Assertion `mExprs.find(stmt) != mExprs.end()' failed.
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
         llvm::outs() << "catch val: " << retVal << "\n";
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