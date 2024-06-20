#include "hook.h"
#include "ws2def.h"
#include "fiber.h"
#include "FdCtx.h"

static thread_local bool t_hook_enable = false;
/**
 * @brief sleep/usleep/nanosleep的hook实现
 * @details 先添加定时器再yield
 */
unsigned int sleep(unsigned int seconds){
    if(!t_hook_enable){
        return sleep_f(seconds);
    }

    Fiber::ptr fiber = Fiber::GetThis();
    IOManager* iom = IOManager::GetThis();
    iom->addTimer(seconds * 1000 , std::bind((void(Scheduler::*)
    (Fiber::ptr, int thread))& IOManager::schedule
    , iom, fiber, -1));
    Fiber::GetThis()->yield();
    return 0;
}

/**
 * @brief socket接口的hook实现
 * @details socket用于创建套接字，需要在拿到fd后将其添加到FdManager中
 */
int socket(int domain, int type, int protocol){
    if(!t_hook_enable){
        return socket_f(domain, type, protocol);
    }
    int fd = socket_f(domain, type, protocol);
    if(fd==-1) return fd;
    FdMgr::GetInstance()->get(fd, true);
    return fd;
}

/**
 * @brief 用于发起非阻塞链接
 * @return 超时时间内，连接成功返回0,；失败或超时返回-1
 */
int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms){
    if(!t_hook_enable){
        return connect_f(fd, addr, addrlen);
    }
    FdCtx::ptr ctx = FdMgr::GetInstance()->get(fd);
    if(!ctx || ctx->m_isClosed()){
        errno = EBADF;
        return -1;
    }

    // 检查文件描述符是否是一个 socket，如果不是，调用原始 connect 函数
    if(!ctx->isSocket())
        return connect_f(fd, addr, addrlen);

    // 检查 socket 是否是非阻塞的，如果是，调用原始 connect 函数
    if(ctx->getUserNonblock())
        return connect_f(fd, addr, addrlen);

    int n = connect_f(fd, addr, addrlen);
    if(n==0) return 0;
    else if(n!=-1 || errno!=EINPROGRESS) return n;

    IOManager* iom  = IOManager::GetThis();
    Timer::ptr timer;
    // 创建一个 timer_info 对象，用于超时处理
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    // 如果设置了超时时间，添加一个条件定时器
    if(timeout_ms !=(uint64_t)-1){
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() {
        auto t = winfo.lock();
        if(!t || t->cancelled) {
        return;
        }
        t->cancelled = ETIMEDOUT;
        iom->cancelEvent(fd, sylar::IOManager::WRITE);
        }, winfo);
    }

    // 为文件描述符添加一个 WRITE 事件
    int rt = iom->addEvent(fd, IOManager::WRITE);
    if(rt==0){
        Fiber::GetThis()->yield();
        if(timer) timer->cancel();{
            if(tinfo->cancelled){
                errno = tinfo->cancelled;
                return -1;
            }
        }else{
            if(timer){
                timer->cancel();
            }
        }
        SYLAR_LOG_ERROR(g_logger) << "connect addEvent(" << fd << ", WRITE) error";
    }

    int error = 0;
    socklen_t len = sizeof(int);
    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)==-1) return -1;
    if(!error) return 0;
    else{
        errno = error;
        return -1;
    }
}