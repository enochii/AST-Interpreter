//==--- tools/clang-check/ClangInterpreter.cpp - Clang Interpreter tool
//--------------===//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"

#include <iostream>

using namespace clang;

#include "Environment.h"

#define DEBUG_FLAG 1

class InterpreterVisitor : public EvaluatedExprVisitor<InterpreterVisitor> {
public:
  explicit InterpreterVisitor(const ASTContext &context, Environment *env)
      : EvaluatedExprVisitor(context), mEnv(env) {}
  virtual ~InterpreterVisitor() {}

  virtual void VisitBinaryOperator(BinaryOperator *bop) {
    VisitStmt(bop);
    mEnv->binop(bop);
  }

  virtual void VisitUnaryOperator(UnaryOperator * uop) {
    VisitStmt(uop);
    mEnv->uop(uop);
  }

  virtual void VisitIntegerLiteral(IntegerLiteral * il) {
    int val = il->getValue().getSExtValue();
    mEnv->bindStmt(il, val);
  }

  virtual void VisitDeclRefExpr(DeclRefExpr *expr) {
    VisitStmt(expr);
    mEnv->declref(expr);
  }
  virtual void VisitCastExpr(CastExpr *expr) {
    VisitStmt(expr);
    mEnv->cast(expr);
  }
  virtual void VisitCallExpr(CallExpr *call) {
    VisitStmt(call);
    mEnv->call(call);
    // FunctionDecl * callee = call->getDirectCallee();
    try {
      VisitStmt(mEnv->stackTop().getPC());
    } catch (ReturnException &e) {
      int retVal = e.getRetVal();
      mEnv->stackPop();
      llvm::errs() << "catch val: " << retVal << "\n";
      mEnv->stackTop().bindStmt(call, retVal);
    }
  }
  virtual void VisitDeclStmt(DeclStmt *declstmt) {
#if DEBUG_FLAG
    llvm::errs() << "VisitDeclStmt"
                 << "\n";
#endif
    // VisitStmt(declstmt);
    mEnv->decl(declstmt);
  }

  int getChildrenSize(Stmt * stmt) {
    int i = 0;
    for (auto c:stmt->children()) {
      llvm::errs() << "child " << i << " " << c << " ";
      i++;
    }
    llvm::errs() << "\n";
    return i;
  }
  virtual void VisitArraySubscriptExpr(ArraySubscriptExpr * arrsubexpr) {
    arrsubexpr->dump();
    llvm::errs() << "children size=" << getChildrenSize(arrsubexpr) << "\n";
    VisitStmt(arrsubexpr);
    mEnv->arraysub(arrsubexpr);
  }

  // virtual void VisitParmVarDecl(ParmVarDecl * parmdecl) {
  // #if DEBUG_FLAG
  //    llvm::errs() << "VisitParmVarDecl" << "\n";
  // #endif
  //    mEnv->parm(parmdecl);
  // }

  virtual void VisitReturnStmt(ReturnStmt *retstmt) {
    VisitStmt(retstmt);
    mEnv->retrn(retstmt);
  }

  virtual void VisitIfStmt(IfStmt *ifstmt) {
    Expr *condExpr = ifstmt->getCond();
    this->Visit(condExpr);
    int cond = mEnv->stackTop().getStmtVal(condExpr);
    if (cond) {
      // llvm::errs() << "then branch\n";
      if (ifstmt->getThen()) this->Visit(ifstmt->getThen());
    } else {
      if (ifstmt->getElse()) this->Visit(ifstmt->getElse());
      // llvm::errs() << "else branch\n";
    }
  }

  virtual void VisitWhileStmt(WhileStmt * wstmt) {
    Expr * condExpr = wstmt->getCond();
    do {
      this->Visit(condExpr);
      int cond = mEnv->getStmtVal(condExpr);
      if(!cond) break;
      this->Visit(wstmt->getBody());
    } while (true);
  }

  virtual void VisitForStmt(ForStmt * fstmt) {
    Stmt * initstmt = fstmt->getInit();
    this->Visit(initstmt);
    Expr * condExpr = fstmt->getCond();
    do {
      this->Visit(condExpr);
      int cond = mEnv->getStmtVal(condExpr);
      if(!cond) break;
      this->Visit(fstmt->getBody());
      this->Visit(fstmt->getInc());
    } while (true);
  }

  /// ??? workaround
  virtual void VisitImplicitCastExpr(ImplicitCastExpr * icastexpr) {
    this->VisitStmt(icastexpr);
    Stmt * stmt = nullptr;
    for(auto c:icastexpr->children()) {
      stmt = c;break;
    }
    
    if(stmt) {
      bool flag = false;
      if(DeclRefExpr * declref = dyn_cast<DeclRefExpr>(stmt)) {
        if(!mEnv->isBuiltInDecl(declref)) {
          stmt->dump();
          mEnv->bindStmt(icastexpr, mEnv->stackTop().getStmtVal(stmt));
        }
      } else if(ArraySubscriptExpr * arrsub = dyn_cast<ArraySubscriptExpr>(stmt)) {
          stmt->dump();
          mEnv->bindStmt(icastexpr, mEnv->stackTop().getStmtVal(stmt));
      }
    }
  }
private:
  Environment *mEnv;
};

class InterpreterConsumer : public ASTConsumer {
public:
  explicit InterpreterConsumer(const ASTContext &context)
      : mEnv(), mVisitor(context, &mEnv) {}
  virtual ~InterpreterConsumer() {}

  virtual void HandleTranslationUnit(clang::ASTContext &Context) {
    TranslationUnitDecl *decl = Context.getTranslationUnitDecl();
    mEnv.init(decl);

    FunctionDecl *entry = mEnv.getEntry();
    mVisitor.VisitStmt(entry->getBody());
  }

private:
  Environment mEnv;
  InterpreterVisitor mVisitor;
};

class InterpreterClassAction : public ASTFrontendAction {
public:
  virtual std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &Compiler, llvm::StringRef InFile) {
    return std::unique_ptr<clang::ASTConsumer>(
        new InterpreterConsumer(Compiler.getASTContext()));
  }
};

int main(int argc, char **argv) {
  if (argc > 1) {
    clang::tooling::runToolOnCode(
        std::unique_ptr<clang::FrontendAction>(new InterpreterClassAction),
        argv[1]);
  }
  // std::cout << "Hello sch001\n";
}
