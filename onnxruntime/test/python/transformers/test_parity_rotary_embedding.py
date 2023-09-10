# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT License.  See License.txt in the project root for
# license information.
# --------------------------------------------------------------------------

import os
import unittest

import numpy as np
import onnxruntime as ort
import torch
import torch.nn as nn
from onnx import TensorProto, helper
from copy import deepcopy

seed = 2
np.random.seed(seed)
torch.manual_seed(seed)
torch.set_printoptions(sci_mode=False)

class SampleInputConfig:
    def __init__(
        self,
        batch_size = 2,
        sequence_length = 8,
        num_heads = 4,
        head_size = 6,
        max_sequence_length = 16,
    ):
        self.batch_size = batch_size
        self.sequence_length = sequence_length
        self.num_heads = num_heads
        self.head_size = head_size
        self.hidden_size = self.num_heads * self.head_size
        self.max_sequence_length = max_sequence_length


# LLaMA Hugging Face model
class LlamaHFRotaryEmbedding(nn.Module):
    def __init__(self, dim, max_position_embeddings=2048, base=10000, device="cpu"):
        super().__init__()

        self.dim = dim
        self.max_position_embeddings = max_position_embeddings
        self.base = base
        inv_freq = 1.0 / (self.base ** (torch.arange(0, self.dim, 2).float().to(device) / self.dim))
        self.register_buffer("inv_freq", inv_freq, persistent=False)

        # Build here to make `torch.jit.trace` work.
        self._set_cos_sin_cache(
            seq_len=max_position_embeddings, device=self.inv_freq.device, dtype=torch.get_default_dtype()
        )

    def _set_cos_sin_cache(self, seq_len, device, dtype):
        self.max_seq_len_cached = seq_len
        t = torch.arange(self.max_seq_len_cached, device=device, dtype=self.inv_freq.dtype)

        freqs = torch.einsum("i,j->ij", t, self.inv_freq)
        # Different from paper, but it uses a different permutation in order to obtain the same calculation
        emb = torch.cat((freqs, freqs), dim=-1)
        self.register_buffer("cos_cached", emb.cos()[None, None, :, :].to(dtype), persistent=False)
        self.register_buffer("sin_cached", emb.sin()[None, None, :, :].to(dtype), persistent=False)

    def get_cos_sin_cache(self, seq_len=None, device=torch.device("cpu"), dtype=torch.float32):
        # x: [bs, num_attention_heads, seq_len, head_size]
        if seq_len > self.max_seq_len_cached:
            self._set_cos_sin_cache(seq_len=seq_len, device=device, dtype=dtype)

        return (
            self.cos_cached[:, :, :seq_len, ...].to(dtype=dtype),
            self.sin_cached[:, :, :seq_len, ...].to(dtype=dtype),
        )

    def rotate_half(self, x):
        """Rotates half the hidden dims of the input."""
        x1 = x[..., : x.shape[-1] // 2]
        x2 = x[..., x.shape[-1] // 2 :]
        return torch.cat((-x2, x1), dim=-1)

    def apply_rotary_pos_emb(self, x, cos, sin, position_ids):
        # The first two dimensions of cos and sin are always 1, so we can `squeeze` them.
        cos = cos.squeeze(1).squeeze(0)  # [seq_len, dim]
        sin = sin.squeeze(1).squeeze(0)  # [seq_len, dim]
        cos = cos[position_ids].unsqueeze(1)  # [bs, 1, seq_len, dim]
        sin = sin[position_ids].unsqueeze(1)  # [bs, 1, seq_len, dim]
        x_embed = (x * cos) + (self.rotate_half(x) * sin)
        return x_embed

    def forward(self, x, cos, sin, pos_ids):
        return self.apply_rotary_pos_emb(x, cos, sin, pos_ids)


# LLaMA Microsoft model
class LlamaMSRotaryEmbedding(nn.Module):
    def __init__(self, hidden_size, num_heads, max_sequence_length):
        super().__init__()

        self.hidden_size = hidden_size
        self.num_heads = num_heads
        self.max_sequence_length = max_sequence_length

    def get_cos_sin_cache(
        self,
        theta: float = 10000.0,
        head_scale=1.0,
        device="cpu",
        dtype=torch.float32
    ):
        hidden_size = self.hidden_size
        n_heads = self.num_heads
        max_seq_len = self.max_sequence_length

        # Precalculate rotary matrices for the sequence
        # According to "Attention Is All You Need", theta_i = 10000 ^ (2 * (i - 1)/dim), i in [1, 2, ..., dim//2]
        head_dim = head_scale * hidden_size / n_heads

        pos = torch.arange(0, 2 * (head_dim // 2), step=2, device=device, dtype=dtype)
        freqs = 1.0 / (theta ** (pos / head_dim))

        idx = torch.arange(max_seq_len, device=freqs.device)
        freqs = torch.outer(idx, freqs)

        cos = torch.reshape(torch.cos(freqs), [1, max_seq_len, 1, -1])
        sin = torch.reshape(torch.sin(freqs), [1, max_seq_len, 1, -1])
        dtype = torch.get_default_dtype()

        return cos.to(dtype), sin.to(dtype)

    def rotate_tensor(
        self,
        x: torch.Tensor,
        cos: torch.Tensor,
        sin: torch.Tensor,
        pos: int,
    ):
        # Dimension of x is [batch_size, seq_len, n_heads, head_dim]
        rot_dim = 2 * cos.shape[3]

        # Dolly requires partial rotation
        x_rot = x[:, :, :, :rot_dim]

        x1 = x_rot[:, :, :, 0::2]
        x2 = x_rot[:, :, :, 1::2]

        seq_len = x.shape[1]
        cos_x = cos[:, pos : pos + seq_len, :, :]
        sin_x = sin[:, pos : pos + seq_len, :, :]

        real = cos_x * x1 - sin_x * x2
        imag = sin_x * x1 + cos_x * x2

        x1_cos = cos_x * x1
        x2_cos = cos_x * x2

        x1_sin = sin_x * x1
        x2_sin = sin_x * x2

        x_rot[:, :, :, 0::2] = real
        x_rot[:, :, :, 1::2] = imag

        return torch.cat((x_rot, x[:, :, :, rot_dim:]), dim=-1)

    def forward(self, x, cos, sin, pos):
        return self.rotate_tensor(x, cos, sin, pos)


class TestLlamaRotaryEmbedding(unittest.TestCase):
    def setUp(self):
        self.config = SampleInputConfig()
        self.llama_hf = LlamaHFRotaryEmbedding(self.config.head_size, self.config.max_sequence_length)
        self.llama_ms = LlamaMSRotaryEmbedding(self.config.hidden_size, self.config.num_heads, self.config.max_sequence_length)

    def create_onnx_graph(self, x_shape, pos_shape, cos, sin):
        inputs = [
            helper.make_tensor_value_info(
                name="input",
                elem_type=TensorProto.FLOAT,
                shape=list(x_shape),
            ),
            helper.make_tensor_value_info(
                name="position_ids",
                elem_type=TensorProto.INT64,
                shape=list(pos_shape),
            ),
        ]
        outputs = [
            helper.make_tensor_value_info(
                name="output",
                elem_type=TensorProto.FLOAT,
                shape=list(x_shape),
            ),
        ]

        initializers = [
            helper.make_tensor(
                name="cos_cache",
                data_type=TensorProto.FLOAT,
                dims=list(torch.squeeze(cos).shape),
                vals=cos.flatten().tolist(),
            ),
            helper.make_tensor(
                name="sin_cache",
                data_type=TensorProto.FLOAT,
                dims=list(torch.squeeze(sin).shape),
                vals=sin.flatten().tolist(),
            ),
        ]
        nodes = [
            helper.make_node(
                op_type="RotaryEmbedding",
                inputs=["input", "position_ids", "cos_cache", "sin_cache"],
                outputs=["output"],
                name="RotaryEmbedding_0",
                domain="com.microsoft",
            ),
        ]

        graph = helper.make_graph(
            nodes=nodes,
            name="RotaryEmbedding_Graph",
            inputs=inputs,
            outputs=outputs,
            initializer=initializers,
        )
        opset_import = helper.make_opsetid(domain="com.microsoft", version=1)
        model = helper.make_model(graph, opset_imports=[opset_import])
        return model.SerializeToString()

    # def test_hf_rotary(self):
    #     x_bnsh = torch.randn(self.config.batch_size, self.config.num_heads, self.config.sequence_length, self.config.head_size)
    #     cos_hf, sin_hf = self.llama_hf.get_cos_sin_cache(self.config.sequence_length)
    #     pos_hf = torch.stack([torch.arange(0, self.config.sequence_length) for _ in range(self.config.batch_size)])
    #     output_hf = self.llama_hf(x_bnsh, cos_hf, sin_hf, pos_hf)

    #     self.assertTrue(torch.allclose(output_hf, output_ort))

    def test_msft_prompt_rotary(self):
        if "CUDAExecutionProvider" not in ort.get_available_providers():
            return
        
        # Calculated this way to match the data in rotary_embedding_op_test.cc
        x_bnsh = torch.randn(self.config.batch_size, self.config.num_heads, self.config.sequence_length, self.config.head_size)
        x_bsnh = x_bnsh.transpose(1, 2)
        x_bsd = deepcopy(x_bsnh)  # deepcopy to avoid changes made by self.llama_ms forward pass
        cos_ms, sin_ms = self.llama_ms.get_cos_sin_cache()
        pos_ms = 0
        output_ms = self.llama_ms(deepcopy(x_bsnh), cos_ms, sin_ms, pos_ms).detach().cpu().numpy()

        x_bsd = x_bsd.reshape(self.config.batch_size, self.config.sequence_length, self.config.hidden_size)
        pos_ms = torch.tensor([pos_ms])
        onnx_graph = self.create_onnx_graph(x_bsd.shape, pos_ms.shape, cos_ms, sin_ms)
        sess = ort.InferenceSession(onnx_graph, providers=["CUDAExecutionProvider"])
        inputs_ort = {
            "input": x_bsd.detach().cpu().numpy(),
            "position_ids": pos_ms.detach().cpu().numpy(),
        }
        output_ort = sess.run(None, inputs_ort)[0]
        output_ort = output_ort.reshape((self.config.batch_size, self.config.sequence_length, self.config.num_heads, self.config.head_size))

        self.assertTrue(np.allclose(x_bsnh.flatten(), x_bsd.flatten()))
        self.assertTrue(np.allclose(output_ms.flatten(), output_ort.flatten()))

    def test_msft_new_pos_id_rotary(self):
        if "CUDAExecutionProvider" not in ort.get_available_providers():
            return
        
        # Calculated this way to match the data in rotary_embedding_op_test.cc
        x_bnsh = torch.randn(self.config.batch_size, self.config.num_heads, self.config.sequence_length, self.config.head_size)
        x_bsnh = x_bnsh.transpose(1, 2)
        x_bsd = deepcopy(x_bsnh)  # deepcopy to avoid changes made by self.llama_ms forward pass
        cos_ms, sin_ms = self.llama_ms.get_cos_sin_cache()
        pos_ms = 2
        output_ms = self.llama_ms(deepcopy(x_bsnh), cos_ms, sin_ms, pos_ms).detach().cpu().numpy()

        x_bsd = x_bsd.reshape(self.config.batch_size, self.config.sequence_length, self.config.hidden_size)
        pos_ms = torch.tensor([pos_ms])
        onnx_graph = self.create_onnx_graph(x_bsd.shape, pos_ms.shape, cos_ms, sin_ms)
        sess = ort.InferenceSession(onnx_graph, providers=["CUDAExecutionProvider"])
        inputs_ort = {
            "input": x_bsd.detach().cpu().numpy(),
            "position_ids": pos_ms.detach().cpu().numpy(),
        }
        output_ort = sess.run(None, inputs_ort)[0]
        output_ort = output_ort.reshape((self.config.batch_size, self.config.sequence_length, self.config.num_heads, self.config.head_size))

        self.assertTrue(np.allclose(x_bsnh.flatten(), x_bsd.flatten()))
        self.assertTrue(np.allclose(output_ms.flatten(), output_ort.flatten()))


if __name__ == "__main__":
    unittest.main()
