//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool
//--------------===//
//===----------------------------------------------------------------------===//
#include <exception>
#include <stdio.h>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/Decl.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

using namespace clang;

class StackFrame {
  /// StackFrame maps Variable Declaration to Value
  /// Which are either integer or addresses (also represented using an Integer
  /// value)
  std::map<Decl *, int> mVars;
  std::map<Stmt *, int> mExprs;
  /// The current stmt
  Stmt *mPC;

public:
  StackFrame() : mVars(), mExprs(), mPC() {}

  void bindDecl(Decl *decl, int val) { mVars[decl] = val; }
  int getDeclVal(Decl *decl) {
    assert(mVars.find(decl) != mVars.end());
    return mVars.find(decl)->second;
  }
  void bindStmt(Stmt *stmt, int val) { mExprs[stmt] = val; }
  int getStmtVal(Stmt *stmt) {
    IntegerLiteral *pi;
    if ((pi = dyn_cast<IntegerLiteral>(stmt))) {
      return pi->getValue().getSExtValue();
    }
    assert(mExprs.find(stmt) != mExprs.end());
    return mExprs[stmt];
  }

  void setPC(Stmt *stmt) { mPC = stmt; }
  Stmt *getPC() { return mPC; }
};

/// Heap maps address to a value
/*
class Heap {
public:
   int Malloc(int size) ;
   void Free (int addr) ;
   void Update(int addr, int val) ;
   int get(int addr);
};
*/

class ReturnException : public std::exception {
  int mRet;

public:
  ReturnException(int ret) : mRet(ret) {}

  int getRetVal() { return mRet; }
};

class Environment {
  std::vector<StackFrame> mStack;

  FunctionDecl *mFree; /// Declartions to the built-in functions
  FunctionDecl *mMalloc;
  FunctionDecl *mInput;
  FunctionDecl *mOutput;

  FunctionDecl *mEntry;

public:
  void stackPop() { mStack.pop_back(); }

  StackFrame &stackTop() { return mStack.back(); }
  static const int SCH001 = 11217991;
  /// Get the declartions to the built-in functions
  Environment()
      : mStack(), mFree(NULL), mMalloc(NULL), mInput(NULL), mOutput(NULL),
        mEntry(NULL) {}

  /// Initialize the Environment
  void init(TranslationUnitDecl *unit) {
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
      }
    }
    mStack.push_back(StackFrame());
  }

  FunctionDecl *getEntry() { return mEntry; }

  /// !TODO Support comparison operation
  void binop(BinaryOperator *bop) {
    Expr *left = bop->getLHS();
    Expr *right = bop->getRHS();
    int lval = mStack.back().getStmtVal(left);
    int rval = mStack.back().getStmtVal(right);

    auto op = bop->getOpcode();
    if (bop->isAssignmentOp()) {
      mStack.back().bindStmt(left, rval);
      if (DeclRefExpr *declexpr = dyn_cast<DeclRefExpr>(left)) {
        Decl *decl = declexpr->getFoundDecl();
        mStack.back().bindDecl(decl, rval);
      }
    } else if (bop->isAdditiveOp()) {
      stackTop().bindStmt(bop, lval + rval);
    } else if (bop->isComparisonOp()) {
      int val = SCH001;
      switch (op) {
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
      llvm::errs() << "op: " << op << "val " << val << "\n";
      stackTop().bindStmt(bop, val);
    }

    else {
      llvm::errs() << bop->getStmtClassName() << "Not Supported\n";
    }
  }

  void parm(ParmVarDecl *parmdecl) { stackTop().bindDecl(parmdecl, 0); }

  void decl(DeclStmt *declstmt) {
    for (DeclStmt::decl_iterator it = declstmt->decl_begin(),
                                 ie = declstmt->decl_end();
         it != ie; ++it) {
      Decl *decl = *it;
      if (VarDecl *vardecl = dyn_cast<VarDecl>(decl)) {
        int val = 0;
        Expr *expr = vardecl->getInit();
        IntegerLiteral *pi;
        if (expr != NULL && (pi = dyn_cast<IntegerLiteral>(expr))) {
          val = pi->getValue().getSExtValue();
        }
        mStack.back().bindDecl(vardecl, val);
      }
    }
  }
  void declref(DeclRefExpr *declref) {
    mStack.back().setPC(declref);
    if (declref->getType()->isIntegerType()) {
      Decl *decl = declref->getFoundDecl();

      int val = mStack.back().getDeclVal(decl);
      mStack.back().bindStmt(declref, val);
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
    } else {
      /// You could add your code here for Function call Return
      mStack.push_back(StackFrame()); // push frame
      // define parameter list
      for (int i = 0; i < callee->getNumParams(); i++) {
        this->parm(callee->getParamDecl(i));
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
