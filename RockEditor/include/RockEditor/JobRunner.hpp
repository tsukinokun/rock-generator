//----------------------------------------------------------------------------
//! @file   JobRunner.hpp
//! @brief  重い生成処理をワーカースレッドへ逃がすもの
//! @detail RockCore はスレッドを作らず、進捗と中断の口だけを持っています。
//!         実際にスレッドへ載せるのはここの仕事です。
//!
//!         Normal ベイクは 1024x1024 でおよそ 0.5〜1 秒かかります。
//!         メインスレッドで回すとスライダを動かすたびに画面が固まるため、
//!         Phase 1 の時点から分けてあります。
//----------------------------------------------------------------------------
#pragma once
#include <RockCore/Pipeline/CancelToken.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! @class JobRunner
    //! 常に1本だけ走るワーカースレッド
    //------------------------------------------------------------------------
    class JobRunner {
    public:
        //! 仕事の本体。進捗の通知先と中断フラグを受け取る
        using Work = std::function<void(const RockCore::ProgressCallback&, const RockCore::CancelToken*)>;

        JobRunner() = default;
        ~JobRunner();

        JobRunner(const JobRunner&)            = delete;
        JobRunner& operator=(const JobRunner&) = delete;

        //------------------------------------------------------------------
        //! 仕事を投げます。
        //!
        //! 既に走っているときは何もしません（呼び出し側が IsBusy を見て
        //! RequestCancel してから投げ直す形を想定）。
        //!
        //! @param [in] work 仕事の本体
        //------------------------------------------------------------------
        void Submit(Work work);

        //! 走っているかを返します。
        //! @return 走っていれば true
        bool IsBusy() const { return m_busy.load(std::memory_order_acquire); }

        //------------------------------------------------------------------
        //! 終わったスレッドを回収します。毎フレーム呼びます。
        //!
        //! @return このフレームで仕事が完了したら true
        //------------------------------------------------------------------
        bool PollCompletion();

        //! 中断を要求します。実際に止まるのは次の確認点です。
        void RequestCancel() { m_cancel.Cancel(); }

        //! 進捗を返します。
        //! @return 0〜1 の進み具合
        float GetProgress() const { return m_progress.load(std::memory_order_relaxed); }

        //! 現在の作業名を返します。
        //! @return 作業名
        std::string GetLabel() const;

    private:
        //! @note 【この宣言順は破棄順序の設計であり、並べ替えてはならない】
        //!       m_thread のデストラクタ（= join 待ち）より先に
        //!       m_cancel や m_busy が消えてはならないため、
        //!       スレッドは最後に宣言する
        RockCore::CancelToken m_cancel;

        std::atomic<bool>  m_busy{false};
        std::atomic<float> m_progress{0.0f};

        mutable std::mutex m_labelMutex;
        std::string        m_label;

        std::thread m_thread;
    };

}    // namespace RockEditor
