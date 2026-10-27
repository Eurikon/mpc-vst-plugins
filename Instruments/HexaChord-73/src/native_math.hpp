#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Modified AssemblyScript math routines, licensed under Apache-2.0.
// Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
// Developed at SunPro, a Sun Microsystems, Inc. business.
// Permission to use, copy, modify, and distribute this software
// is freely granted, provided that this notice is preserved.

namespace hexachord::native {

inline constexpr float PI = 3.141592653589793f;

inline float clamp(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}
inline float max(float a, float b) { return a > b ? a : b; }
inline float min(float a, float b) {
    if (a == b) return std::signbit(a) ? a : b;
    return a < b ? a : (std::isnan(a) ? a : b);
}

namespace detail {
template<class Destination, class Source> inline Destination bits(Source value) {
    static_assert(sizeof(Destination) == sizeof(Source));
    Destination result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

inline constexpr std::array<uint64_t, 32> expTable = {
    0x3FF0000000000000, 0x3FEFD9B0D3158574, 0x3FEFB5586CF9890F, 0x3FEF9301D0125B51,
    0x3FEF72B83C7D517B, 0x3FEF54873168B9AA, 0x3FEF387A6E756238, 0x3FEF1E9DF51FDEE1,
    0x3FEF06FE0A31B715, 0x3FEEF1A7373AA9CB, 0x3FEEDEA64C123422, 0x3FEECE086061892D,
    0x3FEEBFDAD5362A27, 0x3FEEB42B569D4F82, 0x3FEEAB07DD485429, 0x3FEEA47EB03A5585,
    0x3FEEA09E667F3BCD, 0x3FEE9F75E8EC5F74, 0x3FEEA11473EB0187, 0x3FEEA589994CCE13,
    0x3FEEACE5422AA0DB, 0x3FEEB737B0CDC5E5, 0x3FEEC49182A3F090, 0x3FEED503B23E255D,
    0x3FEEE89F995AD3AD, 0x3FEEFF76F2FB5E47, 0x3FEF199BDD85529C, 0x3FEF3720DCEF9069,
    0x3FEF5818DCFBA487, 0x3FEF7C97337B9B5F, 0x3FEFA4AFA2A490DA, 0x3FEFD0765B6E4540,
};
inline constexpr std::array<uint64_t, 32> logTable = {
    0x3FF661EC79F8F3BE, 0xBFDEFEC65B963019, 0x3FF571ED4AAF883D, 0xBFDB0B6832D4FCA4,
    0x3FF49539F0F010B0, 0xBFD7418B0A1FB77B, 0x3FF3C995B0B80385, 0xBFD39DE91A6DCF7B,
    0x3FF30D190C8864A5, 0xBFD01D9BF3F2B631, 0x3FF25E227B0B8EA0, 0xBFC97C1D1B3B7AF0,
    0x3FF1BB4A4A1A343F, 0xBFC2F9E393AF3C9F, 0x3FF12358F08AE5BA, 0xBFB960CBBF788D5C,
    0x3FF0953F419900A7, 0xBFAA6F9DB6475FCE, 0x3FF0000000000000, 0,
    0x3FEE608CFD9A47AC, 0x3FB338CA9F24F53D, 0x3FECA4B31F026AA0, 0x3FC476A9543891BA,
    0x3FEB2036576AFCE6, 0x3FCE840B4AC4E4D2, 0x3FE9C2D163A1AA2D, 0x3FD40645F0C6651C,
    0x3FE886E6037841ED, 0x3FD88E9C2C1B9FF8, 0x3FE767DCF5534862, 0x3FDCE0A44EB17BCC,
};

inline bool zeroInfNan(uint32_t word) { return (word << 1) - 1 >= 0xff000000u - 1; }
inline int checkInteger(uint32_t word) {
    const uint32_t exponent = (word >> 23) & 255;
    if (exponent < 127) return 0;
    if (exponent > 150) return 2;
    const uint32_t bit = 1u << (150 - exponent);
    if (word & (bit - 1)) return 0;
    return word & bit ? 1 : 2;
}

inline double log2Positive(uint32_t word) {
    const uint32_t temporary = word - 0x3f330000u;
    const uint32_t index = (temporary >> 19) & 15;
    const uint32_t top = temporary & 0xff800000u;
    const int exponent = static_cast<int32_t>(top) >> 23;
    const double inverse = bits<double>(logTable[index * 2]);
    const double logarithm = bits<double>(logTable[index * 2 + 1]);
    const double z = bits<float>(word - top);
    double r = z * inverse - 1.0;
    const double y0 = logarithm + double(exponent);
    double y = 0x1.27616c9496e0bp-2 * r - 0x1.71969a075c67ap-2;
    const double p = 0x1.ec70a6ca7baddp-2 * r - 0x1.7154748bef6c8p-1;
    double q = 0x1.71547652ab82bp+0 * r + y0;
    r *= r;
    q += p * r;
    y = y * (r * r) + q;
    return y;
}

inline float exp2Positive(double value, uint32_t signBias) {
    constexpr double shift = 0x1.8p+52 / 32.0;
    const double rounded = value + shift;
    const uint64_t integer = bits<uint64_t>(rounded);
    const double residual = value - (rounded - shift);
    const uint64_t scale = expTable[integer & 31] + ((integer + signBias) << 47);
    const double s = bits<double>(scale);
    const double z = 0x1.c6af84b912394p-5 * residual + 0x1.ebfce50fac4f3p-3;
    double y = 0x1.62e42ff0c52d6p-1 * residual + 1.0;
    y += z * (residual * residual);
    y *= s;
    return float(y);
}

inline constexpr std::array<float, 2049> sineTable = {
    // NATIVE_SINE_TABLE_BEGIN
    0x0.000000p-126f, 0x1.921f8cp-9f, 0x1.921f10p-8f, 0x1.2d96b0p-7f, 0x1.921d20p-7f, 0x1.f6a298p-7f,
    0x1.2d936cp-6f, 0x1.5fd4d4p-6f, 0x1.921560p-6f, 0x1.c454f4p-6f, 0x1.f69374p-6f, 0x1.146860p-5f,
    0x1.2d8658p-5f, 0x1.46a398p-5f, 0x1.5fc00ep-5f, 0x1.78dbaap-5f, 0x1.91f660p-5f, 0x1.ab101ep-5f,
    0x1.c428d2p-5f, 0x1.dd4070p-5f, 0x1.f656eap-5f, 0x1.07b616p-4f, 0x1.144014p-4f, 0x1.20c968p-4f,
    0x1.2d520ap-4f, 0x1.39d9f2p-4f, 0x1.466118p-4f, 0x1.52e776p-4f, 0x1.5f6d02p-4f, 0x1.6bf1b4p-4f,
    0x1.787586p-4f, 0x1.84f872p-4f, 0x1.917a6cp-4f, 0x1.9dfb70p-4f, 0x1.aa7b74p-4f, 0x1.b6fa70p-4f,
    0x1.c3785cp-4f, 0x1.cff534p-4f, 0x1.dc70eep-4f, 0x1.e8eb80p-4f, 0x1.f564e6p-4f, 0x1.00ee8ap-3f,
    0x1.072a06p-3f, 0x1.0d64dcp-3f, 0x1.139f0ep-3f, 0x1.19d894p-3f, 0x1.20116ep-3f, 0x1.264996p-3f,
    0x1.2c8106p-3f, 0x1.32b7c0p-3f, 0x1.38edbcp-3f, 0x1.3f22f8p-3f, 0x1.45576cp-3f, 0x1.4b8b18p-3f,
    0x1.51bdfap-3f, 0x1.57f008p-3f, 0x1.5e2146p-3f, 0x1.6451a8p-3f, 0x1.6a8130p-3f, 0x1.70afdap-3f,
    0x1.76dd9ep-3f, 0x1.7d0a7cp-3f, 0x1.83366ep-3f, 0x1.896174p-3f, 0x1.8f8b84p-3f, 0x1.95b49ep-3f,
    0x1.9bdcc0p-3f, 0x1.a203e2p-3f, 0x1.a82a04p-3f, 0x1.ae4f1ep-3f, 0x1.b47330p-3f, 0x1.ba9634p-3f,
    0x1.c0b826p-3f, 0x1.c6d906p-3f, 0x1.ccf8ccp-3f, 0x1.d31776p-3f, 0x1.d93500p-3f, 0x1.df5164p-3f,
    0x1.e56ca2p-3f, 0x1.eb86b4p-3f, 0x1.f19f9ap-3f, 0x1.f7b748p-3f, 0x1.fdcdc2p-3f, 0x1.01f180p-2f,
    0x1.04fb82p-2f, 0x1.0804e0p-2f, 0x1.0b0d9ep-2f, 0x1.0e15b6p-2f, 0x1.111d28p-2f, 0x1.1423eep-2f,
    0x1.172a0ep-2f, 0x1.1a2f80p-2f, 0x1.1d3444p-2f, 0x1.203858p-2f, 0x1.233bbcp-2f, 0x1.263e6ap-2f,
    0x1.294062p-2f, 0x1.2c41a6p-2f, 0x1.2f422ep-2f, 0x1.3241fcp-2f, 0x1.35410cp-2f, 0x1.383f60p-2f,
    0x1.3b3cf2p-2f, 0x1.3e39bep-2f, 0x1.4135cap-2f, 0x1.443110p-2f, 0x1.472b8ap-2f, 0x1.4a253ep-2f,
    0x1.4d1e26p-2f, 0x1.50163ep-2f, 0x1.530d88p-2f, 0x1.560402p-2f, 0x1.58f9a8p-2f, 0x1.5bee78p-2f,
    0x1.5ee274p-2f, 0x1.61d596p-2f, 0x1.64c7dep-2f, 0x1.67b94ap-2f, 0x1.6aa9d8p-2f, 0x1.6d9988p-2f,
    0x1.708854p-2f, 0x1.73763ep-2f, 0x1.766342p-2f, 0x1.794f5ep-2f, 0x1.7c3a94p-2f, 0x1.7f24dep-2f,
    0x1.820e3cp-2f, 0x1.84f6aap-2f, 0x1.87de2cp-2f, 0x1.8ac4bap-2f, 0x1.8daa52p-2f, 0x1.908ef8p-2f,
    0x1.9372a8p-2f, 0x1.96555cp-2f, 0x1.993716p-2f, 0x1.9c17d6p-2f, 0x1.9ef796p-2f, 0x1.a1d654p-2f,
    0x1.a4b414p-2f, 0x1.a790cep-2f, 0x1.aa6c84p-2f, 0x1.ad4732p-2f, 0x1.b020d8p-2f, 0x1.b2f974p-2f,
    0x1.b5d100p-2f, 0x1.b8a782p-2f, 0x1.bb7cf4p-2f, 0x1.be5154p-2f, 0x1.c1249ep-2f, 0x1.c3f6d6p-2f,
    0x1.c6c7f6p-2f, 0x1.c997fcp-2f, 0x1.cc66eap-2f, 0x1.cf34bcp-2f, 0x1.d2016ep-2f, 0x1.d4cd04p-2f,
    0x1.d79776p-2f, 0x1.da60c8p-2f, 0x1.dd28f2p-2f, 0x1.dfeff8p-2f, 0x1.e2b5d4p-2f, 0x1.e57a86p-2f,
    0x1.e83e10p-2f, 0x1.eb0068p-2f, 0x1.edc194p-2f, 0x1.f08190p-2f, 0x1.f3405ap-2f, 0x1.f5fdf0p-2f,
    0x1.f8ba50p-2f, 0x1.fb7578p-2f, 0x1.fe2f64p-2f, 0x1.00740cp-1f, 0x1.01cfc8p-1f, 0x1.032ae6p-1f,
    0x1.048564p-1f, 0x1.05df40p-1f, 0x1.07387ap-1f, 0x1.089112p-1f, 0x1.09e908p-1f, 0x1.0b4058p-1f,
    0x1.0c9706p-1f, 0x1.0ded0cp-1f, 0x1.0f426cp-1f, 0x1.109726p-1f, 0x1.11eb36p-1f, 0x1.133e9ep-1f,
    0x1.14915cp-1f, 0x1.15e36ep-1f, 0x1.1734d6p-1f, 0x1.188592p-1f, 0x1.19d5a2p-1f, 0x1.1b2502p-1f,
    0x1.1c73b4p-1f, 0x1.1dc1b6p-1f, 0x1.1f0f0ap-1f, 0x1.205baap-1f, 0x1.21a79ap-1f, 0x1.22f2d8p-1f,
    0x1.243d60p-1f, 0x1.258734p-1f, 0x1.26d056p-1f, 0x1.2818c0p-1f, 0x1.296074p-1f, 0x1.2aa770p-1f,
    0x1.2bedb4p-1f, 0x1.2d333ep-1f, 0x1.2e780ep-1f, 0x1.2fbc26p-1f, 0x1.30ff80p-1f, 0x1.324220p-1f,
    0x1.338402p-1f, 0x1.34c526p-1f, 0x1.36058ap-1f, 0x1.374532p-1f, 0x1.388418p-1f, 0x1.39c23ep-1f,
    0x1.3affa4p-1f, 0x1.3c3c46p-1f, 0x1.3d7824p-1f, 0x1.3eb33ep-1f, 0x1.3fed96p-1f, 0x1.412726p-1f,
    0x1.425ff2p-1f, 0x1.4397f6p-1f, 0x1.44cf34p-1f, 0x1.4605a6p-1f, 0x1.473b52p-1f, 0x1.487034p-1f,
    0x1.49a44ap-1f, 0x1.4ad796p-1f, 0x1.4c0a16p-1f, 0x1.4d3bc8p-1f, 0x1.4e6cacp-1f, 0x1.4f9cc2p-1f,
    0x1.50cc0ap-1f, 0x1.51fa82p-1f, 0x1.53282ap-1f, 0x1.545500p-1f, 0x1.558104p-1f, 0x1.56ac34p-1f,
    0x1.57d694p-1f, 0x1.59001ep-1f, 0x1.5a28d4p-1f, 0x1.5b50b4p-1f, 0x1.5c77bcp-1f, 0x1.5d9df0p-1f,
    0x1.5ec34ap-1f, 0x1.5fe7ccp-1f, 0x1.610b76p-1f, 0x1.622e46p-1f, 0x1.63503ap-1f, 0x1.647156p-1f,
    0x1.659194p-1f, 0x1.66b0f4p-1f, 0x1.67cf78p-1f, 0x1.68ed1ep-1f, 0x1.6a09e6p-1f, 0x1.6b25d0p-1f,
    0x1.6c40d8p-1f, 0x1.6d5b00p-1f, 0x1.6e7446p-1f, 0x1.6f8caap-1f, 0x1.70a42cp-1f, 0x1.71bacap-1f,
    0x1.72d084p-1f, 0x1.73e55ap-1f, 0x1.74f94ap-1f, 0x1.760c52p-1f, 0x1.771e76p-1f, 0x1.782fb2p-1f,
    0x1.794006p-1f, 0x1.7a4f72p-1f, 0x1.7b5df4p-1f, 0x1.7c6b8ap-1f, 0x1.7d7836p-1f, 0x1.7e83f8p-1f,
    0x1.7f8ecep-1f, 0x1.8098b8p-1f, 0x1.81a1b4p-1f, 0x1.82a9c2p-1f, 0x1.83b0e2p-1f, 0x1.84b712p-1f,
    0x1.85bc52p-1f, 0x1.86c0a2p-1f, 0x1.87c402p-1f, 0x1.88c670p-1f, 0x1.89c7eap-1f, 0x1.8ac874p-1f,
    0x1.8bc806p-1f, 0x1.8cc6a8p-1f, 0x1.8dc454p-1f, 0x1.8ec10ap-1f, 0x1.8fbcccp-1f, 0x1.90b796p-1f,
    0x1.91b168p-1f, 0x1.92aa42p-1f, 0x1.93a224p-1f, 0x1.94990ep-1f, 0x1.958efep-1f, 0x1.9683f4p-1f,
    0x1.9777f0p-1f, 0x1.986af0p-1f, 0x1.995cf4p-1f, 0x1.9a4dfap-1f, 0x1.9b3e04p-1f, 0x1.9c2d12p-1f,
    0x1.9d1b20p-1f, 0x1.9e0830p-1f, 0x1.9ef43ep-1f, 0x1.9fdf50p-1f, 0x1.a0c95ep-1f, 0x1.a1b26ep-1f,
    0x1.a29a7ap-1f, 0x1.a38186p-1f, 0x1.a4678ep-1f, 0x1.a54c92p-1f, 0x1.a63092p-1f, 0x1.a7138ep-1f,
    0x1.a7f586p-1f, 0x1.a8d678p-1f, 0x1.a9b664p-1f, 0x1.aa9548p-1f, 0x1.ab7326p-1f, 0x1.ac4ffcp-1f,
    0x1.ad2bcap-1f, 0x1.ae0690p-1f, 0x1.aee04ap-1f, 0x1.afb8fep-1f, 0x1.b090a6p-1f, 0x1.b16742p-1f,
    0x1.b23cd4p-1f, 0x1.b3115ap-1f, 0x1.b3e4d4p-1f, 0x1.b4b742p-1f, 0x1.b588a0p-1f, 0x1.b658f2p-1f,
    0x1.b72836p-1f, 0x1.b7f66ap-1f, 0x1.b8c38ep-1f, 0x1.b98fa4p-1f, 0x1.ba5aa6p-1f, 0x1.bb249ap-1f,
    0x1.bbed7cp-1f, 0x1.bcb54cp-1f, 0x1.bd7c0ap-1f, 0x1.be41b6p-1f, 0x1.bf064ep-1f, 0x1.bfc9d2p-1f,
    0x1.c08c44p-1f, 0x1.c14d9ep-1f, 0x1.c20de6p-1f, 0x1.c2cd16p-1f, 0x1.c38b30p-1f, 0x1.c44832p-1f,
    0x1.c50420p-1f, 0x1.c5bef6p-1f, 0x1.c678b4p-1f, 0x1.c73158p-1f, 0x1.c7e8e6p-1f, 0x1.c89f58p-1f,
    0x1.c954b2p-1f, 0x1.ca08f2p-1f, 0x1.cabc18p-1f, 0x1.cb6e22p-1f, 0x1.cc1f10p-1f, 0x1.cccee4p-1f,
    0x1.cd7d9ap-1f, 0x1.ce2b32p-1f, 0x1.ced7b0p-1f, 0x1.cf830ep-1f, 0x1.d02d50p-1f, 0x1.d0d674p-1f,
    0x1.d17e78p-1f, 0x1.d2255cp-1f, 0x1.d2cb22p-1f, 0x1.d36fc8p-1f, 0x1.d4134ep-1f, 0x1.d4b5b2p-1f,
    0x1.d556f6p-1f, 0x1.d5f718p-1f, 0x1.d69618p-1f, 0x1.d733f4p-1f, 0x1.d7d0b0p-1f, 0x1.d86c48p-1f,
    0x1.d906bcp-1f, 0x1.d9a00ep-1f, 0x1.da383ap-1f, 0x1.dacf44p-1f, 0x1.db6526p-1f, 0x1.dbf9e4p-1f,
    0x1.dc8d7ep-1f, 0x1.dd1ff0p-1f, 0x1.ddb13cp-1f, 0x1.de4162p-1f, 0x1.ded060p-1f, 0x1.df5e36p-1f,
    0x1.dfeae6p-1f, 0x1.e0766ep-1f, 0x1.e100ccp-1f, 0x1.e18a04p-1f, 0x1.e21210p-1f, 0x1.e298f4p-1f,
    0x1.e31eaep-1f, 0x1.e3a340p-1f, 0x1.e426a6p-1f, 0x1.e4a8e0p-1f, 0x1.e529f2p-1f, 0x1.e5a9d6p-1f,
    0x1.e62890p-1f, 0x1.e6a61cp-1f, 0x1.e7227ep-1f, 0x1.e79db2p-1f, 0x1.e817bap-1f, 0x1.e89096p-1f,
    0x1.e90844p-1f, 0x1.e97ec4p-1f, 0x1.e9f416p-1f, 0x1.ea683ap-1f, 0x1.eadb30p-1f, 0x1.eb4cf6p-1f,
    0x1.ebbd8ep-1f, 0x1.ec2cf6p-1f, 0x1.ec9b2ep-1f, 0x1.ed0836p-1f, 0x1.ed740ep-1f, 0x1.eddeb6p-1f,
    0x1.ee482ep-1f, 0x1.eeb074p-1f, 0x1.ef178ap-1f, 0x1.ef7d6ep-1f, 0x1.efe222p-1f, 0x1.f045a2p-1f,
    0x1.f0a7f0p-1f, 0x1.f1090cp-1f, 0x1.f168f6p-1f, 0x1.f1c7acp-1f, 0x1.f22530p-1f, 0x1.f28180p-1f,
    0x1.f2dc9cp-1f, 0x1.f33686p-1f, 0x1.f38f3ap-1f, 0x1.f3e6bcp-1f, 0x1.f43d08p-1f, 0x1.f49220p-1f,
    0x1.f4e604p-1f, 0x1.f538b2p-1f, 0x1.f58a2cp-1f, 0x1.f5da70p-1f, 0x1.f6297ep-1f, 0x1.f67756p-1f,
    0x1.f6c3f8p-1f, 0x1.f70f64p-1f, 0x1.f7599ap-1f, 0x1.f7a29ap-1f, 0x1.f7ea62p-1f, 0x1.f830f4p-1f,
    0x1.f87650p-1f, 0x1.f8ba74p-1f, 0x1.f8fd60p-1f, 0x1.f93f16p-1f, 0x1.f97f92p-1f, 0x1.f9bed8p-1f,
    0x1.f9fce6p-1f, 0x1.fa39bcp-1f, 0x1.fa7558p-1f, 0x1.faafbcp-1f, 0x1.fae8e8p-1f, 0x1.fb20dcp-1f,
    0x1.fb5798p-1f, 0x1.fb8d18p-1f, 0x1.fbc162p-1f, 0x1.fbf472p-1f, 0x1.fc2648p-1f, 0x1.fc56e4p-1f,
    0x1.fc8648p-1f, 0x1.fcb470p-1f, 0x1.fce160p-1f, 0x1.fd0d16p-1f, 0x1.fd3792p-1f, 0x1.fd60d2p-1f,
    0x1.fd88dap-1f, 0x1.fdafa8p-1f, 0x1.fdd53ap-1f, 0x1.fdf992p-1f, 0x1.fe1cb0p-1f, 0x1.fe3e92p-1f,
    0x1.fe5f3cp-1f, 0x1.fe7ea8p-1f, 0x1.fe9cdap-1f, 0x1.feb9d2p-1f, 0x1.fed590p-1f, 0x1.fef010p-1f,
    0x1.ff0956p-1f, 0x1.ff2162p-1f, 0x1.ff3830p-1f, 0x1.ff4dc6p-1f, 0x1.ff621ep-1f, 0x1.ff753cp-1f,
    0x1.ff871ep-1f, 0x1.ff97c4p-1f, 0x1.ffa730p-1f, 0x1.ffb55ep-1f, 0x1.ffc252p-1f, 0x1.ffce0ap-1f,
    0x1.ffd886p-1f, 0x1.ffe1c6p-1f, 0x1.ffe9ccp-1f, 0x1.fff094p-1f, 0x1.fff622p-1f, 0x1.fffa72p-1f,
    0x1.fffd88p-1f, 0x1.ffff62p-1f, 0x1.000000p0f, 0x1.ffff62p-1f, 0x1.fffd88p-1f, 0x1.fffa72p-1f,
    0x1.fff622p-1f, 0x1.fff094p-1f, 0x1.ffe9ccp-1f, 0x1.ffe1c6p-1f, 0x1.ffd886p-1f, 0x1.ffce0ap-1f,
    0x1.ffc252p-1f, 0x1.ffb55ep-1f, 0x1.ffa72ep-1f, 0x1.ff97c4p-1f, 0x1.ff871ep-1f, 0x1.ff753cp-1f,
    0x1.ff621ep-1f, 0x1.ff4dc6p-1f, 0x1.ff3830p-1f, 0x1.ff2162p-1f, 0x1.ff0956p-1f, 0x1.fef010p-1f,
    0x1.fed58ep-1f, 0x1.feb9d2p-1f, 0x1.fe9cdap-1f, 0x1.fe7ea8p-1f, 0x1.fe5f3ap-1f, 0x1.fe3e92p-1f,
    0x1.fe1cb0p-1f, 0x1.fdf992p-1f, 0x1.fdd53ap-1f, 0x1.fdafa8p-1f, 0x1.fd88dap-1f, 0x1.fd60d2p-1f,
    0x1.fd3790p-1f, 0x1.fd0d16p-1f, 0x1.fce160p-1f, 0x1.fcb470p-1f, 0x1.fc8646p-1f, 0x1.fc56e4p-1f,
    0x1.fc2646p-1f, 0x1.fbf470p-1f, 0x1.fbc162p-1f, 0x1.fb8d18p-1f, 0x1.fb5796p-1f, 0x1.fb20dcp-1f,
    0x1.fae8e8p-1f, 0x1.faafbcp-1f, 0x1.fa7558p-1f, 0x1.fa39bap-1f, 0x1.f9fce6p-1f, 0x1.f9bed8p-1f,
    0x1.f97f92p-1f, 0x1.f93f14p-1f, 0x1.f8fd60p-1f, 0x1.f8ba74p-1f, 0x1.f87650p-1f, 0x1.f830f4p-1f,
    0x1.f7ea62p-1f, 0x1.f7a29ap-1f, 0x1.f7599ap-1f, 0x1.f70f64p-1f, 0x1.f6c3f8p-1f, 0x1.f67756p-1f,
    0x1.f6297cp-1f, 0x1.f5da6ep-1f, 0x1.f58a2ap-1f, 0x1.f538b2p-1f, 0x1.f4e604p-1f, 0x1.f49220p-1f,
    0x1.f43d08p-1f, 0x1.f3e6bcp-1f, 0x1.f38f3ap-1f, 0x1.f33686p-1f, 0x1.f2dc9cp-1f, 0x1.f28180p-1f,
    0x1.f2252ep-1f, 0x1.f1c7acp-1f, 0x1.f168f6p-1f, 0x1.f1090cp-1f, 0x1.f0a7f0p-1f, 0x1.f045a2p-1f,
    0x1.efe220p-1f, 0x1.ef7d6ep-1f, 0x1.ef178ap-1f, 0x1.eeb074p-1f, 0x1.ee482ep-1f, 0x1.eddeb6p-1f,
    0x1.ed740ep-1f, 0x1.ed0834p-1f, 0x1.ec9b2ep-1f, 0x1.ec2cf4p-1f, 0x1.ebbd8cp-1f, 0x1.eb4cf4p-1f,
    0x1.eadb2ep-1f, 0x1.ea6838p-1f, 0x1.e9f414p-1f, 0x1.e97ec2p-1f, 0x1.e90842p-1f, 0x1.e89096p-1f,
    0x1.e817bap-1f, 0x1.e79db2p-1f, 0x1.e7227cp-1f, 0x1.e6a61cp-1f, 0x1.e6288ep-1f, 0x1.e5a9d6p-1f,
    0x1.e529f0p-1f, 0x1.e4a8e0p-1f, 0x1.e426a4p-1f, 0x1.e3a33ep-1f, 0x1.e31eaep-1f, 0x1.e298f4p-1f,
    0x1.e21210p-1f, 0x1.e18a02p-1f, 0x1.e100ccp-1f, 0x1.e0766cp-1f, 0x1.dfeae4p-1f, 0x1.df5e36p-1f,
    0x1.ded060p-1f, 0x1.de4160p-1f, 0x1.ddb13cp-1f, 0x1.dd1feep-1f, 0x1.dc8d7cp-1f, 0x1.dbf9e4p-1f,
    0x1.db6526p-1f, 0x1.dacf42p-1f, 0x1.da383ap-1f, 0x1.d9a00cp-1f, 0x1.d906bcp-1f, 0x1.d86c48p-1f,
    0x1.d7d0aep-1f, 0x1.d733f4p-1f, 0x1.d69618p-1f, 0x1.d5f718p-1f, 0x1.d556f4p-1f, 0x1.d4b5b2p-1f,
    0x1.d4134cp-1f, 0x1.d36fc8p-1f, 0x1.d2cb22p-1f, 0x1.d2255cp-1f, 0x1.d17e78p-1f, 0x1.d0d674p-1f,
    0x1.d02d50p-1f, 0x1.cf830ep-1f, 0x1.ced7b0p-1f, 0x1.ce2b32p-1f, 0x1.cd7d98p-1f, 0x1.cccee2p-1f,
    0x1.cc1f0ep-1f, 0x1.cb6e20p-1f, 0x1.cabc16p-1f, 0x1.ca08f0p-1f, 0x1.c954b2p-1f, 0x1.c89f58p-1f,
    0x1.c7e8e4p-1f, 0x1.c73158p-1f, 0x1.c678b2p-1f, 0x1.c5bef4p-1f, 0x1.c5041ep-1f, 0x1.c44832p-1f,
    0x1.c38b2ep-1f, 0x1.c2cd12p-1f, 0x1.c20de2p-1f, 0x1.c14d9cp-1f, 0x1.c08c40p-1f, 0x1.bfc9d0p-1f,
    0x1.bf064cp-1f, 0x1.be41b6p-1f, 0x1.bd7c0cp-1f, 0x1.bcb54ep-1f, 0x1.bbed7cp-1f, 0x1.bb249ap-1f,
    0x1.ba5aa6p-1f, 0x1.b98fa2p-1f, 0x1.b8c38ep-1f, 0x1.b7f668p-1f, 0x1.b72834p-1f, 0x1.b658f0p-1f,
    0x1.b588a0p-1f, 0x1.b4b740p-1f, 0x1.b3e4d2p-1f, 0x1.b3115ap-1f, 0x1.b23cd4p-1f, 0x1.b16742p-1f,
    0x1.b090a4p-1f, 0x1.afb8fcp-1f, 0x1.aee04ap-1f, 0x1.ae068ep-1f, 0x1.ad2bc8p-1f, 0x1.ac4ffap-1f,
    0x1.ab7322p-1f, 0x1.aa9544p-1f, 0x1.a9b660p-1f, 0x1.a8d674p-1f, 0x1.a7f586p-1f, 0x1.a7138ep-1f,
    0x1.a63092p-1f, 0x1.a54c92p-1f, 0x1.a4678cp-1f, 0x1.a38184p-1f, 0x1.a29a7ap-1f, 0x1.a1b26ep-1f,
    0x1.a0c95ep-1f, 0x1.9fdf4ep-1f, 0x1.9ef43ep-1f, 0x1.9e082ep-1f, 0x1.9d1b1ep-1f, 0x1.9c2d10p-1f,
    0x1.9b3e04p-1f, 0x1.9a4df8p-1f, 0x1.995cf2p-1f, 0x1.986aeep-1f, 0x1.9777eep-1f, 0x1.9683f2p-1f,
    0x1.958efcp-1f, 0x1.94990cp-1f, 0x1.93a222p-1f, 0x1.92aa40p-1f, 0x1.91b164p-1f, 0x1.90b790p-1f,
    0x1.8fbcc6p-1f, 0x1.8ec106p-1f, 0x1.8dc454p-1f, 0x1.8cc6a8p-1f, 0x1.8bc808p-1f, 0x1.8ac872p-1f,
    0x1.89c7eap-1f, 0x1.88c66ep-1f, 0x1.87c400p-1f, 0x1.86c0a2p-1f, 0x1.85bc52p-1f, 0x1.84b710p-1f,
    0x1.83b0e0p-1f, 0x1.82a9c0p-1f, 0x1.81a1b2p-1f, 0x1.8098b6p-1f, 0x1.7f8eccp-1f, 0x1.7e83f6p-1f,
    0x1.7d7834p-1f, 0x1.7c6b88p-1f, 0x1.7b5df0p-1f, 0x1.7a4f6ep-1f, 0x1.794002p-1f, 0x1.782faep-1f,
    0x1.771e72p-1f, 0x1.760c50p-1f, 0x1.74f946p-1f, 0x1.73e556p-1f, 0x1.72d080p-1f, 0x1.71bac6p-1f,
    0x1.70a42cp-1f, 0x1.6f8caap-1f, 0x1.6e7446p-1f, 0x1.6d5b00p-1f, 0x1.6c40d8p-1f, 0x1.6b25cep-1f,
    0x1.6a09e6p-1f, 0x1.68ed1ep-1f, 0x1.67cf78p-1f, 0x1.66b0f4p-1f, 0x1.659192p-1f, 0x1.647154p-1f,
    0x1.635038p-1f, 0x1.622e44p-1f, 0x1.610b74p-1f, 0x1.5fe7cap-1f, 0x1.5ec348p-1f, 0x1.5d9decp-1f,
    0x1.5c77bap-1f, 0x1.5b50b0p-1f, 0x1.5a28d0p-1f, 0x1.59001ap-1f, 0x1.57d690p-1f, 0x1.56ac32p-1f,
    0x1.558100p-1f, 0x1.5454fcp-1f, 0x1.532824p-1f, 0x1.51fa84p-1f, 0x1.50cc0ap-1f, 0x1.4f9cc4p-1f,
    0x1.4e6cacp-1f, 0x1.4d3bc8p-1f, 0x1.4c0a14p-1f, 0x1.4ad796p-1f, 0x1.49a44ap-1f, 0x1.487032p-1f,
    0x1.473b50p-1f, 0x1.4605a6p-1f, 0x1.44cf32p-1f, 0x1.4397f4p-1f, 0x1.425ff0p-1f, 0x1.412724p-1f,
    0x1.3fed94p-1f, 0x1.3eb33cp-1f, 0x1.3d7820p-1f, 0x1.3c3c42p-1f, 0x1.3affa0p-1f, 0x1.39c23ap-1f,
    0x1.388414p-1f, 0x1.37452ep-1f, 0x1.360588p-1f, 0x1.34c520p-1f, 0x1.3383fcp-1f, 0x1.32421ap-1f,
    0x1.30ff7ap-1f, 0x1.2fbc26p-1f, 0x1.2e7810p-1f, 0x1.2d333ep-1f, 0x1.2bedb2p-1f, 0x1.2aa76ep-1f,
    0x1.296072p-1f, 0x1.2818bep-1f, 0x1.26d054p-1f, 0x1.258734p-1f, 0x1.243d5ep-1f, 0x1.22f2d6p-1f,
    0x1.21a798p-1f, 0x1.205ba8p-1f, 0x1.1f0f06p-1f, 0x1.1dc1b4p-1f, 0x1.1c73b2p-1f, 0x1.1b24fep-1f,
    0x1.19d59ep-1f, 0x1.18858ep-1f, 0x1.1734d2p-1f, 0x1.15e36ap-1f, 0x1.149158p-1f, 0x1.133e98p-1f,
    0x1.11eb30p-1f, 0x1.109720p-1f, 0x1.0f4266p-1f, 0x1.0ded06p-1f, 0x1.0c9700p-1f, 0x1.0b405ap-1f,
    0x1.09e908p-1f, 0x1.089112p-1f, 0x1.07387ap-1f, 0x1.05df3ep-1f, 0x1.048562p-1f, 0x1.032ae4p-1f,
    0x1.01cfc8p-1f, 0x1.00740cp-1f, 0x1.fe2f62p-2f, 0x1.fb7572p-2f, 0x1.f8ba4ap-2f, 0x1.f5fdeap-2f,
    0x1.f34056p-2f, 0x1.f0818cp-2f, 0x1.edc190p-2f, 0x1.eb0064p-2f, 0x1.e83e08p-2f, 0x1.e57a80p-2f,
    0x1.e2b5ccp-2f, 0x1.dfefeep-2f, 0x1.dd28e8p-2f, 0x1.da60bcp-2f, 0x1.d7976cp-2f, 0x1.d4ccf8p-2f,
    0x1.d20164p-2f, 0x1.cf34b0p-2f, 0x1.cc66ecp-2f, 0x1.c997fep-2f, 0x1.c6c7f6p-2f, 0x1.c3f6d6p-2f,
    0x1.c1249ep-2f, 0x1.be5152p-2f, 0x1.bb7cf2p-2f, 0x1.b8a780p-2f, 0x1.b5d0fep-2f, 0x1.b2f970p-2f,
    0x1.b020d4p-2f, 0x1.ad472ep-2f, 0x1.aa6c7ep-2f, 0x1.a790c8p-2f, 0x1.a4b40ep-2f, 0x1.a1d64ep-2f,
    0x1.9ef78ep-2f, 0x1.9c17cep-2f, 0x1.993710p-2f, 0x1.965554p-2f, 0x1.93729ep-2f, 0x1.908ef0p-2f,
    0x1.8daa4ap-2f, 0x1.8ac4aep-2f, 0x1.87de20p-2f, 0x1.84f6a0p-2f, 0x1.820e30p-2f, 0x1.7f24d2p-2f,
    0x1.7c3a96p-2f, 0x1.794f60p-2f, 0x1.766342p-2f, 0x1.73763ep-2f, 0x1.708854p-2f, 0x1.6d9986p-2f,
    0x1.6aa9d6p-2f, 0x1.67b948p-2f, 0x1.64c7dcp-2f, 0x1.61d594p-2f, 0x1.5ee270p-2f, 0x1.5bee76p-2f,
    0x1.58f9a4p-2f, 0x1.5603fcp-2f, 0x1.530d82p-2f, 0x1.501638p-2f, 0x1.4d1e1ep-2f, 0x1.4a2536p-2f,
    0x1.472b82p-2f, 0x1.443106p-2f, 0x1.4135c0p-2f, 0x1.3e39b6p-2f, 0x1.3b3ce6p-2f, 0x1.383f54p-2f,
    0x1.354102p-2f, 0x1.3241f0p-2f, 0x1.2f4222p-2f, 0x1.2c4198p-2f, 0x1.294066p-2f, 0x1.263e6cp-2f,
    0x1.233bbcp-2f, 0x1.203858p-2f, 0x1.1d3444p-2f, 0x1.1a2f80p-2f, 0x1.172a0cp-2f, 0x1.1423eep-2f,
    0x1.111d24p-2f, 0x1.0e15b2p-2f, 0x1.0b0d9ap-2f, 0x1.0804dcp-2f, 0x1.04fb7cp-2f, 0x1.01f17cp-2f,
    0x1.fdcdb6p-3f, 0x1.f7b73cp-3f, 0x1.f19f8ap-3f, 0x1.eb86a6p-3f, 0x1.e56c92p-3f, 0x1.df5152p-3f,
    0x1.d934ecp-3f, 0x1.d31762p-3f, 0x1.ccf8b6p-3f, 0x1.c6d8f0p-3f, 0x1.c0b810p-3f, 0x1.ba961cp-3f,
    0x1.b47316p-3f, 0x1.ae4f22p-3f, 0x1.a82a06p-3f, 0x1.a203e4p-3f, 0x1.9bdcc0p-3f, 0x1.95b49ep-3f,
    0x1.8f8b82p-3f, 0x1.896170p-3f, 0x1.83366cp-3f, 0x1.7d0a78p-3f, 0x1.76dd98p-3f, 0x1.70afd2p-3f,
    0x1.6a8128p-3f, 0x1.6451a0p-3f, 0x1.5e213ap-3f, 0x1.57effep-3f, 0x1.51bdecp-3f, 0x1.4b8b0ap-3f,
    0x1.45575cp-3f, 0x1.3f22e6p-3f, 0x1.38edaap-3f, 0x1.32b7aep-3f, 0x1.2c80f4p-3f, 0x1.264980p-3f,
    0x1.201158p-3f, 0x1.19d87ep-3f, 0x1.139ef4p-3f, 0x1.0d64c2p-3f, 0x1.0729eap-3f, 0x1.00ee8ep-3f,
    0x1.f564ecp-4f, 0x1.e8eb84p-4f, 0x1.dc70eep-4f, 0x1.cff532p-4f, 0x1.c3785ap-4f, 0x1.b6fa6ap-4f,
    0x1.aa7b6ap-4f, 0x1.9dfb64p-4f, 0x1.917a60p-4f, 0x1.84f862p-4f, 0x1.787576p-4f, 0x1.6bf1a0p-4f,
    0x1.5f6ceap-4f, 0x1.52e75cp-4f, 0x1.4660fcp-4f, 0x1.39d9d4p-4f, 0x1.2d51eap-4f, 0x1.20c946p-4f,
    0x1.143ff0p-4f, 0x1.07b5eep-4f, 0x1.f65696p-5f, 0x1.dd401ap-5f, 0x1.c42876p-5f, 0x1.ab0fbcp-5f,
    0x1.91f5fap-5f, 0x1.78db42p-5f, 0x1.5fbfa0p-5f, 0x1.46a3a4p-5f, 0x1.2d8660p-5f, 0x1.146862p-5f,
    0x1.f69372p-6f, 0x1.c454ecp-6f, 0x1.92154cp-6f, 0x1.5fd4b6p-6f, 0x1.2d9346p-6f, 0x1.f6a238p-7f,
    0x1.921caep-7f, 0x1.2d962ep-7f, 0x1.921de4p-8f, 0x1.921ce8p-9f, -0x1.777a5cp-24f, -0x1.9222c6p-9f,
    -0x1.9220d2p-8f, -0x1.2d97a4p-7f, -0x1.921e26p-7f, -0x1.f6a3b0p-7f, -0x1.2d9402p-6f, -0x1.5fd572p-6f,
    -0x1.921608p-6f, -0x1.c455a6p-6f, -0x1.f6942ep-6f, -0x1.1468c0p-5f, -0x1.2d86bep-5f, -0x1.46a402p-5f,
    -0x1.5fbffep-5f, -0x1.78dba0p-5f, -0x1.91f658p-5f, -0x1.ab101ap-5f, -0x1.c428d4p-5f, -0x1.dd4078p-5f,
    -0x1.f656f4p-5f, -0x1.07b61ep-4f, -0x1.14401ep-4f, -0x1.20c974p-4f, -0x1.2d5218p-4f, -0x1.39da02p-4f,
    -0x1.46612cp-4f, -0x1.52e78cp-4f, -0x1.5f6d1ap-4f, -0x1.6bf1cep-4f, -0x1.7875a4p-4f, -0x1.84f890p-4f,
    -0x1.917a8ep-4f, -0x1.9dfb94p-4f, -0x1.aa7b98p-4f, -0x1.b6fa98p-4f, -0x1.c37888p-4f, -0x1.cff562p-4f,
    -0x1.dc711cp-4f, -0x1.e8ebb2p-4f, -0x1.f5651ap-4f, -0x1.00eea6p-3f, -0x1.072a02p-3f, -0x1.0d64dap-3f,
    -0x1.139f0cp-3f, -0x1.19d894p-3f, -0x1.20116ep-3f, -0x1.264998p-3f, -0x1.2c810ap-3f, -0x1.32b7c4p-3f,
    -0x1.38edc2p-3f, -0x1.3f22fcp-3f, -0x1.455774p-3f, -0x1.4b8b22p-3f, -0x1.51be02p-3f, -0x1.57f014p-3f,
    -0x1.5e2152p-3f, -0x1.6451b6p-3f, -0x1.6a8140p-3f, -0x1.70afeap-3f, -0x1.76ddb0p-3f, -0x1.7d0a8ep-3f,
    -0x1.833682p-3f, -0x1.896188p-3f, -0x1.8f8b9ap-3f, -0x1.95b4b6p-3f, -0x1.9bdcd8p-3f, -0x1.a203fcp-3f,
    -0x1.a82a1cp-3f, -0x1.ae4f3ap-3f, -0x1.b4732cp-3f, -0x1.ba9632p-3f, -0x1.c0b826p-3f, -0x1.c6d906p-3f,
    -0x1.ccf8cep-3f, -0x1.d31778p-3f, -0x1.d93502p-3f, -0x1.df516ap-3f, -0x1.e56ca8p-3f, -0x1.eb86bcp-3f,
    -0x1.f19fa0p-3f, -0x1.f7b752p-3f, -0x1.fdcdccp-3f, -0x1.01f186p-2f, -0x1.04fb88p-2f, -0x1.0804e8p-2f,
    -0x1.0b0da4p-2f, -0x1.0e15bep-2f, -0x1.111d30p-2f, -0x1.1423f8p-2f, -0x1.172a18p-2f, -0x1.1a2f8ap-2f,
    -0x1.1d3450p-2f, -0x1.203864p-2f, -0x1.233bc6p-2f, -0x1.263e76p-2f, -0x1.294070p-2f, -0x1.2c41a4p-2f,
    -0x1.2f422cp-2f, -0x1.3241fcp-2f, -0x1.35410cp-2f, -0x1.383f60p-2f, -0x1.3b3cf0p-2f, -0x1.3e39c0p-2f,
    -0x1.4135ccp-2f, -0x1.443110p-2f, -0x1.472b8ep-2f, -0x1.4a2542p-2f, -0x1.4d1e28p-2f, -0x1.501642p-2f,
    -0x1.530d8ep-2f, -0x1.560408p-2f, -0x1.58f9aep-2f, -0x1.5bee80p-2f, -0x1.5ee27cp-2f, -0x1.61d59ep-2f,
    -0x1.64c7e6p-2f, -0x1.67b954p-2f, -0x1.6aa9e2p-2f, -0x1.6d9990p-2f, -0x1.70885ep-2f, -0x1.737648p-2f,
    -0x1.76634ep-2f, -0x1.794f6cp-2f, -0x1.7c3aa0p-2f, -0x1.7f24dcp-2f, -0x1.820e3ap-2f, -0x1.84f6aap-2f,
    -0x1.87de2ap-2f, -0x1.8ac4bap-2f, -0x1.8daa54p-2f, -0x1.908efap-2f, -0x1.9372a8p-2f, -0x1.96555ep-2f,
    -0x1.99371ap-2f, -0x1.9c17d8p-2f, -0x1.9ef79ap-2f, -0x1.a1d65ap-2f, -0x1.a4b418p-2f, -0x1.a790d4p-2f,
    -0x1.aa6c8ap-2f, -0x1.ad4738p-2f, -0x1.b020dep-2f, -0x1.b2f97ap-2f, -0x1.b5d10ap-2f, -0x1.b8a78ap-2f,
    -0x1.bb7cfcp-2f, -0x1.be515cp-2f, -0x1.c124a8p-2f, -0x1.c3f6e0p-2f, -0x1.c6c800p-2f, -0x1.c99808p-2f,
    -0x1.cc66f6p-2f, -0x1.cf34bap-2f, -0x1.d2016ep-2f, -0x1.d4cd04p-2f, -0x1.d79776p-2f, -0x1.da60c8p-2f,
    -0x1.dd28f4p-2f, -0x1.dfeff8p-2f, -0x1.e2b5d6p-2f, -0x1.e57a8ap-2f, -0x1.e83e12p-2f, -0x1.eb006ep-2f,
    -0x1.edc19ap-2f, -0x1.f08196p-2f, -0x1.f34060p-2f, -0x1.f5fdf4p-2f, -0x1.f8ba54p-2f, -0x1.fb757ep-2f,
    -0x1.fe2f6cp-2f, -0x1.007410p-1f, -0x1.01cfccp-1f, -0x1.032aeap-1f, -0x1.048568p-1f, -0x1.05df44p-1f,
    -0x1.07387ep-1f, -0x1.089118p-1f, -0x1.09e90ep-1f, -0x1.0b405ep-1f, -0x1.0c9704p-1f, -0x1.0ded0cp-1f,
    -0x1.0f426cp-1f, -0x1.109724p-1f, -0x1.11eb36p-1f, -0x1.133e9ep-1f, -0x1.14915cp-1f, -0x1.15e370p-1f,
    -0x1.1734d8p-1f, -0x1.188594p-1f, -0x1.19d5a2p-1f, -0x1.1b2504p-1f, -0x1.1c73b6p-1f, -0x1.1dc1bap-1f,
    -0x1.1f0f0cp-1f, -0x1.205baep-1f, -0x1.21a79cp-1f, -0x1.22f2dap-1f, -0x1.243d64p-1f, -0x1.258738p-1f,
    -0x1.26d05ap-1f, -0x1.2818c4p-1f, -0x1.296078p-1f, -0x1.2aa774p-1f, -0x1.2bedb8p-1f, -0x1.2d3342p-1f,
    -0x1.2e7814p-1f, -0x1.2fbc2ap-1f, -0x1.30ff80p-1f, -0x1.32421ep-1f, -0x1.338402p-1f, -0x1.34c526p-1f,
    -0x1.36058cp-1f, -0x1.374532p-1f, -0x1.38841ap-1f, -0x1.39c240p-1f, -0x1.3affa4p-1f, -0x1.3c3c46p-1f,
    -0x1.3d7826p-1f, -0x1.3eb340p-1f, -0x1.3fed98p-1f, -0x1.41272ap-1f, -0x1.425ff4p-1f, -0x1.4397f8p-1f,
    -0x1.44cf36p-1f, -0x1.4605aap-1f, -0x1.473b56p-1f, -0x1.487038p-1f, -0x1.49a44ep-1f, -0x1.4ad79ap-1f,
    -0x1.4c0a18p-1f, -0x1.4d3bccp-1f, -0x1.4e6cb0p-1f, -0x1.4f9cc8p-1f, -0x1.50cc10p-1f, -0x1.51fa88p-1f,
    -0x1.53282ap-1f, -0x1.545500p-1f, -0x1.558104p-1f, -0x1.56ac36p-1f, -0x1.57d694p-1f, -0x1.59001ep-1f,
    -0x1.5a28d4p-1f, -0x1.5b50b4p-1f, -0x1.5c77bep-1f, -0x1.5d9df0p-1f, -0x1.5ec34cp-1f, -0x1.5fe7cep-1f,
    -0x1.610b78p-1f, -0x1.622e48p-1f, -0x1.63503ep-1f, -0x1.647158p-1f, -0x1.659196p-1f, -0x1.66b0f8p-1f,
    -0x1.67cf7cp-1f, -0x1.68ed22p-1f, -0x1.6a09eap-1f, -0x1.6b25d4p-1f, -0x1.6c40dcp-1f, -0x1.6d5b04p-1f,
    -0x1.6e744ap-1f, -0x1.6f8caep-1f, -0x1.70a430p-1f, -0x1.71bacap-1f, -0x1.72d084p-1f, -0x1.73e55ap-1f,
    -0x1.74f94ap-1f, -0x1.760c54p-1f, -0x1.771e76p-1f, -0x1.782fb2p-1f, -0x1.794006p-1f, -0x1.7a4f72p-1f,
    -0x1.7b5df4p-1f, -0x1.7c6b8cp-1f, -0x1.7d7838p-1f, -0x1.7e83fap-1f, -0x1.7f8ed0p-1f, -0x1.8098bap-1f,
    -0x1.81a1b6p-1f, -0x1.82a9c4p-1f, -0x1.83b0dep-1f, -0x1.84b710p-1f, -0x1.85bc50p-1f, -0x1.86c0a0p-1f,
    -0x1.87c400p-1f, -0x1.88c66ep-1f, -0x1.89c7e8p-1f, -0x1.8ac872p-1f, -0x1.8bc806p-1f, -0x1.8cc6a6p-1f,
    -0x1.8dc452p-1f, -0x1.8ec10ap-1f, -0x1.8fbccap-1f, -0x1.90b794p-1f, -0x1.91b168p-1f, -0x1.92aa42p-1f,
    -0x1.93a226p-1f, -0x1.949910p-1f, -0x1.958f00p-1f, -0x1.9683f6p-1f, -0x1.9777f0p-1f, -0x1.986af0p-1f,
    -0x1.995cf4p-1f, -0x1.9a4dfcp-1f, -0x1.9b3e06p-1f, -0x1.9c2d14p-1f, -0x1.9d1b22p-1f, -0x1.9e0832p-1f,
    -0x1.9ef442p-1f, -0x1.9fdf52p-1f, -0x1.a0c962p-1f, -0x1.a1b270p-1f, -0x1.a29a7ep-1f, -0x1.a38188p-1f,
    -0x1.a46790p-1f, -0x1.a54c94p-1f, -0x1.a63096p-1f, -0x1.a71392p-1f, -0x1.a7f58ap-1f, -0x1.a8d67cp-1f,
    -0x1.a9b668p-1f, -0x1.aa954cp-1f, -0x1.ab732ap-1f, -0x1.ac5000p-1f, -0x1.ad2bd0p-1f, -0x1.ae0694p-1f,
    -0x1.aee050p-1f, -0x1.afb904p-1f, -0x1.b090acp-1f, -0x1.b16748p-1f, -0x1.b23cdap-1f, -0x1.b31160p-1f,
    -0x1.b3e4dap-1f, -0x1.b4b73ep-1f, -0x1.b5889ep-1f, -0x1.b658f0p-1f, -0x1.b72832p-1f, -0x1.b7f668p-1f,
    -0x1.b8c38cp-1f, -0x1.b98fa0p-1f, -0x1.ba5aa6p-1f, -0x1.bb249ap-1f, -0x1.bbed7cp-1f, -0x1.bcb54cp-1f,
    -0x1.bd7c0ap-1f, -0x1.be41b6p-1f, -0x1.bf064ep-1f, -0x1.bfc9d2p-1f, -0x1.c08c42p-1f, -0x1.c14d9ep-1f,
    -0x1.c20de4p-1f, -0x1.c2cd16p-1f, -0x1.c38b30p-1f, -0x1.c44834p-1f, -0x1.c50422p-1f, -0x1.c5bef6p-1f,
    -0x1.c678b4p-1f, -0x1.c7315ap-1f, -0x1.c7e8e6p-1f, -0x1.c89f5ap-1f, -0x1.c954b4p-1f, -0x1.ca08f4p-1f,
    -0x1.cabc18p-1f, -0x1.cb6e22p-1f, -0x1.cc1f12p-1f, -0x1.cccee4p-1f, -0x1.cd7d9cp-1f, -0x1.ce2b36p-1f,
    -0x1.ced7b2p-1f, -0x1.cf8312p-1f, -0x1.d02d52p-1f, -0x1.d0d676p-1f, -0x1.d17e7ap-1f, -0x1.d22560p-1f,
    -0x1.d2cb26p-1f, -0x1.d36fccp-1f, -0x1.d41350p-1f, -0x1.d4b5b6p-1f, -0x1.d556f8p-1f, -0x1.d5f71ap-1f,
    -0x1.d6961cp-1f, -0x1.d733fap-1f, -0x1.d7d0b4p-1f, -0x1.d86c4cp-1f, -0x1.d906c2p-1f, -0x1.d9a012p-1f,
    -0x1.da383ep-1f, -0x1.dacf48p-1f, -0x1.db6524p-1f, -0x1.dbf9e2p-1f, -0x1.dc8d7cp-1f, -0x1.dd1feep-1f,
    -0x1.ddb13ap-1f, -0x1.de4160p-1f, -0x1.ded05ep-1f, -0x1.df5e36p-1f, -0x1.dfeae6p-1f, -0x1.e0766ep-1f,
    -0x1.e100ccp-1f, -0x1.e18a02p-1f, -0x1.e21210p-1f, -0x1.e298f4p-1f, -0x1.e31eaep-1f, -0x1.e3a33ep-1f,
    -0x1.e426a4p-1f, -0x1.e4a8e0p-1f, -0x1.e529f0p-1f, -0x1.e5a9d6p-1f, -0x1.e62890p-1f, -0x1.e6a61ep-1f,
    -0x1.e7227ep-1f, -0x1.e79db4p-1f, -0x1.e817bcp-1f, -0x1.e89096p-1f, -0x1.e90844p-1f, -0x1.e97ec4p-1f,
    -0x1.e9f416p-1f, -0x1.ea683ap-1f, -0x1.eadb30p-1f, -0x1.eb4cf6p-1f, -0x1.ebbd8ep-1f, -0x1.ec2cf6p-1f,
    -0x1.ec9b2ep-1f, -0x1.ed0838p-1f, -0x1.ed7410p-1f, -0x1.eddeb8p-1f, -0x1.ee4830p-1f, -0x1.eeb076p-1f,
    -0x1.ef178cp-1f, -0x1.ef7d70p-1f, -0x1.efe222p-1f, -0x1.f045a4p-1f, -0x1.f0a7f2p-1f, -0x1.f1090ep-1f,
    -0x1.f168f8p-1f, -0x1.f1c7aep-1f, -0x1.f22532p-1f, -0x1.f28182p-1f, -0x1.f2dc9ep-1f, -0x1.f33688p-1f,
    -0x1.f38f3ep-1f, -0x1.f3e6bep-1f, -0x1.f43d0ap-1f, -0x1.f49220p-1f, -0x1.f4e602p-1f, -0x1.f538b2p-1f,
    -0x1.f58a2ap-1f, -0x1.f5da6ep-1f, -0x1.f6297cp-1f, -0x1.f67756p-1f, -0x1.f6c3f8p-1f, -0x1.f70f64p-1f,
    -0x1.f7599ap-1f, -0x1.f7a29ap-1f, -0x1.f7ea62p-1f, -0x1.f830f4p-1f, -0x1.f87650p-1f, -0x1.f8ba74p-1f,
    -0x1.f8fd60p-1f, -0x1.f93f16p-1f, -0x1.f97f92p-1f, -0x1.f9bed8p-1f, -0x1.f9fce6p-1f, -0x1.fa39bcp-1f,
    -0x1.fa7558p-1f, -0x1.faafbep-1f, -0x1.fae8eap-1f, -0x1.fb20dcp-1f, -0x1.fb5798p-1f, -0x1.fb8d1ap-1f,
    -0x1.fbc162p-1f, -0x1.fbf472p-1f, -0x1.fc2648p-1f, -0x1.fc56e4p-1f, -0x1.fc8648p-1f, -0x1.fcb470p-1f,
    -0x1.fce160p-1f, -0x1.fd0d16p-1f, -0x1.fd3792p-1f, -0x1.fd60d4p-1f, -0x1.fd88dap-1f, -0x1.fdafa8p-1f,
    -0x1.fdd53ap-1f, -0x1.fdf992p-1f, -0x1.fe1cb0p-1f, -0x1.fe3e94p-1f, -0x1.fe5f3cp-1f, -0x1.fe7eaap-1f,
    -0x1.fe9cdcp-1f, -0x1.feb9d4p-1f, -0x1.fed590p-1f, -0x1.fef010p-1f, -0x1.ff0956p-1f, -0x1.ff2162p-1f,
    -0x1.ff3832p-1f, -0x1.ff4dc6p-1f, -0x1.ff621ep-1f, -0x1.ff753cp-1f, -0x1.ff871ep-1f, -0x1.ff97c4p-1f,
    -0x1.ffa72ep-1f, -0x1.ffb55ep-1f, -0x1.ffc252p-1f, -0x1.ffce0ap-1f, -0x1.ffd886p-1f, -0x1.ffe1c6p-1f,
    -0x1.ffe9ccp-1f, -0x1.fff094p-1f, -0x1.fff622p-1f, -0x1.fffa72p-1f, -0x1.fffd88p-1f, -0x1.ffff62p-1f,
    -0x1.000000p0f, -0x1.ffff62p-1f, -0x1.fffd88p-1f, -0x1.fffa72p-1f, -0x1.fff622p-1f, -0x1.fff094p-1f,
    -0x1.ffe9ccp-1f, -0x1.ffe1c6p-1f, -0x1.ffd886p-1f, -0x1.ffce0ap-1f, -0x1.ffc252p-1f, -0x1.ffb55ep-1f,
    -0x1.ffa72ep-1f, -0x1.ff97c4p-1f, -0x1.ff871ep-1f, -0x1.ff753cp-1f, -0x1.ff621ep-1f, -0x1.ff4dc6p-1f,
    -0x1.ff3830p-1f, -0x1.ff2160p-1f, -0x1.ff0956p-1f, -0x1.fef010p-1f, -0x1.fed58ep-1f, -0x1.feb9d2p-1f,
    -0x1.fe9cdap-1f, -0x1.fe7ea8p-1f, -0x1.fe5f3ap-1f, -0x1.fe3e92p-1f, -0x1.fe1cb0p-1f, -0x1.fdf992p-1f,
    -0x1.fdd53ap-1f, -0x1.fdafa6p-1f, -0x1.fd88dap-1f, -0x1.fd60d2p-1f, -0x1.fd3790p-1f, -0x1.fd0d14p-1f,
    -0x1.fce15ep-1f, -0x1.fcb46ep-1f, -0x1.fc8646p-1f, -0x1.fc56e2p-1f, -0x1.fc2646p-1f, -0x1.fbf470p-1f,
    -0x1.fbc162p-1f, -0x1.fb8d1ap-1f, -0x1.fb5798p-1f, -0x1.fb20dcp-1f, -0x1.fae8eap-1f, -0x1.faafbcp-1f,
    -0x1.fa7558p-1f, -0x1.fa39bap-1f, -0x1.f9fce6p-1f, -0x1.f9bed8p-1f, -0x1.f97f92p-1f, -0x1.f93f14p-1f,
    -0x1.f8fd60p-1f, -0x1.f8ba74p-1f, -0x1.f87650p-1f, -0x1.f830f4p-1f, -0x1.f7ea62p-1f, -0x1.f7a29ap-1f,
    -0x1.f7599ap-1f, -0x1.f70f64p-1f, -0x1.f6c3f8p-1f, -0x1.f67754p-1f, -0x1.f6297cp-1f, -0x1.f5da6ep-1f,
    -0x1.f58a2ap-1f, -0x1.f538b2p-1f, -0x1.f4e602p-1f, -0x1.f49220p-1f, -0x1.f43d08p-1f, -0x1.f3e6bap-1f,
    -0x1.f38f3ap-1f, -0x1.f33684p-1f, -0x1.f2dc9cp-1f, -0x1.f2817ep-1f, -0x1.f2252ep-1f, -0x1.f1c7aap-1f,
    -0x1.f168f4p-1f, -0x1.f1090ap-1f, -0x1.f0a7eep-1f, -0x1.f045a0p-1f, -0x1.efe21ep-1f, -0x1.ef7d6cp-1f,
    -0x1.ef1788p-1f, -0x1.eeb072p-1f, -0x1.ee482cp-1f, -0x1.eddeb4p-1f, -0x1.ed740cp-1f, -0x1.ed0834p-1f,
    -0x1.ec9b2ap-1f, -0x1.ec2cf2p-1f, -0x1.ebbd8ap-1f, -0x1.eb4cf2p-1f, -0x1.eadb2cp-1f, -0x1.ea6836p-1f,
    -0x1.e9f412p-1f, -0x1.e97ec4p-1f, -0x1.e90844p-1f, -0x1.e89096p-1f, -0x1.e817bcp-1f, -0x1.e79db4p-1f,
    -0x1.e7227ep-1f, -0x1.e6a61cp-1f, -0x1.e62890p-1f, -0x1.e5a9d6p-1f, -0x1.e529f0p-1f, -0x1.e4a8e0p-1f,
    -0x1.e426a4p-1f, -0x1.e3a33ep-1f, -0x1.e31eaep-1f, -0x1.e298f4p-1f, -0x1.e21210p-1f, -0x1.e18a02p-1f,
    -0x1.e100ccp-1f, -0x1.e0766cp-1f, -0x1.dfeae6p-1f, -0x1.df5e36p-1f, -0x1.ded05ep-1f, -0x1.de4160p-1f,
    -0x1.ddb13ap-1f, -0x1.dd1feep-1f, -0x1.dc8d7cp-1f, -0x1.dbf9e2p-1f, -0x1.db6524p-1f, -0x1.dacf40p-1f,
    -0x1.da3838p-1f, -0x1.d9a00cp-1f, -0x1.d906bap-1f, -0x1.d86c46p-1f, -0x1.d7d0aep-1f, -0x1.d733f2p-1f,
    -0x1.d69614p-1f, -0x1.d5f714p-1f, -0x1.d556f2p-1f, -0x1.d4b5aep-1f, -0x1.d4134ap-1f, -0x1.d36fc4p-1f,
    -0x1.d2cb1ep-1f, -0x1.d22558p-1f, -0x1.d17e74p-1f, -0x1.d0d66ep-1f, -0x1.d02d4cp-1f, -0x1.cf830ap-1f,
    -0x1.ced7aap-1f, -0x1.ce2b2ep-1f, -0x1.cd7d94p-1f, -0x1.cccedep-1f, -0x1.cc1f0ap-1f, -0x1.cb6e1cp-1f,
    -0x1.cabc12p-1f, -0x1.ca08ecp-1f, -0x1.c954acp-1f, -0x1.c89f5ap-1f, -0x1.c7e8e6p-1f, -0x1.c7315ap-1f,
    -0x1.c678b4p-1f, -0x1.c5bef6p-1f, -0x1.c50420p-1f, -0x1.c44834p-1f, -0x1.c38b30p-1f, -0x1.c2cd14p-1f,
    -0x1.c20de4p-1f, -0x1.c14d9ep-1f, -0x1.c08c42p-1f, -0x1.bfc9d2p-1f, -0x1.bf064ep-1f, -0x1.be41b6p-1f,
    -0x1.bd7c0ap-1f, -0x1.bcb54cp-1f, -0x1.bbed7cp-1f, -0x1.bb2498p-1f, -0x1.ba5aa6p-1f, -0x1.b98fa0p-1f,
    -0x1.b8c38cp-1f, -0x1.b7f666p-1f, -0x1.b72832p-1f, -0x1.b658f0p-1f, -0x1.b5889ep-1f, -0x1.b4b73ep-1f,
    -0x1.b3e4d2p-1f, -0x1.b31158p-1f, -0x1.b23cd2p-1f, -0x1.b16740p-1f, -0x1.b090a2p-1f, -0x1.afb8fap-1f,
    -0x1.aee048p-1f, -0x1.ae068cp-1f, -0x1.ad2bc6p-1f, -0x1.ac4ff8p-1f, -0x1.ab7322p-1f, -0x1.aa9544p-1f,
    -0x1.a9b65ep-1f, -0x1.a8d672p-1f, -0x1.a7f580p-1f, -0x1.a71388p-1f, -0x1.a6308cp-1f, -0x1.a54c8cp-1f,
    -0x1.a46786p-1f, -0x1.a3817ep-1f, -0x1.a29a74p-1f, -0x1.a1b266p-1f, -0x1.a0c958p-1f, -0x1.9fdf48p-1f,
    -0x1.9ef438p-1f, -0x1.9e0828p-1f, -0x1.9d1b18p-1f, -0x1.9c2d0ap-1f, -0x1.9b3e06p-1f, -0x1.9a4dfcp-1f,
    -0x1.995cf4p-1f, -0x1.986af0p-1f, -0x1.9777f0p-1f, -0x1.9683f6p-1f, -0x1.958efep-1f, -0x1.94990ep-1f,
    -0x1.93a224p-1f, -0x1.92aa42p-1f, -0x1.91b166p-1f, -0x1.90b794p-1f, -0x1.8fbccap-1f, -0x1.8ec10ap-1f,
    -0x1.8dc452p-1f, -0x1.8cc6a6p-1f, -0x1.8bc806p-1f, -0x1.8ac870p-1f, -0x1.89c7e8p-1f, -0x1.88c66cp-1f,
    -0x1.87c400p-1f, -0x1.86c0a0p-1f, -0x1.85bc50p-1f, -0x1.84b70ep-1f, -0x1.83b0dep-1f, -0x1.82a9bep-1f,
    -0x1.81a1b0p-1f, -0x1.8098b4p-1f, -0x1.7f8ecap-1f, -0x1.7e83f4p-1f, -0x1.7d7832p-1f, -0x1.7c6b86p-1f,
    -0x1.7b5deep-1f, -0x1.7a4f6cp-1f, -0x1.794000p-1f, -0x1.782facp-1f, -0x1.771e70p-1f, -0x1.760c4ep-1f,
    -0x1.74f944p-1f, -0x1.73e554p-1f, -0x1.72d07ep-1f, -0x1.71bac4p-1f, -0x1.70a424p-1f, -0x1.6f8ca2p-1f,
    -0x1.6e743ep-1f, -0x1.6d5af8p-1f, -0x1.6c40d0p-1f, -0x1.6b25c8p-1f, -0x1.6a09dep-1f, -0x1.68ed16p-1f,
    -0x1.67cf70p-1f, -0x1.66b0ecp-1f, -0x1.65918ap-1f, -0x1.64714cp-1f, -0x1.635030p-1f, -0x1.622e48p-1f,
    -0x1.610b78p-1f, -0x1.5fe7cep-1f, -0x1.5ec34ap-1f, -0x1.5d9df0p-1f, -0x1.5c77bcp-1f, -0x1.5b50b4p-1f,
    -0x1.5a28d4p-1f, -0x1.59001ep-1f, -0x1.57d694p-1f, -0x1.56ac36p-1f, -0x1.558104p-1f, -0x1.5454fep-1f,
    -0x1.532828p-1f, -0x1.51fa80p-1f, -0x1.50cc08p-1f, -0x1.4f9cc0p-1f, -0x1.4e6caap-1f, -0x1.4d3bc4p-1f,
    -0x1.4c0a12p-1f, -0x1.4ad792p-1f, -0x1.49a448p-1f, -0x1.487030p-1f, -0x1.473b4ep-1f, -0x1.4605a4p-1f,
    -0x1.44cf2ep-1f, -0x1.4397f2p-1f, -0x1.425feep-1f, -0x1.412722p-1f, -0x1.3fed90p-1f, -0x1.3eb33ap-1f,
    -0x1.3d781ep-1f, -0x1.3c3c40p-1f, -0x1.3aff9ep-1f, -0x1.39c238p-1f, -0x1.388412p-1f, -0x1.37452cp-1f,
    -0x1.360584p-1f, -0x1.34c51ep-1f, -0x1.3383fap-1f, -0x1.324218p-1f, -0x1.30ff78p-1f, -0x1.2fbc1ep-1f,
    -0x1.2e7806p-1f, -0x1.2d3336p-1f, -0x1.2bedaap-1f, -0x1.2aa766p-1f, -0x1.29606ap-1f, -0x1.2818b6p-1f,
    -0x1.26d04cp-1f, -0x1.25872cp-1f, -0x1.243d56p-1f, -0x1.22f2ccp-1f, -0x1.21a790p-1f, -0x1.205ba0p-1f,
    -0x1.1f0efep-1f, -0x1.1dc1b8p-1f, -0x1.1c73b6p-1f, -0x1.1b2504p-1f, -0x1.19d5a2p-1f, -0x1.188594p-1f,
    -0x1.1734d8p-1f, -0x1.15e36ep-1f, -0x1.14915cp-1f, -0x1.133e9ep-1f, -0x1.11eb36p-1f, -0x1.109724p-1f,
    -0x1.0f426cp-1f, -0x1.0ded0ap-1f, -0x1.0c9704p-1f, -0x1.0b4058p-1f, -0x1.09e906p-1f, -0x1.089110p-1f,
    -0x1.073878p-1f, -0x1.05df3cp-1f, -0x1.048560p-1f, -0x1.032ae2p-1f, -0x1.01cfc6p-1f, -0x1.00740ap-1f,
    -0x1.fe2f5ep-2f, -0x1.fb756ep-2f, -0x1.f8ba46p-2f, -0x1.f5fde6p-2f, -0x1.f34050p-2f, -0x1.f08186p-2f,
    -0x1.edc18ap-2f, -0x1.eb005ep-2f, -0x1.e83e04p-2f, -0x1.e57a7ap-2f, -0x1.e2b5c6p-2f, -0x1.dfefeap-2f,
    -0x1.dd28e4p-2f, -0x1.da60b8p-2f, -0x1.d79766p-2f, -0x1.d4ccf4p-2f, -0x1.d2015ep-2f, -0x1.cf34aap-2f,
    -0x1.cc66d8p-2f, -0x1.c997eap-2f, -0x1.c6c7e2p-2f, -0x1.c3f6c2p-2f, -0x1.c1248ap-2f, -0x1.be513ep-2f,
    -0x1.bb7cdep-2f, -0x1.b8a76cp-2f, -0x1.b5d0ecp-2f, -0x1.b2f95cp-2f, -0x1.b020c0p-2f, -0x1.ad471ap-2f,
    -0x1.aa6c6cp-2f, -0x1.a790b6p-2f, -0x1.a4b416p-2f, -0x1.a1d658p-2f, -0x1.9ef798p-2f, -0x1.9c17d8p-2f,
    -0x1.993718p-2f, -0x1.96555ep-2f, -0x1.9372a8p-2f, -0x1.908ef8p-2f, -0x1.8daa54p-2f, -0x1.8ac4b8p-2f,
    -0x1.87de2ap-2f, -0x1.84f6aap-2f, -0x1.820e3ap-2f, -0x1.7f24dap-2f, -0x1.7c3a90p-2f, -0x1.794f5ap-2f,
    -0x1.76633cp-2f, -0x1.737638p-2f, -0x1.70884ep-2f, -0x1.6d9980p-2f, -0x1.6aa9d2p-2f, -0x1.67b942p-2f,
    -0x1.64c7d6p-2f, -0x1.61d58ep-2f, -0x1.5ee26ap-2f, -0x1.5bee70p-2f, -0x1.58f99ep-2f, -0x1.5603f6p-2f,
    -0x1.530d7ep-2f, -0x1.501632p-2f, -0x1.4d1e18p-2f, -0x1.4a2530p-2f, -0x1.472b7ep-2f, -0x1.443100p-2f,
    -0x1.4135bcp-2f, -0x1.3e39b0p-2f, -0x1.3b3ce0p-2f, -0x1.383f4ep-2f, -0x1.3540fcp-2f, -0x1.3241eap-2f,
    -0x1.2f421cp-2f, -0x1.2c4192p-2f, -0x1.294050p-2f, -0x1.263e56p-2f, -0x1.233ba6p-2f, -0x1.203844p-2f,
    -0x1.1d342ep-2f, -0x1.1a2f6ap-2f, -0x1.1729f8p-2f, -0x1.1423d8p-2f, -0x1.111d0ep-2f, -0x1.0e159cp-2f,
    -0x1.0b0d84p-2f, -0x1.0804c8p-2f, -0x1.04fb68p-2f, -0x1.01f184p-2f, -0x1.fdcdcap-3f, -0x1.f7b74ep-3f,
    -0x1.f19f9ep-3f, -0x1.eb86bap-3f, -0x1.e56ca6p-3f, -0x1.df5166p-3f, -0x1.d93500p-3f, -0x1.d31774p-3f,
    -0x1.ccf8cap-3f, -0x1.c6d904p-3f, -0x1.c0b824p-3f, -0x1.ba962ep-3f, -0x1.b4732ap-3f, -0x1.ae4f16p-3f,
    -0x1.a829fap-3f, -0x1.a203d8p-3f, -0x1.9bdcb4p-3f, -0x1.95b494p-3f, -0x1.8f8b78p-3f, -0x1.896164p-3f,
    -0x1.833660p-3f, -0x1.7d0a6cp-3f, -0x1.76dd8cp-3f, -0x1.70afc6p-3f, -0x1.6a811cp-3f, -0x1.645194p-3f,
    -0x1.5e212ep-3f, -0x1.57eff2p-3f, -0x1.51bde0p-3f, -0x1.4b8afep-3f, -0x1.455750p-3f, -0x1.3f22dap-3f,
    -0x1.38ed9ep-3f, -0x1.32b7a2p-3f, -0x1.2c80e8p-3f, -0x1.264974p-3f, -0x1.20114cp-3f, -0x1.19d872p-3f,
    -0x1.139eeap-3f, -0x1.0d64b6p-3f, -0x1.0729dep-3f, -0x1.00ee64p-3f, -0x1.f56494p-4f, -0x1.e8eb2cp-4f,
    -0x1.dc7098p-4f, -0x1.cff4dcp-4f, -0x1.c37802p-4f, -0x1.b6fa12p-4f, -0x1.aa7b14p-4f, -0x1.9dfb0ep-4f,
    -0x1.917a08p-4f, -0x1.84f80cp-4f, -0x1.78751ep-4f, -0x1.6bf14ap-4f, -0x1.5f6c94p-4f, -0x1.52e784p-4f,
    -0x1.466126p-4f, -0x1.39d9fcp-4f, -0x1.2d5212p-4f, -0x1.20c96ep-4f, -0x1.144018p-4f, -0x1.07b616p-4f,
    -0x1.f656e8p-5f, -0x1.dd406ap-5f, -0x1.c428c8p-5f, -0x1.ab100ep-5f, -0x1.91f64cp-5f, -0x1.78db92p-5f,
    -0x1.5fbff0p-5f, -0x1.46a376p-5f, -0x1.2d8632p-5f, -0x1.146834p-5f, -0x1.f69314p-6f, -0x1.c4548ep-6f,
    -0x1.9214eep-6f, -0x1.5fd458p-6f, -0x1.2d92e8p-6f, -0x1.f6a17cp-7f, -0x1.921bf4p-7f, -0x1.2d9572p-7f,
    -0x1.921c6cp-8f, -0x1.9219f8p-9f, 0x1.777a5cp-23f,
    // NATIVE_SINE_TABLE_END
};
}

inline float exp(float value) {
    const uint32_t word = detail::bits<uint32_t>(value);
    const uint32_t magnitude = (word >> 20) & 0x7ff;
    if (magnitude >= 0x42b) {
        if (word == 0xff800000u) return 0.f;
        if (magnitude >= 0x7f8) return value + value;
        if (value > detail::bits<float>(uint32_t(0x42b17217))) return value * 0x1p+127f;
        if (value < detail::bits<float>(uint32_t(0xc2cff1b4))) return 0.f;
    }
    constexpr double shift = 0x1.8p+52;
    const double z = (0x1.71547652b82fep+0 * 32.0) * double(value);
    const double rounded = z + shift;
    const uint64_t integer = detail::bits<uint64_t>(rounded);
    const double residual = z - (rounded - shift);
    const double s = detail::bits<double>(detail::expTable[integer & 31] + (integer << 47));
    const double polynomial = (0x1.c6af84b912394p-5 / 32.0 / 32.0 / 32.0) * residual +
                              (0x1.ebfce50fac4f3p-3 / 32.0 / 32.0);
    double result = (0x1.62e42ff0c52d6p-1 / 32.0) * residual + 1.0;
    result += polynomial * (residual * residual);
    result *= s;
    return float(result);
}

inline float pow(float x, float y) {
    if (std::fabs(y) <= 2.f) {
        if (y == 2.f) return x * x;
        if (y == .5f) return x != -std::numeric_limits<float>::infinity() ? std::fabs(std::sqrt(x)) :
                              std::numeric_limits<float>::infinity();
        if (y == -1.f) return 1.f / x;
        if (y == 1.f) return x;
        if (y == 0.f) return 1.f;
    }
    uint32_t signBias = 0;
    uint32_t ix = detail::bits<uint32_t>(x);
    const uint32_t iy = detail::bits<uint32_t>(y);
    const bool specialY = detail::zeroInfNan(iy);
    if (ix - 0x00800000u >= 0x7f800000u - 0x00800000u || specialY) {
        if (specialY) {
            if ((iy << 1) == 0) return 1.f;
            if (ix == 0x3f800000u) return std::numeric_limits<float>::quiet_NaN();
            if ((ix << 1) > 0xff000000u || (iy << 1) > 0xff000000u) return x + y;
            if ((ix << 1) == 0x7f000000u) return std::numeric_limits<float>::quiet_NaN();
            if (((ix << 1) < 0x7f000000u) == !(iy >> 31)) return 0.f;
            return y * y;
        }
        if (detail::zeroInfNan(ix)) {
            float squared = x * x;
            if ((ix >> 31) && detail::checkInteger(iy) == 1) squared = -squared;
            return static_cast<int32_t>(iy) < 0 ? 1.f / squared : squared;
        }
        if (static_cast<int32_t>(ix) < 0) {
            const int integer = detail::checkInteger(iy);
            if (integer == 0) return (x - x) / (x - x);
            if (integer == 1) signBias = 1u << 16;
            ix &= 0x7fffffffu;
        }
        if (ix < 0x00800000u) {
            ix = detail::bits<uint32_t>(x * 0x1p+23f) & 0x7fffffffu;
            ix -= 23u << 23;
        }
    }
    const double exponent = double(y) * detail::log2Positive(ix);
    if (((detail::bits<uint64_t>(exponent) >> 47) & 0xffffu) >= 0x80bf) {
        if (exponent > 0x1.fffffffd1d571p+6) return (signBias ? -0x1p+97f : 0x1p+97f) * 0x1p+97f;
        if (exponent <= -150.0) return (signBias ? -0x1p-95f : 0x1p-95f) * 0x1p-95f;
    }
    return detail::exp2Positive(exponent, signBias);
}

inline float sineUnit(float phase) {
    const float position = phase * 2048.f;
    const int32_t index = int32_t(position);
    const float fraction = position - float(index);
    const uint32_t offset = uint32_t(index) & 2047;
    const float first = detail::sineTable[offset];
    return first + (detail::sineTable[offset + 1] - first) * fraction;
}
inline float sineCycle(float phase) {
    phase -= float(int32_t(phase));
    if (phase < 0.f) phase += 1.f;
    return sineUnit(phase);
}
inline float sinePm(float phase) {
    if (phase >= 1.f) phase -= 1.f;
    else if (phase < 0.f) phase += 1.f;
    return sineUnit(phase);
}
inline float softClip(float value) {
    const float x = clamp(value, -3.f, 3.f);
    const float square = x * x;
    return clamp(x * (27.f + square) / (27.f + 9.f * square), -1.f, 1.f);
}
inline float blep(float phase, float dt) {
    if (phase < dt) {
        const float t = phase / dt;
        return t + t - t * t - 1.f;
    }
    if (phase > 1.f - dt) {
        const float t = (phase - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}
inline float polyBlepPulse(float phase, float dt, float width) {
    float edge = phase - width;
    if (edge < 0.f) edge += 1.f;
    return (phase < width ? 1.f : -1.f) + blep(phase, dt) - blep(edge, dt);
}
inline float polyBlepSaw(float phase, float dt) {
    float out = 2.f * phase - 1.f;
    if (phase < dt) {
        const float t = phase / dt;
        out -= t + t - t * t - 1.f;
    } else if (phase > 1.f - dt) {
        const float t = (phase - 1.f) / dt;
        out -= t * t + t + t + 1.f;
    }
    return out;
}
namespace detail {
inline constexpr std::array<float, 188> integerNoteFrequencies = {
    // NATIVE_FREQUENCY_TABLE_BEGIN
    0x1.05a024p0f, 0x1.152ebcp0f, 0x1.25aa32p0f, 0x1.372082p0f, 0x1.49a0a2p0f, 0x1.5d3a72p0f,
    0x1.71fe92p0f, 0x1.87fed0p0f, 0x1.9f4e08p0f, 0x1.b80000p0f, 0x1.d229f2p0f, 0x1.ede21cp0f,
    0x1.05a024p1f, 0x1.152ec2p1f, 0x1.25aa2cp1f, 0x1.372082p1f, 0x1.49a0aap1f, 0x1.5d3a6cp1f,
    0x1.71fe92p1f, 0x1.87fed8p1f, 0x1.9f4dfcp1f, 0x1.b80000p1f, 0x1.d229f2p1f, 0x1.ede21cp1f,
    0x1.05a024p2f, 0x1.152ec2p2f, 0x1.25aa2cp2f, 0x1.372082p2f, 0x1.49a0aap2f, 0x1.5d3a6cp2f,
    0x1.71fe92p2f, 0x1.87fed8p2f, 0x1.9f4dfcp2f, 0x1.b80000p2f, 0x1.d229f2p2f, 0x1.ede21cp2f,
    0x1.05a024p3f, 0x1.152ec2p3f, 0x1.25aa2cp3f, 0x1.372082p3f, 0x1.49a0aap3f, 0x1.5d3a6cp3f,
    0x1.71fe92p3f, 0x1.87fed8p3f, 0x1.9f4dfcp3f, 0x1.b80000p3f, 0x1.d229f2p3f, 0x1.ede21cp3f,
    0x1.05a024p4f, 0x1.152ec2p4f, 0x1.25aa2cp4f, 0x1.372082p4f, 0x1.49a0aap4f, 0x1.5d3a6cp4f,
    0x1.71fe92p4f, 0x1.87fed8p4f, 0x1.9f4dfcp4f, 0x1.b80000p4f, 0x1.d229eap4f, 0x1.ede224p4f,
    0x1.05a024p5f, 0x1.152ec0p5f, 0x1.25aa30p5f, 0x1.372082p5f, 0x1.49a0a6p5f, 0x1.5d3a6ep5f,
    0x1.71fe92p5f, 0x1.87fed2p5f, 0x1.9f4e02p5f, 0x1.b80000p5f, 0x1.d229eap5f, 0x1.ede224p5f,
    0x1.05a024p6f, 0x1.152ec0p6f, 0x1.25aa30p6f, 0x1.372082p6f, 0x1.49a0a6p6f, 0x1.5d3a6ep6f,
    0x1.71fe92p6f, 0x1.87fed2p6f, 0x1.9f4e02p6f, 0x1.b80000p6f, 0x1.d229eep6f, 0x1.ede220p6f,
    0x1.05a024p7f, 0x1.152ec2p7f, 0x1.25aa2ep7f, 0x1.372082p7f, 0x1.49a0a8p7f, 0x1.5d3a6ep7f,
    0x1.71fe92p7f, 0x1.87fed6p7f, 0x1.9f4e00p7f, 0x1.b80000p7f, 0x1.d229eap7f, 0x1.ede220p7f,
    0x1.05a024p8f, 0x1.152ec2p8f, 0x1.25aa2ep8f, 0x1.372082p8f, 0x1.49a0a8p8f, 0x1.5d3a6ep8f,
    0x1.71fe92p8f, 0x1.87fed4p8f, 0x1.9f4e00p8f, 0x1.b80000p8f, 0x1.d229eep8f, 0x1.ede220p8f,
    0x1.05a024p9f, 0x1.152ec2p9f, 0x1.25aa2ep9f, 0x1.372082p9f, 0x1.49a0a8p9f, 0x1.5d3a6ep9f,
    0x1.71fe92p9f, 0x1.87fed4p9f, 0x1.9f4e00p9f, 0x1.b80000p9f, 0x1.d229eep9f, 0x1.ede220p9f,
    0x1.05a024p10f, 0x1.152ec2p10f, 0x1.25aa2ep10f, 0x1.372082p10f, 0x1.49a0a8p10f, 0x1.5d3a6ep10f,
    0x1.71fe92p10f, 0x1.87fed6p10f, 0x1.9f4e00p10f, 0x1.b80000p10f, 0x1.d229eap10f, 0x1.ede224p10f,
    0x1.05a024p11f, 0x1.152ec0p11f, 0x1.25aa30p11f, 0x1.372082p11f, 0x1.49a0a6p11f, 0x1.5d3a6ep11f,
    0x1.71fe92p11f, 0x1.87fed2p11f, 0x1.9f4e02p11f, 0x1.b80000p11f, 0x1.d229eap11f, 0x1.ede224p11f,
    0x1.05a024p12f, 0x1.152ec0p12f, 0x1.25aa30p12f, 0x1.372082p12f, 0x1.49a0a6p12f, 0x1.5d3a6ep12f,
    0x1.71fe92p12f, 0x1.87fed2p12f, 0x1.9f4e02p12f, 0x1.b80000p12f, 0x1.d229f2p12f, 0x1.ede21cp12f,
    0x1.05a024p13f, 0x1.152ec2p13f, 0x1.25aa2cp13f, 0x1.372082p13f, 0x1.49a0aap13f, 0x1.5d3a6cp13f,
    0x1.71fe92p13f, 0x1.87fed8p13f, 0x1.9f4dfcp13f, 0x1.b80000p13f, 0x1.d229f2p13f, 0x1.ede21cp13f,
    0x1.05a024p14f, 0x1.152ec2p14f, 0x1.25aa2cp14f, 0x1.372082p14f, 0x1.49a0aap14f, 0x1.5d3a6cp14f,
    0x1.71fe92p14f, 0x1.87fed8p14f, 0x1.9f4dfcp14f, 0x1.b80000p14f, 0x1.d229f2p14f, 0x1.ede21cp14f,
    0x1.05a024p15f, 0x1.152ec2p15f, 0x1.25aa2cp15f, 0x1.372082p15f, 0x1.49a0aap15f, 0x1.5d3a6cp15f,
    0x1.71fe92p15f, 0x1.87fed8p15f,
    // NATIVE_FREQUENCY_TABLE_END
};
}
inline float noteToHz(float note) {
    if (note >= -36.f && note <= 151.f) {
        const int integer = int(note);
        if (float(integer) == note) return detail::integerNoteFrequencies[integer + 36];
    }
    return 440.f * pow(2.f, (note - 69.f) / 12.f);
}
inline float dbToLinear(float db) { return pow(10.f, db * .05f); }
inline float smoothCoeff(float timeMs, float sampleRate) {
    return timeMs <= 0.f ? 0.f : exp(-1000.f / (timeMs * sampleRate));
}
}
