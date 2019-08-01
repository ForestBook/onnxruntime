// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "gsl/gsl_util"
#include "core/providers/cuda/cudnn_common.h"
#include "core/providers/cuda/cuda_common.h"
#include <cudnn.h>

namespace onnxruntime {
namespace cuda {

enum RNN_Input_Index {
  X = 0,
  W = 1,
  R = 2,
  B = 3,
  sequence_lens = 4,
  initial_h = 5,
  initial_c = 6
};

class CudnnRNN {
 public:
  CudnnRNN() : cudnn_rnn_desc_(nullptr) {
  }

  ~CudnnRNN() {
    if (cudnn_rnn_desc_ != nullptr) {
      cudnnDestroyRNNDescriptor(cudnn_rnn_desc_);
      cudnn_rnn_desc_ = nullptr;
    }
  }

  Status Set(const cudnnHandle_t& cudnnHandle, int64_t hidden_size, int num_layers,
             cudnnDropoutDescriptor_t cudnn_dropout_desc, cudnnDirectionMode_t cudnn_direction_model,
             cudnnRNNMode_t rnn_mode, cudnnDataType_t dataType) {
    if (!cudnn_rnn_desc_)
      CUDNN_RETURN_IF_ERROR(cudnnCreateRNNDescriptor(&cudnn_rnn_desc_));

    CUDNN_RETURN_IF_ERROR(cudnnSetRNNDescriptor(cudnnHandle,
                                                cudnn_rnn_desc_,
                                                gsl::narrow_cast<int>(hidden_size),
                                                num_layers,
                                                cudnn_dropout_desc,
                                                CUDNN_LINEAR_INPUT,  // We can also skip the input matrix transformation
                                                cudnn_direction_model,
                                                rnn_mode,
                                                CUDNN_RNN_ALGO_STANDARD,  //CUDNN_RNN_ALGO_PERSIST_STATIC, CUDNN_RNN_ALGO_PERSIST_DYNAMIC
                                                dataType));

    return Status::OK();
  }

  operator cudnnRNNDescriptor_t() const {
    return cudnn_rnn_desc_;
  }

 private:
  cudnnRNNDescriptor_t cudnn_rnn_desc_;
};

struct RNNDescriptors {
  CudnnFilterDescriptor filter_desc;
  CudnnRNN rnn_desc;

  OrtMutex mutex;
};

template <typename T>
class CudnnRnnBase : public CudaKernel {
  const std::set<std::string> allowed_directions{"forward", "reverse", "bidirectional"};

 public:
  CudnnRnnBase(const OpKernelInfo& info) : CudaKernel{info} {
    reverse_ = false;
    ORT_ENFORCE(info.GetAttr("direction", &direction_).IsOK());
    num_directions_ = direction_ == "bidirectional" ? 2 : 1;
    ORT_ENFORCE(allowed_directions.find(direction_) != allowed_directions.end());

    ORT_ENFORCE(info.GetAttr("hidden_size", &hidden_size_).IsOK() && hidden_size_ > 0);
    rnn_mode_ = CUDNN_LSTM;
    num_layers_ = 1;
    weight_cached_ = false;
    w_data_cache_ = nullptr;
  }

  Status SetCudnnRnnDesc();

  Status CacheCudnnRnnWeights(const OpKernelInfo& info);

  Status ComputeInternal(OpKernelContext* ctx) const override;

  void SetRNNMode(cudnnRNNMode_t rnn_mode) { rnn_mode_ = rnn_mode; }

 private:
  Status SetCudnnRnnWeightBias(const cudnnHandle_t cudnn_handle,
                               const cudnnRNNDescriptor_t rnn_desc,
                               const cudnnTensorDescriptor_t x_desc,
                               const cudnnFilterDescriptor_t w_desc,
                               void* w_data,
                               const T* W_data,
                               const T* R_data,
                               const T* B_data) const;

  Status ReorganizeWeights(const Tensor* W, const Tensor* R, const Tensor* B,
                           IAllocatorUniquePtr<void>& target_w_data,
                           CudnnFilterDescriptor& target_w_desc) const;

  void SetWeightBias(const cudnnHandle_t handle,
                     const cudnnRNNDescriptor_t rnn_desc,
                     const int pseudo_layer,
                     const cudnnTensorDescriptor_t x_desc,
                     const cudnnFilterDescriptor_t w_desc,
                     const cudnnFilterDescriptor_t filter_desc,
                     const void* w_data,
                     const int lin_layer_id,
                     const T* pos,
                     int& offset,
                     bool is_matrix) const;

 protected:
  int64_t num_directions_;
  // required
  int64_t hidden_size_;
  std::vector<int> W_lin_layer_id_;
  std::vector<int> R_lin_layer_id_;
  bool reverse_;
  int num_layers_;

 private:
  cudnnRNNMode_t rnn_mode_;
  // optional
  std::string direction_;
  // w_desc_cache_ & cudnn_dropout_desc_ are changed in Constructor only
  CudnnFilterDescriptor w_desc_cache_;
  CudnnDropout cudnn_dropout_desc_;

  // filter_desc & rnn_desc could be changed in Compute
  mutable RNNDescriptors rnn_descriptors_;

  IAllocatorUniquePtr<void> w_data_cache_;
  bool weight_cached_;
  IAllocatorUniquePtr<void> state_buffer_;
  size_t state_size_;

  enum Output_Index {
    Y = 0,
    Y_h = 1,
    Y_c = 2
  };
};

}  // namespace cuda
}  // namespace onnxruntime
