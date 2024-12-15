#pragma once
#include "definitions_cxx.hpp"
#include "dsp_ng/core/types.hpp"
#include <argon.hpp>
#include <array>
#include <cstdint>
#include <span>
extern const int16_t windowedSincKernel[][17][16];

namespace deluge::dsp {
struct [[gnu::hot]] Interpolator {

	Interpolator() = default;
	StereoSample<q31_t> interpolate(size_t channels, int32_t whichKernel, uint32_t oscPos);
	StereoSample<q31_t> interpolateLinear(size_t channels, uint32_t oscPos);

	[[gnu::always_inline]] constexpr void pushL(int16_t value) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= 1; i--) {
			buffer_l[i] = buffer_l[i - 1];
		}
		buffer_l[0] = value;
	}

	[[gnu::always_inline]] constexpr void pushR(int16_t value) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= 1; i--) {
			buffer_r[i] = buffer_r[i - 1];
		}
		buffer_r[0] = value;
	}

	[[gnu::always_inline]] constexpr void jumpForward(size_t num_samples) {
#pragma unroll
		for (int32_t i = kInterpolationMaxNumSamples - 1; i >= num_samples; i--) {
			buffer_l[i] = buffer_l[i - num_samples];
			buffer_r[i] = buffer_r[i - num_samples];
		}
	}

	// These are the state buffers (quadword alignment for NEON)
	alignas(sizeof(int32_t) * 4) std::array<int16_t, kInterpolationMaxNumSamples> buffer_l;
	alignas(sizeof(int32_t) * 4) std::array<int16_t, kInterpolationMaxNumSamples> buffer_r;
};

template <typename T = float>
constexpr T InterpolateLinear(const T x_0, const T x_1, float fractional) {
	return static_cast<T>(x_0 + ((x_1 - x_0) * fractional));
}

template <typename T = float, typename U = float>
constexpr T InterpolateHermite(const T x_m1, const T x_0, const T x_1, const T x_2, U fractional) {
	const T c = (x_1 - x_m1) * 0.5f;
	const T v = x_0 - x_1;
	const T w = c + v;
	const T a = w + v + ((x_2 - x_0) * 0.5f);
	const T b_neg = w + a;
	return (((((a * fractional) - b_neg) * fractional) + c) * fractional) + x_0;
}

template <typename T>
inline float InterpolateHermiteTable(std::span<T> table, float index) {
	auto index_integral = static_cast<size_t>(index);
	float index_fractional = index - static_cast<float>(index_integral);

	const T x_m1 = table[index_integral - 1];
	const T x_0 = table[index_integral + 0];
	const T x_1 = table[index_integral + 1];
	const T x_2 = table[index_integral + 2];
	return InterpolateHermite(x_m1, x_0, x_1, x_2, index_fractional);
}

/// @brief Transpose a 4x4 matrix comprised of an array of vectors
/// {a, b, c, d}    {a, e, i, m}
/// {e, f, g, h} -> {b, f, j, n}
/// {i, j, k, l} -> {c, g, k, o}
/// {m, n, o, p}    {d, h, l, p}
template <typename T>
std::array<Argon<T>, 4> transpose(Argon<T> r_0, Argon<T> r_1, Argon<T> r_2, Argon<T> r_3) {
	// {a, b, c, d}    {a, e, c, g}
	// {e, f, g, h} -> {b, f, d, h}
	// {i, j, k, l} -> {i, j, k, l}
	// {m, n, o, p}    {m, n, o, p}
	std::tie(r_0, r_1) = r_0.TransposeWith(r_1);

	// {a, e, c, g}    {a, e, c, g}
	// {b, f, d, h} -> {b, f, d, h}
	// {i, j, k, l} -> {i, m, k, o}
	// {m, n, o, p}    {j, n, l, p}
	std::tie(r_2, r_3) = r_2.TransposeWith(r_3);

	// {a, e, c, g}    {a, e, i, m}
	// {b, f, d, h} -> {b, f, d, h}
	// {i, m, k, o} -> {c, g, k, o}
	// {j, n, l, p}    {j, n, l, p}
	std::tie(r_0, r_2) =
	    std::make_tuple(r_0.GetLow().CombineWith(r_2.GetLow()), r_0.GetHigh().CombineWith(r_2.GetHigh()));

	// {a, e, i, m}    {a, e, i, m}
	// {b, f, d, h} -> {b, f, j, n}
	// {c, g, k, o} -> {c, g, k, o}
	// {j, n, l, p}    {d, h, l, p}
	std::tie(r_1, r_3) =
	    std::make_tuple(r_1.GetLow().CombineWith(r_3.GetLow()), r_1.GetHigh().CombineWith(r_3.GetHigh()));

	return std::array{r_0, r_1, r_2, r_3};
}

template <typename T>
inline Argon<T> InterpolateHermiteTableSIMD(std::span<T> table, Argon<uint32_t> index_integral,
                                            Argon<float> index_fractional) {
	index_integral = index_integral - 1;

	// writeback incurred
	std::array index_integral_arr = index_integral.to_array();

	// x_m1 = { l0[-1], l1[-1], l2[-1], l3[-1] }
	// x_0  = { l0[0],  l1[0],  l2[0],  l3[0]  }
	// x_1  = { l0[1],  l1[1],  l2[1],  l3[1]  }
	// x_2  = { l0[2],  l1[2],  l2[2],  l3[2]  }
	auto [x_m1, x_0, x_1, x_2] = transpose(            //<
	    Argon<T>::Load(&table[index_integral_arr[0]]), // {l0[-1], l0[0], l0[1], l0[2]}
	    Argon<T>::Load(&table[index_integral_arr[1]]), // {l1[-1], l1[0], l1[1], l1[2]}
	    Argon<T>::Load(&table[index_integral_arr[2]]), // {l2[-1], l2[0], l2[1], l2[2]}
	    Argon<T>::Load(&table[index_integral_arr[3]])  // {l3[-1], l3[0], l3[1], l3[2]}
	);
	return InterpolateHermite(x_m1, x_0, x_1, x_2, index_fractional);
}

template <typename T>
inline Argon<T> InterpolateHermiteTableSIMD(std::span<T> table, Argon<float> index) {
	auto index_integral = index.ConvertTo<uint32_t>();
	Argon<float> index_fractional = index - index_integral.ConvertTo<float>();
	return InterpolateHermiteTableSIMD<T>(table, index_integral, index_fractional);
}

} // namespace deluge::dsp
