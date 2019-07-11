#pragma once
#include "core/framework/op_kernel.h"
#include "core/framework/func_api.h"
#include "core/framework/op_kernel_context_internal.h"
#include "core/graph/function.h"

const OrtCustomOpApi& GetCustomOpApi();

namespace onnxruntime {

void* allocate_helper_func(void* allocator, size_t alignment, size_t size);

void release_helper_func(void* allocator, void* p);

//A kernel that wrapper the ComputeFunction call generated by execution provider when fuse the sub-graph
class FunctionKernel : public OpKernel {
 public:
  //The original design is we load the dll, find the entry point and wrapper it.
  //Here for quick prototype, we keep the entry pointer in the node.
  explicit FunctionKernel(const OpKernelInfo& info)
      : OpKernel(info),
        get_fused_funcs_status_(Status::OK()) {
    num_inputs_ = info.node().InputDefs().size();
    num_outputs_ = info.node().OutputDefs().size();
    CreateFunctionStateFunc create_func;
    get_fused_funcs_status_ = info.GetFusedFuncs(&func_, &create_func, &release_func_);
    // disable the check as current there's a bug that subgraph's session has empty func_mgr
    // the failure is not fatal if FunctionKernel::Compute of such kernels are not invoked
    // the check is deferred into Compute
    //ORT_ENFORCE(status.IsOK(), status.ErrorMessage());
    if (create_func) {
      //TODO: we are only provide host allocate method in compute context.
      //Do we need to hold the ref-counting here?
      host_allocator_ = info.GetAllocator(0, OrtMemType::OrtMemTypeDefault);
      ComputeContext context = {allocate_helper_func, release_helper_func, host_allocator_.get(), info.node().Name().c_str()};
      ORT_ENFORCE(create_func(&context, &func_state_) == 0);
    }
  }

  ~FunctionKernel() override {
    if (release_func_ && func_state_) {
      release_func_(func_state_);
    }
  }

  virtual Status Compute(OpKernelContext* context) const override {
    ORT_RETURN_IF_ERROR(get_fused_funcs_status_);
    auto* context_internal = static_cast<OpKernelContextInternal*>(context);
    return func_(func_state_, &GetCustomOpApi(), reinterpret_cast<OrtKernelContext*>(context_internal));
  }

 private:
  ComputeFunc func_;
  DestroyFunctionStateFunc release_func_;
  FunctionState func_state_;
  size_t num_inputs_;
  size_t num_outputs_;
  AllocatorPtr host_allocator_;
  Status get_fused_funcs_status_;
};
}  // namespace onnxruntime
