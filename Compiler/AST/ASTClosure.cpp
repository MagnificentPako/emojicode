//
//  ASTClosure.cpp
//  Emojicode
//
//  Created by Theo Weidmann on 16/08/2017.
//  Copyright © 2017 Theo Weidmann. All rights reserved.
//

#include "ASTClosure.hpp"
#include "Analysis/FunctionAnalyser.hpp"
#include "Analysis/SemanticAnalyser.hpp"
#include "Analysis/ThunkBuilder.hpp"
#include "Types/TypeDefinition.hpp"
#include "Types/TypeExpectation.hpp"
#include "MemoryFlowAnalysis/MFFunctionAnalyser.hpp"

namespace EmojicodeCompiler {

ASTClosure::ASTClosure(std::unique_ptr<Function> &&closure, const SourcePosition &p)
        : ASTExpr(p), closure_(std::move(closure)) {}

Type ASTClosure::analyse(FunctionAnalyser *analyser, const TypeExpectation &expectation) {
    closure_->setMutating(analyser->function()->mutating());
    closure_->setOwner(analyser->function()->owner());
    closure_->setFunctionType(analyser->function()->functionType());
    closure_->setClosure();

    analyser->semanticAnalyser()->analyseFunctionDeclaration(closure_.get());

    applyBoxingFromExpectation(analyser, expectation);

    auto closureAnaly = FunctionAnalyser(closure_.get(), std::make_unique<CapturingSemanticScoper>(analyser->scoper()),
                                         analyser->semanticAnalyser());
    closureAnaly.analyse();
    capture_.captures = dynamic_cast<CapturingSemanticScoper &>(closureAnaly.scoper()).captures();
    if (closureAnaly.pathAnalyser().hasPotentially(PathAnalyserIncident::UsedSelf)) {
        capture_.captureSelf = true;
    }
    return Type(closure_.get());
}

void ASTClosure::analyseMemoryFlow(MFFunctionAnalyser *analyser, MFType type) {
    analyseAllocation(type);
    for (auto &capture : capture_.captures) {
        analyser->recordVariableGet(capture.sourceId, type);
    }
}

void ASTClosure::applyBoxingFromExpectation(FunctionAnalyser *analyser, const TypeExpectation &expectation) {
    if (expectation.type() != TypeType::Callable ||
            expectation.genericArguments().size() - 1 != closure_->parameters().size()) {
        return;
    }

    auto expReturn = expectation.genericArguments().front();
    auto returnType = closure_->returnType()->type();
    if (returnType.compatibleTo(expReturn, analyser->typeContext()) &&
            returnType.storageType() != expReturn.storageType()) {
        switch (expReturn.storageType()) {
            case StorageType::SimpleOptional:
                assert(returnType.storageType() == StorageType::Simple);
                closure_->setReturnType(std::make_unique<ASTLiteralType>(returnType.optionalized()));
                break;
            case StorageType::SimpleError:
                assert(returnType.storageType() == StorageType::Simple);
                closure_->setReturnType(std::make_unique<ASTLiteralType>(returnType.errored(expReturn.errorEnum())));
                break;
            case StorageType::Simple:
                // We cannot deal with this, can we?
                break;
            case StorageType::Box: {
                closure_->setReturnType(std::make_unique<ASTLiteralType>(returnType.boxedFor(expReturn)));
                break;
            }
        }
    }

    for (size_t i = 0; i < closure_->parameters().size(); i++) {
        auto &param = closure_->parameters()[i];
        auto expParam = expectation.genericArguments()[i + 1];
        if (param.type->type().compatibleTo(expParam, analyser->typeContext()) &&
                param.type->type().storageType() != expParam.storageType()) {
            switch (expParam.storageType()) {
                case StorageType::SimpleOptional:
                    assert(param.type->type().storageType() == StorageType::Simple);
                    closure_->setParameterType(i, std::make_unique<ASTLiteralType>(param.type->type().optionalized()));
                    break;
                case StorageType::SimpleError:
                    assert(param.type->type().storageType() == StorageType::Simple);
                    closure_->setParameterType(i, std::make_unique<ASTLiteralType>(param.type->type().errored(expParam.errorEnum())));
                    break;
                case StorageType::Simple:
                    // We cannot deal with this, can we?
                    break;
                case StorageType::Box:
                    closure_->setParameterType(i, std::make_unique<ASTLiteralType>(param.type->type().boxedFor(expParam)));
                    break;
            }
        }
    }
}

}  // namespace EmojicodeCompiler
