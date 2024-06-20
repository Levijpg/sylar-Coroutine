#include <memory>
#include <functional>
#include <ucontext.h>
#include <vector>
#include <list>
#include <thread>


class Scheduler{
public:
    typedef std::shared_ptr<Scheduler> ptr;
    typedef Mutex MutexType;

    /**
     * 创建调度器
     * threads---线程数
     * use_caller---是否将当前线程也作为调度线程
     * name---名称
     */
    Scheduler(size_t threads=1, bool use_caller = true, const std::string &name = "Scheduler");
    virtual ~Scheduler();

    // 获取调度器名称
    const std::string &getName() const { return m_name;} 

    // 获取当前线程调度器指针
    static Scheduler *GetThis();

    // 当前线程的主协程
    static Fiber *GetMainFiber();

    /**添加调度任务
     * FiberOrCb调度任务类型，可以是协程对象或函数指针
     * fc 协程对象或指针
     * thread 指定运行该任务的线程号， -1表示任何线程
     */
    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int thread=-1)
    {
        bool need_tickle = false;
        {
            MutexType::Lock lock(m_mutex);
            need_tickle = scheduleNoLock(fc, thread);
        }
        if(need_tickle)
            tickle(); // 唤醒idle协程
    }

    // 启动调度器
    void start();

    // 停止调度器
    void stop();

    

protected:
    virtual void tickle(); //通知协程调度器有任务
    void run(); // 协程调度函数
    virtual void idle(); // 无任务调度时执行idle协程
    virtual bool stopping(); // 返回是否可以停止
    void setThis(); // 设置当前的协程调度器
    bool hasIdleThreads(){ return m_idleThreadCount>0;} // 返回是否有空闲线程----当调度协程进⼊idle时空闲线程数加1，从idle协程返回时空闲线程数减1

private:
    // 调度任务，协程/函数二选一，可指定在哪个线程上调度
    struct ScheduleTask{
        Fiber::ptr fiber;
        std::function<void()> cb;
        int thread;

        ScheduleTask(Fiber::ptr f, int thr){
            fiber = f;
            thread = thr;
        }

        ScheduleTask(Fiber::ptr *f, int thr){
            fiber.swap(*f);
            thread = thr;
        }

        ScheduleTask(std::function<void()>f, int thr){
            cb = f;
            thread = thr;
        }

        ScheduleTask(){thread=-1;}

        void reset(){
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }
    };

private:
    /// 协程调度器名称
    std::string m_name;

    /// 互斥锁
    MutexType m_mutex;

    /// 线程池
    std::vector<Thread::ptr> m_threads;

    /// 任务队列
    std::list<ScheduleTask> m_tasks;

    /// 线程池的线程ID数组
    std::vector<int> m_threadIds;

    /// ⼯作线程数量，不包含use_caller的主线程
    size_t m_threadCount = 0;

    /// 活跃线程数
    std::atomic<size_t> m_activeThreadCount = {0};

    /// idle线程数
    std::atomic<size_t> m_idleThreadCount = {0};

    /// 是否use caller
    bool m_useCaller;

    /// use_caller为true时，调度器所在线程的调度协程
    Fiber::ptr m_rootFiber;

    /// use_caller为true时，调度器所在线程的id
    int m_rootThread = 0;

    /// 是否正在停⽌
    bool m_stopping = false;

    /// 当前线程的调度器，同⼀个调度器下的所有线程指同同⼀个调度器实例
    static thread_local Scheduler *t_scheduler = nullptr;

    /// 当前线程的调度协程，每个线程都独有⼀份，包括caller线程
    static thread_local Fiber *t_scheduler_fiber = nullptr;
};