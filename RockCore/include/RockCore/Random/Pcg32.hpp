//----------------------------------------------------------------------------
//! @file   Pcg32.hpp
//! @brief  PCG32 乱数生成器とシードの派生
//! @detail std::rand はグローバル状態を持ち、実装によって系列が変わるため使いません。
//!         RockCore は「RockParams と seed が同じなら必ず同じ岩になる」ことを
//!         不変条件にしているので、乱数は自前で閉じた実装を持ちます。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Types.hpp>

// 名前空間 RockCore
namespace RockCore {

    //------------------------------------------------------------------------
    //! @class Pcg32
    //! PCG-XSH-RR 32bit 出力の乱数生成器
    //------------------------------------------------------------------------
    class Pcg32 {
    public:
        //! 既定のストリーム定数でシードします。
        //! @param [in] seed 初期シード
        explicit Pcg32(u64 seed) : m_state(0), m_inc(kDefaultStream) {
            NextU32();
            m_state += seed;
            NextU32();
        }

        //! 次の 32bit 乱数を取り出します。
        //! @return 0 〜 0xFFFFFFFF の乱数
        u32 NextU32() {
            const u64 old = m_state;
            m_state       = old * kMultiplier + m_inc;

            const u32 xorshifted = static_cast<u32>(((old >> 18) ^ old) >> 27);
            const u32 rot        = static_cast<u32>(old >> 59);
            return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
        }

        //! [0, 1) の浮動小数を取り出します。
        //! @return 0 以上 1 未満の乱数
        float NextFloat() {
            // 上位24bitだけ使う。float の仮数が24bitなので下位は落ちて無駄になる
            return static_cast<float>(NextU32() >> 8) * (1.0f / 16777216.0f);
        }

        //! [-1, 1] の浮動小数を取り出します。
        //! @return -1 以上 1 以下の乱数
        float NextSigned() { return NextFloat() * 2.0f - 1.0f; }

        //! [0, bound) の整数を取り出します。
        //! @param  [in] bound 上限（これ自身は含まない）
        //! @return 0 以上 bound 未満の乱数
        u32 NextBelow(u32 bound) {
            // 剰余のバイアスを捨てる（棄却法）。bound が 2 の冪でないときに偏るため
            const u32 threshold = (~bound + 1u) % bound;
            for(;;) {
                const u32 value = NextU32();
                if(value >= threshold) {
                    return value % bound;
                }
            }
        }

    private:
        static constexpr u64 kMultiplier    = 6364136223846793005ULL;
        static constexpr u64 kDefaultStream = 1442695040888963407ULL;

        u64 m_state;
        u64 m_inc;
    };

    //------------------------------------------------------------------------
    //! シードから、用途ごとの独立したサブシードを派生させます。
    //!
    //! ノイズの各層はこれで固定のサブシードを受け取ります。層ごとに同じ
    //! Pcg32 を共有すると、ある層のオクターブ数を1つ増やしただけで後続の層の
    //! 乱数列がずれ、触っていないスライダの結果まで変わってしまいます。
    //!
    //! @param  [in] seed  元のシード
    //! @param  [in] index 用途を区別する番号（層番号など）
    //! @return 派生したシード
    //------------------------------------------------------------------------
    constexpr u64 DeriveSeed(u64 seed, u32 index) {
        // SplitMix64 の finalizer。index を混ぜてから撹拌する
        u64 value = seed + 0x9E3779B97F4A7C15ULL * (static_cast<u64>(index) + 1ULL);
        value     = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
        value     = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31);
    }

}    // namespace RockCore
