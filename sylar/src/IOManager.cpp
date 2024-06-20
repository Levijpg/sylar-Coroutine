#include "IOManager.h"
#include <string.h>
#include "windows.h"
#include "io.h"

/**
 * @brief 构造函数
 * @param[in] thread 线程数量
 * @param[in] use_caller 是否将调用线程包含进去
 * @param[in] name 调度器名称
 */
IOManager::IOManager(size_t threads, bool use_caller, const std::string &name)
:Scheduler(threads, use_caller, name){
    // 创建epoll实例
    m_epfd = epoll_create(5000);
    SYLAR_ASSERT(m_epfd>0);

    // 创建pipe，获取m_tickleFds[2]，其中m_tickleFds[0]是管道的读端，
    // m_tickleFds[1]是管道的写端
    int rt = pipe(m_tickleFds);
    SYLAR_ASSERT(!rt);

    // 注册pipe读句柄的可读事件，用于tickle调度协程，通过epoll_eventdata.fd保存描述符
    epoll_event event;
    memset(&event, 0, sizeof(epoll_event));
    event.events = EPOLLIN|EPOLLET;
    event.data.fd = m_tickleFds[0];

    // 非阻塞方式，配合边缘触发
    rt = fcntl(m_tickleFds[0], F_SETFL, O_NONBLOCK);
    SYLAR_ASSERT(!rt);

    // 将管道的读描述符加入epoll多路复用，如果管道可读，idle中的epoll_wait会返回
    rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds[0], &event);
    SYLAR_ASSERT(!rt);
    contextResize(32);

    // 开启scheduler
    start();
}

/**
 * @brief 通知调度器有任务要调度
 * @details 写pipe让idle协程从epoll_wait退出，待idle协程yield之后Scheduler::run就可以调度其他任务
 * 如果当前没有空闲调度线程吗，则不用发通知
 */
void IOManager::tickle()
{
    SYLAR_LOG_DEBUG(g_logger) << "tickle";
    if(!hasIdleThreads())
        return ;
        int rt = write(m_tickleFds[1], "T", 1);
        SYLAR_ASSERT(rt==1);
}

/**
 * @brief idle协程
 * @details 对于IO协程调度，应阻塞在等待IO事件上，idle退出的时机是epoll_wait返回，对应的操作
 * 是tickle或注册的IO事件就绪
 * 调度器无调度任务时会阻塞idle协程上，对于IO调度器而言，idle状态应该关注两件事，1有没有新的
 * 调度任务，对应scheduler::schedule()；如果有新的调度任务，应立刻退出idle状态，并执行对应的任务
 * 2关注当前注册的所有IO事件有没有触发，如果有，应该立刻执行IO事件对应的回调函数
 */
void IOManager::idle()
{
    SYLAR_LOG_DEBUG(g_logger) << "idle";

    // 一次epoll_wait最多检测256个就绪时间，如果超过这个数，那么会在下轮epoll_wait继续处理
    const uint64_t MAX_EVENTS = 256;
    epoll_event *events = new epoll_event[MAX_EVENTS]();
    // 使用自定义的删除器 [](epoll_event *ptr){ delete[] ptr; }，确保当 shared_events 被销毁时正确释放 events 数组的内存。
    std::shared_ptr<epoll_event> shared_events(events, [](epoll_event *ptr){
        delete[] ptr;
    });

    // 进入循环，等待事件
    while(true)
    {
        // 获取下一个定时器的超时时间，顺便判断调度器是否停止
        uint64_t next_timeout = 0;
        if( SYLAR_UNLIKELY(stopping(next_timeout))) {
            SYLAR_LOG_DEBUG(g_logger) << "name=" << getName() << "idle stopping exit";
            break;
        }

        // 阻塞在epoll_wait上，等待事件发生或定时器超时
        int rt = 0;
        do{
            // 默认超时时间5秒，如果下一个定时器的超时时间大于5秒，仍以5秒来计算超时。
            // 避免定时器超时时间太大时，epoll_wait一直在阻塞
            static const int MAX_TIMEOUT = 5000;
            if(next_timeout != ~0ull)
                next_timeout = std::min((int)next_timeout, MAX_TIMEOUT);
            else
                next_timeout = MAX_TIMEOUT;
            
            rt = epoll_wait(m_epfd, events, MAX_EVENTS, (int)next_timeout);

            if(rt<0 && errno==EINTR)
                continue;
            else    break;
        }while(true);

        // 收集所有已超时的定时器，执行回调函数
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()){
            for(const auto &cb:cbs)
                schedule(cb);
            cbs.clear();
        }
        
        // 遍历所有发生的事件，根据epoll_wait的私有指针找到对应的FdContext，进行事件处理
        for(int i=0; i<rt; ++i){
            epoll_event &event = events[i];
            if(event.data.fd==m_tickleFds[0])
            {
                // ticklefd[0]用于通知协程调度，这是只需要把管道里的内容读完即可，本轮idle结束
                // Scheduler::run会重新执行协程调度
                uint8_t dummy[256];
                while(read(m_tickleFds[0], dummy, sizeof(dummy))>0);
                continue;
                // 如果事件是由 tickle 触发的（用于唤醒 epoll_wait），则读取管道中的数据并继续。
            }

            // 处理非tickle事件
            // 通过epoll_event的私有指针获取FdContext
            FdContext *fd_ctx = (FdContext*)event.data.ptr;
            FdContext::MutexType::Lock lock(fd_ctx->mutex);

            /**
             * EPOLLERR 出错，比如写读端已经关闭的pipe
             * EPOLLHUP 套接字对端关闭
             * 出现这两种时间，应该同时触发fd的读写事件，否则有可能出现注册的事件永远执行不到的情况
             */
            if(event.events & (EPOLLERR|EPOLLHUP)){
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            int real_events = NONE;
            if(event.events & EPOLLIN){
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) {
                real_events |= WRITE;
            }
            if ((fd_ctx->events & real_events) == NONE) {
                continue;
            }

            /**
             * 修改epoll事件并处理已发生的事件
             * 删除已经发生的事件，将剩下的事件重新加入epoll_wait，
             * 如果剩下的事件为0，表示这个fd已经不需要关注了，直接从epoll删除
             */
            int left_events = (fd_ctx->events & ~real_events);
            int op = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
            event.events = EPOLLET | left_events;

            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if(rt2){
                SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
                << (EpollCtlOp)op << ", " << fd_ctx->fd << ", " << (EPOLL_EVENTS)event.events << "):"
                << rt2 << " (" << errno << ") (" << 
                strerror(errno) << ")";
                continue;
            }

            // 处理已经发生的事件，也就是让调度器调度制定的函数或协程
            if(real_events & READ)
            {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }

            if(real_events & WRITE)
            {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        }

        /**
         * @brief 当前协程让出 CPU，等待调度器重新调度：
         * @details 上面triggerEvent实际上也只是把对应的fiber重新加入调度，
         * 要执行的话还要等idle协程退出
         */
        Fiber::ptr cur = Fiber::GetThis();
        auto  raw_pyr = cur.get();
        cur.reset();
        raw_pyr->yield();
    }  
}

/**
 * @brief 添加事件
 * @details fd描述符发生了event事件时执行cb函数
 * @param[in] fd socket句柄
 * @param[in] event 事件
 * @param[in] cb 事件回调函数，如果为空，则默认把当前协程作为回调执行体
 * @return 添加成功返回0，失败返回-1
 */
int IOManager::addEvent(int fd, Event event, std::function<void()> cb)
{
    // 找到fd对应的FdContext，如果不存在，那就分配一个
    FdContext *fd_ctx = nullptr;
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size()>fd){
        fd_ctx = m_fdContexts[fd];
        lock.unlock();
    }else{
        lock.unlock();
        RWMutexType::WriteLock lock2(m_mutex);
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    // 同一个fd不允许重复添加相同的事件
    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if (SYLAR_UNLIKELY(fd_ctx->events & event)) {
        SYLAR_LOG_ERROR(g_logger) << "addEvent assert fd=" << fd
        << " event=" << (EPOLL_EVENTS)event
        << " fd_ctx.event=" << (EPOLL_EVENTS)fd_ctx->events;
        SYLAR_ASSERT(!(fd_ctx->events & event));
    }

    // 将新的事件加入epoll_wait，使用epoll_event的私有指针存储FdContext的位置
    int op = fd_ctx->events ? EPOLL_CTL_MOD:EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events = EPOLLET | fd_ctx->events | event;
    epevent.data.ptr = fd_ctx;
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "

        << (EpollCtlOp)op << ", " << fd << ", " << 
        (EPOLL_EVENTS)epevent.events << "):"

        << rt << " (" << errno << ") (" << strerror(errno) << 

        ") fd_ctx->events="

        << (EPOLL_EVENTS)fd_ctx->events;
        return -1;
    }

    // 待执行IO事件数+1
    ++m_pendingEventCount;

    // 找到这个fd的event事件对应的EventContext，对其中的scheduler, cb, fiber进行赋值
    fd_ctx->events = (Event)(fd_ctx->events | event);
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
    SYLAR_ASSERT(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);

    // 赋值scheduler和回调函数，如果回调函数为空，则把当前协程当成回调执行体(当该事件触发时，将执行当前正在运行的协程)
    event_ctx.scheduler = Scheduler::GetThis();
    if(cb){
        event_ctx.cb.swap(cb);
    }else{
        event_ctx.fiber = Fiber::GetThis();
        SYLAR_ASSERT2(event_ctx.fiber->getState() == Fiber::RUNNING, "state=" << 
        event_ctx.fiber->getState());
    }
    return 0;
}

/**
 * @brief 删除事件
 * @param[in] fd socket句柄
 * @param[in] event 事件类型
 * @return 是否删除成功
 */
// 锁定并查找：确保对 m_fdContexts 的访问安全，并获取对应的 FdContext。
// 检查事件：确认要删除的事件存在。
// 更新 epoll：根据剩余事件情况，修改或删除 epoll 事件。
// 错误处理：记录并处理 epoll_ctl 调用中的错误。
// 更新状态：减少待执行事件计数并重置事件上下文。
bool IOManager::delEvent(int fd, Event event)
{
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size()<=fd){
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    if (SYLAR_UNLIKELY(!(fd_ctx->events & event))) return false;


    // 清除指定的事件，表示不关心这个时间了，如果清除之后结果为0，则从epoll_wait中删除该fd
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) {
        SYLAR_LOG_ERROR(g_logger) << "epoll_ctl(" << m_epfd << ", "
        << (EpollCtlOp)op << ", " << fd << ", " << 
        (EPOLL_EVENTS)epevent.events << "):"
        << rt << " (" << errno << ") (" << strerror(errno) << 
        ")";
        return false;
    }

    // 待执行事件数-1
    --m_pendingEventCount;
    // 重置该fd对应的event事件上下文
    fd_ctx->events = new_events;
    FdContext::EventContext &event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}

/**
 * @brief 取消事件
 * @param[in] event 事件类型
 * @param[in] fd socket句柄
 * @return 是否删除成功
 */
bool IOManager::cancelEvent(int fd, Event event){
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size()<=fd)
        return false;
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(SYLAR_UNLIKELY(!(fd_ctx->events & event))){
        return false;
    }

    // 删除事件
    Event new_events = (Event)(fd_ctx->events & ~event); // 该new_events包含了原来fd_ctx->events中除了event之外的所有事件
    int op = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL; //如果还有剩余时间，则修改epoll事件，否则删除该fd
    epoll_event epevent;
    epevent.events = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if(rt)
        return false;

    // 删除之前触发一次事件：为了确保在删除事件之前，所有未处理的事件都能够得到处理
    fd_ctx->triggerEvent(event);
    // 活跃事件数-1
    --m_pendingEventCount;
    return true;
}

/**
 * @brief 取消所有事件
 * @details 所有被注册的回调函数在cancel之前都会执行一次
 * @param[in] fd socket句柄
 * @return 是否删除成功
 */
// 检查并获取 fd 对应的 FdContext。
// 使用 epoll_ctl 删除 epoll 实例中的所有事件。
// 触发并处理所有已注册的事件。
// 减少相应的待处理事件计数。
// 确保 FdContext 中的事件已清空。
bool IOManager::cancelAll(int fd) {
    // 找到fd对应的FdContext
    RWMutexType::ReadLock lock(m_mutex);
    if((int)m_fdContexts.size()<=fd)
    {
        return false;
    }
    FdContext *fd_ctx = m_fdContexts[fd];
    lock.unlock();

    FdContext::MutexType::Lock lock2(fd_ctx->mutex);
    if(!fd_ctx->events) return false; // 没有注册事件，返回false

    // 删除全部事件
    int op = EPOLL_CTL_DEL; // 删除fd上的所有事件
    epoll_event epevent;
    epevent.events = 0;
    epevent.data.ptr = fd_ctx;
    int  rt = epoll_ctl(m_epfd, op, fd, &epevent); // 将fd从epoll实例m_epfd中删除
    if(rt) return false;

    // 触发全部已注册的事件
    if(fd_ctx->events & READ){
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }
    if(fd_ctx->events & WRITE){
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }
    SYLAR_ASSERT(fd_ctx->events == 0);
    return true;
}

IOManager::~IOManager()
{
    stop();
    close(m_epfd);
    close(m_tickleFds[0]);
    close(m_tickleFds[1]);

    for(size_t i=0; i<m_fdContexts.size(); ++i)
    {
        if(m_fdContexts[i])
            delete m_fdContexts[i];
    }
}

bool IOManager::stopping()
{
    // 对于IOManager而言，必须等所有待调度的IO事件都执行完了才可以退出
    return m_pendingEventCount==0 && Scheduler::stopping();
}