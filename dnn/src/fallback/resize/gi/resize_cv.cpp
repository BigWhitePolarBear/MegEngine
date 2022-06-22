/**
 * By downloading, copying, installing or using the software you agree to this license.
 * If you do not agree to this license, do not download, install,
 * copy or use the software.
 *
 *
 *                           License Agreement
 *                For Open Source Computer Vision Library
 *                        (3-clause BSD License)
 *
 * Copyright (C) 2000-2020, Intel Corporation, all rights reserved.
 * Copyright (C) 2009-2011, Willow Garage Inc., all rights reserved.
 * Copyright (C) 2009-2016, NVIDIA Corporation, all rights reserved.
 * Copyright (C) 2010-2013, Advanced Micro Devices, Inc., all rights reserved.
 * Copyright (C) 2015-2016, OpenCV Foundation, all rights reserved.
 * Copyright (C) 2015-2016, Itseez Inc., all rights reserved.
 * Copyright (C) 2019-2020, Xperience AI, all rights reserved.
 * Third party copyrights are property of their respective owners.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *
 *   * Neither the names of the copyright holders nor the names of the contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * This software is provided by the copyright holders and contributors "as is" and
 * any express or implied warranties, including, but not limited to, the implied
 * warranties of merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall copyright holders or contributors be liable for any direct,
 * indirect, incidental, special, exemplary, or consequential damages
 * (including, but not limited to, procurement of substitute goods or services;
 * loss of use, data, or profits; or business interruption) however caused
 * and on any theory of liability, whether in contract, strict liability,
 * or tort (including negligence or otherwise) arising in any way out of
 * the use of this software, even if advised of the possibility of such damage.
 *
 * ---------------------------------------------------------------------------
 */
#include "src/fallback/resize/gi/resize_cv.h"
#include <cstring>
#include "src/common/cv/common.h"
#include "src/common/cv/helper.h"
#include "src/common/utils.h"
#include "src/fallback/handle.h"
#include "src/fallback/resize/opr_impl.h"

#include "midout.h"

MIDOUT_DECL(megdnn_fallback_resizecv_imode)
MIDOUT_DECL(megdnn_fallback_resizecv_dtype)

using namespace megdnn;
using namespace fallback;
using namespace megcv;

namespace {

using InterpolationMode = param::Resize::InterpolationMode;
using IMode = InterpolationMode;

void resize_nearest_32f(const Mat32f& src, Mat32f& dst) {
    AlignedVector<int> tabx(dst.rows());
    AlignedVector<int> taby(dst.cols());
    const double fx = static_cast<double>(dst.rows()) / src.rows();
    const double fy = static_cast<double>(dst.cols()) / src.cols();
    const double ifx = 1.0f / fx;
    const double ify = 1.0f / fy;
    const size_t ch = src.channels();
    for (size_t dx = 0; dx < tabx.size(); ++dx) {
        double rx = dx * ifx;
        int sx = static_cast<int>(floor(rx));
        sx = megcv::saturate(sx, 0, static_cast<int>(src.rows()));
        tabx[dx] = sx;
    }
    for (size_t dy = 0; dy < taby.size(); ++dy) {
        double ry = dy * ify;
        int sy = static_cast<int>(floor(ry));
        sy = megcv::saturate(sy, 0, static_cast<int>(src.cols()));
        taby[dy] = sy;
    }
    // taby[taby.size() - 1] = src.cols() - 1;
    size_t tabxsize = tabx.size();
    size_t tabysize = taby.size();
    if (ch == 1) {
        for (size_t dx = 0; dx < tabxsize; ++dx) {
            float* pdst = dst.ptr(dx);
            const float* psrc = src.ptr(tabx[dx]);
            size_t dy = 0;
            for (; dy < tabysize; dy++) {
                const float* pcsrc = psrc + taby[dy];
                pdst[dy] = pcsrc[0];
            }
        }
    } else if (ch == 3) {
        for (size_t dx = 0; dx < tabxsize; ++dx) {
            float* pdst = dst.ptr(dx);
            const float* psrc = src.ptr(tabx[dx]);
            size_t dy3 = 0;
            for (size_t dy = 0; dy < tabysize; ++dy, dy3 += 3) {
                float* pcdst = pdst + dy3;
                const float* pcsrc = psrc + taby[dy] * 3;
                pcdst[0] = pcsrc[0];
                pcdst[1] = pcsrc[1];
                pcdst[2] = pcsrc[2];
            }
        }
    }
}

// linear 32f
void build_tabs_linear_32f(
        const Mat32f& src, const Mat32f& dst, AlignedVector<int>& tabsx,
        AlignedVector<int>& tabsy, AlignedVector<float>& tabrx,
        AlignedVector<float>& tabry) {
    megdnn_assert(src.rows() >= 2);
    megdnn_assert(src.cols() >= 2);
    megdnn_assert(dst.rows() >= 2);
    megdnn_assert(dst.cols() >= 2);
    const float fx = static_cast<float>(dst.rows()) / src.rows();
    const float fy = static_cast<float>(dst.cols()) / src.cols();
    const float ifx = 1.0f / fx;
    const float ify = 1.0f / fy;
    for (size_t dx = 0; dx < dst.rows(); ++dx) {
        float rx = (dx + 0.5f) * ifx - 0.5f;
        int sx = static_cast<int>(floor(rx));
        rx -= sx;
        if (sx < 0) {
            sx = 0;
            rx = 0;
        } else if (sx + 1 >= static_cast<int>(src.rows())) {
            sx = src.rows() - 2;
            rx = 1;
        }
        tabsx[dx] = sx;
        tabrx[dx] = rx;
    }
    for (size_t dy = 0; dy < dst.cols(); ++dy) {
        float ry = (dy + 0.5f) * ify - 0.5f;
        int sy = static_cast<int>(floor(ry));
        ry -= sy;
        if (sy < 0) {
            sy = 0;
            ry = 0;
        } else if (sy + 1 >= static_cast<int>(src.cols())) {
            sy = src.cols() - 2;
            ry = 1;
        }
        tabsy[dy] = sy;
        tabry[dy] = ry;
    }
}

void calc_cache_linear_32fc1_1(
        const Mat32f& src, const Mat32f& dst, const AlignedVector<int>& tabsx,
        const AlignedVector<int>& tabsy, const AlignedVector<float>& tabrx,
        const AlignedVector<float>& tabry, int dx, AlignedVector<float>& cache0,
        AlignedVector<float>& cache1) {
    (void)tabrx;
    const float* psrc1 = src.ptr(tabsx[dx] + 1);
    size_t dstcols = dst.cols();
    size_t dy = 0;

    // cache0 = cache1;
    std::swap(cache0, cache1);
    for (; dy < dstcols; ++dy) {
        const float* pcsrc10 = psrc1 + (tabsy[dy] + 0);
        const float* pcsrc11 = psrc1 + (tabsy[dy] + 1);
        float ry = tabry[dy];
        float iry = 1.0f - ry;
        cache1[dy] = pcsrc11[0] * ry + pcsrc10[0] * iry;
    }
}

void calc_cache_linear_32fc1_2(
        const Mat32f& src, const Mat32f& dst, const AlignedVector<int>& tabsx,
        const AlignedVector<int>& tabsy, const AlignedVector<float>& tabrx,
        const AlignedVector<float>& tabry, int dx, AlignedVector<float>& cache0,
        AlignedVector<float>& cache1) {
    (void)tabrx;
    const float* psrc0 = src.ptr(tabsx[dx] + 0);
    const float* psrc1 = src.ptr(tabsx[dx] + 1);
    int dstcols = dst.cols();
    int dy = 0;

    // 4 pixels each time
    float* cache0_ptr = cache0.data();
    float* cache1_ptr = cache1.data();
    const float* tabry_ptr = tabry.data();
    for (; dy + 4 <= dstcols; dy += 4) {
#define EXPAND(dy)                                                                    \
    {                                                                                 \
        int t0 = tabsy[dy + 0];                                                       \
        int t1 = tabsy[dy + 1];                                                       \
        int t2 = tabsy[dy + 2];                                                       \
        int t3 = tabsy[dy + 3];                                                       \
        const float pcsrc00[4] = {                                                    \
                psrc0[t0 + 0], psrc0[t1 + 0], psrc0[t2 + 0], psrc0[t3 + 0]};          \
        const float pcsrc01[4] = {                                                    \
                psrc0[t0 + 1],                                                        \
                psrc0[t1 + 1],                                                        \
                psrc0[t2 + 1],                                                        \
                psrc0[t3 + 1],                                                        \
        };                                                                            \
        const float pcsrc10[4] = {                                                    \
                psrc1[t0 + 0],                                                        \
                psrc1[t1 + 0],                                                        \
                psrc1[t2 + 0],                                                        \
                psrc1[t3 + 0],                                                        \
        };                                                                            \
        const float pcsrc11[4] = {                                                    \
                psrc1[t0 + 1],                                                        \
                psrc1[t1 + 1],                                                        \
                psrc1[t2 + 1],                                                        \
                psrc1[t3 + 1],                                                        \
        };                                                                            \
        GI_FLOAT32_t v_pcsrc00 = GiLoadFloat32(pcsrc00);                              \
        GI_FLOAT32_t v_pcsrc01 = GiLoadFloat32(pcsrc01);                              \
        GI_FLOAT32_t v_pcsrc10 = GiLoadFloat32(pcsrc10);                              \
        GI_FLOAT32_t v_pcsrc11 = GiLoadFloat32(pcsrc11);                              \
        GI_FLOAT32_t v_ry = GiLoadFloat32(tabry_ptr + dy);                            \
        GI_FLOAT32_t v_iry = GiSubtractFloat32(GiBroadcastFloat32(1.0f), v_ry);       \
        GiStoreFloat32(                                                               \
                cache0_ptr + dy,                                                      \
                GiMlaqFloat32(GiMultiplyFloat32(v_pcsrc01, v_ry), v_pcsrc00, v_iry)); \
        GiStoreFloat32(                                                               \
                cache1_ptr + dy,                                                      \
                GiMlaqFloat32(GiMultiplyFloat32(v_pcsrc11, v_ry), v_pcsrc10, v_iry)); \
    }                                                                                 \
    while (0)

        EXPAND(dy);
#undef EXPAND
    }
    for (; dy < dstcols; ++dy) {
        const float* pcsrc00 = psrc0 + (tabsy[dy] + 0);
        const float* pcsrc01 = psrc0 + (tabsy[dy] + 1);
        const float* pcsrc10 = psrc1 + (tabsy[dy] + 0);
        const float* pcsrc11 = psrc1 + (tabsy[dy] + 1);
        float ry = tabry[dy];
        float iry = 1.0f - ry;
        cache0[dy] = pcsrc01[0] * ry + pcsrc00[0] * iry;
        cache1[dy] = pcsrc11[0] * ry + pcsrc10[0] * iry;
    }
}

void calc_cache_linear_32fc3_1(
        const Mat32f& src, const Mat32f& dst, const AlignedVector<int>& tabsx,
        const AlignedVector<int>& tabsy, const AlignedVector<float>& tabrx,
        const AlignedVector<float>& tabry, int dx, AlignedVector<float>& cache0,
        AlignedVector<float>& cache1) {
    (void)tabrx;
    const float* psrc1 = src.ptr(tabsx[dx] + 1);
    const size_t dstcols = dst.cols();
    size_t dy = 0, dy3 = 0;

    // cache0 = cache1;
    std::swap(cache0, cache1);
    for (; dy < dstcols; ++dy, dy3 += 3) {
        const float* pcsrc10 = psrc1 + (tabsy[dy] + 0) * 3;
        const float* pcsrc11 = psrc1 + (tabsy[dy] + 1) * 3;
        float ry = tabry[dy];
        float iry = 1.0f - ry;
        cache1[dy3 + 0] = pcsrc11[0] * ry + pcsrc10[0] * iry;
        cache1[dy3 + 1] = pcsrc11[1] * ry + pcsrc10[1] * iry;
        cache1[dy3 + 2] = pcsrc11[2] * ry + pcsrc10[2] * iry;
    }
}

void calc_cache_linear_32fc3_2(
        const Mat32f& src, const Mat32f& dst, const AlignedVector<int>& tabsx,
        const AlignedVector<int>& tabsy, const AlignedVector<float>& tabrx,
        const AlignedVector<float>& tabry, int dx, AlignedVector<float>& cache0,
        AlignedVector<float>& cache1) {
    (void)tabrx;
    const float* psrc0 = src.ptr(tabsx[dx] + 0);
    const float* psrc1 = src.ptr(tabsx[dx] + 1);
    int dstcols = dst.cols();
    int dy = 0, dy3 = 0;

    for (; dy < dstcols; ++dy, dy3 += 3) {
        const float* pcsrc00 = psrc0 + (tabsy[dy] + 0) * 3;
        const float* pcsrc01 = psrc0 + (tabsy[dy] + 1) * 3;
        const float* pcsrc10 = psrc1 + (tabsy[dy] + 0) * 3;
        const float* pcsrc11 = psrc1 + (tabsy[dy] + 1) * 3;
        float ry = tabry[dy];
        float iry = 1.0f - ry;
        cache0[dy3 + 0] = pcsrc01[0] * ry + pcsrc00[0] * iry;
        cache1[dy3 + 0] = pcsrc11[0] * ry + pcsrc10[0] * iry;
        cache0[dy3 + 1] = pcsrc01[1] * ry + pcsrc00[1] * iry;
        cache1[dy3 + 1] = pcsrc11[1] * ry + pcsrc10[1] * iry;
        cache0[dy3 + 2] = pcsrc01[2] * ry + pcsrc00[2] * iry;
        cache1[dy3 + 2] = pcsrc11[2] * ry + pcsrc10[2] * iry;
    }
}
void resize_linear_32f_gi(const Mat32f& src, Mat32f& dst) {
    AlignedVector<int> tabsx(dst.rows());
    AlignedVector<int> tabsy(dst.cols());
    AlignedVector<float> tabrx(dst.rows());
    AlignedVector<float> tabry(dst.cols());
    build_tabs_linear_32f(src, dst, tabsx, tabsy, tabrx, tabry);

    if (src.channels() == 1) {
        AlignedVector<float> cache0(dst.cols()), cache1(dst.cols());
        int dstrows = dst.rows();
        int dstcols = dst.cols();
        for (int dx = 0; dx < dstrows; ++dx) {
            if (dx == 0 || tabsx[dx] != tabsx[dx - 1]) {
                if (dx > 0 && tabsx[dx] == tabsx[dx - 1] + 1) {
                    calc_cache_linear_32fc1_1(
                            src, dst, tabsx, tabsy, tabrx, tabry, dx, cache0, cache1);
                } else {
                    calc_cache_linear_32fc1_2(
                            src, dst, tabsx, tabsy, tabrx, tabry, dx, cache0, cache1);
                }
            }
            const float* cache0_ptr = cache0.data();
            const float* cache1_ptr = cache1.data();
            float rx = tabrx[dx];
            float irx = 1.0f - rx;
            float* pdst = dst.ptr(dx);
            int dy = 0;
#define EXPAND(x)                                  \
    v_cache0 = GiLoadFloat32(cache0_ptr + dy + x); \
    v_cache1 = GiLoadFloat32(cache1_ptr + dy + x); \
    GiStoreFloat32(                                \
            pdst + dy + x,                         \
            GiMlaqFloat32(GiMultiplyFloat32(v_rx, v_cache1), v_irx, v_cache0));
            GI_FLOAT32_t v_rx = GiBroadcastFloat32(rx);
            GI_FLOAT32_t v_irx = GiBroadcastFloat32(irx);
            for (; dy + 8 <= dstcols; dy += 8) {
                GI_FLOAT32_t v_cache0;
                GI_FLOAT32_t v_cache1;
                EXPAND(0);
                EXPAND(4);
            }
            if (dy + 4 <= dstcols) {
                GI_FLOAT32_t v_cache0;
                GI_FLOAT32_t v_cache1;
                EXPAND(0);
                dy += 4;
            }
#undef EXPAND
            for (; dy < dstcols; ++dy) {
                float* pcdst = pdst + dy;
                pcdst[0] = rx * cache1[dy] + irx * cache0[dy];
            }
        }
    } else if (src.channels() == 3) {
        int dstrows = dst.rows();
        int dstcols = dst.cols() * 3;
        AlignedVector<float> cache0(dstcols), cache1(dstcols);
        for (int dx = 0; dx < dstrows; ++dx) {
            if (dx == 0 || tabsx[dx] != tabsx[dx - 1]) {
                if (dx > 0 && tabsx[dx] == tabsx[dx - 1] + 1) {
                    calc_cache_linear_32fc3_1(
                            src, dst, tabsx, tabsy, tabrx, tabry, dx, cache0, cache1);
                } else {
                    calc_cache_linear_32fc3_2(
                            src, dst, tabsx, tabsy, tabrx, tabry, dx, cache0, cache1);
                }
            }
            const float* cache0_ptr = cache0.data();
            const float* cache1_ptr = cache1.data();
            float rx = tabrx[dx];
            float irx = 1.0f - rx;
            float* pdst = dst.ptr(dx);
            int dy = 0;
            GI_FLOAT32_t v_rx = GiBroadcastFloat32(rx);
            GI_FLOAT32_t v_irx = GiBroadcastFloat32(irx);
#define EXPAND(x)                                                                 \
    v_cache0 = GiLoadUzipFloat32V3(cache0_ptr + dy + (x)*3);                      \
    v_cache1 = GiLoadUzipFloat32V3(cache1_ptr + dy + (x)*3);                      \
    a0 = GiMlaqFloat32(                                                           \
            GiMultiplyFloat32(v_rx, GiGetSubVectorFloat32V3(v_cache1, 0)), v_irx, \
            GiGetSubVectorFloat32V3(v_cache0, 0));                                \
    GiSetSubVectorFloat32V3(v_dst, 0, a0);                                        \
    a1 = GiMlaqFloat32(                                                           \
            GiMultiplyFloat32(v_rx, GiGetSubVectorFloat32V3(v_cache1, 1)), v_irx, \
            GiGetSubVectorFloat32V3(v_cache0, 1));                                \
    GiSetSubVectorFloat32V3(v_dst, 1, a1);                                        \
    a2 = GiMlaqFloat32(                                                           \
            GiMultiplyFloat32(v_rx, GiGetSubVectorFloat32V3(v_cache1, 2)), v_irx, \
            GiGetSubVectorFloat32V3(v_cache0, 2));                                \
    GiSetSubVectorFloat32V3(v_dst, 2, a2);                                        \
    GiStoreZipFloat32V3(pdst + dy + (x)*3, v_dst);

            GI_FLOAT32_t a0, a1, a2;
            for (; dy + 8 * 3 <= dstcols; dy += 8 * 3) {
                GI_FLOAT32_V3_t v_cache0;
                GI_FLOAT32_V3_t v_cache1;
                GI_FLOAT32_V3_t v_dst;

                EXPAND(0);
                EXPAND(4);
            }

            if (dy + 4 * 3 <= dstcols) {
                GI_FLOAT32_V3_t v_cache0;
                GI_FLOAT32_V3_t v_cache1;
                GI_FLOAT32_V3_t v_dst;

                EXPAND(0);

                dy += 4 * 3;
            }
#undef EXPAND
            for (; dy < dstcols; dy += 3) {
                float* pcdst = pdst + dy;
                pcdst[0] = rx * cache1[dy + 0] + irx * cache0[dy + 0];
                pcdst[1] = rx * cache1[dy + 1] + irx * cache0[dy + 1];
                pcdst[2] = rx * cache1[dy + 2] + irx * cache0[dy + 2];
            }
        }
    } else {
        megdnn_throw(("nr. of channels must be 1 or 3."));
    }
}

void resize_linear_32f(const Mat32f& src, Mat32f& dst) {
    return resize_linear_32f_gi(src, dst);
}

const int INTER_RESIZE_COEF_BITS = 11;
const int INTER_RESIZE_COEF_SCALE = 1 << INTER_RESIZE_COEF_BITS;
const float MEGCV_PI = acos(-1);
struct HResizeNoVec {
    int operator()(
            const float**, float**, int, const int*, const float*, int, int, int, int,
            int) const {
        return 0;
    }
};
template <typename T, typename WT>
struct ResizeAreaFastNoVec {
    ResizeAreaFastNoVec(int, int) {}
    ResizeAreaFastNoVec(int, int, int, int) {}
    int operator()(const T*, T*, int) const { return 0; }
};

struct VResizeCubicVec_32f {
    int operator()(const float** src, float* dst, const float* beta, int width) const {
        const float *S0 = src[0], *S1 = src[1], *S2 = src[2], *S3 = src[3];
        int x = 0;
        GI_FLOAT32_t v_b0 = GiBroadcastFloat32(beta[0]),
                     v_b1 = GiBroadcastFloat32(beta[1]),
                     v_b2 = GiBroadcastFloat32(beta[2]),
                     v_b3 = GiBroadcastFloat32(beta[3]);

        for (; x <= width - 8; x += 8) {
            GI_FLOAT32_t s0_t = GiLoadFloat32(S0 + x);
            GI_FLOAT32_t s1_t = GiLoadFloat32(S1 + x);
            GI_FLOAT32_t s2_t = GiLoadFloat32(S2 + x);
            GI_FLOAT32_t s3_t = GiLoadFloat32(S3 + x);
            GI_FLOAT32_t tmp = GiMlaqFloat32(
                    GiMlaqFloat32(
                            GiMlaqFloat32(GiMultiplyFloat32(v_b0, s0_t), v_b1, s1_t),
                            v_b2, s2_t),
                    v_b3, s3_t);
            GiStoreFloat32(dst + x, tmp);

            s0_t = GiLoadFloat32(S0 + x + 4);
            s1_t = GiLoadFloat32(S1 + x + 4);
            s2_t = GiLoadFloat32(S2 + x + 4);
            s3_t = GiLoadFloat32(S3 + x + 4);
            tmp = GiMlaqFloat32(
                    GiMlaqFloat32(
                            GiMlaqFloat32(GiMultiplyFloat32(v_b0, s0_t), v_b1, s1_t),
                            v_b2, s2_t),
                    v_b3, s3_t);
            GiStoreFloat32(dst + x + 4, tmp);
        }

        return x;
    }
};

struct VResizeLanczos4Vec_32f {
    int operator()(const float** src, float* dst, const float* beta, int width) const {
        const float *S0 = src[0], *S1 = src[1], *S2 = src[2], *S3 = src[3],
                    *S4 = src[4], *S5 = src[5], *S6 = src[6], *S7 = src[7];
        int x = 0;
        GI_FLOAT32_t v_b0 = GiBroadcastFloat32(beta[0]),
                     v_b1 = GiBroadcastFloat32(beta[1]),
                     v_b2 = GiBroadcastFloat32(beta[2]),
                     v_b3 = GiBroadcastFloat32(beta[3]),
                     v_b4 = GiBroadcastFloat32(beta[4]),
                     v_b5 = GiBroadcastFloat32(beta[5]),
                     v_b6 = GiBroadcastFloat32(beta[6]),
                     v_b7 = GiBroadcastFloat32(beta[7]);

        for (; x <= width - 4; x += 4) {
            GI_FLOAT32_t v_dst0 = GiMlaqFloat32(
                    GiMlaqFloat32(
                            GiMlaqFloat32(
                                    GiMultiplyFloat32(v_b0, GiLoadFloat32(S0 + x)),
                                    v_b1, GiLoadFloat32(S1 + x)),
                            v_b2, GiLoadFloat32(S2 + x)),
                    v_b3, GiLoadFloat32(S3 + x));
            GI_FLOAT32_t v_dst1 = GiMlaqFloat32(
                    GiMlaqFloat32(
                            GiMlaqFloat32(
                                    GiMultiplyFloat32(v_b4, GiLoadFloat32(S4 + x)),
                                    v_b5, GiLoadFloat32(S5 + x)),
                            v_b6, GiLoadFloat32(S6 + x)),
                    v_b7, GiLoadFloat32(S7 + x));
            GiStoreFloat32(dst + x, GiAddFloat32(v_dst0, v_dst1));
        }

        return x;
    }
};
struct VResizeLinearVec_32f {
    int operator()(const float** src, float* dst, const float* beta, int width) const {
        const float *S0 = src[0], *S1 = src[1];
        int x = 0;

        GI_FLOAT32_t v_b0 = GiBroadcastFloat32(beta[0]),
                     v_b1 = GiBroadcastFloat32(beta[1]);

        for (; x <= width - 8; x += 8) {
            GI_FLOAT32_t v_src00 = GiLoadFloat32(S0 + x),
                         v_src01 = GiLoadFloat32(S0 + x + 4);
            GI_FLOAT32_t v_src10 = GiLoadFloat32(S1 + x),
                         v_src11 = GiLoadFloat32(S1 + x + 4);

            GiStoreFloat32(
                    dst + x,
                    GiMlaqFloat32(GiMultiplyFloat32(v_src00, v_b0), v_src10, v_b1));
            GiStoreFloat32(
                    dst + x + 4,
                    GiMlaqFloat32(GiMultiplyFloat32(v_src01, v_b0), v_src11, v_b1));
        }

        return x;
    }
};

typedef HResizeNoVec HResizeLinearVec_32f;

struct ResizeAreaFastVec_SIMD_32f {
    ResizeAreaFastVec_SIMD_32f(int _scale_x, int _scale_y, int _cn, int _step)
            : scale_x(_scale_x),
              scale_y(_scale_y),
              cn(_cn),
              step(_step * sizeof(float)) {
        fast_mode = scale_x == 2 && scale_y == 2 && (cn == 1 || cn == 3 || cn == 4);
    }

    int operator()(const float* S, float* D, int w) const {
        if (!fast_mode)
            return 0;

        const float *S0 = S, *S1 = (const float*)((const float*)(S0) + step);
        int dx = 0;

        GI_FLOAT32_t v_025 = GiBroadcastFloat32(0.25f);

        if (cn == 1) {
            for (; dx <= w - 4; dx += 4, S0 += 8, S1 += 8, D += 4) {
                GI_FLOAT32_V2_t v_row0 = GiLoadUzipFloat32V2(S0),
                                v_row1 = GiLoadUzipFloat32V2(S1);

                GI_FLOAT32_t v_dst0 = GiAddFloat32(
                        GiGetSubVectorFloat32V2(v_row0, 0),
                        GiGetSubVectorFloat32V2(v_row0, 1));
                GI_FLOAT32_t v_dst1 = GiAddFloat32(
                        GiGetSubVectorFloat32V2(v_row1, 0),
                        GiGetSubVectorFloat32V2(v_row1, 1));

                GiStoreFloat32(
                        D, GiMultiplyFloat32(GiAddFloat32(v_dst0, v_dst1), v_025));
            }
        }

        return dx;
    }

private:
    int scale_x, scale_y;
    int cn;
    bool fast_mode;
    int step;
};

struct DecimateAlpha {
    int si, di;
    float alpha;
};
template <typename T>
using ResizeFunc = void (*)(
        const Mat<T>& src, Mat<T>& dst, const int* xofs, const void* alpha,
        const int* yofs, const void* beta, int xmin, int xmax, int ksize);
template <typename T>
using ResizeAreaFastFunc = void (*)(
        const Mat<T>& src, Mat<T>& dst, const int* ofs, const int* xofs, int scale_x,
        int scale_y);
template <typename T>
using ResizeAreaFunc = void (*)(
        const Mat<T>& src, Mat<T>& dst, const DecimateAlpha* xtab, int xtab_size,
        const DecimateAlpha* ytab, int ytab_size, const int* yofs);

static GI_FORCEINLINE void interpolate_cubic(float x, float* coeffs) {
    const float A = -0.75f;

    coeffs[0] = ((A * (x + 1) - 5 * A) * (x + 1) + 8 * A) * (x + 1) - 4 * A;
    coeffs[1] = ((A + 2) * x - (A + 3)) * x * x + 1;
    coeffs[2] = ((A + 2) * (1 - x) - (A + 3)) * (1 - x) * (1 - x) + 1;
    coeffs[3] = 1.f - coeffs[0] - coeffs[1] - coeffs[2];
}
static GI_FORCEINLINE void interpolate_lanczos4(float x, float* coeffs) {
    static const double s45 = 0.70710678118654752440084436210485;
    static const double cs[][2] = {{1, 0},  {-s45, -s45}, {0, 1},  {s45, -s45},
                                   {-1, 0}, {s45, s45},   {0, -1}, {-s45, s45}};

    if (x < FLT_EPSILON) {
        for (int i = 0; i < 8; i++)
            coeffs[i] = 0;
        coeffs[3] = 1;
        return;
    }

    float sum = 0;
    double y0 = -(x + 3) * MEGCV_PI * 0.25, s0 = sin(y0), c0 = cos(y0);
    for (int i = 0; i < 8; i++) {
        double y = -(x + 3 - i) * MEGCV_PI * 0.25;
        coeffs[i] = (float)((cs[i][0] * s0 + cs[i][1] * c0) / (y * y));
        sum += coeffs[i];
    }

    sum = 1.f / sum;
    for (int i = 0; i < 8; i++)
        coeffs[i] *= sum;
}

template <typename T, typename WT, typename AT>
struct HResizeLanczos4 {
    typedef T value_type;
    typedef WT buf_type;
    typedef AT alpha_type;

    void operator()(
            const T** src, WT** dst, int count, const int* xofs, const AT* alpha,
            int swidth, int dwidth, int cn, int xmin, int xmax) const {
        for (int k = 0; k < count; k++) {
            const T* S = src[k];
            WT* D = dst[k];
            int dx = 0, limit = xmin;
            if (cn == 1) {
                for (;;) {
                    for (; dx < limit; dx++, alpha += 8) {
                        int j, sx = xofs[dx] - 1 * 3;
                        WT v = 0;
                        for (j = 0; j < 8; j++) {
                            int sxj = sx + j * 1;
                            if ((unsigned)sxj >= (unsigned)swidth) {
                                while (sxj < 0)
                                    sxj += 1;
                                while (sxj >= swidth)
                                    sxj -= 1;
                            }
                            v += S[sxj] * alpha[j];
                        }
                        D[dx] = v;
                    }
                    if (limit == dwidth)
                        break;
                    for (; dx < xmax; dx++, alpha += 8) {
                        int sx = xofs[dx];
                        D[dx] = S[sx - 1 * 3] * alpha[0] + S[sx - 1 * 2] * alpha[1] +
                                S[sx - 1] * alpha[2] + S[sx] * alpha[3] +
                                S[sx + 1] * alpha[4] + S[sx + 1 * 2] * alpha[5] +
                                S[sx + 1 * 3] * alpha[6] + S[sx + 1 * 4] * alpha[7];
                    }
                    limit = dwidth;
                }
            } else {
                megdnn_assert(cn == 3);
                for (;;) {
                    for (; dx < limit; dx++, alpha += 8) {
                        int j, sx = xofs[dx] - 3 * 3;
                        WT v = 0;
                        for (j = 0; j < 8; j++) {
                            int sxj = sx + j * 3;
                            if ((unsigned)sxj >= (unsigned)swidth) {
                                while (sxj < 0)
                                    sxj += 3;
                                while (sxj >= swidth)
                                    sxj -= 3;
                            }
                            v += S[sxj] * alpha[j];
                        }
                        D[dx] = v;
                    }
                    if (limit == dwidth)
                        break;
                    for (; dx < xmax; dx++, alpha += 8) {
                        int sx = xofs[dx];
                        D[dx] = S[sx - 3 * 3] * alpha[0] + S[sx - 3 * 2] * alpha[1] +
                                S[sx - 3] * alpha[2] + S[sx] * alpha[3] +
                                S[sx + 3] * alpha[4] + S[sx + 3 * 2] * alpha[5] +
                                S[sx + 3 * 3] * alpha[6] + S[sx + 3 * 4] * alpha[7];
                    }
                    limit = dwidth;
                }
            }
            alpha -= dwidth * 8;
        }
    }
};
template <typename T, typename WT, typename AT, int ONE, class VecOp>
struct HResizeLinear {
    typedef T value_type;
    typedef WT buf_type;
    typedef AT alpha_type;

    void operator()(
            const T** src, WT** dst, int count, const int* xofs, const AT* alpha,
            int swidth, int dwidth, int cn, int xmin, int xmax) const {
        int dx, k;
        VecOp vecOp;

        int dx0 =
                vecOp((const float**)src, (float**)dst, count, xofs,
                      (const float*)alpha, swidth, dwidth, cn, xmin, xmax);

        for (k = 0; k <= count - 2; k++) {
            const T *S0 = src[k], *S1 = src[k + 1];
            WT *D0 = dst[k], *D1 = dst[k + 1];
            for (dx = dx0; dx < xmax; dx++) {
                int sx = xofs[dx];
                WT a0 = alpha[dx * 2], a1 = alpha[dx * 2 + 1];
                WT t0 = S0[sx] * a0 + S0[sx + cn] * a1;
                WT t1 = S1[sx] * a0 + S1[sx + cn] * a1;
                D0[dx] = t0;
                D1[dx] = t1;
            }

            for (; dx < dwidth; dx++) {
                int sx = xofs[dx];
                D0[dx] = WT(S0[sx] * ONE);
                D1[dx] = WT(S1[sx] * ONE);
            }
        }

        for (; k < count; k++) {
            const T* S = src[k];
            WT* D = dst[k];
            for (dx = 0; dx < xmax; dx++) {
                int sx = xofs[dx];
                D[dx] = S[sx] * alpha[dx * 2] + S[sx + cn] * alpha[dx * 2 + 1];
            }

            for (; dx < dwidth; dx++)
                D[dx] = WT(S[xofs[dx]] * ONE);
        }
    }
};
template <typename T, typename WT, typename AT>
struct HResizeCubic {
    typedef T value_type;
    typedef WT buf_type;
    typedef AT alpha_type;

    void operator()(
            const T** src, WT** dst, int count, const int* xofs, const AT* alpha,
            int swidth, int dwidth, int cn, int xmin, int xmax) const {
        for (int k = 0; k < count; k++) {
            const T* S = src[k];
            WT* D = dst[k];
            int dx = 0, limit = xmin;
            if (cn == 1) {
                for (;;) {
                    for (; dx < limit; dx++, alpha += 4) {
                        int j, sx = xofs[dx] - 1;
                        WT v = 0;
                        for (j = 0; j < 4; j++) {
                            int sxj = sx + j * 1;
                            if ((unsigned)sxj >= (unsigned)swidth) {
                                while (sxj < 0)
                                    sxj += 1;
                                while (sxj >= swidth)
                                    sxj -= 1;
                            }
                            v += S[sxj] * alpha[j];
                        }
                        D[dx] = v;
                    }
                    if (limit == dwidth)
                        break;
                    for (; dx < xmax; dx++, alpha += 4) {
                        int sx = xofs[dx];
                        D[dx] = S[sx - 1] * alpha[0] + S[sx] * alpha[1] +
                                S[sx + 1] * alpha[2] + S[sx + 1 * 2] * alpha[3];
                    }
                    limit = dwidth;
                }
            } else {
                megdnn_assert(cn == 3);
                for (;;) {
                    for (; dx < limit; dx++, alpha += 4) {
                        int j, sx = xofs[dx] - 3;
                        WT v = 0;
                        for (j = 0; j < 4; j++) {
                            int sxj = sx + j * 3;
                            if ((unsigned)sxj >= (unsigned)swidth) {
                                while (sxj < 0)
                                    sxj += 3;
                                while (sxj >= swidth)
                                    sxj -= 3;
                            }
                            v += S[sxj] * alpha[j];
                        }
                        D[dx] = v;
                    }
                    if (limit == dwidth)
                        break;
                    for (; dx < xmax; dx++, alpha += 4) {
                        int sx = xofs[dx];
                        D[dx] = S[sx - 3] * alpha[0] + S[sx] * alpha[1] +
                                S[sx + 3] * alpha[2] + S[sx + 3 * 2] * alpha[3];
                    }
                    limit = dwidth;
                }
            }
            alpha -= dwidth * 4;
        }
    }
};

template <typename T, typename WT, typename AT, class CastOp, class VecOp>
struct VResizeLanczos4 {
    typedef T value_type;
    typedef WT buf_type;
    typedef AT alpha_type;

    void operator()(const WT** src, T* dst, const AT* beta, int width) const {
        CastOp castOp;
        VecOp vecOp;
        int k, x = vecOp((const float**)src, (float*)dst, (const float*)beta, width);
#if MEGCV_ENABLE_UNROLLED
        for (; x <= width - 4; x += 4) {
            WT b = beta[0];
            const WT* S = src[0];
            WT s0 = S[x] * b, s1 = S[x + 1] * b, s2 = S[x + 2] * b, s3 = S[x + 3] * b;

            for (k = 1; k < 8; k++) {
                b = beta[k];
                S = src[k];
                s0 += S[x] * b;
                s1 += S[x + 1] * b;
                s2 += S[x + 2] * b;
                s3 += S[x + 3] * b;
            }

            dst[x] = castOp(s0);
            dst[x + 1] = castOp(s1);
            dst[x + 2] = castOp(s2);
            dst[x + 3] = castOp(s3);
        }
#endif

        for (; x < width; x++) {
            dst[x] = castOp(
                    src[0][x] * beta[0] + src[1][x] * beta[1] + src[2][x] * beta[2] +
                    src[3][x] * beta[3] + src[4][x] * beta[4] + src[5][x] * beta[5] +
                    src[6][x] * beta[6] + src[7][x] * beta[7]);
        }
    }
};
template <typename T, typename WT, typename AT, class CastOp, class VecOp>
struct VResizeLinear {
    typedef T value_type;
    typedef WT buf_type;
    typedef AT alpha_type;

    void operator()(const WT** src, T* dst, const AT* beta, int width) const {
        WT b0 = beta[0], b1 = beta[1];
        const WT *S0 = src[0], *S1 = src[1];
        CastOp castOp;
        VecOp vecOp;
        int x = vecOp((const float**)src, (float*)dst, (const float*)beta, width);
#if MEGCV_ENABLE_UNROLLED
        for (; x <= width - 4; x += 4) {
            WT t0, t1;
            t0 = S0[x] * b0 + S1[x] * b1;
            t1 = S0[x + 1] * b0 + S1[x + 1] * b1;
            dst[x] = castOp(t0);
            dst[x + 1] = castOp(t1);
            t0 = S0[x + 2] * b0 + S1[x + 2] * b1;
            t1 = S0[x + 3] * b0 + S1[x + 3] * b1;
            dst[x + 2] = castOp(t0);
            dst[x + 3] = castOp(t1);
        }
#endif
        for (; x < width; x++)
            dst[x] = castOp(S0[x] * b0 + S1[x] * b1);
    }
};
template <typename T, typename WT, typename AT, class CastOp, class VecOp>
struct VResizeCubic {
    typedef T value_type;
    typedef WT buf_type;
    typedef AT alpha_type;

    void operator()(const WT** src, T* dst, const AT* beta, int width) const {
        WT b0 = beta[0], b1 = beta[1], b2 = beta[2], b3 = beta[3];
        const WT *S0 = src[0], *S1 = src[1], *S2 = src[2], *S3 = src[3];
        CastOp castOp;
        VecOp vecOp;

        int x = vecOp((const float**)src, (float*)dst, (const float*)beta, width);
        for (; x < width; x++)
            dst[x] = castOp(S0[x] * b0 + S1[x] * b1 + S2[x] * b2 + S3[x] * b3);
    }
};

template <class HResize, class VResize, class MT>
void resizeGeneric_(
        const Mat<MT>& src, Mat<MT>& dst, const int* xofs, const void* _alpha,
        const int* yofs, const void* _beta, int xmin, int xmax, int ksize) {
    typedef typename HResize::value_type T;
    typedef typename HResize::buf_type WT;
    typedef typename HResize::alpha_type AT;

    const AT* beta = static_cast<const AT*>(_beta);
    const AT* alpha = static_cast<const AT*>(_alpha);
    int swidth = src.width();
    int sheight = src.height();
    int dwidth = dst.width();
    int dheight = dst.height();
    int cn = src.channels();
    swidth *= cn;
    dwidth *= cn;
    xmin *= cn;
    xmax *= cn;
    // image resize is a separable operation. In case of not too strong
    // dsize.height
    int dy;
    HResize hresize;
    VResize vresize;

    int bufstep = static_cast<int>(align_size(dwidth, 16));
    AlignedVector<WT> _buffer(bufstep * ksize);
    WT* buffer = _buffer.data();
    const T* srows[16] = {0};
    WT* rows[16] = {0};
    int prev_sy[16];

    for (int k = 0; k < ksize; ++k) {
        prev_sy[k] = -1;
        rows[k] = buffer + bufstep * k;
    }

    for (dy = 0; dy < dheight; ++dy, beta += ksize) {
        int sy0 = yofs[dy], k0 = ksize, k1 = 0, ksize2 = ksize / 2;

        for (int k = 0; k < ksize; ++k) {
            int sy = saturate(sy0 - ksize2 + 1 + k, 0, sheight);
            for (k1 = std::max(k1, k); k1 < ksize; ++k1) {
                if (sy == prev_sy[k1]) {
                    if (k1 > k)
                        memcpy(rows[k], rows[k1], bufstep * sizeof(rows[0][0]));
                    break;
                }
            }
            if (k1 == ksize)
                k0 = std::min(k0, k);
            srows[k] = src.ptr(sy);
            prev_sy[k] = sy;
        }
        if (k0 < ksize)
            hresize(srows + k0, rows + k0, ksize - k0, xofs, alpha, swidth, dwidth, cn,
                    xmin, xmax);
        vresize((const WT**)(rows), dst.ptr(dy), beta, dwidth);
    }
}

template <typename T>
void setup_resize_env(
        InterpolationMode /* ip */, int& /* ksize */, bool& /* fixedpt */,
        ResizeFunc<T>& /* func */) {
    megdnn_throw(("unimplemented"));
}
template <>
void setup_resize_env(
        InterpolationMode ip, int& ksize, bool& fixedpt, ResizeFunc<float>& func) {
    fixedpt = false;
    switch (ip) {
        case IMode::INTER_CUBIC:
            ksize = 4;
            func = resizeGeneric_<
                    HResizeCubic<float, float, float>,
                    VResizeCubic<
                            float, float, float, Cast<float, float>,
                            VResizeCubicVec_32f>,
                    float>;
            break;
        case IMode::INTER_LANCZOS4:
            ksize = 8;
            func = resizeGeneric_<
                    HResizeLanczos4<float, float, float>,
                    VResizeLanczos4<
                            float, float, float, Cast<float, float>,
                            VResizeLanczos4Vec_32f>,
                    float>;
            break;
        case IMode::INTER_LINEAR:
        case IMode::INTER_AREA:
            ksize = 2;
            func = resizeGeneric_<
                    HResizeLinear<float, float, float, 1, HResizeLinearVec_32f>,
                    VResizeLinear<
                            float, float, float, Cast<float, float>,
                            VResizeLinearVec_32f>,
                    float>;
            break;
        default:
            megdnn_throw(("unknown interpolation method"));
    }
}

int compute_resize_area_tab(
        int ssize, int dsize, int cn, double scale, DecimateAlpha* tab) {
    int k = 0;
    for (int dx = 0; dx < dsize; dx++) {
        double fsx1 = dx * scale;
        double fsx2 = fsx1 + scale;
        double cellWidth = std::min(scale, ssize - fsx1);

        int sx1 = ceil(fsx1), sx2 = floor(fsx2);

        sx2 = std::min(sx2, ssize - 1);
        sx1 = std::min(sx1, sx2);

        if (sx1 - fsx1 > 1e-3) {
            megdnn_assert(k < ssize * 2);
            tab[k].di = dx * cn;
            tab[k].si = (sx1 - 1) * cn;
            tab[k++].alpha = (float)((sx1 - fsx1) / cellWidth);
        }

        for (int sx = sx1; sx < sx2; sx++) {
            megdnn_assert(k < ssize * 2);
            tab[k].di = dx * cn;
            tab[k].si = sx * cn;
            tab[k++].alpha = float(1.0 / cellWidth);
        }

        if (fsx2 - sx2 > 1e-3) {
            megdnn_assert(k < ssize * 2);
            tab[k].di = dx * cn;
            tab[k].si = sx2 * cn;
            tab[k++].alpha =
                    (float)(std::min(std::min(fsx2 - sx2, 1.), cellWidth) / cellWidth);
        }
    }
    return k;
}

// resize Area Fast
template <typename T, typename WT, typename VecOp>
void resizeAreaFast_(
        const Mat<T>& src, Mat<T>& dst, const int* ofs, const int* xofs, int scale_x,
        int scale_y) {
    // Range range(0, dst.rows);
    int swidth = src.width();
    int sheight = src.height();
    int dwidth = dst.width();
    int dheight = dst.height();
    int cn = src.channels();
    int area = scale_x * scale_y;
    float scale = 1.f / (area);
    int dwidth1 = (swidth / scale_x) * cn;
    dwidth *= cn;
    swidth *= cn;
    int dy, dx, k = 0;

    VecOp vop(scale_x, scale_y, src.channels(), (int)src.step());

    for (dy = 0; dy < dheight; dy++) {
        T* D = (T*)(dst.ptr(dy));
        int sy0 = dy * scale_y;
        int w = sy0 + scale_y <= sheight ? dwidth1 : 0;

        if (sy0 >= sheight) {
            for (dx = 0; dx < dwidth; dx++)
                D[dx] = 0;
            continue;
        }

        dx = vop((const T*)(src.ptr(sy0)), D, w);
        for (; dx < w; dx++) {
            const T* S = (const T*)(src.ptr(sy0)) + xofs[dx];
            WT sum = 0;
            k = 0;
#if MEGCV_ENABLE_UNROLLED
            for (; k <= area - 4; k += 4)
                sum += S[ofs[k]] + S[ofs[k + 1]] + S[ofs[k + 2]] + S[ofs[k + 3]];
#endif
            for (; k < area; k++)
                sum += S[ofs[k]];

            D[dx] = saturate_cast<T>(sum * scale);
        }

        for (; dx < dwidth; dx++) {
            WT sum = 0;
            int count = 0, sx0 = xofs[dx];
            if (sx0 >= swidth)
                D[dx] = 0;

            for (int sy = 0; sy < scale_y; sy++) {
                if (sy0 + sy >= sheight)
                    break;
                const T* S = (const T*)(src.ptr(sy0 + sy)) + sx0;
                for (int sx = 0; sx < scale_x * cn; sx += cn) {
                    if (sx0 + sx >= swidth)
                        break;
                    sum += S[sx];
                    count++;
                }
            }

            D[dx] = saturate_cast<T>((float)sum / count);
        }
    }
}

template <typename T>
ResizeAreaFastFunc<T> get_resize_area_fast_func() {
    megdnn_throw(("unknown type"));
}

template <>
ResizeAreaFastFunc<float> get_resize_area_fast_func<float>() {
    return resizeAreaFast_<float, float, ResizeAreaFastVec_SIMD_32f>;
}

// Resize Area
template <typename T, typename WT>
static void resizeArea_(
        const Mat<T>& src, Mat<T>& dst, const DecimateAlpha* xtab, int xtab_size,
        const DecimateAlpha* ytab, int ytab_size, const int* tabofs) {
    // parallel_for_(Range(0, dst.rows),
    // ResizeArea_Invoker<T, WT>(src, dst, xtab, xtab_size, ytab, ytab_size,
    // tabofs), dst.total()/((double)(1 << 16)));
    (void)ytab_size;
    int dwidth = dst.width(), dheight = dst.height();
    int cn = dst.channels();
    dwidth *= cn;
    AlignedVector<WT> _buffer(dwidth * 2);
    WT *buf = _buffer.data(), *sum = buf + dwidth;
    int j_start = tabofs[0], j_end = tabofs[dheight], j, k, dx,
        prev_dy = ytab[j_start].di;

    for (dx = 0; dx < dwidth; dx++)
        sum[dx] = (WT)0;

    for (j = j_start; j < j_end; j++) {
        WT beta = ytab[j].alpha;
        int dy = ytab[j].di;
        int sy = ytab[j].si;

        {
            const T* S = (const T*)(src.ptr(sy));
            for (dx = 0; dx < dwidth; dx++)
                buf[dx] = (WT)0;

            if (cn == 1)
                for (k = 0; k < xtab_size; k++) {
                    int dxn = xtab[k].di;
                    WT alpha = xtab[k].alpha;
                    buf[dxn] += S[xtab[k].si] * alpha;
                }
            else if (cn == 3)
                for (k = 0; k < xtab_size; k++) {
                    int sxn = xtab[k].si;
                    int dxn = xtab[k].di;
                    WT alpha = xtab[k].alpha;
                    WT t0 = buf[dxn] + S[sxn] * alpha;
                    WT t1 = buf[dxn + 1] + S[sxn + 1] * alpha;
                    WT t2 = buf[dxn + 2] + S[sxn + 2] * alpha;
                    buf[dxn] = t0;
                    buf[dxn + 1] = t1;
                    buf[dxn + 2] = t2;
                }
            else {
                megdnn_throw(("nr. of channels must be 1 or 3"));
            }
        }

        if (dy != prev_dy) {
            T* D = dst.ptr(prev_dy);

            for (dx = 0; dx < dwidth; dx++) {
                D[dx] = saturate_cast<T>(sum[dx]);
                sum[dx] = beta * buf[dx];
            }
            prev_dy = dy;
        } else {
            for (dx = 0; dx < dwidth; dx++)
                sum[dx] += beta * buf[dx];
        }
    }

    {
        T* D = dst.ptr(prev_dy);
        for (dx = 0; dx < dwidth; dx++)
            D[dx] = saturate_cast<T>(sum[dx]);
    }
}

template <typename T>
ResizeAreaFunc<T> get_resize_area_func() {
    megdnn_throw(("unknown type"));
}

template <>
ResizeAreaFunc<float> get_resize_area_func<float>() {
    return resizeArea_<float, float>;
}

template <typename T>
void resize_opencv(const Mat<T>& src, Mat<T>& dst, InterpolationMode ip) {
    // fake area mode missing here
    int dwidth = dst.width();
    int dheight = dst.height();
    int swidth = src.width();
    int sheight = src.height();
    int xmin = 0, xmax = dwidth, width = dwidth * dst.channels();
    double inv_scale_x = static_cast<double>(dwidth) / swidth;
    double inv_scale_y = static_cast<double>(dheight) / sheight;
    double scale_x = 1.0 / inv_scale_x;
    double scale_y = 1.0 / inv_scale_y;
    int dx, sx, dy, sy, k;
    float fx, fy;
    int cn = src.channels();
    {
        int iscale_x = saturate_cast<int>(scale_x);
        int iscale_y = saturate_cast<int>(scale_y);

        bool is_area_fast = std::abs(scale_x - iscale_x) < DBL_EPSILON &&
                            std::abs(scale_y - iscale_y) < DBL_EPSILON;
        if (ip == IMode::INTER_LINEAR && is_area_fast && iscale_x == 2 &&
            iscale_y == 2) {
            ip = IMode::INTER_AREA;
        }
        if (ip == IMode::INTER_AREA && scale_x >= 1 && scale_y >= 1) {
            if (is_area_fast) {
                int area = iscale_x * iscale_y;
                size_t srcstep = src.step();
                AlignedVector<int> _ofs(area + dwidth * cn);
                int* ofs = _ofs.data();
                int* xofs = ofs + area;
                ResizeAreaFastFunc<T> func =
                        get_resize_area_fast_func<T>();  /// need change
                for (sy = 0, k = 0; sy < iscale_y; ++sy)
                    for (sx = 0; sx < iscale_x; ++sx)
                        ofs[k++] = static_cast<int>(sy * srcstep + sx * cn);
                for (dx = 0; dx < dwidth; ++dx) {
                    int j = dx * cn;
                    sx = iscale_x * j;
                    for (k = 0; k < cn; ++k)
                        xofs[j + k] = sx + k;
                }
                func(src, dst, ofs, xofs, iscale_x, iscale_y);
                return;
            }
            ResizeAreaFunc<T> func = get_resize_area_func<T>();
            AlignedVector<DecimateAlpha> _xytab((swidth + sheight) * 2);
            DecimateAlpha *xtab = _xytab.data(), *ytab = xtab + swidth * 2;
            int xtab_size = compute_resize_area_tab(swidth, dwidth, cn, scale_x, xtab);
            int ytab_size = compute_resize_area_tab(sheight, dheight, 1, scale_y, ytab);
            AlignedVector<int> _tabofs(dheight + 1);
            int* tabofs = _tabofs.data();
            for (k = 0, dy = 0; k < ytab_size; ++k) {
                if (k == 0 || ytab[k].di != ytab[k - 1].di) {
                    megdnn_assert(ytab[k].di == dy);
                    tabofs[dy++] = k;
                }
            }
            tabofs[dy] = ytab_size;
            func(src, dst, xtab, xtab_size, ytab, ytab_size, tabofs);
            return;
        }
    }
    bool area_mode = (ip == IMode::INTER_AREA);
    int ksize, ksize2;
    ResizeFunc<T> func;
    bool fixedpt;
    setup_resize_env<T>(ip, ksize, fixedpt, func);
    ksize2 = ksize / 2;
    AlignedVector<uchar> _buffer(
            (width + dst.height()) * (sizeof(int) + sizeof(float) * ksize));
    uchar* buffer = _buffer.data();
    int* xofs = static_cast<int*>(static_cast<void*>(buffer));
    int* yofs = xofs + width;
    float* alpha = static_cast<float*>(static_cast<void*>(yofs + dst.height()));
    short* ialpha = static_cast<short*>(static_cast<void*>(alpha));
    float* beta = alpha + width * ksize;
    short* ibeta = static_cast<short*>(static_cast<void*>(beta));
    // float cbuf[16];
    float cbuf[16] = {0};
    for (dx = 0; dx < dwidth; ++dx) {
        if (!area_mode) {
            fx = (float)((dx + 0.5) * scale_x - 0.5);
            sx = floor(fx);
            fx -= sx;
        } else {
            sx = floor(dx * scale_x);
            fx = (float)((dx + 1) - (sx + 1) * inv_scale_x);
            fx = (fx <= 0 ? 0.0f : fx - floor(fx));
        }

        if (sx < ksize2 - 1) {
            xmin = dx + 1;
            if (sx < 0 && (ip != IMode::INTER_CUBIC && ip != IMode::INTER_LANCZOS4)) {
                fx = 0;
                sx = 0;
            }
        }
        if (sx + ksize2 >= swidth) {
            xmax = std::min(xmax, dx);
            if (sx >= swidth - 1 && ip != IMode::INTER_CUBIC &&
                ip != IMode::INTER_LANCZOS4) {
                fx = 0;
                sx = swidth - 1;
            }
        }
        int k;
        for (k = 0, sx *= cn; k < cn; ++k)
            xofs[dx * cn + k] = sx + k;
        if (ip == IMode::INTER_CUBIC) {
            interpolate_cubic(fx, cbuf);
        } else if (ip == IMode::INTER_LANCZOS4) {
            interpolate_lanczos4(fx, cbuf);
        } else {
            cbuf[0] = 1.0f - fx;
            cbuf[1] = fx;
        }
        if (fixedpt) {
            for (k = 0; k < ksize; ++k) {
                ialpha[dx * cn * ksize + k] =
                        saturate_cast<short>(cbuf[k] * INTER_RESIZE_COEF_SCALE);
            }
            for (; k < cn * ksize; ++k) {
                ialpha[dx * cn * ksize + k] = ialpha[dx * cn * ksize + k - ksize];
            }
        } else {
            for (k = 0; k < ksize; ++k) {
                alpha[dx * cn * ksize + k] = cbuf[k];
            }
            for (; k < cn * ksize; ++k) {
                alpha[dx * cn * ksize + k] = alpha[dx * cn * ksize + k - ksize];
            }
        }
    }
    for (dy = 0; dy < dheight; ++dy) {
        if (!area_mode) {
            fy = static_cast<float>((dy + 0.5) * scale_y - 0.5);
            sy = floor(fy);
            fy -= sy;
        } else {
            sy = floor(dy * scale_y);
            fy = static_cast<float>((dy + 1) - (sy + 1) * inv_scale_y);
            fy = (fy <= 0 ? 0.0f : fy - floor(fy));
        }
        yofs[dy] = sy;
        if (ip == IMode::INTER_CUBIC) {
            interpolate_cubic(fy, cbuf);
        } else if (ip == IMode::INTER_LANCZOS4) {
            interpolate_lanczos4(fy, cbuf);
        } else {
            cbuf[0] = 1.0f - fy;
            cbuf[1] = fy;
        }
        if (fixedpt) {
            for (int k = 0; k < ksize; ++k) {
                ibeta[dy * ksize + k] =
                        saturate_cast<short>(cbuf[k] * INTER_RESIZE_COEF_SCALE);
            }
        } else {
            for (int k = 0; k < ksize; ++k) {
                beta[dy * ksize + k] = cbuf[k];
            }
        }
    }
    func(src, dst, xofs,
         fixedpt ? static_cast<void*>(ialpha) : static_cast<void*>(alpha), yofs,
         fixedpt ? static_cast<void*>(ibeta) : static_cast<void*>(beta), xmin, xmax,
         ksize);
}

}  // anonymous namespace

void megdnn::fallback::resize_cv_gi_exec(
        _megdnn_tensor_in src, _megdnn_tensor_out dst,
        param::Resize::InterpolationMode imode) {
    megdnn_assert(src.layout[3] == 1 || src.layout[3] == 3, "unsupported src channel");
    for (size_t i = 0; i < src.layout.shape[0]; ++i) {
        if (dst.layout.dtype == dtype::Float32()) {
            MIDOUT_BEGIN(megdnn_fallback_resizecv_dtype, midout_iv(0)) {
                Mat<float> src_mat = TensorND2Mat<float>(src, i);
                Mat<float> dst_mat = TensorND2Mat<float>(dst, i);
                switch (imode) {
                    case IMode::INTER_NEAREST:
                        MIDOUT_BEGIN(megdnn_fallback_resizecv_imode, midout_iv(0)) {
                            resize_nearest_32f(src_mat, dst_mat);
                        }
                        MIDOUT_END();
                        break;
                    case IMode::INTER_LINEAR:
                        MIDOUT_BEGIN(megdnn_fallback_resizecv_imode, midout_iv(1)) {
                            resize_linear_32f(src_mat, dst_mat);
                        }
                        MIDOUT_END();
                        break;
                    case IMode::INTER_CUBIC:
                    case IMode::INTER_LANCZOS4:
                    case IMode::INTER_AREA:
                        MIDOUT_BEGIN(megdnn_fallback_resizecv_imode, midout_iv(2)) {
                            resize_opencv<float>(src_mat, dst_mat, imode);
                        }
                        MIDOUT_END();
                        break;
                    default:
                        megdnn_throw("unsupported interpolation mode");
                        break;
                }
            }
            MIDOUT_END();
        } else {
            megdnn_throw("Unsupported datatype of resize optr.");
        }
    }
}

// vim: syntax=cpp.doxygen
