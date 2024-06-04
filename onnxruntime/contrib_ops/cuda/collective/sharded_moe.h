// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "contrib_ops/cuda/moe/ft_moe/moe_kernel.h"
#include "contrib_ops/cuda/moe/moe_base.h"
#include "core/common/common.h"
#include "nccl_kernels.h"

namespace onnxruntime {
namespace contrib {
namespace cuda {

#if defined(ORT_USE_NCCL)

using namespace onnxruntime::cuda;

template <typename T>
class ShardedMoE final : public NcclKernel, public MoEBase {
 public:
  explicit ShardedMoE(const OpKernelInfo& op_kernel_info);
  Status ComputeInternal(OpKernelContext* ctx) const override;

 private:
  Status SynchronizeExpertsStartIndex(AllocatorPtr& alloc, OpKernelContext* ctx, cudaEvent_t& cuda_event) const;
  void SynchronizeExpertsStartIndexImpl(AllocatorPtr& alloc, OpKernelContext* ctx, cudaEvent_t& cuda_event,
                                        cudaError_t& cuda_result, ncclResult_t& nccl_result) const;

  int64_t local_experts_start_index_;
  int64_t tensor_shards_;
  mutable InlinedVector<int64_t> rank_to_experts_start_index_;

  // A global resource pack for IPC memory used in custom reduce kernel.
  // Resource retrieval and deserialization are made atomic to thread safety of accessing it.
  mutable onnxruntime::cuda::collective::GlobalIPCMemoryResourcePack g_ipc_mem_res_pack_;

  mutable std::once_flag flag_;
};

#endif

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime
