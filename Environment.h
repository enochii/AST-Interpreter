//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool
//--------------===//
//===----------------------------------------------------------------------===//
#include <exception>
#include <stdio.h>
#include <vector>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

// class Environment;
class StackFrame {
  // friend class Environment;
  /// StackFrame maps Variable Declaration to Value
  /// Which are either integer or addresses (also represented using an Integer
  /// value)
  std::map<Decl *, int> mVars;
  std::map<Stmt *, int> mExprs;
  /// The current stmt
  Stmt *mPC;

public:
  StackFrame() : mVars(), mExprs(), mPC() {}

  bool hasDecl(Decl *decl) { return mVars.find(decl) != mVars.end(); }
  void bindDecl(Decl *decl, int val) { mVars[decl] = val; }
  int getDeclVal(Decl *decl) {
    assert(mVars.find(decl) != mVars.end());
    return mVars.find(decl)->second;
  }

  bool hasStmt(Stmt *stmt) { return mExprs.find(stmt) != mExprs.end(); }
  void bindStmt(Stmt *stmt, int val) { mExprs[stmt] = val; }
  int getStmtVal(Stmt *stmt) {
    // IntegerLiteral *pi;
    // if ((pi = dyn_cast<IntegerLiteral>(stmt))) {
    //   return pi->getValue().getSExtValue();
    // }
    if(!hasStmt(stmt)) {
      llvm::errs() << "We cannot find the value of below stmt:\n";
      stmt->dump();
    }
    assert(mExprs.find(stmt) != mExprs.end());
    return mExprs[stmt];
  }

  void setPC(Stmt *stmt) { mPC = stmt; }
  Stmt *getPC() { return mPC; }
};

/// Heap maps address to a value
class Heap {
public:
  typedef int HeapAddr;
private:
  /// In this simple heap implementation, we malloc a space.
  /// And we will keep it until the interpreter exits
  static const HeapAddr INIT_HEAP_SIZE = sizeof(int) * 1024; 
  void *mHeapPtr;
  HeapAddr mOffset;

  inline int* actualAddr(HeapAddr addr) {
    assert(addr <= INIT_HEAP_SIZE);
    return (int*)((char*)mHeapPtr+addr);
  }
public:
    
    Heap():mHeapPtr(malloc(INIT_HEAP_SIZE)), mOffset(0) {}
    ~Heap(){ free(mHeapPtr); }
    HeapAddr Malloc(int size) {
      HeapAddr ret = mOffset;
      mOffset =+ size;
      llvm::errs() << "allocate size=" << size << ", return address=" << ret << ", still have " << INIT_HEAP_SIZE-mOffset << "\n";
      assert(mOffset <= INIT_HEAP_SIZE);
      return ret;
    }
    void Free (HeapAddr addr) {
      /// do nothing?
      /// this naive implementation will run out of memory when we simply keep malloc then free.
    }
    void Update(HeapAddr addr, int val) {
      int * ptr = actualAddr(addr);
      *ptr = val;
      llvm::errs() << "Update *" << addr << " -> " << val << "\n";
    }
    int get(HeapAddr addr) {
      int * ptr = actualAddr(addr);
      return *ptr;
    }
    /// sizeof(int*)
    static int getPtrSize() {
      return sizeof(HeapAddr);
    }

    /// (int*)x + 1, we need to increment 4!
    static int step2Size(int step) {
      return step * getPtrSize();
    }
};


class ReturnException : public std::exception {
  int mRet;

public:
  ReturnException(int ret) : mRet(ret) {}

  int getRetVal() { return mRet; }
};

class Array {
  int mScope;
  std::vector<int> mArr;
public:
  Array(int sz, int scope):mArr(sz), mScope(scope) {}
  void set(int i, int val) {
    assert(i <= mArr.size());
    mArr[i] = val;
  }
  int get(int i) {
    assert(i <= mArr.size());
    return mArr[i];
  }
};

class Environment {
  Heap mHeap;
  std::vector<StackFrame> mStack;
  std::vector<Array> mArrays;

  FunctionDecl *mFree; /// Declartions to the built-in functions
  FunctionDecl *mMalloc;
  FunctionDecl *mInput;
  FunctionDecl *mOutput;

  FunctionDecl *mEntry;

public:
  void stackPop() { 
    mStack.pop_back();
    //TODO: clear array
  }

  StackFrame &stackTop() { return mStack.back(); }

  StackFrame &globalScope() { return mStack[0]; } 

  void bindDecl(Decl *decl, int val) { 
    if(stackTop().hasDecl(decl)) {
      stackTop().bindDecl(decl, val);
    } else {
      /// it should be a global variable
      llvm::errs() << "bind global decl\n";
      globalScope().bindDecl(decl, val);
    }
  }
  int getDeclVal(Decl *decl) {
    if(stackTop().hasDecl(decl)) {
      return stackTop().getDeclVal(decl);
    } else {
      /// it should be a global variable
      // llvm::errs() << "get global decl\n";
      // decl->dump();
      return globalScope().getDeclVal(decl);
    }
  }
  void bindStmt(Stmt *stmt, int val) { stackTop().bindStmt(stmt, val); }
  int getStmtVal(Stmt *stmt) {
    return stackTop().getStmtVal(stmt);
  }

  static const int SCH001 = 11217991;
  /// Get the declartions to the built-in functions
  Environment()
      : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL),
        mEntry(NULL) {}

  /// Initialize the Environment
  void init(TranslationUnitDecl *unit) {
    mStack.push_back(StackFrame());
    for (TranslationUnitDecl::decl_iterator i = unit->decls_begin(),
                                            e = unit->decls_end();
         i != e; ++i) {
      if (FunctionDecl *fdecl = dyn_cast<FunctionDecl>(*i)) {
        if (fdecl->getName().equals("FREE"))
          mFree = fdecl;
        else if (fdecl->getName().equals("MALLOC"))
          mMalloc = fdecl;
        else if (fdecl->getName().equals("GET"))
          mInput = fdecl;
        else if (fdecl->getName().equals("PRINT"))
          mOutput = fdecl;
        else if (fdecl->getName().equals("main"))
          mEntry = fdecl;
      } else if(VarDecl *vdecl = dyn_cast<VarDecl>(*i)) {
        /// global variable?
        this->handleVarDecl(vdecl);
      }
    }
  }

  FunctionDecl *getEntry() { return mEntry; }

  void uop(UnaryOperator * uop) {
    auto opCode = uop->getOpcode();
    int val = stackTop().getStmtVal(uop->getSubExpr());
    switch(opCode) {
      case UO_Minus:
        val = -val;
        break;
      case UO_Plus:
        break;
      case UO_Not:
        val = ~val;
        break;
      case UO_LNot:
        val = !val;
        break;
      case UO_Deref:
        val = mHeap.get(val);
        break;
      default:
        llvm::errs() << "Below uop is not supported: \n";
        uop->dump();
        break;
    }
    stackTop().bindStmt(uop, val);
  }
  int handleAdditive(int opCode, Expr * left, Expr * right, int lval, int rval) {
    /// handle 
    auto ltype = left->getType();
    auto rtype = right->getType();
    bool lIsPtr = ltype->isPointerType();
    bool rIsPtr = rtype->isPointerType();
    if(lIsPtr && rIsPtr) {
      assert(opCode == BO_Sub);
      return (lval-rval)/Heap::getPtrSize();
    } else if(lIsPtr) {
      rval = Heap::step2Size(rval);
    } else if(rIsPtr) {
      lval = Heap::step2Size(lval);
    } /// else both are integers
    if (opCode == BO_Add) return lval + rval;
    else return lval - rval;
  }
  /// !TODO Support comparison operation
  void binop(BinaryOperator *bop) {
    Expr *left = bop->getLHS();
    Expr *right = bop->getRHS();
    // int lval = mStack.back().getStmtVal(left);
    int rval = mStack.back().getStmtVal(right);

    auto opCode = bop->getOpcode();
    int res = 0;
    if (bop->isAssignmentOp()) {
      mStack.back().bindStmt(left, rval);
      if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left)) {
        Decl *decl = declexpr->getFoundDecl();
        this->bindDecl(decl, rval);
      } else if(ArraySubscriptExpr * arrsub = dyn_cast<ArraySubscriptExpr>(left)) {
        auto& arr = getArray(arrsub);
        auto idx = getArrayIdx(arrsub);
        arr.set(idx, rval);
        this->bindStmt(arrsub, rval);
      } else if(UnaryOperator * uop = dyn_cast<UnaryOperator>(left)) {
        assert(uop->getOpcode() == UO_Deref); /// currently supported
        int addr = stackTop().getStmtVal(uop->getSubExpr());
        mHeap.Update(addr, rval);
        this->bindStmt(uop, rval); /// `*ptr = VAL;` should return VAL
      } else {
        llvm::errs() << "Below Assignment(LHS) is Not Supported\n";
        left->dump();
      }
    } else if (bop->isAdditiveOp()) {
      int lval = mStack.back().getStmtVal(left);
      res = handleAdditive(opCode, left, right, lval, rval);
      stackTop().bindStmt(bop, res);
    } else if (bop->isMultiplicativeOp()) {
      int lval = mStack.back().getStmtVal(left);
      if(opCode == BO_Mul) res = lval * rval;
      else res = lval % rval;
      stackTop().bindStmt(bop, res);
    } else if (bop->isComparisonOp()) {
      int lval = mStack.back().getStmtVal(left);
      int val = SCH001;
      switch (opCode) {
      case BO_LT:
        val = (lval < rval);
        break;
      case BO_GT:
        val = (lval > rval);
        break;
      case BO_LE:
        val = (lval <= rval);
        break;
      case BO_GE:
        val = (lval >= rval);
        break;
      case BO_EQ:
        val = (lval == rval);
        break;
      case BO_NE:
        val = (lval != rval);
        break;
      }
      // llvm::errs() << "op: " << op << "val " << val << "\n";
      stackTop().bindStmt(bop, val);
    }

    else {
      llvm::errs() << "Below Binary op is Not Supported\n";
      bop->dump();
    }
  }

  void parm(ParmVarDecl *parmdecl, int val) { stackTop().bindDecl(parmdecl, val); }

  /// use by global & local
  void handleVarDecl(VarDecl * vardecl) {
    auto typeInfo = vardecl->getType();
    if(typeInfo->isArrayType()) {
      // array type
      assert(typeInfo->isConstantArrayType());
      const ArrayType * arrayType = vardecl->getType()->getAsArrayTypeUnsafe();
      auto carrayType = dyn_cast<const ConstantArrayType>(arrayType);
      int sz = carrayType->getSize().getSExtValue();
      assert(sz > 0);
      llvm::errs() << "Init a array with size=" << carrayType->getSize() << "\n";
      mArrays.emplace_back(sz, mStack.size());
      stackTop().bindDecl(vardecl, mArrays.size()-1);
    }
    int val = 0;
    Expr *expr = vardecl->getInit();
    IntegerLiteral *pi;
    if (expr != NULL && (pi = dyn_cast<IntegerLiteral>(expr))) {
      val = pi->getValue().getSExtValue();
    }
    mStack.back().bindDecl(vardecl, val);
  }

  void decl(DeclStmt *declstmt) {
    for (DeclStmt::decl_iterator it = declstmt->decl_begin(),
                                 ie = declstmt->decl_end();
         it != ie; ++it) {
      Decl *decl = *it;
      if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {
        handleVarDecl(vardecl);
        // llvm::errs() << "opaque data: " << vardecl->getTypeSourceInfo()->getTypeLoc().getOpaqueData() << "\n";
      }
    }
  }

  Array& getArray(ArraySubscriptExpr * arrsubexpr) {
    int arrayID = stackTop().getStmtVal(arrsubexpr->getBase());
    assert(arrayID < mArrays.size());
    return mArrays[arrayID];
  }

  int getArrayIdx(ArraySubscriptExpr * arrsubexpr) {
    return stackTop().getStmtVal(arrsubexpr->getIdx());
  }

  void arraysub(ArraySubscriptExpr * arrsubexpr) {
    // llvm::errs() << "getBase() " << arrsubexpr->getBase() << "\n";
    auto& arr = getArray(arrsubexpr);
    int idx = getArrayIdx(arrsubexpr);
    int res = arr.get(idx);
    // llvm::errs() << "arr[" << idx << "]-> " << res << "\n";
    stackTop().bindStmt(arrsubexpr, res);
  }

  static bool isValidDeclRefType(DeclRefExpr *declref) {
    auto tp = declref->getType();
    return tp->isIntegerType() || tp->isArrayType() || tp->isPointerType();
  }

  bool isBuiltInDecl(DeclRefExpr *declref) {
    const Decl * decl = declref->getReferencedDeclOfCallee();
    return declref->getType()->isFunctionType() &&
    (decl == mInput || decl == mOutput || decl == mMalloc || decl == mFree);
  }

  void declref(DeclRefExpr *declref) {
    mStack.back().setPC(declref);
    if (isValidDeclRefType(declref)) {
      // declref->dump();
      Decl *decl = declref->getFoundDecl();

      int val = this->getDeclVal(decl);
      mStack.back().bindStmt(declref, val);
    } else if(!isBuiltInDecl(declref)){
      llvm::errs() << "Below declref is not supported:\n";
      declref->dump();
    }
  }

  void cast(CastExpr *castexpr) {
    mStack.back().setPC(castexpr);
    if (castexpr->getType()->isIntegerType()) {
      Expr *expr = castexpr->getSubExpr();
      int val = mStack.back().getStmtVal(expr);
      mStack.back().bindStmt(castexpr, val);
    }
  }

  /// !TODO Support Function Call
  void call(CallExpr *callexpr) {
    mStack.back().setPC(callexpr);
    int val = 0;
    FunctionDecl *callee = callexpr->getDirectCallee();
    if (callee == mInput) {
      llvm::errs() << "Please Input an Integer Value : ";
      scanf("%d", &val);

      mStack.back().bindStmt(callexpr, val);
    } else if (callee == mOutput) {
      Expr *decl = callexpr->getArg(0);
      val = mStack.back().getStmtVal(decl);
      llvm::errs() << val;
    } else if (callee == mMalloc) {
      Expr *decl = callexpr->getArg(0);
      val = mStack.back().getStmtVal(decl); /// malloc size
      int addr = mHeap.Malloc(val); /// our "address"
      stackTop().bindStmt(callexpr, addr);
    } else if (callee == mFree) {
      Expr *decl = callexpr->getArg(0);
      val = mStack.back().getStmtVal(decl); /// address waited to free
      mHeap.Free(val);
    } else {
      /// first we get the arguments from caller frame
      std::vector<int> args;
      Expr ** exprList = callexpr->getArgs();
      for(int i=0; i<callexpr->getNumArgs(); i++) {
        int val = stackTop().getStmtVal(exprList[i]);
        args.push_back(val);
      }
      /// You could add your code here for Function call Return
      mStack.push_back(StackFrame()); // push frame
      // define parameter list
      assert(callee->getNumParams() == callexpr->getNumArgs());
      for (int i = 0; i < callee->getNumParams(); i++) {
        this->parm(callee->getParamDecl(i), args[i]);
      }
      int retVal = 0;
      mStack.back().setPC(callee->getBody());
    }
  }

  void retrn(ReturnStmt *retstmt) {
    stackTop().setPC(retstmt);
    int val = stackTop().getStmtVal(retstmt->getRetValue());
    llvm::errs() << "return val: " << val << "\n";
    throw ReturnException(val);
  }
};
