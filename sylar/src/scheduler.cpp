#include "scheduler.h"
#include "fiber.cpp"
#include <vector>

/**
 * @brief 创建调度器
 * @param[in] threads 线程数
 * @param[in] use_caller 是否将当前线程也作为调度线程
 * @param[in] name 名称
 */
Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name){
    SYLAR_ASSERT(threads > 0);
    m_useCaller = use_caller;
    m_name = name;

    if(use_caller)
    {
        --threads;
        Fiber::GetThis();
        SYLAR_ASSERT(GetThis() == nullptr);
        t_scheduler = this;

        /**
         * 在user_caller为true的情况下，初始化caller线程的调度协程
         * caller线程的调度协程不会被调度器调度，而且caller线程的调取协程停止时，
         * 应该返回caller线程的主协程
         */
        // 创建一个新的Fiber对象，绑定Scheduler的run方法，并将其作为根协程
        m_rootFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false)); 

        Thread::SetName(m_name); //设置线程名称
        t_scheduler_fiber = m_rootFiber.get();  //获取主Fiber的指针
        m_rootThread = GetThreadID();  //获取当前线程ID
        m_threadIds.push_back(m_rootThread); //将当前线程ID添加到线程ID列表
    }
    else{m_rootThread=-1;}
    m_threadCount = threads;  // 设置线程数量
}

Scheduler *Scheduler::GetThis(){
    return t_scheduler;
}

void Scheduler::start(){
    SYLAR_LOG_DEBUG(g_logger) << "start";
    MutexType::Lock lock(m_mutex); // 锁住调度器的互斥锁，确保线程安全
    if (m_stopping) {
        SYLAR_LOG_ERROR(g_logger) << "Scheduler is stopped";
        return;
    }
    SYLAR_ASSERT(m_threads.empty()); // 确保调度线程列表为空
    m_threads.resize(m_threadCount);
    for(size_t i=0; i<m_threadCount; i++)
    {
        // 创建新的线程，每个线程执行Scheduler::run方法
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                            m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getID());
    }
}

/**用于调度任务与管理线程
 * 调度器通过循环从任务队列中获取任务并执行
 * 直到没有任务可以调度或调度器停止
 */
void Scheduler::run(){
    SYLAR_LOG_DEBUG(g_logger) << "run";
    // 设置当前线程的调度器上下文，如果当前线程不是主线程，则获取当前线程的协程并保存
    setThis();
    if(GetThreadID()!=m_rootThread){
        t_scheduler_fiber = Fiber::GetThis().get();
    }

    //  idle_fiber 被执行时，实际上会调用 Scheduler 实例的 idle 方法。
    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;
    ScheduleTask task;
    while(true)
    {
        task.reset();
        bool tickle_me = false;  // 是否tickle其他线程进行任务调度
        {
            MutexType::Lock lock(m_mutex);
            auto it = m_tasks.begin();
            
            //  遍历所有调度任务
            while(it!=m_tasks.end()){
                if(it->thread!=-1 && it->thread != GetThreadId()){
                    // 制定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行
                    // 调度，然后跳过这个任务，继续下一个
                    ++it;
                    tickle_me = true;
                    continue;
                }

                // 找到一个未指定线程，或是制定了当前线程的任务
                SYLAR_ASSERT(it->fiber || it->cb);
                if(it->fiber)
                {
                    // 任务队列的协程一定是ready状态
                    SYLAR_ASSERT(it->fiber->getState() == Fiber::READY);
                }
                // 当前调度线程找到⼀个任务，准备开始调度，将其从任务队列中剔除，活动线程数加1
                task = *it;
                m_tasks.erase(it++);
                ++m_activeThreadCount;
                break;
            }
            // 当前线程拿完一个任务后，发现任务队列还有剩余，需要tickle一下其他线程
            tickle_me |= (it!=m_tasks.end());
        }

        // 如果有剩余任务，则通知其他线程进行调度
        if(tickle_me) tickle();

        // 如果任务是协程（fiber），恢复该协程执行；如果任务是回调函数（cb），则创建或重置协程并执行。
        if(task.fiber){
            // resume协程，resume返回时，协程要么执行结束，要么半路yield，总之这个任务完成了活跃线程数-
            task.fiber->resume();
            --m_activeThreadCount;
            task.reset();
        }else if(task.cb){
            if(cb_fiber) cb_fiber->reset(task.cb);
            else cb_fiber.reset(new Fiber(task.cb));
            task.reset();
            cb_fiber->resume();
            --m_activeThreadCount;
            cb_fiber.reset();
        }else{
            // 进到这个分支时，任务队列为空，调整idle协程即可
            if(idle_fiber->getState()==Fiber::TERM){
                // 如果调度器没有调度任务，那么idle协程会不停的resume/yield，不会结束，
                // 如果idle协程结束了，那一定是调度器停止了
                SYLAR_LOG_DEBUG(g_logger) << "idle fiber term";
                break;
            }
            ++m_idleThreadCount;
            idle_fiber->resume();
            --m_idleThreadCount;
        }
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::run() exit";
    }
}

void Scheduler::stop(){
    SYLAR_LOG_DEBUG(g_logger) << "stop";
    if(stopping()) return ;
    m_stopping = true;

    // 如果use caller，那么只能由caller线程发起stop
    if(m_useCaller)
        SYLAR_ASSERT(GetThis()==this);
    else
        SYLAR_ASSERT(GetThis()!=this);

    // 唤醒所有工作线程，以便他们可以检测到调度器的停止状态并安全的结束
    for(size_t i=0; i<m_threadCount; i++)
        tickle();
    
    // 如果存在主协程，则唤醒主协程，检测调度器的停滞状态
    if(m_rootFiber)
        tickle();

    // 在usecaller情况下，调度器协程结束时，应该返回caller协程
    if(m_rootFiber)
    {
        m_rootFiber->resume();
        SYLAR_LOG_DEBUG(g_logger) << "m_rootFiber end";
    }

    // 将调度器管理的所有线程对象存储到本地变量thrs
    // 逐个等待线程结束，确保所有线程都安全退出
    std::vector<Thread::ptr> thrs;
    {
        MutexType::Lock lock(m_mutex);
        thrs.swap(m_threads);
    }

    for(auto&i : thrs)
        i->join();
}