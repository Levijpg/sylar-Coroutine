#include "scheduler.h"
#include <mutex>
#include <atomic>
#include <cstddef>
#include "TimerManager.h"

class IOManager : public Scheduler, public TimerManager{
public:
    typedef std::shared_ptr<IOManager> ptr;
    typedef RWMutex RWMutexType;

    IOManager(size_t threads, bool use_caller, const std::string &name);
    ~IOManager();
    virtual void tickle(); //通知协程调度器有任务
    void run(); // 协程调度函数
    virtual void idle(); // 无任务调度时执行idle协程
    virtual bool stopping(); // 返回是否可以停止
    

    /**IO 事件，继承自epoll对事件的定义
     * 此处只关心socket fd的读写时间，其他epoll事件会归类到这两类事件中
    */
   enum Event{
    NONE = 0x0,  // 无事件
    READ = 0x1,  // 读事件(EPOLLIN)
    WRITE = 0x4, // 写事件(EPOLLOUT)
   };

   int addEvent(int fd, Event event, std::function<void()> cb);
   bool IOManager::delEvent(int fd, Event event);
   bool IOManager::cancelEvent(int fd, Event event);
   bool IOManager::cancelAll(int fd);

    /**对描述符-事件类型-回调函数三元组定义，该三元组为fd上下文
     * 使用结构体FdContext表示。每个socket fd对应一个FdContext
     */
    // socket fd上下文类
    struct FdContext{
        typedef std::mutex MutexType;
        /**事件上下文类，fd的每个事件都有一个事件上下文，保存这个事件的回调函数
         * 以及回调函数的调度器
         */
        struct EventContext{
            // 执行事件回调的调度器
            Scheduler *scheduler = nullptr;
            // 事件回调协程
            Fiber::ptr fiber;
            // 事件回调函数
            std::function<void()> cb;
        };

        /**获取事件上下文类
         * @param[in]event事件类型
         * @return 返回对应事件的上下文
         */
        EventContext &getEventContext(Event event);

        /**
         * @brief 重置事件上下文
         * @param[in, out] ctx待重置的事件上下文对象
         */
        void resetEventContext(EventContext &ctx);

        /**
         * @brief 触发事件
         * @details 根据事件类型调用对应上下文结构中的调度器去调度回调协程或回调函数
         * @param[in] event 事件类型
         */
        void triggerEvent(Event event);

        

        // 读事件上下文
        EventContext read;
        // 写事件上下文
        EventContext write;
        // 事件关联的句柄
        int fd = 0;
        // 该fd添加了哪些事件的回调函数
        Event events = NONE;
        // 事件的Mutex
        MutexType mutex;
    };

private:
    /// epoll ⽂件句柄
    int m_epfd = 0;

    /// pipe ⽂件句柄，fd[0]读端，fd[1]写端
    int m_tickleFds[2];

    /// 当前等待执⾏的IO事件数量
    std::atomic<std::size_t> m_pendingEventCount = {0};

    /// IOManager的Mutex
    RWMutexType m_mutex;

    /// socket事件上下⽂的容器
    std::vector<FdContext *> m_fdContexts;

};