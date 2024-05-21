#pragma once

#include "contrib_ops/cpu/vec/vec.h"

namespace onnxruntime::vec {

// slow path
template <typename scalar_t, typename Op>
inline scalar_t vec_reduce_all(
    const Op& vec_fun,
    vec::Vectorized<scalar_t> acc_vec,
    int64_t size) {
  using Vec = vec::Vectorized<scalar_t>;
  scalar_t acc_arr[Vec::size()];
  acc_vec.store(acc_arr);

  for (int64_t i = 1; i < size; i++) {
    std::array<scalar_t, Vec::size()> acc_arr_next = {0};
    acc_arr_next[0] = acc_arr[i];
    Vec acc_vec_next = Vec::loadu(acc_arr_next.data());
    acc_vec = vec_fun(acc_vec, acc_vec_next);
  }
  acc_vec.store(acc_arr);
  return acc_arr[0];
}

template <typename scalar_t, typename Op>
struct VecReduceAllSIMD {
  static inline scalar_t apply(const Op& vec_fun, const Vectorized<scalar_t>& acc_vec) {
    return vec_reduce_all(vec_fun, acc_vec, Vectorized<scalar_t>::size());
  }
};

#if defined(__GNUC__) && (__GNUC__ > 5) && !defined(_MSC_VER)

#if defined(CPU_CAPABILITY_AVX2)
template <typename Op>
struct VecReduceAllSIMD<float, Op> {
  static inline float apply(const Op& vec_fun, const Vectorized<float>& acc_vec) {
    using Vec = Vectorized<float>;
    Vec v = acc_vec;
    // 128-bit shuffle
    Vec v1 = _mm256_permute2f128_ps(v, v, 0x1);
    v = vec_fun(v, v1);
    // 64-bit shuffle
    v1 = _mm256_shuffle_ps(v, v, 0x4E);
    v = vec_fun(v, v1);
    // 32-bit shuffle
    v1 = _mm256_shuffle_ps(v, v, 0xB1);
    v = vec_fun(v, v1);
    return _mm256_cvtss_f32(v);
  }
};
#endif // defined(CPU_CAPABILITY_AVX2)

#if defined(CPU_CAPABILITY_AVX512)
template <typename Op>
struct VecReduceAllSIMD<float, Op> {
  static inline float apply(const Op& vec_fun, const Vectorized<float>& acc_vec) {
    using Vec = Vectorized<float>;
    Vec v = acc_vec;
    // 256-bit shuffle
    Vec v1 = _mm512_shuffle_f32x4(v, v, 0x4E);
    v = vec_fun(v, v1);
    // 128-bit shuffle
    v1 = _mm512_shuffle_f32x4(v, v, 0xB1);
    v = vec_fun(v, v1);
    // 64-bit shuffle
    v1 = _mm512_shuffle_ps(v, v, 0x4E);
    v = vec_fun(v, v1);
    // 32-bit shuffle
    v1 = _mm512_shuffle_ps(v, v, 0xB1);
    v = vec_fun(v, v1);
    return _mm512_cvtss_f32(v);
  }
};
#endif // defined(CPU_CAPABILITY_AVX512)

#endif // defined(__GNUC__) && (__GNUC__ > 5) && !defined(_MSC_VER)

#if defined(__aarch64__)  && !defined(__CUDACC__)
template <typename Op>
struct VecReduceAllSIMD<float, Op> {
  static inline float apply(const Op& vec_fun, const Vectorized<float>& acc_vec) {
    using Vec = Vectorized<float>;
    Vec v = acc_vec;

    // 128-bit shuffle: [a1, a2, a3, a4, a5, a6, a7, a8] -> [a5, a6, a7, a8, a1, a2, a3, a4]
    Vec v1 = {v.get_high(), v.get_low()};
    // [a1+a5, a2+a6, a3+a7, a4+a8, -, -, -, -] ('+' stands for the reduction function. Note that the last 4 elements are not required)
    v = vec_fun(v, v1);

    // 64-bit shuffle: [a1+a5, a2+a6, a3+a7, a4+a8, -, -, -, -] -> [a3+a7, a4+a8, a1+a5, a2+a6, -, -, -, -]
    float32x4_t v1_1 = vextq_f32(v.get_low(), v.get_low(), 2);
    v1 = {v1_1, v1_1};
    // [a1+a3+a5+a7, a2+a4+a6+a8, a1+a3+a5+a7, a2+a4+a6+a8, -, -, -, -]
    v = vec_fun(v, v1);

    // 32-bit shuffle: [a1+a3+a5+a7, a2+a4+a6+a8, a1+a3+a5+a7, a2+a4+a6+a8, -, -, -, -] -> [a2+a4+a6+a8, a1+a3+a5+a7, a2+a4+a6+a8, a1+a3+a5+a7, -, -, -, -]
    v1_1 = vrev64q_f32(v.get_low());
    v1 = {v1_1, v1_1};
    // [a1+a2+a3+a4+a5+a6+a7+a8, a1+a2+a3+a4+a5+a6+a7+a8, a1+a2+a3+a4+a5+a6+a7+a8, a1+a2+a3+a4+a5+a6+a7+a8, -, -, -, -]
    v = vec_fun(v, v1);

    return v.get_low()[0];
  }
};
#endif // defined(__aarch64__)

template <typename scalar_t, typename Op>
inline scalar_t vec_reduce_all(const Op& vec_fun, const Vectorized<scalar_t>& acc_vec) {
  return VecReduceAllSIMD<scalar_t, Op>::apply(vec_fun, acc_vec);
}

template <typename scalar_t, typename Op,
          typename std::enable_if_t<!is_reduced_floating_point_v<scalar_t>, int> = 0>
inline scalar_t reduce_all(const Op& vec_fun, const scalar_t* data, int64_t size) {
  using Vec = vec::Vectorized<scalar_t>;
  if (size < Vec::size())
    return vec_reduce_all(vec_fun, Vec::loadu(data, size), size);
  int64_t d = Vec::size();
  Vec acc_vec = Vec::loadu(data);
  for (; d < size - (size % Vec::size()); d += Vec::size()) {
    Vec data_vec = Vec::loadu(data + d);
    acc_vec = vec_fun(acc_vec, data_vec);
  }
  if (size - d > 0) {
    Vec data_vec = Vec::loadu(data + d, size - d);
    acc_vec = Vec::set(acc_vec, vec_fun(acc_vec, data_vec), size - d);
  }
  return vec_reduce_all(vec_fun, acc_vec);
}

template <typename scalar_t, typename Op,
          typename std::enable_if_t<!is_reduced_floating_point_v<scalar_t>, int> = 0>
inline void map(
    const Op& vec_fun,
    scalar_t* output_data,
    const scalar_t* input_data,
    int64_t size) {
  using Vec = vec::Vectorized<scalar_t>;
  int64_t d = 0;
  for (; d < size - (size % Vec::size()); d += Vec::size()) {
    Vec output_vec = vec_fun(Vec::loadu(input_data + d));
    output_vec.store(output_data + d);
  }
  if (size - d > 0) {
    Vec output_vec = vec_fun(Vec::loadu(input_data + d, size - d));
    output_vec.store(output_data + d, size - d);
  }
}

template <typename scalar_t, typename Op,
          typename std::enable_if_t<!is_reduced_floating_point_v<scalar_t>, int> = 0>
inline void map2(
    const Op& vec_fun,
    scalar_t* output_data,
    const scalar_t* input_data,
    const scalar_t* input_data2,
    int64_t size) {
  using Vec = vec::Vectorized<scalar_t>;
  int64_t d = 0;
  for (; d < size - (size % Vec::size()); d += Vec::size()) {
    Vec data_vec = Vec::loadu(input_data + d);
    Vec data_vec2 = Vec::loadu(input_data2 + d);
    Vec output_vec = vec_fun(data_vec, data_vec2);
    output_vec.store(output_data + d);
  }
  if (size > d) {
    Vec data_vec = Vec::loadu(input_data + d, size - d);
    Vec data_vec2 = Vec::loadu(input_data2 + d, size - d);
    Vec output_vec = vec_fun(data_vec, data_vec2);
    output_vec.store(output_data + d, size - d);
  }
}

} // namespace onnxruntime::vec
