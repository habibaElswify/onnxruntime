// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "core/providers/cuda/cuda_graph.h"

#include "core/providers/cuda/cuda_common.h"
#include <cuda_runtime_api.h>
#include <driver_types.h>

namespace onnxruntime {

CUDAGraph::CUDAGraph(cudaStream_t stream) : stream_(stream) {
}

void CUDAGraph::SetStream(cudaStream_t stream) {
  stream_ = stream;
}

void CUDAGraph::CaptureBegin(GraphAnnotationOptional_t cuda_graph_annotation_id) {
  if (!cuda_graph_annotation_id.has_value()) {
    std::cout << "CaptureBegin: cuda_graph_annotation_id is empty" << std::endl;
    ORT_ENFORCE(!has_graph_exec_,
                "This cuda graph has already captured a graph. "
                "Create a new instance to capture a new graph.");
  } else {
    std::cout << "CaptureBegin: cuda_graph_annotation_id is " << *cuda_graph_annotation_id << std::endl;

    cuda_graph_annotation_id_ = cuda_graph_annotation_id;
  }

  CUDA_CALL_THROW(cudaStreamSynchronize(stream_));
  // For now cuda graph can only work with a single thread. In the future, we
  // will support multiple threads. For multiple threads with multiple graphs
  // and streams, `cudaStreamCaptureModeGlobal` needs to be changed to
  // `cudaStreamCaptureModeThreadLocal`
  CUDA_CALL_THROW(cudaStreamBeginCapture(stream_, cudaStreamCaptureModeGlobal));
}

void CUDAGraph::CaptureEnd() {
  if (cuda_graph_annotation_id_.has_value()) {
    std::cout << "CaptureEnd: cuda_graph_annotation_id is " << *cuda_graph_annotation_id_ << std::endl;
    CUDA_CALL_THROW(cudaStreamEndCapture(stream_, &additional_graph_));
    if (additional_graph_ == NULL) {
      ORT_THROW("CUDAGraph::CaptureEnd: additional_graph_ is NULL");
    }

    cudaGraphExec_t graph_exec = NULL;

    has_additional_graph_ = true;
    CUDA_CALL_THROW(cudaGraphInstantiate(&graph_exec, additional_graph_, NULL, NULL, 0));
    CUDA_CALL_THROW(cudaGraphDestroy(additional_graph_));
    has_additional_graph_ = false;

    GraphAnnotation_t cuda_graph_id = cuda_graph_annotation_id_.value();
    graph_exec_map_.emplace(cuda_graph_id, graph_exec);

    return;
  }

  std::cout << "CaptureEnd: cuda_graph_annotation_id is empty" << std::endl;
  CUDA_CALL_THROW(cudaStreamEndCapture(stream_, &graph_));
  if (graph_ == NULL) {
    ORT_THROW("CUDAGraph::CaptureEnd: graph_ is NULL");
  }

  has_graph_ = true;
  CUDA_CALL_THROW(cudaGraphInstantiate(&graph_exec_, graph_, NULL, NULL, 0));
  has_graph_exec_ = true;
  CUDA_CALL_THROW(cudaGraphDestroy(graph_));
  has_graph_ = false;
}

Status CUDAGraph::Replay(GraphAnnotationOptional_t cuda_graph_annotation_id) {
  // Although this function is not thread safe, the lock is not needed here because
  // CUDA EP maintains a separate cuda graph per thread
  if (cuda_graph_annotation_id_.has_value()) {
    std::cout << "Replaying CUDA graph on stream " << stream_ << " with cuda_graph_annotation_id " << *cuda_graph_annotation_id << std::endl;
    LOGS_DEFAULT(INFO) << "Replaying CUDA graph on stream " << stream_ << " with cuda_graph_annotation_id " << *cuda_graph_annotation_id;
    auto it = graph_exec_map_.find(*cuda_graph_annotation_id);
    if (it == graph_exec_map_.end()) {
      return ORT_MAKE_STATUS(ONNXRUNTIME,
                             FAIL,
                             "CUDAGraph::Replay: graph_exec_map_ does not contain the cuda_graph_annotation_id");
    }
    CUDA_RETURN_IF_ERROR(cudaGraphLaunch(it->second, stream_));
  } else {
    std::cout << "Replaying CUDA graph on stream " << stream_ << std::endl;
    LOGS_DEFAULT(INFO) << "Replaying CUDA graph on stream " << stream_;
    CUDA_RETURN_IF_ERROR(cudaGraphLaunch(graph_exec_, stream_));
  }

  CUDA_RETURN_IF_ERROR(cudaStreamSynchronize(stream_));
  return Status::OK();
}

bool CUDAGraph::IsAdditionalGraphCaptured() const {
  return !graph_exec_map_.empty();
}

bool CUDAGraph::IsGraphCaptureAllowedOnRun() const {
  if (!cuda_graph_annotation_id_.has_value()) {
    return true;
  }
  return *cuda_graph_annotation_id_ != kDefaultSkipGraphCapture;
}

void CUDAGraph::Reset() {
  if (has_graph_) {
    CUDA_CALL_THROW(cudaGraphDestroy(graph_));
    has_graph_ = false;
  }
  if (has_graph_exec_) {
    CUDA_CALL_THROW(cudaGraphExecDestroy(graph_exec_));
    has_graph_exec_ = false;
  }
}

void CUDAGraph::ResetAdditional() {
  if (has_additional_graph_) {
    CUDA_CALL_THROW(cudaGraphDestroy(additional_graph_));
    has_additional_graph_ = false;
  }
  if (!graph_exec_map_.empty()) {
    for (auto& it : graph_exec_map_) {
      CUDA_CALL_THROW(cudaGraphExecDestroy(it.second));
    }
    graph_exec_map_.clear();
  }
}

CUDAGraph::~CUDAGraph() {
  Reset();
  ResetAdditional();
}

}  // namespace onnxruntime
