#pragma once

#include "src/common/utils.h"
#include "src/fallback/general_intrinsic/gi_float.h"
#include "src/fallback/general_intrinsic/gi_int.h"
#include "src/fallback/quantized_converter.h"

using namespace megdnn;
using namespace fallback;

namespace {

/*****************************Mean Reducer***********************/
template <typename dtype, typename ctype, typename comp_type, bool C1>
struct MeanReducer;

template <>
struct MeanReducer<dt_qint8, int8_t, int32_t, true> {
    using ctype = int8_t;
    static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(int8_t);

    int32_t res;
    float coef;
    MeanReducer(DType, size_t cnt) : res(0), coef(1.0 / cnt) {}
    MeanReducer() = default;
    void feed(const int8_t* val) { res += GiReduceAddInt8(GiLoadInt8(val)); }
    void feed_remain(const int8_t* val) { res += *val; }
    void post(int8_t* dst) {
        float sum = res * coef;
        *dst = std::round(sum);
    }
};

template <>
struct MeanReducer<dt_qint8, int8_t, int32_t, false> {
    using ctype = int8_t;
    static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(int8_t);

    GI_INT32_FIXLEN_t res[4];
    int32_t remain;
    int32_t cnt;
    float coef;
    GI_FLOAT32_FIXLEN_t vcoef;
    MeanReducer(DType, size_t cnt) : remain(0), cnt(cnt), coef(1.0 / cnt) {
        memset(res, 0, sizeof(res));
        vcoef = GiFloat32Type2FixLenType(GiBroadcastFloat32(coef));
    }
    MeanReducer() = default;
    void feed(const int8_t* val) { feed_vector(GiLoadInt8(val)); }
    void feed_vector(const GI_INT8_t vval) {
        const GI_INT16_t vval_low = GiMoveLowLongInt8(vval);
        const GI_INT16_t vval_high = GiMoveHighLongInt8(vval);

        const GI_INT32_t vval_low_low = GiMoveLowLongInt16(vval_low);
        const GI_INT32_t vval_low_high = GiMoveHighLongInt16(vval_low);
        const GI_INT32_t vval_high_low = GiMoveLowLongInt16(vval_high);
        const GI_INT32_t vval_high_high = GiMoveHighLongInt16(vval_high);

        res[0] = GiInt32Type2FixLenType(
                GiAddInt32(GiFixLenType2GiInt32Type(res[0]), vval_low_low));
        res[1] = GiInt32Type2FixLenType(
                GiAddInt32(GiFixLenType2GiInt32Type(res[1]), vval_low_high));
        res[2] = GiInt32Type2FixLenType(
                GiAddInt32(GiFixLenType2GiInt32Type(res[2]), vval_high_low));
        res[3] = GiInt32Type2FixLenType(
                GiAddInt32(GiFixLenType2GiInt32Type(res[3]), vval_high_high));
    }
    void feed_remain(const int8_t* val) { remain += *val; }
    void post(int8_t* dst) {
        for (int i = 0; i < 4; i += 2) {
            auto tmp = GiFixLenType2GiFloat32Type(vcoef);
            GI_FLOAT32_t vitem0 = GiMultiplyFloat32(
                    GiCastToFloat32(GiFixLenType2GiInt32Type(res[i])), tmp);
            GI_FLOAT32_t vitem1 = GiMultiplyFloat32(
                    GiCastToFloat32(GiFixLenType2GiInt32Type(res[i + 1])), tmp);
            GI_FLOAT32_V2_t ret;
            GiSetSubVectorFloat32V2(ret, 0, vitem0);
            GiSetSubVectorFloat32V2(ret, 1, vitem1);
            GiStoreLowInt8(dst, (QConverter::convert<GI_INT8_t, GI_FLOAT32_V2_t>(ret)));
            dst += 8;
        }
    }
    void post_remain(int8_t* dst) {
        float sum = remain * coef;
        *dst = std::round(sum);
    }
};

template <>
struct MeanReducer<dt_float32, float, float, true> {
    using ctype = float;
    static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(float);

    GI_FLOAT32_FIXLEN_t res;
    float result;
    float coef;
    MeanReducer(DType, size_t cnt) : result(0.0f), coef(1.0 / cnt) {
        res = GiFloat32Type2FixLenType(GiBroadcastFloat32(0.0f));
    }
    MeanReducer() = default;
    void feed(const float* val) {
        res = GiFloat32Type2FixLenType(
                GiAddFloat32(GiLoadFloat32(val), GiFixLenType2GiFloat32Type(res)));
    }
    void feed_remain(const float* val) { result += *val; }
    void post(float* dst) {
        result += GiReduceAddFloat32(GiFixLenType2GiFloat32Type(res));
        *dst = result * coef;
    }
};

template <>
struct MeanReducer<dt_float32, float, float, false> {
    using ctype = float;
    static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(float);

    GI_FLOAT32_FIXLEN_t res;
    float remain;
    float coef;
    MeanReducer(DType, size_t cnt) : remain(0.0f), coef(1.0 / cnt) {
        res = GiFloat32Type2FixLenType(GiBroadcastFloat32(0.0f));
    }
    MeanReducer() = default;
    void feed(const float* val) { feed_vector(GiLoadFloat32(val)); }
    void inline feed_vector(const GI_FLOAT32_t& val) {
        res = GiFloat32Type2FixLenType(
                GiAddFloat32(val, GiFixLenType2GiFloat32Type(res)));
    }
    void feed_remain(const float* val) { remain += *val; }
    void post(float* dst) {
        res = GiFloat32Type2FixLenType(
                GiMultiplyScalerFloat32(GiFixLenType2GiFloat32Type(res), coef));
        GiStoreFloat32(dst, GiFixLenType2GiFloat32Type(res));
    }
    void post_remain(float* dst) { *dst = remain * coef; }
};

/******************************max min Reducer****************************/
template <typename dtype, typename ctype, typename comp_type, bool C1>
struct maxReducer;
template <typename dtype, typename ctype, typename comp_type, bool C1>
struct minReducer;

#define REDUCER_MAX_MIN_C1(_mode, _Mode, _init)                                    \
    template <>                                                                    \
    struct _mode##Reducer<dt_float32, float, float, true> {                        \
        using ctype = float;                                                       \
        static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(float);        \
        GI_FLOAT32_FIXLEN_t res;                                                   \
        _mode##Reducer(DType, size_t) {                                            \
            res = GiFloat32Type2FixLenType(GiBroadcastFloat32(_init));             \
        }                                                                          \
        _mode##Reducer() = default;                                                \
        void feed(const float* val) {                                              \
            auto vval = GiLoadFloat32(val);                                        \
            res = GiFloat32Type2FixLenType(                                        \
                    Gi##_Mode##NanFloat32(GiFixLenType2GiFloat32Type(res), vval)); \
        }                                                                          \
        void feed_remain(const float* val) {                                       \
            auto vval = GiBroadcastFloat32(*val);                                  \
            res = GiFloat32Type2FixLenType(                                        \
                    Gi##_Mode##NanFloat32(vval, GiFixLenType2GiFloat32Type(res))); \
        }                                                                          \
        void post(float* dst) {                                                    \
            *dst = GiReduce##_Mode##NanFloat32(GiFixLenType2GiFloat32Type(res));   \
        }                                                                          \
    }

REDUCER_MAX_MIN_C1(max, Max, std::numeric_limits<dt_float32>::lowest());
REDUCER_MAX_MIN_C1(min, Min, std::numeric_limits<dt_float32>::max());
#undef REDUCER_MAX_MIN_C1

#define Max_NAN(a, b) (isnan(a) || (a) > (b)) ? (a) : (b);
#define Min_NAN(a, b) (isnan(a) || (a) < (b)) ? (a) : (b);

#define REDUCER_MAX_MIN_C(_mode, _Mode, _init)                                    \
    template <>                                                                   \
    struct _mode##Reducer<dt_float32, float, float, false> {                      \
        using ctype = float;                                                      \
        static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(float);       \
        GI_FLOAT32_FIXLEN_t res;                                                  \
        float remain;                                                             \
        _mode##Reducer(DType, size_t) {                                           \
            res = GiFloat32Type2FixLenType(GiBroadcastFloat32(_init));            \
            remain = _init;                                                       \
        }                                                                         \
        _mode##Reducer() = default;                                               \
        void feed(const float* val) { feed_vector(GiLoadFloat32(val)); }          \
        void inline feed_vector(const GI_FLOAT32_t& val) {                        \
            res = GiFloat32Type2FixLenType(                                       \
                    Gi##_Mode##NanFloat32(GiFixLenType2GiFloat32Type(res), val)); \
        }                                                                         \
        void feed_remain(const float* val) {                                      \
            using namespace std;                                                  \
            remain = _Mode##_NAN(*val, remain);                                   \
        }                                                                         \
        void post(float* dst) {                                                   \
            GiStoreFloat32(dst, GiFixLenType2GiFloat32Type(res));                 \
        }                                                                         \
        void post_remain(float* dst) { *dst = remain; }                           \
    }

REDUCER_MAX_MIN_C(max, Max, std::numeric_limits<dt_float32>::lowest());
REDUCER_MAX_MIN_C(min, Min, std::numeric_limits<dt_float32>::max());
#undef REDUCER_MAX_MIN_C
#undef Max_NAN
#undef Min_NAN

#define REDUCER_MAX_MIN_C1(_mode, _Mode, _init)                               \
    template <>                                                               \
    struct _mode##Reducer<dt_qint8, int8_t, int8_t, true> {                   \
        using ctype = int8_t;                                                 \
        static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(int8_t);  \
        GI_INT8_FIXLEN_t res;                                                 \
        _mode##Reducer(DType, size_t) {                                       \
            res = GiInt8Type2FixLenType(GiBroadcastInt8(_init));              \
        }                                                                     \
        _mode##Reducer() = default;                                           \
        void feed(const int8_t* val) {                                        \
            GI_INT8_t vval = GiLoadInt8(val);                                 \
            res = GiInt8Type2FixLenType(                                      \
                    Gi##_Mode##imumInt8(vval, GiFixLenType2GiInt8Type(res))); \
        }                                                                     \
        void feed_remain(const int8_t* val) {                                 \
            GI_INT8_t vval = GiBroadcastInt8(*val);                           \
            res = GiInt8Type2FixLenType(                                      \
                    Gi##_Mode##imumInt8(GiFixLenType2GiInt8Type(res), vval)); \
        }                                                                     \
        void post(int8_t* dst) {                                              \
            *dst = GiReduce##_Mode##Int8(GiFixLenType2GiInt8Type(res));       \
        }                                                                     \
    }

REDUCER_MAX_MIN_C1(max, Max, -128);
REDUCER_MAX_MIN_C1(min, Min, 127);
#undef REDUCER_MAX_MIN_C1

#define REDUCER_MAX_MIN_C(_mode, _Mode, _init)                                     \
    template <>                                                                    \
    struct _mode##Reducer<dt_qint8, int8_t, int8_t, false> {                       \
        using ctype = int8_t;                                                      \
        static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(int8_t);       \
        GI_INT8_FIXLEN_t res;                                                      \
        int8_t remain;                                                             \
        _mode##Reducer(DType, size_t) {                                            \
            res = GiInt8Type2FixLenType(GiBroadcastInt8(_init));                   \
            remain = _init;                                                        \
        }                                                                          \
        _mode##Reducer() = default;                                                \
        void feed(const int8_t* val) { feed_vector(GiLoadInt8(val)); }             \
        void inline feed_vector(GI_INT8_t val) {                                   \
            res = GiInt8Type2FixLenType(                                           \
                    Gi##_Mode##imumInt8(GiFixLenType2GiInt8Type(res), val));       \
        }                                                                          \
        void feed_remain(const int8_t* val) {                                      \
            using namespace std;                                                   \
            remain = _mode(*val, remain);                                          \
        }                                                                          \
        void post(int8_t* dst) { GiStoreInt8(dst, GiFixLenType2GiInt8Type(res)); } \
        void post_remain(int8_t* dst) { *dst = remain; }                           \
    }

REDUCER_MAX_MIN_C(max, Max, -128);
REDUCER_MAX_MIN_C(min, Min, 127);
#undef REDUCER_MAX_MIN_C

/***************************Sum Product Reducer***************************/
template <typename dtype, typename ctype, typename comp_type, bool C1>
struct SumReducer;
template <typename dtype, typename ctype, typename comp_type, bool C1>
struct ProductReducer;

#define REDUCER_SUM_PRODUCT_C1(_mode, _Mode, _op, _init)                           \
    template <>                                                                    \
    struct _mode##Reducer<dt_float32, float, float, true> {                        \
        using ctype = float;                                                       \
        static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(float);        \
        GI_FLOAT32_FIXLEN_t res;                                                   \
        float remain;                                                              \
        _mode##Reducer(DType, size_t) {                                            \
            res = GiFloat32Type2FixLenType(GiBroadcastFloat32(_init));             \
            remain = _init;                                                        \
        }                                                                          \
        _mode##Reducer() = default;                                                \
        void feed(const float* val) {                                              \
            GI_FLOAT32_t vval = GiLoadFloat32(val);                                \
            res = GiFloat32Type2FixLenType(                                        \
                    Gi##_Mode##Float32(vval, GiFixLenType2GiFloat32Type(res)));    \
        }                                                                          \
        void feed_remain(const float* val) {                                       \
            using namespace std;                                                   \
            auto op = _op<float>();                                                \
            remain = op(remain, *val);                                             \
        }                                                                          \
        void post(float* dst) {                                                    \
            using namespace std;                                                   \
            auto op = _op<float>();                                                \
            *dst =                                                                 \
                    op(remain,                                                     \
                       GiReduce##_Mode##Float32(GiFixLenType2GiFloat32Type(res))); \
        }                                                                          \
    }

REDUCER_SUM_PRODUCT_C1(Sum, Add, plus, 0.0f);
REDUCER_SUM_PRODUCT_C1(Product, Multiply, multiplies, 1.0f);
#undef REDUCER_SUM_PRODUCT_C1

#define REDUCER_SUM_PRODUCT_C(_mode, _Mode, _op, _init)                        \
    template <>                                                                \
    struct _mode##Reducer<dt_float32, float, float, false> {                   \
        using ctype = float;                                                   \
        static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(float);    \
        GI_FLOAT32_FIXLEN_t res;                                               \
        float remain;                                                          \
        _mode##Reducer(DType, size_t) {                                        \
            res = GiFloat32Type2FixLenType(GiBroadcastFloat32(_init));         \
            remain = _init;                                                    \
        }                                                                      \
        _mode##Reducer() = default;                                            \
        void feed(const float* val) { feed_vector(GiLoadFloat32(val)); }       \
        void inline feed_vector(GI_FLOAT32_t val) {                            \
            res = GiFloat32Type2FixLenType(                                    \
                    Gi##_Mode##Float32(val, GiFixLenType2GiFloat32Type(res))); \
        }                                                                      \
        void feed_remain(const float* val) {                                   \
            using namespace std;                                               \
            auto op = _op<float>();                                            \
            remain = op(remain, (*val));                                       \
        }                                                                      \
        void post(float* dst) {                                                \
            GiStoreFloat32(dst, GiFixLenType2GiFloat32Type(res));              \
        }                                                                      \
        void post_remain(float* dst) { *dst = remain; }                        \
    }

REDUCER_SUM_PRODUCT_C(Sum, Add, plus, 0.0f);
REDUCER_SUM_PRODUCT_C(Product, Multiply, multiplies, 1.0f);
#undef REDUCER_SUM_PRODUCT_C

/***************************SumSqr Reducer***************************/
template <typename dtype, typename ctype, typename comp_type, bool C1>
struct SumSqrReducer;

template <>
struct SumSqrReducer<dt_float32, float, float, true> {
    using ctype = float;
    static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(float);

    GI_FLOAT32_FIXLEN_t res;
    float result;
    SumSqrReducer(DType, size_t cnt) : result(0.0f) {
        MEGDNN_MARK_USED_VAR(cnt);
        res = GiFloat32Type2FixLenType(GiBroadcastFloat32(0.0f));
    }
    SumSqrReducer() = default;
    void feed(const float* val) {
        GI_FLOAT32_t vval = GiLoadFloat32(val);
        res = GiFloat32Type2FixLenType(GiAddFloat32(
                GiMultiplyFloat32(vval, vval), GiFixLenType2GiFloat32Type(res)));
    }
    void feed_remain(const float* val) {
        float vval = *val;
        result += vval * vval;
    }
    void post(float* dst) {
        result += GiReduceAddFloat32(GiFixLenType2GiFloat32Type(res));
        *dst = result;
    }
};
template <>
struct SumSqrReducer<dt_float32, float, float, false> {
    using ctype = float;
    static constexpr int SIMD_WIDTH = GI_SIMD_LEN_BYTE / sizeof(float);

    GI_FLOAT32_FIXLEN_t res;
    float remain;
    SumSqrReducer(DType, size_t cnt) : remain(0.0f) {
        MEGDNN_MARK_USED_VAR(cnt);
        res = GiFloat32Type2FixLenType(GiBroadcastFloat32(0.0f));
    }
    SumSqrReducer() = default;
    void feed(const float* val) { feed_vector(GiLoadFloat32(val)); }
    void inline feed_vector(GI_FLOAT32_t src) {
        res = GiFloat32Type2FixLenType(GiAddFloat32(
                GiMultiplyFloat32(src, src), GiFixLenType2GiFloat32Type(res)));
    }
    void feed_remain(const float* val) { remain += (*val) * (*val); }
    void post(float* dst) { GiStoreFloat32(dst, GiFixLenType2GiFloat32Type(res)); }
    void post_remain(float* dst) { *dst = remain; }
};

/**************************************do reduce*************************/

template <typename Reducer, bool C1>
struct Exec {
    static void do_reduce(
            const typename Reducer::ctype* src, typename Reducer::ctype* dst,
            DType src_dtype, size_t A, size_t B, size_t C);
};

template <typename Reducer>
struct Exec<Reducer, true> {
    static void do_reduce(
            const typename Reducer::ctype* src, typename Reducer::ctype* dst,
            DType src_dtype, size_t A, size_t B, size_t) {
        size_t a = 0;
        for (; a < A; a++) {
            Reducer reducer0(src_dtype, B);
            auto temp_src0 = src + a * B;
            size_t b = 0;
            for (; b + Reducer::SIMD_WIDTH <= B; b += Reducer::SIMD_WIDTH) {
                reducer0.feed(temp_src0);
                temp_src0 += Reducer::SIMD_WIDTH;
            }
            for (; b < B; b++) {
                reducer0.feed_remain(temp_src0);
                temp_src0++;
            }
            reducer0.post(dst);
            dst++;
        }
    }
};

template <typename Reducer>
struct Exec<Reducer, false> {
    static void do_reduce(
            const typename Reducer::ctype* src, typename Reducer::ctype* dst,
            DType src_dtype, size_t A, size_t B, size_t C) {
        for (size_t a = 0; a < A; a++) {
            size_t c = 0;
            for (; c + Reducer::SIMD_WIDTH <= C; c += Reducer::SIMD_WIDTH) {
                Reducer reducer(src_dtype, B);
                for (size_t b = 0; b < B; b++)
                    reducer.feed(src + c + C * b);
                reducer.post(dst);
                dst += Reducer::SIMD_WIDTH;
            }
            for (; c < C; c++) {
                Reducer reducer(src_dtype, B);
                for (size_t b = 0; b < B; b++)
                    reducer.feed_remain(src + c + C * b);
                reducer.post_remain(dst);
                dst++;
            }
            src += B * C;
        }
    }
};

template <typename Reducer, typename dtype, size_t B>
struct ExecC1SmallB {
    static void do_reduce(
            const dtype* src, dtype* dst, DType src_dtype, size_t A, size_t, size_t C);
};

#define ImplementC1SmallB(_ctype, _gi_type, _gi_ins)                                 \
    template <typename Reducer, size_t B>                                            \
    struct ExecC1SmallB<Reducer, _ctype, B> {                                        \
        static void do_reduce(                                                       \
                const _ctype* src, _ctype* dst, DType src_dtype, size_t A, size_t,   \
                size_t) {                                                            \
            size_t a = 0;                                                            \
            for (; a + Reducer::SIMD_WIDTH < A; a += Reducer::SIMD_WIDTH) {          \
                Reducer reducer(src_dtype, B);                                       \
                auto src_ptr = src + a * B;                                          \
                if (B == 4) {                                                        \
                    GI_##_gi_type##_V4_t data_v4 = GiLoadUzip##_gi_ins##V4(src_ptr); \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V4(data_v4, 0));    \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V4(data_v4, 1));    \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V4(data_v4, 2));    \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V4(data_v4, 3));    \
                }                                                                    \
                if (B == 3) {                                                        \
                    GI_##_gi_type##_V3_t data_v3 = GiLoadUzip##_gi_ins##V3(src_ptr); \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V3(data_v3, 0));    \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V3(data_v3, 1));    \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V3(data_v3, 2));    \
                }                                                                    \
                if (B == 2) {                                                        \
                    GI_##_gi_type##_V2_t data_v2 = GiLoadUzip##_gi_ins##V2(src_ptr); \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V2(data_v2, 0));    \
                    reducer.feed_vector(GiGetSubVector##_gi_ins##V2(data_v2, 1));    \
                }                                                                    \
                reducer.post(dst);                                                   \
                dst += Reducer::SIMD_WIDTH;                                          \
            }                                                                        \
            for (; a < A; a++) {                                                     \
                Reducer reducer(src_dtype, B);                                       \
                auto src_ptr = src + a * B;                                          \
                for (size_t i = 0; i < B; i++)                                       \
                    reducer.feed_remain(src_ptr + i);                                \
                reducer.post_remain(dst);                                            \
                dst++;                                                               \
            }                                                                        \
        }                                                                            \
    }

ImplementC1SmallB(float, FLOAT32, Float32);
ImplementC1SmallB(int8_t, INT8, Int8);

}  // namespace

// vim: syntax=cpp.doxygen
