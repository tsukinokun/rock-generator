//----------------------------------------------------------------------------
//! @file   CancelToken.hpp
//! @brief  長い処理の中断と進捗の通知
//! @detail RockCore はスレッドを作りません。ワーカースレッドへ載せるのは
//!         呼び出し側（RockEditor の JobRunner）の仕事で、コアは
//!         「中断を問い合わせる」「進捗を報告する」口だけを持ちます。
//----------------------------------------------------------------------------
#pragma once
#include <atomic>
#include <functional>
#include <string_view>

// 名前空間 RockCore
namespace RockCore {

    //! 進捗の通知。第1引数は 0〜1 の進み具合、第2引数は現在の作業名
    using ProgressCallback = std::function<void(float, std::string_view)>;

    //------------------------------------------------------------------------
    //! @class CancelToken
    //! 中断要求を伝えるためのフラグ
    //------------------------------------------------------------------------
    class CancelToken {
    public:
        //! 中断を要求します。別スレッドから呼んで構いません。
        void Cancel() { m_cancelled.store(true, std::memory_order_relaxed); }

        //! 中断が要求されているかを返します。
        //! @return 要求されていれば true
        bool IsCancelled() const { return m_cancelled.load(std::memory_order_relaxed); }

        //! 中断フラグを下ろします。処理を始める直前に呼びます。
        void Reset() { m_cancelled.store(false, std::memory_order_relaxed); }

    private:
        std::atomic<bool> m_cancelled{false};
    };

    //------------------------------------------------------------------------
    //! 進捗を報告します。コールバックが空でも安全に呼べます。
    //! @param [in] callback 進捗の通知先
    //! @param [in] ratio    0〜1 の進み具合
    //! @param [in] label    現在の作業名
    //------------------------------------------------------------------------
    inline void ReportProgress(const ProgressCallback& callback, float ratio, std::string_view label) {
        if(callback) {
            callback(ratio, label);
        }
    }

}    // namespace RockCore
