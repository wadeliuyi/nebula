/* Copyright (c) 2018 - present, VE Software Inc. All rights reserved
 *
 * This source code is licensed under Apache 2.0 License
 *  (found in the LICENSE.Apache file in the root directory)
 */

#include "base/Base.h"
#include "vgraphd/PipeExecutor.h"

namespace vesoft {
namespace vgraph {

PipeExecutor::PipeExecutor(Sentence *sentence,
                           ExecutionContext *ectx) : TraverseExecutor(ectx) {
    sentence_ = static_cast<PipedSentence*>(sentence);
}


Status PipeExecutor::prepare() {
    left_ = makeTraverseExecutor(sentence_->left());
    right_ = makeTraverseExecutor(sentence_->right());
    DCHECK(left_ != nullptr);
    DCHECK(right_ != nullptr);

    auto status = left_->prepare();
    if (!status.ok()) {
        FLOG_ERROR("Prepare executor `%s' failed: %s",
                    left_->name(), status.toString().c_str());
        return status;
    }

    right_->setInputResultSchema(left_->resultSchema());

    status = right_->prepare();
    if (!status.ok()) {
        FLOG_ERROR("Prepare executor `%s' failed: %s",
                    right_->name(), status.toString().c_str());
        return status;
    }

    auto onError = [this] (Status s) {
        /**
         * TODO(dutor)
         * If we allow concurrent execution of both sides, we must wait for all their
         * internal on-fly async-requests' to arrive before call `onError_'.
         * For the sake of simplicity, we don't allow the pipeline way for now.
         */
        onError_(std::move(s));
    };

    // Setup dependencies
    {
        auto onFinish = [this] () {
            // Start executing `right_' when `left_' is finished.
            right_->execute();
        };
        left_->setOnFinish(onFinish);

        auto onResult = [this] (TraverseRecords records) {
            // Feed results from `left_' to `right_'
            right_->feedResult(std::move(records));
        };
        left_->setOnResult(onResult);

        left_->setOnError(onError);
    }
    {
        auto onFinish = [this] () {
            // This executor is done when `right_' finishes.
            DCHECK(onFinish_);
            onFinish_();
        };
        right_->setOnFinish(onFinish);

        if (onResult_) {
            auto onResult = [this] (TraverseRecords records) {
                // This executor takes results of `right_' as results.
                onResult_(std::move(records));
            };
            right_->setOnResult(onResult);
        } else {
            // `right_' is the right most part of the pipeline
        }

        right_->setOnError(onError);
    }

    return Status::OK();
}


void PipeExecutor::execute() {
    left_->execute();
}


void PipeExecutor::feedResult(TraverseRecords records) {
    left_->feedResult(std::move(records));
}


void PipeExecutor::setupResponse(cpp2::ExecutionResponse &resp) {
    /**
     * `setupResponse()' could be invoked if and only if this executor
     * is the right most one, i.e. `onResult_' wasn't set.
     */
    DCHECK(!onResult_);
    right_->setupResponse(resp);
}

}   // namespace vgraph
}   // namespace vesoft