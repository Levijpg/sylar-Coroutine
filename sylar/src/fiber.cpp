#include<fiber.h>
#include "ucontext.h"
#include <cassert>
#include<mutex>




namespace StackAllocator{

    void* Alloc(size_t size) {
        // 分配内存
        if (currentOffset + size > stackSize) {
            // 没有足够的空间
            return nullptr;
        }
        void* ptr = static_cast<char*>(stack) + currentOffset;
        currentOffset += size;
        return ptr;
    }

    void deallocate(void* ptr) {
        // 栈分配器通常不支持单独的 deallocate
        // 这里可以实现成 noop，或者进行某种回退操作
    }


    void* stack;
    size_t stackSize;
    size_t currentOffset;
}

/**构造函数
 * 无参构造函数：用于创建线程的第一个协程，即线程主函数对应的协程
 * 这个协程只能由GetThis()方法调用
 */
Fiber::Fiber()
{
    SetThis(this);
    m_state = RUNNING;

    if(getcontext(&m_ctx))
    {
        SYLAR_ASSERT2(false, "getcontext");
    }
    ++s_fiber_count;
    m_id = s_fiber_count++;

    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() main id = " << m_id;
}

/**
 * 构造函数，用于创建用户协程
 * cb--协程入口函数
 * stacksize--栈大小，默认为128k
 */
Fiber::Fiber(std::function<void()> cb, size_t statcksize) : m_id(s_fiber_id++), m_cb(cb)
{
    ++s_fiber_count;
    m_stacksize = statcksize ? statcksize: g_fiber_stack_size->getValue();
    m_stack = StackAllocator::Alloc(m_stacksize);

    if(getcontext(&m_ctx))
    {
        SYLAR_ASSERT2(false, "getcontext"); // 获取当前的执行上下文,返回非零值（表示失败），则触发断言失败
    }

    m_ctx.uc_link = nullptr; // 下⼀个激活的上下⽂对象的指针
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() id = " << m_id;
}


/*增加m_runInScheduler成员，表示当前协程是否参与调度器调度，在
协程的resume和yield时，根据协程的运⾏环境确定是和线程主协程进⾏交换还是和调度协程进⾏交换 */
Fiber::Fiber(std::function<void()> cb, size_t statcksize, bool run_in_scheduler)
:m_id(s_fiber_id++), m_cb(cb), m_runInScheduler(run_in_scheduler)
{
    ++s_fiber_count;
    m_stacksize = statcksize ? statcksize : g_fiber_stack_size->getValue();
    m_stack = StackAllocator::Alloc(m_stacksize);

    if(getcontext(&m_ctx))
    {
        SYLAR_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link = nullptr; // 下⼀个激活的上下⽂对象的指针
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    SYLAR_LOG_DEBUG(g_logger) << "Fiber::Fiber() id = " << m_id;
}


/**
 * 返回当前线程正在执行的协程
 * 如果当前线程未创建协程，则创建该线程的第一个协程，且该协程为当前线程的主协程，
 * 其余协程由它调度，即其他协程结束时都要切回主协程，由主协程重新选择新的协程进行resume；
 * ！！！注意：线程如果创建协程，应该首先执行Fiber::GetThis()操作，从而初始化主函数协程
 */
Fiber::ptr GetThis()
{
    if(t_fiber) // 当前线程正在运⾏的协程
        return t_fiber->shared_from_this();
    Fiber::ptr main_fiber(new Fiber);
    SYLAR_ASSERT(t_fiber == main_fiber.get());
    t_thread_fiber = main_fiber;
    return t_thread_fiber->shared_from_this();
}


// 协程切换：resume+yield
// 执⾏resume时的当前执⾏环境⼀定是位于线程主协程⾥，所以这⾥的swapcontext操作的结果
// 把主协程的上下⽂保存到t_thread_fiber->m_ctx中，并且激活⼦协程的上下⽂；⽽执⾏yield时，当前执⾏环境⼀
// 定是位于⼦协程⾥，所以这⾥的swapcontext操作的结果是把⼦协程的上下⽂保存到协程⾃⼰的m_ctx中，同时从
// t_thread_fiber获得主协程的上下⽂并激活
void Fiber::resume()
{
    SYLAR_ASSERT(m_state != TERM && m_state != RUNNING);
    SetThis(this);
    m_state = RUNNING;

    if(m_runInScheduler)
    {
        if(swapcontext(&(Scheduler::GetMainFiber()->m_ctx), &m_ctx))
        {
            SYLAR_ASSERT2(false, "swapcontext");
        }else{
            if (swapcontext(&(t_thread_fiber->m_ctx), &m_ctx)) {
            SYLAR_ASSERT2(false, "swapcontext");
            }
        }
    }
}                                      

void Fiber::yield()
{
    // 协程运行完会自动yield一次，用于回到主协程，此时状态为结束状态
    SYLAR_ASSERT(m_state == RUNNING || m_state == TERM);
    SetThis(t_thread_fiber.get()); // 
    if(m_state!=TERM) 
    {
        m_state = READY;
    }

    // 如果协程参与调度器调度，那么应该和调度器的主协程进行swap，而不是线程主协程
    if(m_runInScheduler)
    {
        if(swapcontext(&m_ctx, &(Scheduler::GetMainFiber()->m_ctx)))
        {
            SYLAR_ASSERT2(false, "swapcontext");
        }else{
            if(swapcontext(&m_ctx, &(t_thread_fiber->m_ctx)))
            {
                SYLAR_ASSERT2(false, "swapcontext");
            }
        }
    }
}

/* 协程入口函数 */
void Fiber::MainFunc()
{
    Fiber::ptr cur = GetThis();
    SYLAR_ASSERT(cur);

    cur->m_cb(); // 执行协程的入口函数
    cur->m_cb = nullptr;
    cur->m_state = TERM;

    auto raw_ptr = cur.get();
    cur.reset();
    raw_ptr->yield(); // 协程结束时自动yield，回到主携程
}

/*协程重置---重复利用已结束的协程，复用其栈空间，创建新协程，此处强制只有TERM状态的协程才可以重置*/
void Fiber::reset(std::function<void()> cb)
{
    SYLAR_ASSERT(m_stack);
    SYLAR_ASSERT(m_state == TERM);
    m_cb = cb;
    if(getcontext(&m_ctx))
    {
        SYLAR_ASSERT2(false, "getcontext");
    }

    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    m_state = READY;
}
