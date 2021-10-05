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
    bool notBuiltin = mEnv->call(call);
    // FunctionDecl * callee = call->getDirectCallee();
    try {
      /// visit function body
      if(notBuiltin)
        VisitStmt(mEnv->stackTop().getPC());
    } catch (ReturnException &e) {
      int retVal = e.getRetVal();
      mEnv->stackPop();
      // llvm::outs() << "catch val: " << retVal << "\n";
      mEnv->stackTop().bindStmt(call, retVal);
    }
  }
  virtual void VisitDeclStmt(DeclStmt *declstmt) {
#if DEBUG_FLAG
    // llvm::outs() << "VisitDeclStmt" << "\n";
#endif
    // VisitStmt(declstmt);
    mEnv->decl(declstmt);
  }

  int getChildrenSize(Stmt * stmt) {
    int i = 0;
    for (auto c:stmt->children()) {
      // llvm::outs() << "child " << i << " " << c << " ";
      i++;
    }
    // llvm::outs() << "\n";
    return i;
  }
  virtual void VisitArraySubscriptExpr(ArraySubscriptExpr * arrsubexpr) {
    // arrsubexpr->dump();
    // llvm::outs() << "children size=" << getChildrenSize(arrsubexpr) << "\n";
    VisitStmt(arrsubexpr);
    mEnv->arraysub(arrsubexpr);
  }

  // virtual void VisitParmVarDecl(ParmVarDecl * parmdecl) {
  // #if DEBUG_FLAG
  //    llvm::outs() << "VisitParmVarDecl" << "\n";
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
      // llvm::outs() << "then branch\n";
      if (ifstmt->getThen()) this->Visit(ifstmt->getThen());
    } else {
      if (ifstmt->getElse()) this->Visit(ifstmt->getElse());
      // llvm::outs() << "else branch\n";
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
    if (initstmt) this->Visit(initstmt);
    Expr * condExpr = fstmt->getCond();
    do {
      this->Visit(condExpr);
      int cond = mEnv->getStmtVal(condExpr);
      if(!cond) break;
      this->Visit(fstmt->getBody());
      this->Visit(fstmt->getInc());
    } while (true);
  }

  virtual void VisitCStyleCastExpr(CStyleCastExpr * ccastexpr) {
    this->VisitStmt(ccastexpr);
    stealBindingFromChild(ccastexpr);
  }
  virtual void VisitImplicitCastExpr(ImplicitCastExpr * icastexpr) {
    this->VisitStmt(icastexpr);
    stealBindingFromChild(icastexpr);
  }
  virtual void VisitParenExpr(ParenExpr * parenexpr) {
    this->VisitStmt(parenexpr);
    stealBindingFromChild(parenexpr);
  } 
  /// for some AST(e.g., ImplicitCastExpr, CStyleCastExpr), we need to have their "value" binding.
  /// so we steal the value binding from their children. Usually, they have only one child.
  void stealBindingFromChild(Stmt * parent) {
    Stmt * stmt = nullptr;
    for(auto c:parent->children()) {
      stmt = c;break;
    }
    
    if(stmt) {
      if(mEnv->stackTop().hasStmt(stmt)) 
        mEnv->bindStmt(parent, mEnv->stackTop().getStmtVal(stmt));
    //   if(DeclRefExpr * declref = dyn_cast<DeclRefExpr>(stmt)) {
    //     if(!declref->getType()->isFunctionType()/*!mEnv->isBuiltInDecl(declref)*/) {
    //       // stmt->dump();
    //       mEnv->bindStmt(icastexpr, mEnv->stackTop().getStmtVal(stmt));
    //     }
    //   } else if(ArraySubscriptExpr * arrsub = dyn_cast<ArraySubscriptExpr>(stmt)) {
    //       // stmt->dump();
    //       mEnv->bindStmt(icastexpr, mEnv->stackTop().getStmtVal(stmt));
    //   }
    }

  }

  virtual void VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr * uexpr) {
    this->VisitStmt(uexpr);
    /// we assume the op must be `sizeof` 
    // uexpr->getExprStmt()->dump();
    auto argType = uexpr->getArgumentTypeInfo()->getType();
    int sz = 0;
    if(argType->isPointerType()) {
      sz = sizeof(Heap::HeapAddr);
    } else if(argType->isIntegerType()) {
      sz = sizeof(int);
    } else {
      llvm::outs() << "Unknown Type:\n";
      argType.dump();
      throw std::exception();
    }
    mEnv->bindStmt(uexpr, sz);
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
    try {
      mVisitor.VisitStmt(entry->getBody());
    } catch (ReturnException & e) {
      /// catch main return value
      if(e.getRetVal() != 0) {
        llvm::outs() << "main exit with a non-zero code!\n";
      }
    }
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
