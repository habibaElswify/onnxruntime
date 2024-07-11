
#pragma once

namespace onnxruntime {
namespace contrib {
namespace cuda {

// Bit masks for sdpa_kernel cuda provider option to enable different SDPA kernels.
// Shall not change the values of existing enum members to avoid breaking existing models.
enum class AttentionBackend : int {
  MATH = 1,
  FLASH_ATTENTION = 2,
  EFFICIENT_ATTENTION = 4,
  TRT_FUSED_ATTENTION = 8,

  // TODO: Deprecate the following kernels
  TRT_FLASH_ATTENTION = 16,
  TRT_CROSS_ATTENTION = 32,
  TRT_CAUSAL_ATTENTION = 64,
};

class AttentionKernelOptions {
 public:
  static const AttentionKernelOptions* GetInstance(int sdpa_kernel);

  bool UseFlashAttention() const { return use_flash_attention_; }
  bool UseEfficientAttention() const { return use_efficient_attention_; }
  bool UseTrtFusedAttention() const { return use_trt_fused_attention_; }
  bool UseUnfusedAttention() const { return use_unfused_; }
  bool UseTrtFlashAttention() const { return use_trt_flash_attention_; }
  bool UseTrtCrossAttention() const { return use_trt_cross_attention_; }
  bool UseTrtCausalAttention() const { return use_trt_causal_attention_; }

 protected:
  void Initialize(int value);

 private:
  bool use_flash_attention_{true};
  bool use_efficient_attention_{true};
  bool use_trt_fused_attention_{true};
  bool use_unfused_{true};
  bool use_trt_flash_attention_{true};
  bool use_trt_cross_attention_{true};
  bool use_trt_causal_attention_{true};

  bool initialized_{false};
  static AttentionKernelOptions instance;
};

}  // namespace cuda
}  // namespace contrib
}  // namespace onnxruntime
