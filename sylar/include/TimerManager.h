#include<Timer.h>
#include<vector>
#include<set>
#include "mutex.h"

/**
 * @brief 定时器管理器
 */
class TimerManager{
    friend class Timer;
public:
    // 读写锁类型
    typedef RWMutex RWMutexType;
    // 构造函数
    TimerManager();
    // 析构函数
    ~TimerManager();

    /**
     * @brief 添加定时器
     * @param[in] ms 定时器执行的间隔事件
     * @param[in] cb 定时器回调函数
     * @param[in] recurring 是否循环定时器
     */
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb, bool recurring=false);

    /**
     * @brief 添加条件定时器
     * @param[in] ms 定时器执行间隔事件
     * @param[in] cb 定时器回调函数
     * @param[in] weak_cond 条件
     * @param[in] recurring 是否循环
     */
    Timer::ptr addConditionTimer(uint64_t ms, std::function<void()> cb, std::weak_ptr<void> weak_cond, bool recurring = false);

    /**
     * @brief 到最近一个定时器执行的时间间隔
     */
    uint64_t getNextTimer();

    /**
     * @brief 获取需要执行的定时器的回调函数列表
     * @param[out] cbs 回调函数数组
     */
    void listExpiredCb(std::vector<std::function<void()>>& cbs);

    /**
     * @brief 是否有定时器
     */
    bool hasTimer();

protected:
    /**
     * @brief 当有新的定时器插入到定时器的首部，执行该函数
     */
    virtual void onTimerInsertAtFront() = 0;

    /**
     * @brief 将定时器添加到管理器中
     */
    void addTimer(Timer::ptr val, RWMutexType::WriteLock& lock);

private:
    // Mutex
    RWMutexType m_mutex;
    // 定时器集合
    std::set<Timer::ptr, Timer::Comparator> m_timers;
    // 是否触发onTimerInsertAtFront
    bool m_tickled = false;
    // 上次执行事件
    uint64_t m_previousTime = 0;
}