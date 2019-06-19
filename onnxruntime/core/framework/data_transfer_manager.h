// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "core/common/status.h"
#include "core/framework/data_transfer.h"
#include "core/framework/tensor.h"

namespace onnxruntime {

// Data transfer manager, which has all functions registered to copy tensors with different location.
// It's not thread-safe.
class DataTransferManager {
 public:
  static DataTransferManager& Instance();

  common::Status RegisterDataTransfer(std::unique_ptr<IDataTransfer> data_transfer);

  common::Status CopyTensor(const Tensor& src, Tensor& dst) const;
  common::Status CopyTensor(const Tensor& src, Tensor& dst, int exec_queue_id) const;

 private:
  DataTransferManager() = default;

  ORT_DISALLOW_COPY_ASSIGNMENT_AND_MOVE(DataTransferManager);

  // It's assumed that data transfers in this array have no overlap in terms of copying functionality.
  std::vector<std::unique_ptr<IDataTransfer>> datatransfers_;
};
}  // namespace onnxruntime
