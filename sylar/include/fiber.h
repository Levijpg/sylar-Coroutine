/**
 * @file fiber.h
 * @brief 协程封装
 * @author sylar.yin
 * @email 564628276@qq.com
 * @date 2019-05-24
 * @copyright Copyright (c) 2019年 sylar.yin All rights reserved (www.sylar.top)
 */
#ifndef __SYLAR_FIBER_H__
#define __SYLAR_FIBER_H__

#include <memory>
#include <functional>
#include <ucontext.h>



class Fiber: public std::enable_shared_from_this<Fiber>{
public:
    typedef std::shared_ptr<Fiber> ptr;
    /*协程状态，在sylar的基础上简化了状态：running、ready、term*/
    enum State{
        READY, // 就绪态，刚创建或者yield之后的状态
        RUNNING, // 运行态，resume之后的状态
        TERM  // 结束态，协程的回调函数执行完之后为term
    };

private:
    /**
     * Fiber构造函数，用于创建用户协程:
     * cb--协程入口函数
     * stacksize--栈大小
     * run_in_scheduler--本协程是否参与调度器调度，默认true
     * */
     Fiber(std::function<void()> cb, size_t stacksize=0, bool run_in_scheduler=true);
     Fiber();
     Fiber(std::function<void()> cb, size_t stacksize=0);
     /**
      * 析构函数
      */
     ~Fiber();



public:
    // 设置当前正在运行的协程---设置县城局部变量t_fiber的值
    static void SetThis(Fiber *f);

    /**
     * 返回当前线程正在执行的协程---
     * 如果当前线程未创建协程，则创建该线程的第一个协程，且该协程为当前线程的主协程，
     * 其余协程由它调度，即其他协程结束时都要切回主协程，由主协程重新选择新的协程进行resume；
     * ！！！注意：线程如果创建协程，应该首先执行Fiber::GetThis()操作，从而初始化主函数协程
     */
    static Fiber::ptr GetThis();

    // 获取总协程数
    static uint64_t TotalFibers();

    // 协程入口函数
    static void MainFunc();

    // 当前协程ID
    static uint64_t GetFiberID();
    
    // 重置协程状态和入口函数，复用栈空间，不重新创建栈
     void reset(std::function<void()> cb);

    //  将当前协程切换到执行状态--即当前协程与正在运行的协程进行交换，前者状态（当前）变为running，后者为ready
    void resume();
    
    // 当前协程转让出执行权--当前协程与上次resume时退出后台的协程进行交换，前者为ready，后者变为running
    void yield();

    // 获取协程ID
    uint64_t getID() const{ return m_id;}

    // 获取协程状态
    State getState() const { return m_state;}

private:
    uint64_t m_id = 0; // 协程ID
    uint32_t m_stacksize = 0; // 协程栈大小
    State m_state = READY; // 协程状态
    ucontext_t m_ctx; // 协程上下文
    void *m_stack = nullptr; // 协程栈地址
    std::function<void()> m_cb; // 协程入口函数
    bool m_runInScheduler; // 本协程是否参与调度器调度
};


#endif