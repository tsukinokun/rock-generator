//----------------------------------------------------------------------------
//! @file   JobRunner.cpp
//! @brief  ワーカースレッドの実装
//----------------------------------------------------------------------------
#include <RockEditor/JobRunner.hpp>

// 名前空間 RockEditor
namespace RockEditor {

    //------------------------------------------------------------------------
    //! 後片付けします。
    //------------------------------------------------------------------------
    JobRunner::~JobRunner() {
        // 走ったままプロセスを畳むと、ワーカーが触っているデータが先に
        // 消えて落ちる。必ず中断を伝えてから待つ
        m_cancel.Cancel();
        if(m_thread.joinable()) {
            m_thread.join();
        }
    }

    //------------------------------------------------------------------------
    //! 仕事を投げます。
    //------------------------------------------------------------------------
    void JobRunner::Submit(Work work) {
        if(IsBusy() || !work) {
            return;
        }

        // 前回のスレッドが終わっていて join していなければここで回収する
        if(m_thread.joinable()) {
            m_thread.join();
        }

        m_cancel.Reset();
        m_progress.store(0.0f, std::memory_order_relaxed);
        m_busy.store(true, std::memory_order_release);

        m_thread = std::thread([this, work = std::move(work)]() {
            const RockCore::ProgressCallback progress = [this](float ratio, std::string_view label) {
                m_progress.store(ratio, std::memory_order_relaxed);

                std::lock_guard<std::mutex> lock(m_labelMutex);
                m_label.assign(label);
            };

            work(progress, &m_cancel);

            // busy を下ろすのは最後。これより前に下ろすと、メインスレッドが
            // まだ書き込み中の結果を読み始める
            m_busy.store(false, std::memory_order_release);
        });
    }

    //------------------------------------------------------------------------
    //! 終わったスレッドを回収します。
    //------------------------------------------------------------------------
    bool JobRunner::PollCompletion() {
        if(IsBusy() || !m_thread.joinable()) {
            return false;
        }

        m_thread.join();
        return true;
    }

    //------------------------------------------------------------------------
    //! 現在の作業名を返します。
    //------------------------------------------------------------------------
    std::string JobRunner::GetLabel() const {
        std::lock_guard<std::mutex> lock(m_labelMutex);
        return m_label;
    }

}    // namespace RockEditor
